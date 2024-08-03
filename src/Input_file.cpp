// a lot of code is copied or modified from https://github.com/rui314/mold

#include <algorithm>
#include <iostream>
#include "Relocatable_file.h"
#include "Symbol.h"
#include "Input_file.h"
#include "Linking_context.h"
#include "Mergeable_section.h"
#include "ELF_util.h"


static void Init_local_symbols(std::unique_ptr<Symbol[]> &dst, const Relocatable_file &rel_file, std::size_t n_local_sym);

static Mergeable_section Split_section(const Relocatable_file &file, Linking_context &ctx, std::size_t shndx);

static void Sort_relocation(const Input_file &input_file);

static void Init_local_symbols(std::unique_ptr<Symbol[]> &dst, const Relocatable_file &rel_file, std::size_t n_local_sym)
{
    dst = std::make_unique<Symbol[]>(n_local_sym);
     
    dst[0] = Symbol(rel_file, 0);

    for(std::size_t i = 1 ; i < n_local_sym ; i++)
    {
        Symbol sym(rel_file, i);
        auto &elf_sym = rel_file.symbol_table()->data(i);
        if (nELF_util::Is_sym_common(elf_sym) == true)
            FATALF("%s", "local common??");

        sym.val = elf_sym.st_value;
  
        dst[i] = std::move(sym);
    }
}

Input_file::Input_file(Relocatable_file &src) : m_src(&src)
{    
    m_section_relocate_state_list.resize(m_src->section_hdr_table().header_count());
    m_input_section_list.resize(m_src->section_hdr_table().header_count());

    for(std::size_t i = 0 ; i < m_src->section_hdr_table().header_count() ; i++)
    {
        auto &shdr = m_src->section_hdr(i);
        
        switch (shdr.sh_type)
        {
            case SHT_GROUP:
            case SHT_RISC_V_ATTRIBUTES:
            case SHT_REL:
            case SHT_RELA:
            case SHT_SYMTAB:
            case SHT_SYMTAB_SHNDX:
            case SHT_STRTAB:
            case SHT_NULL:
                m_section_relocate_state_list[i] = eRelocate_state::no_need;
            break;
            
            default:
                std::string_view name = m_src->section_hdr_table().string_table().data() + shdr.sh_name;

                if (name == ".debug_gnu_pubnames" || name == ".debug_gnu_pubtypes") // not supported 
                {
                    m_section_relocate_state_list[i] = eRelocate_state::no_need;
                    break;
                }
                else
                {
                    m_section_relocate_state_list[i] = eRelocate_state::relocatable;
                    m_input_section_list[i] = Input_section(this->src(), i);
                    if (shdr.sh_info & SHF_COMPRESSED)
                        FATALF("%scompressed section is supported", "");
                }
            break;
        }
    }

    if (m_src->symbol_table() == nullptr || m_src->symbol_table()->count() == 0)
        return;

    m_n_local_sym = m_src->linking_mdata().first_global;

    Init_local_symbols(m_local_sym_list, this->src(), m_n_local_sym);

    symbol_list.resize(m_src->symbol_table()->count());
    
    for(std::size_t i = 0 ; i < m_n_local_sym ; i++)
        symbol_list[i] = &m_local_sym_list[i];

    for(std::size_t i = 0 ; i < m_src->section_hdr_table().header_count() ; i++)
    {
        auto &shdr = m_src->section_hdr(i);

        if (shdr.sh_type != SHT_REL && shdr.sh_type != SHT_RELA)
            continue;
        
        if (m_section_relocate_state_list[shdr.sh_info] != eRelocate_state::relocatable)
            FATALF("m_section_relocate_state_list[%d]: %d is not relocatable??", shdr.sh_info, m_section_relocate_state_list[shdr.sh_info]);

        m_input_section_list[shdr.sh_info].relsec_idx = i;

       // std::cout << name() << " link " << m_src->section_hdr(i).sh_link << "\n";
    }

    Sort_relocation(*this);
}

Input_file::~Input_file() = default;



//put defined global symbols into the gloable symbal map
void Input_file::Put_global_symbol(Linking_context &ctx)
{
    if (src().symbol_table() == nullptr)
        return;

    for(std::size_t i = src().linking_mdata().first_global ; i < src().symbol_table()->count() ; i++)
    {
        auto &esym = src().symbol_table()->data(i);

        if (nELF_util::Is_sym_undef(esym))
            continue;

        if (nELF_util::Is_sym_common(esym))
            FATALF("common symbol is not supported");
            
        if ( !nELF_util::Is_sym_abs(esym) && !nELF_util::Is_sym_common(esym))
        {
            if (m_section_relocate_state_list[src().get_shndx(esym)] == eRelocate_state::no_need)
                continue;
        }

        auto &link_pkg = ctx.Insert_global_symbol(*this, i);

        symbol_list[i] = link_pkg.symbol.get();
    }
}

// it's copied from https://github.com/rui314/mold
static size_t find_null(std::string_view data, std::size_t pos, std::size_t entsize)
{
    if (entsize == 1)
    return data.find('\0', pos);

    for (; pos <= data.size() - entsize; pos += entsize)
    {
        if (data.substr(pos, entsize).find_first_not_of('\0') == data.npos)
            return pos;
    }
  return data.npos;
}

static Mergeable_section Split_section(const Relocatable_file &file, Linking_context &ctx, std::size_t shndx)
{
    Mergeable_section ret;

    auto &shdr = file.section_hdr(shndx);

    std::size_t entsize = shdr.sh_entsize;

    if (entsize == 0)
        entsize = (shdr.sh_flags & SHF_STRINGS) ? 1 : (int)shdr.sh_addralign;

    if (entsize == 0)
        return ret;

    ret.data = std::string_view{file.section(shndx), shdr.sh_size};
    ret.p2_align = nUtil::to_p2align(file.section_hdr(shndx).sh_addralign);

    if (ret.data.size() > UINT32_MAX)
        FATALF("the mergeable section size %ld, is too large",ret.data.size());

    std::size_t addralign = shdr.sh_addralign;

    if (addralign == 0)
        addralign = 1;

    auto push_frag_info = [&ret](std::size_t start, std::size_t end) 
    {
        ret.frag_offset_list.push_back(start);
        ret.frag_hash_list.push_back(std::hash<std::string_view>{}(ret.data.substr(start, end - start)));
    };

    if (shdr.sh_flags & SHF_STRINGS)
    {
        for(std::size_t pos = 0 ; pos < ret.data.size() ;)
        {    
            size_t end = find_null(ret.data, pos, entsize);
            
            if (end == ret.data.npos)
            {
                std::cout << ret.data;
                FATALF(":string is not null terminated");
            }
            push_frag_info(pos, end);
            pos = end + entsize;
        }
    }
    else
    {
        if (ret.data.size() % entsize)
            FATALF("%ld : section size is not multiple of sh_entsize", ret.data.size());

        ret.frag_offset_list.reserve(ret.data.size() / entsize);

        for (std::size_t pos = 0; pos < ret.data.size(); pos += entsize)
            push_frag_info(pos, pos + entsize);
    }
    return ret;
}

static void Sort_relocation(const Input_file &input_file)
{
    for(std::size_t i = 0 ; i < input_file.src().section_hdr_table().header_count() ; i++)
    {
        auto &shdr = input_file.src().section_hdr(i);

        if (shdr.sh_type == SHT_REL)
        {
            Elf64_Rel *rel = reinterpret_cast<Elf64_Rel*>(input_file.src().section(i));
            auto end = rel + shdr.sh_size / shdr.sh_entsize;
            auto cmp = [](const Elf64_Rel &lhs, const Elf64_Rel &rhs){return lhs.r_offset < rhs.r_offset;};
            if (std::is_sorted(rel, end, cmp) == false)
                std::sort(rel, end, cmp);
        }
        else if (shdr.sh_type == SHT_RELA)
        {
            Elf64_Rela *rela = reinterpret_cast<Elf64_Rela*>(input_file.src().section(i));
            auto end = rela + shdr.sh_size / shdr.sh_entsize;
            auto cmp = [](const Elf64_Rela &lhs, const Elf64_Rela &rhs){return lhs.r_offset < rhs.r_offset;};
            if (std::is_sorted(rela, end, cmp) == false)
                std::sort(rela, end, cmp);
        }
        else
            continue;
    }
}

void Input_file::Init_mergeable_section(Linking_context &ctx)
{
    m_mergeable_section_list.resize(m_src->section_hdr_table().header_count());

    for(std::size_t i = 0 ; i < m_src->section_hdr_table().header_count() ; i++)
    {
        auto &shdr = m_src->section_hdr(i);

        if ((shdr.sh_flags & SHF_MERGE) == 0 || shdr.sh_size == 0 || m_section_relocate_state_list[i] == eRelocate_state::no_need)
            continue;

        m_section_relocate_state_list[i] = eRelocate_state::mergeable;

        //std::cout << m_src->section_hdr_table().string_table().data() + shdr.sh_name << " megeable\n";

        m_mergeable_section_list[i] = std::make_unique<Mergeable_section>(Split_section(*m_src, ctx, i));
    }
}

void Input_file::Resolve_sesction_pieces(Linking_context &ctx)
{
    // Attach section pieces to symbols.
    for(std::size_t i = 1 ; i < src().symbol_table()->count() ; i++)
    {
        Symbol *sym = symbol_list[i];
        auto &esym = src().symbol_table()->data(i);

        if (nELF_util::Is_sym_abs(esym) || nELF_util::Is_sym_common(esym) || nELF_util::Is_sym_undef(esym))
            continue;

        auto shndx = src().get_shndx(esym);

        if (m_section_relocate_state_list[shndx] != eRelocate_state::mergeable)
            continue;

        if (m_mergeable_section_list[shndx]->frag_offset_list.empty() == true)
            continue;

        std::tie(sym->fragment, sym->val) 
            = m_mergeable_section_list[shndx]->get_fragment(esym.st_value);
    }

    uint64_t nfrag_syms = 0;
    for(std::size_t i = 0 ; i < m_input_section_list.size() ; i++)
    {
        if (m_section_relocate_state_list[i] == eRelocate_state::no_need)
            continue;
        
        if (!(m_input_section_list[i].shdr().sh_flags & SHF_ALLOC))
            continue;
        
        for(std::size_t rel_idx = 0 ; rel_idx < m_input_section_list[i].rel_count() ; rel_idx++)
        {
            auto rel = m_input_section_list[i].rela_at(rel_idx);
            auto &esym = src().symbol_table()->data(rel.sym());

            if (ELF64_ST_TYPE(esym.st_info) == STT_SECTION 
                && m_section_relocate_state_list[m_src->get_shndx(esym)] == eRelocate_state::mergeable)
                nfrag_syms++;
        }
    }

    frag_symbol_list.resize(nfrag_syms);

    // For each relocation referring a mergeable section symbol, we create
    // a new dummy non-section symbol and redirect the relocation to the
    // newly-created symbol.
    uint64_t idx = 0;
    for(std::size_t i = 0 ; i < m_input_section_list.size() ; i++)
    {
        if (m_section_relocate_state_list[i] == eRelocate_state::no_need)
            continue;
        
        if (!(m_input_section_list[i].shdr().sh_flags & SHF_ALLOC))
            continue;
        
        for(std::size_t rel_idx = 0 ; rel_idx < m_input_section_list[i].rel_count() ; rel_idx++)
        {
            auto rel = m_input_section_list[i].rela_at(rel_idx);
            auto &esym = src().symbol_table()->data(rel.sym());

            if (   ELF64_ST_TYPE(esym.st_info) != STT_SECTION 
                || m_section_relocate_state_list[m_src->get_shndx(esym)] != eRelocate_state::mergeable)
                continue;

            Mergeable_section &msection = *m_mergeable_section_list[m_src->get_shndx(esym)].get();
          
            std::string_view frag_data;
            std::size_t frag_offset;

            std::tie(frag_data, frag_offset) =  msection.get_fragment(esym.st_value + rel.r_addend);

            if (frag_data.empty() == true)
                FATALF("%s", "bad fragment relocated");

            auto &new_sym = frag_symbol_list[idx];
            new_sym = Symbol(*m_src, rel.sym());
            new_sym.name = "<fragment>";
            new_sym.sym_idx = rel.sym();
            new_sym.fragment = frag_data;
            new_sym.val = frag_offset - rel.r_addend;
            rel.Set_sym(m_src->symbol_table()->count() + idx); // redirect the relocation to the new symbol
            idx++;
        }
    }
    assert(idx == frag_symbol_list.size());

    for(auto &frag_sym : frag_symbol_list)
        symbol_list.push_back(&frag_sym);
}