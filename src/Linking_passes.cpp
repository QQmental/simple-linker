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

// a lot of code is copied from https://github.com/rui314/mold

static void Set_virtual_addresses(Linking_context &ctx);
static std::size_t Set_file_offsets(Linking_context &ctx);
static bool is_tbss(Chunk *chunk);
static uint64_t align_with_skew(uint64_t val, uint64_t align, uint64_t skew);


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
        bool undef = nELF_util::Is_sym_undef(esym) == false && nELF_util::Is_sym_weak(esym) == false;
        bool common = nELF_util::Is_sym_common(esym) && nELF_util::Is_sym_common(input_file.symbol_list[sym_idx]->elf_sym()) == false;
        
        if (undef ==false && common == false)
            continue;
        
        auto it = ctx.Find_symbol(nELF_util::Get_symbol_name(input_file.src(), sym_idx));
        
        if (it == ctx.global_symbol_map().end())
        {
/*              std::cout << nELF_util::Get_symbol_name(input_file.src(), sym_idx) << \
            " size: " << esym.st_size << " type: " << ELF64_ST_TYPE(esym.st_info) << \
            " bind: " << ELF64_ST_BIND(esym.st_info) << " not found \n";  */
            continue;
        }

        // bind the undef symbol with the defined global symbol which is from the other file
        input_file.symbol_list[sym_idx] = it->second.symbol.get();
        
        reference_file(input_file);
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

    bool ctors_in_init_array = ctx.Has_ctors_and_init_array();

    struct IN_OUT_section_bind
    {
        const Input_section *isec;
        Output_section *osec;
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
                auto key = ctx.Get_output_section_key(isec, ctors_in_init_array);

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
        io_bind.osec->member_list[io_bind.member_offset] = io_bind.isec;
        std::cout << io_bind.osec->name << " " << io_bind.member_offset << " " << io_bind.isec->shndx << "\n";
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
        symbols.entry = it->second.symbol.get();

    it = ctx.global_symbol_map().find(symbols.fiini_name) ; 
    if (it != ctx.global_symbol_map().end())
        symbols.fiini = it->second.symbol.get();

    it = ctx.global_symbol_map().find(symbols.init_name) ; 
    if (it != ctx.global_symbol_map().end())
        symbols.init = it->second.symbol.get();     
}


void nLinking_passes::Assign_input_section_offset(Linking_context &ctx)
{
    for(auto &[p_osec, osec]: ctx.osec_pool())
    {
        osec->input_section_offset_list.resize(osec->member_list.size());
        std::size_t offset = 0, p2align = 0;
        for(std::size_t idx = 0 ; idx < osec->member_list.size() ; idx++)
        {
            auto &isec = *osec->member_list[idx];

            offset = nUtil::align_to(offset, 1 << p2align);
            osec->input_section_offset_list[idx] = offset;
            
            offset += isec.shdr().sh_size;
            p2align = std::max(p2align, isec.shdr().sh_addralign);
        }

        osec->shdr.sh_size = offset;
        osec->shdr.sh_addralign = 1 << p2align;
    }
}

void nLinking_passes::Create_synthetic_sections(Linking_context &ctx)
{
    ctx.phdr = ctx.Insert_chunk(std::make_unique<Output_phdr>(SHF_ALLOC));
    ctx.ehdr = ctx.Insert_chunk(std::make_unique<Output_ehdr>(SHF_ALLOC));
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




void nLinking_passes::Compute_section_headers(Linking_context &ctx)
{
    // Update sh_size for each chunk.
    for(auto &output_chunk : ctx.output_chunk_list)
        output_chunk.Update_shdr();
    
    // Remove empty chunks.
    for(auto it = ctx.output_chunk_list.begin() ; it != ctx.output_chunk_list.end() ;)
    {
        if (   it->is_osec() == false && it->chunk().shdr.sh_size == 0)
            it = ctx.output_chunk_list.erase(it);
        else
            ++it;
    }

    // Set section indices.
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

static bool is_tbss(Chunk *chunk)
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
        if (is_tbss(&it->chunk()))
        {
            uint64_t addr2 = addr;
            for (;;)
            {
                addr2 = nUtil::align_to(addr2, alignment(it->chunk()));
                it->chunk().shdr.sh_addr = addr2;
                addr2 += it->chunk().shdr.sh_size;
                if (std::next(it, 2) == ctx.output_chunk_list.end() || !is_tbss(&std::next(it)->chunk()))
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
static uint64_t align_with_skew(uint64_t val, uint64_t align, uint64_t skew)
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
            fileoff = align_with_skew(fileoff, ctx.page_size, first.shdr.sh_addr);


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