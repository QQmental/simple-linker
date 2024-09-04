#pragma once
#include <stdint.h>
#include <vector>
#include <string_view>
#include <unordered_map>
#include <atomic>
#include <type_traits>


#include "elf/ELF.h"
#include "Relocatable_file.h"
#include "Input_file.h"
#include "third_party/Spin_lock.h"
#include "Chunk/Output_section.h"
#include "Chunk/Output_phdr.h"
#include "Chunk/Output_ehdr.h"
#include "Chunk/Output_shdr.h"
#include "Chunk/Symtab_shndx_section.h"
#include "Chunk/Shstrtab_section.h"
#include "Chunk/Strtab_section.h"
#include "Chunk/Symtab_section.h"
#include "Chunk/Riscv_attributes_section.h"
#include "Chunk/Got_section.h"
#include "Output_chunk.h"

struct Merged_section;




class Linking_context
{
public:
    enum class eLink_machine_optinon : uint16_t
    {
        unknown = 0,
        elf64lriscv = EM_RISC_V
    };

    // some flags and options for linking
    struct Link_option_args
    {
        // path of a file
        using path_of_file_t = std::string;

        std::vector<std::string> library_search_path;
        std::vector<std::string> library_name;
        std::vector<path_of_file_t> library;
        std::vector<path_of_file_t> obj_file;
        std::string output_file = "a.out";
        eLink_machine_optinon link_machine_optinon = eLink_machine_optinon::unknown;
        int argc;
        char **argv;
    };

    struct linking_package
    {
        std::unique_ptr<Symbol> symbol;
        Input_file *input_file;
    };

    Linking_context(Link_option_args link_option_args);

    struct
    {
        Symbol *entry = nullptr;
        Symbol *fiini = nullptr;
        Symbol *init = nullptr;
        
        std::string_view entry_name = "_start";
        std::string_view fiini_name = "_fini";
        std::string_view init_name = "_init";
    }special_symbols;

    void insert_object_file(Relocatable_file src, bool is_from_lib);

    // return an iterator to the target, and the target has a const linking_package
    auto Find_symbol(std::string_view name) const
    {   
        auto it = global_symbol_map().find(name);;
    
        return it;
    }

    linking_package& Insert_global_symbol(Input_file &symbol_input_file_src, std::size_t sym_idx);

    void Link();

    // str should be a null-terminated string
    // after inserted, it's returned as a string_view
    std::string_view Insert_string(std::unique_ptr<char[]> str)
    {
        std::string_view ret{str.get()};
        m_string_pool.push_back(std::move(str));
        return ret;
    }

    Output_section* Insert_osec(std::unique_ptr<Output_section> src)
    {
        Output_section *ret = src.get();

        Output_section_key key;
        key.name = src->name;
        key.type = src->type;
        
        m_osec_pool.insert(std::make_pair(key, std::move(src)));
        output_chunk_list.push_back(Output_chunk(ret, *this));
        
        return ret;
    }

    template<typename chunk_t>
    chunk_t* Insert_chunk(std::unique_ptr<chunk_t> src)
    {
        static_assert(std::is_base_of<Chunk, chunk_t>::value == true);
        static_assert(std::is_same_v<Output_section, chunk_t> == false, "You should put Output_section by calling 'Insert_osec'");
        static_assert(std::is_same_v<Merged_section, chunk_t> == false, "You should put Merged_section by calling 'Insert_merged_section'");
        chunk_t *ret = src.get();
        m_chunk_pool.push_back(std::move(src));
        output_chunk_list.push_back(Output_chunk(ret, *this));
        return ret;
    }

    bool Has_ctors_and_init_array() const;

    uint64_t Canonicalize_type(std::string_view name, uint64_t sh_type) const;

    Output_section_key Get_output_section_key(const Input_section &isec, bool ctors_in_init_array) const;

    uint64_t Get_input_section_addr(Input_section *isec) const;    
    uint64_t Get_symbol_addr(const Symbol &sym, uint64_t flags = 0) const ;

    eLink_machine_optinon maching_option() const {return m_link_option_args.link_machine_optinon;}

    Link_option_args link_option_args() const {return m_link_option_args;}
    
    const std::unordered_map<std::string_view, linking_package>& global_symbol_map() const {return m_global_symbol_map;}

    const std::vector<Input_file>& input_file_list() const {return m_input_file;}

    const std::unordered_map<Output_section_key, std::unique_ptr<Output_section>, Output_section_key::Hash_func>& osec_pool() const {return m_osec_pool;}

    struct Output_merged_section_id
    {
        Output_merged_section_id() = default;

        Output_merged_section_id(const Merged_section &src);

        Output_merged_section_id(const Output_merged_section_id &src) = default;
        ~Output_merged_section_id() = default;
        
        bool operator==(const Output_merged_section_id &src) const;   
        
        struct Hash_func
        {
            size_t operator() (const Output_merged_section_id& id) const;
        };

        std::string_view name;
        decltype(Elf64_Shdr::sh_flags) flags;
        decltype(Elf64_Shdr::sh_type) type;
        decltype(Elf64_Shdr::sh_entsize) entsize;
    };

    const std::unordered_map<Output_merged_section_id, 
                             std::unique_ptr<Merged_section>, 
                             Output_merged_section_id::Hash_func>& merged_section_map() const {return m_merged_section_map;}

    Merged_section* Insert_merged_section(const Output_merged_section_id &id, std::unique_ptr<Merged_section> src)
    {
        Merged_section *ptr = m_merged_section_map.insert(std::make_pair(id, std::move(src))).first->second.get();
        output_chunk_list.push_back(Output_chunk(ptr, *this));
        return ptr;
    }


    // Do not insert Output_chunk into this list directly, use 'Insert_chunk' or 'Insert_osec' instead
    std::vector<Output_chunk> output_chunk_list;
    std::unique_ptr<uint8_t[]> buf;
    Output_phdr *phdr = nullptr;
    Output_ehdr *ehdr = nullptr;
    Output_shdr *shdr = nullptr;
    Symtab_shndx_section *symtab_shndx_section = nullptr;
    Symtab_section *symtab_section = nullptr;
    Shstrtab_section *shstrtab_section = nullptr;
    Strtab_section *strtab_section = nullptr;
    Riscv_attributes_section *riscv_attributes_section = nullptr;
    Got_section *got = nullptr;
    uint64_t page_size = 1<<12;
    uint64_t image_base = 0x200000;
    // maybe default -1 is not a robust way to representing a null value 
    uint64_t physical_image_base = (uint64_t)-1;

private:
    Link_option_args m_link_option_args;
    std::vector<std::unique_ptr<Relocatable_file>> m_rel_file;
    std::vector<bool> m_is_alive;
    std::vector<Input_file> m_input_file;
    std::unordered_map<Output_merged_section_id, std::unique_ptr<Merged_section>, Output_merged_section_id::Hash_func> m_merged_section_map;
    std::unordered_map<std::string_view, linking_package> m_global_symbol_map;
    std::vector<std::unique_ptr<char[]>> m_string_pool;
    std::unordered_map<Output_section_key, std::unique_ptr<Output_section>, Output_section_key::Hash_func> m_osec_pool;
    std::vector<std::unique_ptr<Chunk>> m_chunk_pool;
    Spin_lock m_lock;
};


inline void Linking_context::insert_object_file(Relocatable_file src, bool is_from_lib)
{
    elf64_hdr hdr = src.elf_hdr();
    
    if (hdr.e_machine != (Elf64_Half)maching_option())
        FATALF("%sincompatible maching type for the given relocatable file: ", "");

    m_rel_file.push_back(std::make_unique<Relocatable_file>(std::move(src)));
    m_is_alive.push_back(is_from_lib == false);
}

//return its linking_package
inline Linking_context::linking_package& 
Linking_context::Insert_global_symbol(Input_file &symbol_input_file_src, std::size_t sym_idx)
{
    linking_package lpkg;
    lpkg.input_file = &symbol_input_file_src;
    lpkg.symbol = std::make_unique<Symbol>(symbol_input_file_src.src(), sym_idx);
    
    std::string_view sym_name = lpkg.symbol->name;

    m_lock.lock();
    auto it = m_global_symbol_map.insert(std::make_pair(sym_name, std::move(lpkg)));
    m_lock.unlock();
    
    return it.first->second;
}


inline Linking_context::Output_merged_section_id::
Output_merged_section_id(const Merged_section &src) 
                        :name(src.name), 
                         flags(src.shdr.sh_flags), 
                         type(src.shdr.sh_type), 
                         entsize(src.shdr.sh_entsize){}

inline bool Linking_context::Output_merged_section_id::operator==(const Output_merged_section_id &src) const
{
    return  src.name == name
         && src.flags == flags
         && src.type == type
         && src.entsize == entsize;
}


inline size_t Linking_context::Output_merged_section_id::Hash_func::operator() (const Output_merged_section_id& id) const
{
    return  std::hash<std::string_view>()(id.name)
          ^ std::hash<decltype(id.flags)>{}(id.flags)
          ^ std::hash<decltype(id.type)>{}(id.type)
          ^ std::hash<decltype(id.entsize)>{}(id.entsize);
}


inline uint64_t Linking_context::Canonicalize_type(std::string_view name, uint64_t sh_type) const
{
  // Some old assemblers don't recognize these section names and
  // create them as SHT_PROGBITS.
    if (sh_type == SHT_PROGBITS)
    {
        if (name == ".init_array" || name.rfind(".init_array.", 0) == 0)
            return SHT_INIT_ARRAY;
        if (name == ".fini_array" || name.rfind(".fini_array.", 0) == 0)
            return SHT_FINI_ARRAY;
    }
    return sh_type;
}

inline Output_section_key Linking_context::Get_output_section_key(const Input_section &isec, bool ctors_in_init_array) const
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

inline bool Linking_context::Has_ctors_and_init_array() const
{
    bool x = false;
    bool y = false;

    for(const Input_file &file : input_file_list())
    {
        x |= file.has_ctors;
        y |= file.has_init_array;
    }

    return x && y;
}

inline uint64_t Linking_context::Get_input_section_addr(Input_section *isec) const
{
    auto key = Get_output_section_key(*isec, Has_ctors_and_init_array());

    auto it = osec_pool().find(key);
    
    for(std::size_t i = 0 ; i < it->second->member_list.size() ; i++)
    {
        if (it->second->member_list[i] == isec)
            return it->second->shdr.sh_addr + it->second->input_section_offset_list[i];
    }

    FATALF("%s", "unreachable");
    return 0;
}

// copied from mold
inline uint64_t Linking_context::Get_symbol_addr(const Symbol &sym, uint64_t flags) const
{
    if (Merged_section::Piece *piece = sym.piece() ; piece != nullptr)
    {
        if (piece->is_alive == false) {
            // This condition is met if a non-alloc section refers an
            // alloc section and if the referenced piece of data is
            // garbage-collected. Typically, this condition occurs if a
            // debug info section refers a string constant in .rodata.
            return 0;
        }

        return piece->Get_addr() + sym.val;
    }

    Input_file *input_file = global_symbol_map().find(sym.name)->second.input_file;

    //It's safe because nothing is modified
    Input_section *isec = input_file->Get_symbol_input_section(sym);

    if (isec == nullptr)
        return sym.val; // absolute symbol
    
    return Get_input_section_addr(isec) + sym.val;
}