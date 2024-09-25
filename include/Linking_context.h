#pragma once
#include <stdint.h>
#include <vector>
#include <string_view>
#include <unordered_map>
#include <atomic>
#include <type_traits>
#include <list>

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
#include "Output_file.h"

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
        linking_package(std::unique_ptr<Symbol> symbol, Input_file *input_file) 
                      : symbol(std::move(symbol)), 
                        input_file(input_file),
                        is_ref_outside(false){}
                        
        linking_package(std::unique_ptr<Symbol> symbol) 
                      : symbol(std::move(symbol)), 
                        input_file(nullptr),
                        is_ref_outside(false){}

        Symbol* Mark_ref() const
        {
            is_ref_outside = true; 
            return symbol.get();
        }

        std::unique_ptr<Symbol> symbol;
        Input_file *input_file;
        mutable bool is_ref_outside;
    };

    Linking_context(Link_option_args link_option_args);

    struct
    {
        Symbol *entry = nullptr;
        Symbol *fiini = nullptr;
        Symbol *init = nullptr;

        Symbol *global_pointer = nullptr;
        Symbol *bss_start = nullptr;
        Symbol *end = nullptr;
        Symbol *_end = nullptr;
        Symbol *etext = nullptr;
        Symbol *_etext = nullptr;
        Symbol *edata = nullptr;
        Symbol *_edata = nullptr;
        Symbol *libc_fini = nullptr;
        Symbol *preinit_array_end = nullptr;
        Symbol *preinit_array_start = nullptr;
        Symbol *init_array_start = nullptr;
        Symbol *init_array_end = nullptr;
        Symbol *fini_array_start = nullptr;
        Symbol *fini_array_end = nullptr;

        std::string_view entry_name = "_start";
        std::string_view fiini_name = "_fini";
        std::string_view init_name = "_init";
        std::string_view global_pointer_name = "__global_pointer$";
        std::string_view bss_start_name = "__bss_start";
        std::string_view end_name = "end";
        std::string_view _end_name = "_end";
        std::string_view etext_name = "etext";
        std::string_view _etext_name = "_etext";
        std::string_view edata_name = "edata";
        std::string_view _edata_name = "_edata";
        std::string_view libc_fini_name = "__libc_fini";
        std::string_view preinit_array_end_name = "__preinit_array_end";
        std::string_view preinit_array_start_name = "__preinit_array_start";
        std::string_view init_array_start_name = "__init_array_start";
        std::string_view init_array_end_name = "__init_array_end";
        std::string_view fini_array_start_name = "__fini_array_start";
        std::string_view fini_array_end_name = "__fini_array_end";
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

    uint64_t Canonicalize_type(std::string_view name, uint64_t sh_type) const;

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
    std::list<Output_chunk> output_chunk_list;
    uint8_t *buf;
    Output_file output_file;
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
    uint64_t filesize = 0;
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

    void Create_output_symtab();
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
    linking_package lpkg(std::make_unique<Symbol>(symbol_input_file_src.src(), sym_idx),
                         &symbol_input_file_src);
    
    std::string_view sym_name = lpkg.symbol->name;

    m_lock.lock();
    auto it = m_global_symbol_map.insert(std::make_pair(sym_name, std::move(lpkg)));
    m_lock.unlock();
    
    return it.first->second;
}


inline Linking_context::Output_merged_section_id::
Output_merged_section_id(const Merged_section &src) 
                       : name(src.name), 
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

