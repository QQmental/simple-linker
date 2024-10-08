// a lot of code is copied or modified from https://github.com/rui314/mold

#include <algorithm>
#include <iostream>
#include "Relocatable_file.h"
#include "Symbol.h"
#include "Input_file.h"
#include "Linking_context.h"
#include "Mergeable_section.h"
#include "ELF_util.h"
#include "Chunk/Merged_section.h"

static void Init_local_symbols(std::unique_ptr<Symbol[]> &dst, const Relocatable_file &rel_file, std::size_t n_local_sym);

static std::unique_ptr<Mergeable_section> Split_section(const Relocatable_file &file, Linking_context &ctx, const Input_section &input_sec);

[[nodiscard]] static Merged_section* 
Get_merged_final_dst(Linking_context &ctx,
                     std::string_view name,
                     std::size_t type, 
                     std::size_t flags, 
                     std::size_t entsize, 
                     std::size_t addralign);

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
  
        dst[i] = std::move(sym);
    }
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



Input_file::Input_file(Relocatable_file &src) : m_src(&src)
{    
    m_relocate_state_list.resize(m_src->section_hdr_table().header_count());
    input_section_list.reserve(m_src->section_hdr_table().header_count());

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
                m_relocate_state_list[i] = eRelocate_state::no_need;
            break;
            
            default:
                std::string_view name = m_src->section_hdr_table().string_table().data() + shdr.sh_name;

                if (   name == ".debug_gnu_pubnames"
                    || name == ".debug_gnu_pubtypes"
                    || name == ".eh_frame") // not supported 
                {
                    m_relocate_state_list[i] = eRelocate_state::no_need;
                    break;
                }
                else
                {
                    if (   shdr.sh_type == SHT_INIT_ARRAY
                        || shdr.sh_type == SHT_FINI_ARRAY
                        || shdr.sh_type == SHT_PREINIT_ARRAY)
                        this->has_init_array = true;

                    if (   name == ".ctors"
                        || name.rfind(".ctors.", 0) == 0
                        || name == ".dtors" 
                        || name.rfind(".dtors.", 0) == 0)
                        this->has_ctors = true;

                    m_relocate_state_list[i] = eRelocate_state::relocatable;
                    input_section_list.push_back(Input_section(this->src(), i));
                    if (shdr.sh_info & SHF_COMPRESSED)
                        FATALF("%scompressed section is supported", "");
                }
            break;
        }
    }

    input_section_list.shrink_to_fit();
    
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
        
        if (auto *ptr = Get_input_section(shdr.sh_info) ; ptr != nullptr)
            ptr->Set_relsec_idx(i);
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
            if (m_relocate_state_list[src().get_shndx(esym)] == eRelocate_state::no_need)
                continue;
        }
      
        auto &link_pkg = ctx.Insert_global_symbol(*this, i);

        // a symbol having weak flag and not being synthetic which is not supported
        // Hence, no symbol's rank is compared.
        symbol_list[i] = link_pkg.symbol.get();
    }
}

[[nodiscard]] static Merged_section* 
Get_merged_final_dst(Linking_context &ctx,
                     std::string_view name,
                     std::size_t type, 
                     std::size_t flags, 
                     std::size_t entsize, 
                     std::size_t addralign)
{
    flags = flags & ~(uint64_t)SHF_GROUP & ~(uint64_t)SHF_COMPRESSED;

    std::unique_ptr<char[]> maybe_new_name;
    
    if (nELF_util::get_merged_output_name(&maybe_new_name, name, flags, entsize, addralign) == true)
        name = ctx.Insert_string(std::move(maybe_new_name));

    Linking_context::Output_merged_section_id id;
    id.name = name;
    id.type = type;
    id.flags = flags;
    id.entsize = entsize;
    auto it = ctx.merged_section_map().find(id);
   
    if (it != ctx.merged_section_map().end())
        return it->second.get();

    auto ptr = std::make_unique<Merged_section>(name, flags, type, entsize);
    
    return ctx.Insert_merged_section(id, std::move(ptr));
}             

// copied from https://github.com/rui314/mold
// Mergeable sections (sections with SHF_MERGE bit) typically contain
// string literals. Linker is expected to split the section contents
// into null-terminated strings, merge them with mergeable strings
// from other object files, and emit uniquified strings to an output
// file.
//
// This mechanism reduces the size of an output file. If two source
// files happen to contain the same string literal, the output will
// contain only a single copy of it.
//
// It is less common than string literals, but mergeable sections can
// contain fixed-sized read-only records too.
//
// This function splits the section contents into small pieces that we
// call "section fragments". Section fragment is a unit of merging.
//
// We do not support mergeable sections that have relocations.
static std::unique_ptr<Mergeable_section> Split_section(const Relocatable_file &file, Linking_context &ctx, const Input_section &input_sec)
{
    std::unique_ptr<Mergeable_section> ret;

    auto &shdr = input_sec.shdr();

    std::size_t entsize = shdr.sh_entsize;

    if (entsize == 0)
        entsize = (shdr.sh_flags & SHF_STRINGS) ? 1 : (int)shdr.sh_addralign;

    if (entsize == 0)
        return nullptr;

    uint64_t addralign = shdr.sh_addralign;
    if (addralign == 0)
        addralign = 1;
    
    ret = std::make_unique<Mergeable_section>();

    ret->data = input_sec.data;
    ret->p2_align = nUtil::to_p2align(shdr.sh_addralign);

    ret->final_dst = Get_merged_final_dst(ctx, input_sec.name(), shdr.sh_type, shdr.sh_flags, entsize, addralign);

    if (ret->data.size() > UINT32_MAX)
        FATALF("the mergeable section size %ld, is too large",ret->data.size());

    auto push_frag_info = [&ret](std::size_t start, std::size_t end) 
    {
        ret->piece_offset_list.push_back(start);
        ret->piece_hash_list.push_back(std::hash<std::string_view>{}(ret->data.substr(start, end - start)));
    };

    if (input_sec.shdr().sh_flags & SHF_STRINGS)
    {
        for(std::size_t pos = 0 ; pos < ret->data.size() ;)
        {    
            size_t end = nUtil::Find_null(ret->data, pos, entsize);
            
            if (end == ret->data.npos)
            {
                std::cout << ret->data;
                FATALF(":string is not null terminated");
            }
            push_frag_info(pos, end);
            pos = end + entsize;
        }
    }
    else
    {
        if (ret->data.size() % entsize)
            FATALF("%ld : section size is not multiple of sh_entsize", ret->data.size());

        ret->piece_offset_list.reserve(ret->data.size() / entsize);

        for (std::size_t pos = 0; pos < ret->data.size(); pos += entsize)
            push_frag_info(pos, pos + entsize);
    }
    return ret;
}




void Input_file::Init_mergeable_section(Linking_context &ctx)
{
    m_mergeable_section_list.resize(m_src->section_hdr_table().header_count());

    for(Input_section &isec : input_section_list)
    {
        auto shndx = isec.shndx;

        auto &shdr = m_src->section_hdr(shndx);

        // if a section that is mergeable needs relocation, it is not transformed into a "Mergeable_section"
        if (   (shdr.sh_flags & SHF_MERGE) == 0 
            || shdr.sh_size == 0 
            || m_relocate_state_list[shndx] == eRelocate_state::no_need
            || isec.rel_count() != 0)
            continue;

        m_relocate_state_list[shndx] = eRelocate_state::mergeable;

        m_mergeable_section_list[shndx] = Split_section(*m_src, ctx, isec);
    }
}

void Input_file::Collect_mergeable_section_piece()
{
    for (std::unique_ptr<Mergeable_section> &m : m_mergeable_section_list)
    {
        if (m)
        {
            m->piece_list.resize(m->piece_offset_list.size());

            for (std::size_t i = 0; i < m->piece_offset_list.size(); i++)
            {
                Merged_section::Piece *piece = m->final_dst->Insert(m->Get_contents(i), m->piece_hash_list[i], m->p2_align);
                m->piece_list[i] = piece;
            }

            // Reclaim memory as we'll never use this vector again
            m->piece_hash_list.clear();
            m->piece_hash_list.shrink_to_fit();
        }
    }
}

void Input_file::Resolve_sesction_pieces(Linking_context &ctx)
{
    // Attach mergeable section pieces to defined symbols.
    for(std::size_t i = 1 ; i < src().symbol_table()->count() ; i++)
    {
        Symbol *sym = symbol_list[i];
        auto &esym = src().symbol_table()->data(i);

        if (nELF_util::Is_sym_abs(esym) || nELF_util::Is_sym_common(esym) || nELF_util::Is_sym_undef(esym))
            continue;

        auto shndx = src().get_shndx(esym);

        if (   m_relocate_state_list[shndx] != eRelocate_state::mergeable
            || m_mergeable_section_list[shndx] == nullptr
            || m_mergeable_section_list[shndx]->piece_offset_list.empty() == true)
            continue;

        auto pair = m_mergeable_section_list[shndx]->Get_mergeable_piece(esym.st_value);
        sym->Set_piece(*pair.first);
        sym->val = pair.second;
    }

    // calculate the number of relocation needed mergeable section symbol
    uint64_t nfrag_syms = 0;
    for(Input_section &isec : input_section_list)
    {
        auto shndx = isec.shndx;

        if (m_relocate_state_list[shndx] == eRelocate_state::no_need)
            continue;
        
        if (!(isec.shdr().sh_flags & SHF_ALLOC))
            continue;
        
        for(std::size_t rel_idx = 0 ; rel_idx < isec.rel_count() ; rel_idx++)
        {
            auto rel = isec.rela_at(rel_idx);
            auto &esym = src().symbol_table()->data(rel.sym());

            if (ELF64_ST_TYPE(esym.st_info) == STT_SECTION 
                && m_relocate_state_list[m_src->get_shndx(esym)] == eRelocate_state::mergeable)
                nfrag_syms++;
        }
    }

    m_mergeable_section_symbol_list.resize(nfrag_syms);

    // For each relocation referring a mergeable section symbol, we create
    // a new dummy non-section symbol and redirect the relocation to the
    // newly-created symbol.
    uint64_t idx = 0;
    for(Input_section &isec : input_section_list)
    {
        auto shndx = isec.shndx;

        if (m_relocate_state_list[shndx] == eRelocate_state::no_need)
            continue;
        
        if (!(isec.shdr().sh_flags & SHF_ALLOC))
            continue;
        
        for(std::size_t rel_idx = 0 ; rel_idx < isec.rel_count() ; rel_idx++)
        {
            auto rel = isec.rela_at(rel_idx);
            auto &esym = src().symbol_table()->data(rel.sym());

            if (   ELF64_ST_TYPE(esym.st_info) != STT_SECTION 
                || m_mergeable_section_list[m_src->get_shndx(esym)].get() == nullptr)
                continue;

            Mergeable_section &msection = *m_mergeable_section_list[m_src->get_shndx(esym)].get();
          
            Merged_section::Piece *mergeable_section_piece;
            std::size_t sym_offset;

            std::tie(mergeable_section_piece, sym_offset) = msection.Get_mergeable_piece(esym.st_value + rel.r_addend);

            if (mergeable_section_piece == nullptr)
                FATALF("%s", "bad fragment relocated");

            auto &new_sym = m_mergeable_section_symbol_list[idx];
            new_sym = Symbol(src(), rel.sym());
            new_sym.name = "<fragment>";
            new_sym.Set_piece(*mergeable_section_piece);
            new_sym.val = sym_offset - rel.r_addend;
            rel.Set_sym(m_src->symbol_table()->count() + idx); // redirect the relocation to the new symbol, 
                                                               // the 'idx' is the index in m_mergeable_section_symbol_list
            idx++;
        }
    }
    assert(idx == m_mergeable_section_symbol_list.size());

    for(auto &frag_sym : m_mergeable_section_symbol_list)
        symbol_list.push_back(&frag_sym);
}

static bool should_write_to_local_symtab(Linking_context &ctx, Symbol &sym, const Input_file &file)
{
    if (sym.Get_type() == STT_SECTION)
        return false;

    //discard mergeable symbols
    if (sym.name.rfind(".L", 0) == 0)
    {
        return false;

        if (auto *isec = const_cast<Input_file*>(&file)->Get_symbol_input_section(sym))
        {
            if (isec->shdr().sh_flags & SHF_MERGE)
               return false;
        }
    }

    return true;
}

void Input_file::Compute_symtab_size(Linking_context &ctx)
{
    this->output_sym_indices.resize(this->src().symbol_table()->count(), -1);

    // nullptr check is done in this function, and it returns false
    auto is_alive = [&](const Symbol *sym) -> bool
    {
        if (sym == nullptr)
            return false;

        if (Merged_section::Piece *piece = sym->piece())
            return piece->is_alive;

        return Get_symbol_input_section(*sym) != nullptr;
    };

    for (std::size_t i = 1; i < this->n_local_sym(); i++)
    {
        Symbol *sym = this->symbol_list[i];

        if (is_alive(sym) && should_write_to_local_symtab(ctx, *sym, *this))
        {
            this->strtab_size += sym->name.size() + 1;
            this->output_sym_indices[i] = this->num_local_symtab++;
            sym->write_to_symtab = true;
        }
    }

    // Compute the size of global symbols.
    for (std::size_t i = this->n_local_sym(); i < this->src().symbol_table()->count(); i++)
    {
        Symbol *sym = this->symbol_list[i];

        if (is_alive(sym) && sym->file() == &this->src() && sym->write_to_symtab)
        {
            this->strtab_size += sym->name.size() + 1;
            // Global symbols can be demoted to local symbols based on visibility,
            // version scripts etc.
            if (nELF_util::Is_sym_local(sym->elf_sym()))
                this->output_sym_indices[i] = this->num_local_symtab++;
            else
                this->output_sym_indices[i] = this->num_global_symtab++;
            sym->write_to_symtab = true;
        }
    }
}