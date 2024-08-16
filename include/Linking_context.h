#pragma once
#include <stdint.h>
#include <vector>
#include <string_view>
#include <unordered_map>
#include <atomic>

#include "elf/ELF.h"
#include "Relocatable_file.h"
#include "Input_file.h"
#include "third_party/Spin_lock.h"

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

    Linking_context(Link_option_args link_option_args, eLink_machine_optinon maching_opt);

    void insert_object_file(Relocatable_file src, bool is_from_lib)
    {
        elf64_hdr hdr = src.elf_hdr();
        
        if (hdr.e_machine != (Elf64_Half)m_link_machine_optinon)
            FATALF("%sincompatible maching type for the given relocatable file: ", "");

        m_rel_file.push_back(std::make_unique<Relocatable_file>(std::move(src)));
        m_is_alive.push_back(is_from_lib == false);
    }

    // return an iterator to the target, and the target has a const linking_package
    auto Find_symbol(std::string_view name) const
    {   
        auto it = std::as_const(m_global_symbol_map).find(name);;
    
        return it;
    }

    //return its linking_package
    linking_package& Insert_global_symbol(Input_file &symbol_input_file_src, std::size_t sym_idx)
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

    void Link();

    // str should be a null-terminated string
    // after inserted, it's returned as a string_view
    std::string_view Insert_string(std::unique_ptr<char[]> str)
    {
        std::string_view ret{str.get()};
        m_string_pool.push_back(std::move(str));
        return ret;
    }

    eLink_machine_optinon maching_option() const {return m_link_machine_optinon;}

    Link_option_args link_option_args() const {return m_link_option_args;}

    const std::unordered_map<std::string_view, linking_package>& global_symbol_map() const {return m_global_symbol_map;}

    const std::vector<Input_file>& input_file_list() const {return m_input_file;}

private:
    Link_option_args m_link_option_args;
    std::vector<std::unique_ptr<Relocatable_file>> m_rel_file;
    std::vector<bool> m_is_alive;
    std::vector<Input_file> m_input_file;
    std::unordered_map<std::string_view, linking_package> m_global_symbol_map;
    eLink_machine_optinon m_link_machine_optinon;
    std::vector<std::unique_ptr<char[]>> m_string_pool;

public: 
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


    std::unordered_map<Output_merged_section_id, std::unique_ptr<Merged_section>, Output_merged_section_id::Hash_func> merged_section_map;

private:
    Spin_lock m_lock;
};

inline 
Linking_context::Output_merged_section_id::
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