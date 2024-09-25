#include <iostream>
#include <unordered_map>
#include <numeric>
#include <atomic>
#include <algorithm>
#include <queue>

#include "Linking_passes.h"
#include "Linking_context_helper.h"
#include "ELF_util.h"
#include "Chunk/Output_section.h"

using nLinking_context_helper::to_phdr_flags;
using nUtil::bit;
using nUtil::EPOI;
// a lot of code is copied from https://github.com/rui314/mold

static void Set_virtual_addresses(Linking_context &ctx);
static std::size_t Set_file_offsets(Linking_context &ctx);
static bool Is_tbss(const Chunk *chunk);
static uint64_t Align_with_skew(uint64_t val, uint64_t align, uint64_t skew);
static void write_itype(uint8_t *loc, uint32_t val);
static void write_stype(uint8_t *loc, uint32_t val);
static void write_btype(uint8_t *loc, uint32_t val);
static void write_utype(uint8_t *loc, uint32_t val);
static void write_jtype(uint8_t *loc, uint32_t val);
static void write_citype(uint8_t *loc, uint32_t val);
static void write_cbtype(uint8_t *loc, uint32_t val);
static void write_cjtype(uint8_t *loc, uint32_t val);
static void set_rs1(uint8_t *loc, uint32_t rs1);
static void Reloc_alloc(Linking_context &ctx, Output_section &osec, std::size_t isec_idx);
static void Reloc_non_alloc(Linking_context &ctx, Output_section &osec, std::size_t isec_idx);

void nLinking_passes::Check_duplicate_smbols(const Input_file &file)
{
    if (file.src().symbol_table() == nullptr)
        return;

    for (std::size_t i = file.n_local_sym(); i < file.src().symbol_table()->count(); i++)
    {
        auto &esym = file.src().symbol_table()->data(i);
        
        // a global symbol could be a nullptr
        auto *sym = file.symbol_list[i];

        if (   sym != nullptr
            && (sym->file() == &file.src()))
            continue;
        if (nELF_util::Is_sym_undef(esym) || nELF_util::Is_sym_common(esym))
            continue;
    
        if (nELF_util::Is_sym_abs(esym) == false)
        {
            std::size_t shndx = file.src().get_shndx(esym);

            // Skip if the symbol is in a dead section.
            if (file.relocate_state_list()[shndx] == Input_file::eRelocate_state::no_need)
                continue;
        }
        std::cout << "duplicate symbols " << file.name() << " " << sym->file()->name() << "\n";
        abort();
    }
}

// Bind undef global symbols in symbol list of a input file to defined global symbols.
// If a file is not referenced by other files through global symbols, then
// content of this file is no need to be linked into the output file.
// A file with no reference will be discarded from the file list.
void nLinking_passes::Reference_dependent_file(Input_file &input_file, 
                                               Linking_context &ctx, 
                                               const std::function<void(const Input_file&)> &reference_file)
{        
    for(std::size_t sym_idx = input_file.src().linking_mdata().first_global ; sym_idx < input_file.src().symbol_table()->count() ; sym_idx++)
    {
        if (input_file.symbol_list[sym_idx] != nullptr) //not nullptr, no need to be binded to the global symbal 
            continue;

        auto &esym = input_file.src().symbol_table()->data(sym_idx);

        // a file would be referenced when it has a defined symbol 
        // which is not defined in 'the other' file

        // it's defined, don't mark alive its source, because it's itself
        bool undef = nELF_util::Is_sym_undef(esym) == true;
        bool common = nELF_util::Is_sym_common(esym) == true && nELF_util::Is_sym_common(input_file.symbol_list[sym_idx]->elf_sym()) == false;
        
        if (undef == false && common == false)
            continue;
        
        auto it = ctx.Find_symbol(nELF_util::Get_symbol_name(input_file.src(), sym_idx));

        if (it == ctx.global_symbol_map().end())
        {
/*              std::cout << nELF_util::Get_symbol_name(input_file.src(), sym_idx) << \
            " size: " << esym.st_size << " type: " << ELF64_ST_TYPE(esym.st_info) << \
            " bind: " << ELF64_ST_BIND(esym.st_info) << " not found \n";  */
            FATALF("undefined symbol");
            continue;
        }

        // bind the undef symbol with the defined global symbol which is from the other file
        input_file.symbol_list[sym_idx] = it->second.Mark_ref();

        reference_file(*it->second.input_file);
    }
}

void nLinking_passes::Resolve_symbols(Linking_context &ctx, std::vector<Input_file> &input_file_list, std::vector<bool> &is_alive)
{    
    std::queue<std::size_t> file_idx_queue;

    std::function reference_file = [&ctx, &input_file_list, &is_alive, &file_idx_queue](const Input_file &src)->void
    {
        assert(&src >= &*input_file_list.begin() && &src < &*input_file_list.end());
        
        auto idx = &src - &*input_file_list.begin();
        if (is_alive[idx] == false)
        {
            file_idx_queue.push(idx);
            is_alive[idx] = true;
        }
    };


    for(std::size_t i = 0 ; i < input_file_list.size() ; i++)
    {
        if (is_alive[i] == true)
            file_idx_queue.push(i);
    }

    while(file_idx_queue.empty() == false)
    {
        auto idx = file_idx_queue.front();
        file_idx_queue.pop();
        nLinking_passes::Reference_dependent_file(input_file_list[idx], ctx, reference_file);
    }
}

void nLinking_passes::Combined_input_sections(Linking_context &ctx)
{
    using counter_t = std::size_t;

    struct Osec_bind_state
    {
        Osec_bind_state(Output_section *osec):osec(osec), member_cnt(std::make_unique<counter_t>(0)), sh_flag(std::make_unique<counter_t>(0)){}

        Osec_bind_state(const Osec_bind_state &src) = delete;
        Osec_bind_state(Osec_bind_state &&src) :osec(src.osec), member_cnt(std::move(src.member_cnt)), sh_flag(std::move(src.sh_flag))
        {

        }
        Output_section *osec;
        std::unique_ptr<counter_t> member_cnt;
        std::unique_ptr<counter_t> sh_flag;
    };

    // all of output_section in osec_map will be move into ctx.osec_pool,
    // so there is no life time issue due to Output_section*
    std::unordered_map<Output_section_key, Osec_bind_state, Output_section_key::Hash_func> osec_map;

    bool ctors_in_init_array = nLinking_context_helper::Has_ctors_and_init_array(ctx);

    struct IN_OUT_section_bind
    {
        const Input_section *isec;
        Output_section *osec;
        const Input_file *owner_file;
        std::size_t member_offset;
    };

    std::size_t isec_cnt = 0;
    counter_t bind_offset = 0;

    for(const Input_file &input_file : ctx.input_file_list())
    {
        for(auto state : input_file.relocate_state_list())
            isec_cnt += state == Input_file::eRelocate_state::relocatable;
    }

    std::vector<IN_OUT_section_bind> in_out_section_bind(isec_cnt);

    for(const Input_file &input_file : ctx.input_file_list())
    {
        for(const Input_section &isec : input_file.input_section_list)
        {
            if (input_file.relocate_state_list()[isec.shndx] != Input_file::eRelocate_state::relocatable)
                continue;

            auto &shdr = isec.shdr();
            
            auto sh_flags = shdr.sh_flags
                          & ~SHF_MERGE 
                          & ~SHF_STRINGS 
                          & ~SHF_COMPRESSED
                          & ~SHF_GROUP;

            
            auto merge_input_section = [&]()->void
            {
                auto key = nLinking_passes::Get_output_section_key(ctx, isec, ctors_in_init_array);

                Osec_bind_state *targ;

                if (auto it = osec_map.find(key) ; it != osec_map.end())
                {
                    targ = &it->second;
                }           
                else
                {
                    auto ptr = new Output_section(key);
                    auto it2 = osec_map.insert(std::make_pair(key, Osec_bind_state{ptr}));

                    targ = &it2.first->second;
                }
                auto offset = bind_offset++;
                in_out_section_bind[offset].isec = &isec;
                in_out_section_bind[offset].osec = targ->osec;
                in_out_section_bind[offset].owner_file = &input_file;
                in_out_section_bind[offset].member_offset = (*(targ->member_cnt))++;

                if ((*(targ->sh_flag) & sh_flags) != sh_flags)
                    *(targ->sh_flag) |= sh_flags;
            };

            merge_input_section();
        }
    }

    for(auto &item : osec_map)
    {
        item.second.osec->shdr.sh_flags = *item.second.sh_flag;
        item.second.osec->member_list.resize(*item.second.member_cnt);
    }
    
    for(auto &obs : osec_map)
       ctx.Insert_osec(std::unique_ptr<Output_section>(obs.second.osec));

    // Add input sections to output sections
    for(auto &io_bind : in_out_section_bind)
    {
        io_bind.osec->member_list[io_bind.member_offset].isec = io_bind.isec;
        io_bind.osec->member_list[io_bind.member_offset].file = io_bind.owner_file;
        //std::cout << io_bind.osec->name << " " << io_bind.member_offset << " " << io_bind.isec->shndx << "\n";
    }
}


void nLinking_passes::Bind_special_symbols(Linking_context &ctx)
{
    auto &symbols = ctx.special_symbols;

    auto it = ctx.global_symbol_map().find(symbols.entry_name) ; 
    if (it == ctx.global_symbol_map().end())
    {
        FATALF("%s", "entry symbol is not found!");
    }
    else
        symbols.entry = it->second.Mark_ref();

    it = ctx.global_symbol_map().find(symbols.fiini_name) ; 
    if (it != ctx.global_symbol_map().end())
        symbols.fiini = it->second.Mark_ref();

    it = ctx.global_symbol_map().find(symbols.init_name) ; 
    if (it != ctx.global_symbol_map().end())
        symbols.init = it->second.Mark_ref();

    it = ctx.global_symbol_map().find(symbols.bss_start_name) ; 
    assert(it != ctx.global_symbol_map().end());
    symbols.bss_start = it->second.Mark_ref();

    it = ctx.global_symbol_map().find(symbols.end_name) ; 
    assert(it != ctx.global_symbol_map().end());
    symbols.end = it->second.Mark_ref();


}


void nLinking_passes::Assign_input_section_offset(Linking_context &ctx)
{
    for(auto &[p_osec, osec]: ctx.osec_pool())
    {
        std::size_t offset = 0, p2align = 0;
        for(std::size_t idx = 0 ; idx < osec->member_list.size() ; idx++)
        {
            auto &isec = *osec->member_list[idx].isec;

            offset = nUtil::align_to(offset, 1 << p2align);
            osec->member_list[idx].offset = offset;
            
            offset += isec.shdr().sh_size;
            p2align = std::max(p2align, isec.shdr().sh_addralign);
        }

        osec->shdr.sh_size = offset;
        osec->shdr.sh_addralign = 1 << p2align;
    }
}

void nLinking_passes::Create_synthetic_sections(Linking_context &ctx)
{
    ctx.phdr = ctx.Insert_chunk(std::make_unique<Output_phdr>(0));
    ctx.ehdr = ctx.Insert_chunk(std::make_unique<Output_ehdr>(0));
    ctx.shdr = ctx.Insert_chunk(std::make_unique<Output_shdr>());
    ctx.symtab_section = ctx.Insert_chunk(std::make_unique<Symtab_section>());
    ctx.shstrtab_section = ctx.Insert_chunk(std::make_unique<Shstrtab_section>());
    ctx.strtab_section = ctx.Insert_chunk(std::make_unique<Strtab_section>());
    ctx.riscv_attributes_section = ctx.Insert_chunk(std::make_unique<Riscv_attributes_section>());
}

void nLinking_passes::Sort_output_sections(Linking_context &ctx)
{
    auto get_rank1 = [&ctx](const Chunk *chunk)->int32_t
    {
        auto type = chunk->shdr.sh_type;
        auto flags = chunk->shdr.sh_flags;

        if (chunk == ctx.ehdr)
            return 0;
        if (chunk == ctx.phdr)
            return 1;
        if (type == SHT_NOTE && (flags & SHF_ALLOC))
            return 3;
        if (chunk == ctx.shdr)
            return INT32_MAX -1;

        
        bool alloc = (flags & SHF_ALLOC);
        bool writable = (flags & SHF_WRITE);
        bool exec = (flags & SHF_EXECINSTR);
        bool tls = (flags & SHF_TLS);
        bool is_bss = (type == SHT_NOBITS);

        return   (1 << 10) | (!alloc << 9) | (writable << 8) 
               | (exec << 7) | (!tls << 6) | (is_bss << 4);
    };

    auto get_rank2 = [&ctx](const Chunk *chunk) -> int64_t
    {
        auto &shdr = chunk->shdr;
        if (shdr.sh_type == SHT_NOTE)
            return -shdr.sh_addralign;

        if (chunk == ctx.got)
            return 2;
        if (chunk->name == ".toc")
            return 3;
        if (chunk->name == ".alpha_got")
            return 4;
        return 0;
    };

    ctx.output_chunk_list.sort([&](const Output_chunk &a, const Output_chunk &b)
    {
        return  std::tuple{get_rank1(&a.chunk()), get_rank2(&a.chunk()), a.chunk().name}
            < std::tuple{get_rank1(&b.chunk()), get_rank2(&b.chunk()), b.chunk().name};
    });

}



elf64_sym nLinking_passes::to_output_esym(Linking_context &ctx, Symbol &sym, uint32_t st_name, uint32_t *shndx)
{
    FATALF("%s", "TODO function");
}

static void Populate_symtab(Linking_context &ctx, const Input_file &input_file)
{
    elf64_sym *symtab_base = (elf64_sym *)(ctx.buf + ctx.symtab_section->shdr.sh_offset);

    uint8_t *strtab_base = ctx.buf + ctx.strtab_section->shdr.sh_offset;
    uint64_t strtab_off = input_file.strtab_offset;

    auto write_sym = [&](Symbol &sym, std::size_t idx)
    {
        uint32_t *xindex = nullptr;
        if (ctx.symtab_shndx_section)
            xindex = (uint32_t*)(ctx.buf + ctx.symtab_shndx_section->shdr.sh_offset) + idx;

        symtab_base[idx] = nLinking_passes::to_output_esym(ctx, sym, strtab_off, xindex);
        strtab_off += nUtil::Write_string(strtab_base + strtab_off, sym.name);
    };

    uint64_t local_symtab_idx = input_file.local_symtab_idx;
    uint64_t global_symtab_idx = input_file.global_symtab_idx;

    for (std::size_t i = 1; i < input_file.n_local_sym() ; i++)
    {
        if (Symbol &sym = *input_file.symbol_list[i]; sym.write_to_symtab)
           write_sym(sym, local_symtab_idx++);
    }

    for (std::size_t i = input_file.n_local_sym(); i < input_file.src().symbol_table()->count(); i++)
    {
        Symbol *sym = input_file.symbol_list[i];
        if (sym && sym->file() == &input_file.src() && sym->write_to_symtab)
        {
            if (nELF_util::Is_sym_local(sym->elf_sym()))
                write_sym(*sym, local_symtab_idx++);
            else
                write_sym(*sym, global_symtab_idx++);
        }
    }
}

void Populate_symtab(Linking_context &ctx)
{
    for(auto &input_file : ctx.input_file_list())
        Populate_symtab(ctx, input_file);

    for(auto &output_chunk : ctx.output_chunk_list)
        output_chunk.Populate_symtab();
    
}

void nLinking_passes::Compute_section_headers(Linking_context &ctx)
{
    // Update sh_size for each chunk.
    for(auto &output_chunk : ctx.output_chunk_list)
        output_chunk.Update_shdr();
    
    // Remove empty chunks.
    for(auto it = ctx.output_chunk_list.begin() ; it != ctx.output_chunk_list.end() ;)
    {
        if (it->is_osec() == false && it->chunk().shdr.sh_size == 0)
            it = ctx.output_chunk_list.erase(it);
        else
            ++it;
    }

    // Set section indices.
    // section index 0 is reserved as an undefined value
    int64_t shndx = 1;
    for (auto &output_chunk : ctx.output_chunk_list)
    {
        if (output_chunk.chunk().is_header() == false)
            output_chunk.chunk().shndx = shndx++;
    }
    
    if (ctx.symtab_section && SHN_LORESERVE <= shndx)
    {
        Symtab_shndx_section *sec = new Symtab_shndx_section;
        sec->shndx = shndx++;
        sec->shdr.sh_link = ctx.symtab_section->shndx;
        ctx.symtab_shndx_section = ctx.Insert_chunk(std::unique_ptr<Symtab_shndx_section>(sec));
    }

    if (ctx.shdr)
        ctx.shdr->shdr.sh_size = shndx * sizeof(elf64_shdr);
    
    // Some types of section header refer other section by index.
    // Recompute the section header to fill such fields with correct values.
    for (auto &output_chunk : ctx.output_chunk_list)
        output_chunk.Update_shdr();

    if (ctx.symtab_shndx_section)
    {
        std::size_t symtab_size = ctx.symtab_section->shdr.sh_size / sizeof(elf64_sym);
        ctx.symtab_shndx_section->shdr.sh_size = symtab_size * 4;
    }
}

static bool Is_tbss(const Chunk *chunk)
{
  return (chunk->shdr.sh_type == SHT_NOBITS) && (chunk->shdr.sh_flags & SHF_TLS);
}

//just cpoied from https://github.com/rui314/mold
static void Set_virtual_addresses(Linking_context &ctx)
{
    uint64_t addr = ctx.image_base;

    // TLS chunks alignments are special: in addition to having their virtual
    // addresses aligned, they also have to be aligned when the region of
    // tls_begin is copied to a new thread's storage area. In other words, their
    // offset against tls_begin also has to be aligned.
    //
    // A good way to achieve this is to take the largest alignment requirement
    // of all TLS sections and make tls_begin also aligned to that.
    Chunk *first_tls_chunk = nullptr;
    uint64_t tls_alignment = 1;
    for (Output_chunk &output_chunk : ctx.output_chunk_list)
    {
        if (output_chunk.chunk().shdr.sh_flags & SHF_TLS)
        {
            if (!first_tls_chunk)
                first_tls_chunk = &output_chunk.chunk();
            tls_alignment = std::max(tls_alignment, (uint64_t)output_chunk.chunk().shdr.sh_addralign);
        }
    }

    auto alignment = [&](const Chunk &chunk)
    {
        return &chunk == first_tls_chunk ? tls_alignment : (uint64_t)chunk.shdr.sh_addralign;
    };

    for (auto it = ctx.output_chunk_list.begin() ; it != ctx.output_chunk_list.end() ; ++it)
    {
        if (!(it->chunk().shdr.sh_flags & SHF_ALLOC))
            continue;
    
        // Memory protection works at page size granularity. We need to
        // put sections with different memory attributes into different
        // pages. We do it by inserting paddings here.
        if (it != ctx.output_chunk_list.begin())
        {
            uint64_t flags1 = to_phdr_flags(ctx, std::prev(it)->chunk());
            uint64_t flags2 = to_phdr_flags(ctx, it->chunk());
            
            if (flags1 != flags2)
            {
                if (addr % ctx.page_size != 0)
                    addr += ctx.page_size;
            }
        }

        // TLS BSS sections are laid out so that they overlap with the
        // subsequent non-tbss sections. Overlapping is fine because a STT_TLS
        // segment contains an initialization image for newly-created threads,
        // and no one except the runtime reads its contents. Even the runtime
        // doesn't need a BSS part of a TLS initialization image; it just
        // leaves zero-initialized bytes as-is instead of copying zeros.
        // So no one really read tbss at runtime.
        //
        // We can instead allocate a dedicated virtual address space to tbss,
        // but that would be just a waste of the address and disk space.
        if (Is_tbss(&it->chunk()))
        {
            uint64_t addr2 = addr;
            for (;;)
            {
                addr2 = nUtil::align_to(addr2, alignment(it->chunk()));
                it->chunk().shdr.sh_addr = addr2;
                addr2 += it->chunk().shdr.sh_size;
                if (std::next(it, 2) == ctx.output_chunk_list.end() || !Is_tbss(&std::next(it)->chunk()))
                    break;
                ++it;
            }
            continue;
        }

        addr = nUtil::align_to(addr, alignment(it->chunk()));
        it->chunk().shdr.sh_addr = addr;
        addr += it->chunk().shdr.sh_size;
    }
}


// Returns the smallest integer N that satisfies N >= val and
// N mod align == skew mod align.
//
// Section's file offset must be congruent to its virtual address modulo
// the page size. We use this function to satisfy that requirement.
// just cpoied from https://github.com/rui314/mold
static uint64_t Align_with_skew(uint64_t val, uint64_t align, uint64_t skew)
{
  uint64_t x = nUtil::align_down(val, align) + skew % align;
  return (val <= x) ? x : x + align;
}






// just cpoied from https://github.com/rui314/mold
// assign file offset to output chunks
static std::size_t Set_file_offsets(Linking_context &ctx)
{
    uint64_t fileoff = 0;

    for(auto it = ctx.output_chunk_list.begin() ; it != ctx.output_chunk_list.end() ;)
    {
        Chunk &first = it->chunk();

        if ((first.shdr.sh_flags & SHF_ALLOC) == false)
        {
            fileoff = nUtil::align_to(fileoff, first.shdr.sh_addralign);
            first.shdr.sh_offset = fileoff;
            fileoff += first.shdr.sh_size;
            ++it;
            continue;
        }

        if (first.shdr.sh_type == SHT_NOBITS)
        {
            first.shdr.sh_offset = fileoff;
            ++it;
            continue;
        }

        if (first.shdr.sh_addralign > ctx.page_size)
            fileoff = nUtil::align_to(fileoff, first.shdr.sh_addralign);
        else
            fileoff = Align_with_skew(fileoff, ctx.page_size, first.shdr.sh_addr);


        // Assign ALLOC sections contiguous file offsets as long as they
        // are contiguous in memory.
        for (;;)
        {
            it->chunk().shdr.sh_offset = fileoff 
                                       + it->chunk().shdr.sh_addr 
                                       - first.shdr.sh_addr;
            ++it;

            if (   it == ctx.output_chunk_list.end()
                || (it->chunk().shdr.sh_flags & SHF_ALLOC) == false
                || it->chunk().shdr.sh_type == SHT_NOBITS)
                break;

            uint64_t gap_size = it->chunk().shdr.sh_addr 
                              - std::prev(it)->chunk().shdr.sh_addr
                              - std::prev(it)->chunk().shdr.sh_size;

            // If --start-section is given, there may be a large gap between
            // sections. We don't want to allocate a disk space for a gap if
            // exists.
            if (gap_size >= ctx.page_size)
                break;
        }

        fileoff = std::prev(it)->chunk().shdr.sh_offset + std::prev(it)->chunk().shdr.sh_size;

        while (   it != ctx.output_chunk_list.end()
               && (it->chunk().shdr.sh_flags & SHF_ALLOC)
               && it->chunk().shdr.sh_type == SHT_NOBITS)
        {
            it->chunk().shdr.sh_offset = fileoff;
            ++it;
        }
    }

    return fileoff;
}


[[nodiscard]] std::size_t nLinking_passes::Set_output_chunk_locations(Linking_context &ctx)
{ 
    for (;;)
    {
        Set_virtual_addresses(ctx);

        // Assigning new offsets may change the contents and the length
        // of the program header, so repeat it until converge.
        std::size_t fileoff = Set_file_offsets(ctx);

        if (ctx.phdr)
        {
            auto sz = ctx.phdr->shdr.sh_size;
            Output_chunk(ctx.phdr, ctx).Update_shdr();
            
            if (sz != ctx.phdr->shdr.sh_size)
                continue;
        }

        return fileoff;
  }
}

void nLinking_passes::Fix_up_synthetic_symbols(Linking_context &ctx)
{
    auto it = std::find_if(ctx.output_chunk_list.begin(), 
                           ctx.output_chunk_list.end(), 
                           [&ctx](const Output_chunk &chk){return chk.chunk().name == ".bss";});

    if (it != ctx.output_chunk_list.end())
        ctx.special_symbols.bss_start->val = it->chunk().shdr.sh_addr;


    for (Output_chunk &output_chunk : ctx.output_chunk_list)
    {
        if (!output_chunk.chunk().is_header() && (output_chunk.chunk().shdr.sh_flags & SHF_ALLOC))
            continue;
    
        if (output_chunk.chunk().shdr.sh_flags & SHF_ALLOC)
        {
            ctx.special_symbols._end->val = it->chunk().shdr.sh_addr;
            ctx.special_symbols.end->val = it->chunk().shdr.sh_addr;
        }

        if (output_chunk.chunk().shdr.sh_flags & SHF_EXECINSTR)
        {
            ctx.special_symbols._etext->val = it->chunk().shdr.sh_addr;
            ctx.special_symbols.etext->val = it->chunk().shdr.sh_addr;
        }

        if (output_chunk.chunk().shdr.sh_type != SHT_NOBITS && (output_chunk.chunk().shdr.sh_flags & SHF_ALLOC))
        {
            ctx.special_symbols._edata->val = it->chunk().shdr.sh_addr;
            ctx.special_symbols.edata->val = it->chunk().shdr.sh_addr;
        }
    }


    
}

void nLinking_passes::Copy_chunks(Linking_context &ctx)
{
    for(auto &output_chunk : ctx.output_chunk_list)
    {
        if (output_chunk.chunk().shdr.sh_type != SHT_REL)
            output_chunk.Copy_chunk();
    }

    for(auto &output_chunk : ctx.output_chunk_list)
    {
        if (output_chunk.chunk().shdr.sh_type == SHT_REL)
            output_chunk.Copy_chunk();
    }
}

static void write_itype(uint8_t *loc, uint32_t val)
{
    *(uint32_t *)loc &= 0b000000'00000'11111'111'11111'1111111;
    *(uint32_t *)loc |= nUtil::EPOI(val, 11, 0) << 20;
}

static void write_stype(uint8_t *loc, uint32_t val)
{
    *(uint32_t *)loc &= 0b000000'11111'11111'111'00000'1111111;
    *(uint32_t *)loc |= nUtil::EPOI(val, 11, 5) << 25 
                    | nUtil::EPOI(val, 4, 0) << 7;
}

static void write_btype(uint8_t *loc, uint32_t val)
{
    *(uint32_t *)loc &= 0b000000'11111'11111'111'00000'1111111;
    *(uint32_t *)loc |= nUtil::bit(val, 12) << 31 
                    | nUtil::EPOI(val, 10, 5) << 25 
                    | nUtil::EPOI(val, 4, 1) << 8
                    | nUtil::bit(val, 11) << 7;
}

static void write_utype(uint8_t *loc, uint32_t val)
{
    *(uint32_t *)loc &= 0b000000'00000'00000'000'11111'1111111;

    // U-type instructions are used in combination with I-type
    // instructions. U-type insn sets an immediate to the upper 20-bits
    // of a register. I-type insn sign-extends a 12-bits immediate and
    // adds it to a register value to construct a complete value. 0x800
    // is added here to compensate for the sign-extension.
    *(uint32_t *)loc |= (val + 0x800) & 0xffff'f000;
}

static void write_jtype(uint8_t *loc, uint32_t val)
{
    *(uint32_t *)loc &= 0b000000'00000'00000'000'11111'1111111;
    *(uint32_t *)loc |= nUtil::bit(val, 20) << 31
                      | nUtil::EPOI(val, 10, 1) << 21 
                      | nUtil::bit(val, 11) << 20
                      | nUtil::EPOI(val, 19, 12) << 12;
}

static void write_citype(uint8_t *loc, uint32_t val)
{
    *(uint16_t *)loc &= 0b111'0'11111'00000'11;
    *(uint16_t *)loc |= bit(val, 5) << 12 | nUtil::EPOI(val, 4, 0) << 2;
}

static void write_cbtype(uint8_t *loc, uint32_t val)
{
    *(uint16_t *)loc &= 0b111'000'111'00000'11;
    *(uint16_t *)loc |= bit(val, 8) << 12 | bit(val, 4) << 11 | bit(val, 3) << 10 |
                        bit(val, 7) << 6  | bit(val, 6) << 5  | bit(val, 2) << 4  |
                        bit(val, 1) << 3  | bit(val, 5) << 2;
}

static void write_cjtype(uint8_t *loc, uint32_t val)
{
    *(uint16_t *)loc &= 0b111'00000000000'11;
    *(uint16_t *)loc |= bit(val, 11) << 12 | bit(val, 4)  << 11 | bit(val, 9) << 10 |
                        bit(val, 8)  << 9  | bit(val, 10) << 8  | bit(val, 6) << 7  |
                        bit(val, 7)  << 6  | bit(val, 3)  << 5  | bit(val, 2) << 4  |
                        bit(val, 1)  << 3  | bit(val, 5)  << 2;
}

static void set_rs1(uint8_t *loc, uint32_t rs1)
{
    assert(rs1 < 32);
    *(uint32_t *)loc &= 0b111111'11111'00000'111'11111'1111111;
    *(uint32_t *)loc |= rs1 << 15;
}

static void Reloc_alloc(Linking_context &ctx, Output_section &osec, std::size_t isec_idx)
{
    const Input_section &isec = *osec.member_list[isec_idx].isec;
    const Input_file &file = *osec.member_list[isec_idx].file;
    // input section offset from the begining of the file
    auto isec_file_offset = osec.shdr.sh_offset + osec.member_list[isec_idx].offset;
    auto isec_addr = osec.shdr.sh_addr + osec.member_list[isec_idx].offset;
    uint8_t *base = ctx.buf + isec_file_offset;

    auto get_rd = [&](const Input_section &isec, uint64_t offset) -> uint32_t
    {
        // Returns the rd register of an R/I/U/J-type instruction.
        uint32_t instr = *(uint32_t *)(isec.data.data() + offset);
        return (instr << 20) >> 27;
    };

    auto get_sym_addr = [&ctx, isec_addr, &file](Symbol *sym, const Elf64_Sym &esym) -> uint64_t
    {
        if (sym && sym->piece())
            return nLinking_passes::Get_global_symbol_addr(ctx, *sym);
        else if (nELF_util::Is_sym_local(esym) == true)
            return isec_addr + sym->elf_sym().st_value ;
        else if (sym != nullptr)
            return nLinking_passes::Get_global_symbol_addr(ctx, *sym);
        else
            FATALF("why this symbol is not binded?"); 
    };

    for(std::size_t rel_idx = 0 ; rel_idx < isec.rel_count() ; rel_idx++)
    {
        nELF_util::ELF_Rel rel = isec.rela_at(rel_idx);
        if (   (eReloc_type)rel.type() == eReloc_type::R_RISCV_NONE 
            || (eReloc_type)rel.type() == eReloc_type::R_RISCV_RELAX)
            continue;

        Symbol *sym = osec.member_list[isec_idx].file->symbol_list[rel.sym()];

        auto r_offset = rel.offset();

        auto check = [&](int64_t val, int64_t lo, int64_t hi)
        {
            if (val < lo || hi <= val)
                FATALF("%s", "relocation out of range");
        };

        uint8_t *loc = base + r_offset;
        uint64_t S = get_sym_addr(sym, file.src().symbol_table(rel.sym()));
        uint64_t A = rel.r_addend;
        uint64_t P = isec_addr + r_offset;
        // no linker relaxation supported in current version, so it is set to 0.
        uint64_t removed_bytes = 0;
/*         if (   isec.name() == ".text")
        {
            std::cout <<std::hex <<rel.type() << " " << rel.offset() << " " << S << " " << A << " " << P << "\n";

        } */
        switch (rel.type())
        {
            case (uint32_t)eReloc_type::R_RISCV_32:
                new (loc) uint32_t(S + A);
            break;

            case (uint32_t)eReloc_type::R_RISCV_64:
                new (loc) uint32_t(S + A);
            break;

            case (uint32_t)eReloc_type::R_RISCV_BRANCH:
                check(S + A - P, -(1 << 12), 1 << 12);
                write_btype(loc, S + A - P);
            break;

            case (uint32_t)eReloc_type::R_RISCV_JAL:
                check(S + A - P, -(1 << 20), 1 << 20);
                write_jtype(loc, S + A - P);
            break;
            
            case (uint32_t)eReloc_type::R_RISCV_CALL:
            case (uint32_t)eReloc_type::R_RISCV_CALL_PLT:
            {
                uint64_t val = S + A - P;
                uint64_t rd = get_rd(isec, rel.offset() + 4);
                // Calling an undefined weak symbol does not make sense.
                // We make such call into an infinite loop. This should
                // help debugging of a faulty program.
                if (sym == nullptr && nELF_util::Is_sym_undef_weak(file.src().symbol_table(rel.sym())) == true)
                    val = 0;
                check(val, -(1LL << 31), 1LL << 31);
                write_utype(loc, val);
                write_itype(loc + 4, val);
            break;
            }

            case (uint32_t)eReloc_type::R_RISCV_PCREL_LO12_I:
                write_itype(loc, S-P);
            break;

            case (uint32_t)eReloc_type::R_RISCV_PCREL_LO12_S:
                write_stype(loc, S-P);
            break;

            case (uint32_t)eReloc_type::R_RISCV_PCREL_HI20:
                write_utype(loc, S + A - P);
            break;

            case (uint32_t)eReloc_type::R_RISCV_HI20:
                check(S + A, -(1LL << 31), 1LL << 31);
                write_utype(loc, S + A);
            break;

            case (uint32_t)eReloc_type::R_RISCV_LO12_I:
            case (uint32_t)eReloc_type::R_RISCV_LO12_S:
            if (rel.type() == (uint32_t)eReloc_type::R_RISCV_LO12_I)
                write_itype(loc, S + A);
            else
                write_stype(loc, S + A);

            // Rewrite `lw t1, 0(t0)` with `lw t1, 0(x0)` if the address is
            // accessible relative to the zero register because if that's the
            // case, corresponding LUI might have been removed by relaxation.
            if (nUtil::sign_extend(S + A, 11) == S + A)
                set_rs1(loc, 0);
            break;

            case (uint32_t)eReloc_type::R_RISCV_ADD8:
                loc += S + A;
            break;
            
            case (uint32_t)eReloc_type::R_RISCV_ADD16:
                *(uint16_t*)loc += S + A;
            break;
            
            case (uint32_t)eReloc_type::R_RISCV_ADD32:
                *(uint32_t*)loc += S + A;
            break;
            
            case (uint32_t)eReloc_type::R_RISCV_ADD64:
                *(uint64_t*)loc += S + A;
            break;
            
            case (uint32_t)eReloc_type::R_RISCV_SUB8:
                loc -= S + A;
            break;
            
            case (uint32_t)eReloc_type::R_RISCV_SUB16:
                *(uint16_t*)loc -= S + A;
            break;
            
            case (uint32_t)eReloc_type::R_RISCV_SUB32:
                *(uint32_t*)loc -= S + A;
            break;
            
            case (uint32_t)eReloc_type::R_RISCV_SUB64:
                *(uint64_t*)loc -= S + A;
            break;

            case (uint32_t)eReloc_type::R_RISCV_ALIGN:
            {
                // A R_RISCV_ALIGN is followed by a NOP sequence. We need to remove
                // zero or more bytes so that the instruction after R_RISCV_ALIGN is
                // aligned to a given alignment boundary.
                //
                // We need to guarantee that the NOP sequence is valid after byte
                // removal (e.g. we can't remove the first 2 bytes of a 4-byte NOP).
                // For the sake of simplicity, we always rewrite the entire NOP sequence.
                
                int64_t padding_bytes = rel.r_addend - removed_bytes;
                assert((padding_bytes & 1) == 0);

                int64_t i = 0;
                for (; i <= padding_bytes - 4; i += 4)
                    *(uint32_t *)(loc + i) = 0x0000'0013; // nop
                if (i < padding_bytes)
                    *(uint16_t *)(loc + i) = 0x0001;      // c.nop
                break;
            }

            case (uint32_t)eReloc_type::R_RISCV_RVC_BRANCH:
                check(S + A - P, -(1 << 8), 1 << 8);
                write_cbtype(loc, S + A - P);
            break;

            case (uint32_t)eReloc_type::R_RISCV_RVC_JUMP:
                check(S + A - P, -(1 << 11), 1 << 11);
                write_cjtype(loc, S + A - P);
            break;

            case (uint32_t)eReloc_type::R_RISCV_SUB6:
                *loc = (*loc & 0b1100'0000) | ((*loc - S - A) & 0b0011'1111);
            break;

            case (uint32_t)eReloc_type::R_RISCV_SET6:
                *loc = (*loc & 0b1100'0000) | ((S + A) & 0b0011'1111);
            break;

            case(uint32_t)eReloc_type:: R_RISCV_SET8:
                *loc = S + A;
            break;

            case (uint32_t)eReloc_type::R_RISCV_SET16:
                *(uint16_t*)loc = S + A;
            break;

            case (uint32_t)eReloc_type::R_RISCV_SET32:
                *(uint32_t*)loc = S + A;
            break;

            case (uint32_t)eReloc_type::R_RISCV_PLT32:
            case (uint32_t)eReloc_type::R_RISCV_32_PCREL:
                *(uint32_t*)loc = S + A - P;
            break;
            
            case (uint32_t)eReloc_type::R_RISCV_SET_ULEB128:
                nUtil::Overwrite_uleb(loc, S + A);
            break;
            
            case (uint32_t)eReloc_type::R_RISCV_SUB_ULEB128:
                nUtil::Overwrite_uleb(loc, nUtil::read_uleb(loc) - S - A);
            break;
            
            default:
                FATALF("non supported link type %u", rel.type());
            break;
        }
    }

}
static void Reloc_non_alloc(Linking_context &ctx, Output_section &osec, std::size_t isec_idx)
{
    Reloc_alloc(ctx, osec, isec_idx); // TODO, use better implementation
}

void nLinking_passes::Relocate_symbols(Linking_context &ctx, Output_section &osec)
{
    for(std::size_t i = 0 ; i < osec.member_list.size() ; i++)
    {
        memcpy(ctx.buf + osec.shdr.sh_offset + osec.member_list[i].offset, 
               osec.member_list[i].isec->data.data(),
               osec.member_list[i].isec->data.size());

        if (osec.member_list[i].isec->shdr().sh_flags & SHF_ALLOC)
            Reloc_alloc(ctx, osec, i);
        else
            Reloc_non_alloc(ctx, osec, i);
    }
}