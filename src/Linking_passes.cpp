#include <iostream>
#include <unordered_map>
#include <numeric>
#include <atomic>
#include <algorithm>
#include <queue>

#include "Linking_passes.h"
#include "ELF_util.h"
#include "Chunk/Output_section.h"

// a lot of code is copied from https://github.com/rui314/mold

static bool Has_ctors_and_init_array(Linking_context &ctx);
static uint64_t Canonicalize_type(std::string_view name, uint64_t type);
static Output_section_key Get_output_section_key(const Input_section &isec, bool ctors_in_init_array);
static uint64_t to_phdr_flags(Linking_context &ctx, const Chunk *chunk);

static bool Has_ctors_and_init_array(Linking_context &ctx)
{
    bool x = false;
    bool y = false;

    for(const Input_file &file : ctx.input_file_list())
    {
        x |= file.has_ctors;
        y |= file.has_init_array;
    }

    return x && y;
}


static uint64_t Canonicalize_type(std::string_view name, uint64_t type)
{
  // Some old assemblers don't recognize these section names and
  // create them as SHT_PROGBITS.
    if (type == SHT_PROGBITS)
    {
        if (name == ".init_array" || name.rfind(".init_array.", 0) == 0)
            return SHT_INIT_ARRAY;
        if (name == ".fini_array" || name.rfind(".fini_array.", 0) == 0)
            return SHT_FINI_ARRAY;
    }
    return type;
}


static Output_section_key Get_output_section_key(const Input_section &isec, bool ctors_in_init_array)
{
    // If .init_array/.fini_array exist, .ctors/.dtors must be merged
    // with them.
    //
    // CRT object files contain .ctors/.dtors sections without any
    // relocations. They contain sentinel values, 0 and -1, to mark the
    // beginning and the end of the initializer/finalizer pointer arrays.
    // We do not place them into .init_array/.fini_array because such
    // invalid pointer values would simply make the program to crash.
    if (ctors_in_init_array && !isec.rel_count() == 0) {
    std::string_view name = isec.name();
    if (name == ".ctors" || name.rfind(".ctors.", 0) == 0)
        return {".init_array", SHT_INIT_ARRAY};
    if (name == ".dtors" || name.rfind(".dtors.", 0) == 0)
        return {".fini_array", SHT_FINI_ARRAY};
    }

    auto &shdr = isec.shdr();
    
    std::string_view name = nELF_util::get_output_name(isec.name(), shdr.sh_flags);
    uint64_t type = Canonicalize_type(name, shdr.sh_type);

    return Output_section_key{name, type};
}

static uint64_t to_phdr_flags(Linking_context &ctx, const Chunk *chunk)
{
  bool write = (chunk->shdr.sh_flags & SHF_WRITE);
  bool exec = (chunk->shdr.sh_flags & SHF_EXECINSTR);

  return  (uint64_t)eSegment_flag::PF_R 
        | (write ? (uint64_t)eSegment_flag::PF_W : (uint64_t)eSegment_flag::PF_NONE)
        | (exec ? (uint64_t)eSegment_flag::PF_X : (uint64_t)eSegment_flag::PF_NONE);
}


void nLinking_passes::Check_duplicate_smbols(const Input_file &file)
{
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
        if (undef || common)
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

    std::size_t size = ctx.osec_pool().size();

    bool ctors_in_init_array = Has_ctors_and_init_array(ctx);

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
                auto key = Get_output_section_key(isec, ctors_in_init_array);

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
    

    ctx.chunk_list.reserve(osec_map.size() - size + ctx.merged_section_map.size());
    
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
    for(auto &osec : ctx.osec_pool())
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

    std::sort(ctx.chunk_list.begin(),
              ctx.chunk_list.end(), 
              [&](const Chunk *a, const Chunk *b)
              {
                  return  std::tuple{get_rank1(a), get_rank2(a), a->name}
                        < std::tuple{get_rank1(b), get_rank2(b), b->name};
              });

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