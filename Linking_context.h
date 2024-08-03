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

struct Mergeable_section;

class Linking_context
{
public:
    enum class eLink_machine_optinon : uint16_t
    {
        unknown = 0,
        elf64lriscv = EM_RISC_V
    };

    struct linking_package
    {
        std::unique_ptr<Symbol> symbol;
        Input_file *input_file;
    };

    Linking_context(eLink_machine_optinon maching_opt) : m_link_machine_optinon(maching_opt){}

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

    void link();


    eLink_machine_optinon maching_option() const {return m_link_machine_optinon;}

    const std::unordered_map<std::string_view, linking_package>& global_symbol_map() const {return m_global_symbol_map;}

private:
    std::vector<std::unique_ptr<Relocatable_file>> m_rel_file;
    std::vector<bool> m_is_alive;
    std::vector<Input_file> m_input_file;
    std::unordered_map<std::string_view, linking_package> m_global_symbol_map;
    eLink_machine_optinon m_link_machine_optinon;
    Spin_lock m_lock;
};