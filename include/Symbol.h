#pragma once
#include "Relocatable_file.h"
#include <string_view>
#include "ELF_util.h"
#include "Merged_section.h"


struct Symbol
{
public:

    Symbol(const Relocatable_file &rel_file, std::size_t sym_idx) 
          : m_rel_file(&rel_file), 
            sym_idx(sym_idx),
            name(nELF_util::Get_symbol_name(*m_rel_file, sym_idx))
    {
        
    }

    Symbol() = default;
    ~Symbol() = default;
    
    const elf64_sym& elf_sym() const {return file()->symbol_table()->data(sym_idx);}
    const Relocatable_file* file() const {return m_rel_file;}
    Elf64_Addr val;

    const Relocatable_file *m_rel_file;
    Merged_section::Section_fragment *mergeable_section_piece;
    // index of this symbol in the relocatable file
    std::size_t sym_idx;
    std::string_view name;
};