#pragma once
#include <string_view>

#include "Relocatable_file.h"
#include "ELF_util.h"
#include "Chunk/Merged_section.h"


struct Symbol
{
public:

    Symbol(const Relocatable_file &rel_file, std::size_t sym_idx) 
         : m_rel_file(&rel_file),
           mergeable_section_piece(nullptr),
           sym_idx(sym_idx),
           name(nELF_util::Get_symbol_name(*m_rel_file, sym_idx))
    {
        
    }

    Symbol() : m_rel_file(nullptr), mergeable_section_piece(nullptr){};
    ~Symbol() = default;

    const elf64_sym& elf_sym() const {return file()->symbol_table()->data(sym_idx);}
    const Relocatable_file* file() const {return m_rel_file;}
    Merged_section::Piece* piece() const {return mergeable_section_piece;}

    void Set_piece(Merged_section::Piece &src)
    {
        mergeable_section_piece = &src;
    }


private:
    const Relocatable_file *m_rel_file;

public:
    Merged_section::Piece *mergeable_section_piece;
    // index of this symbol in the relocatable file, or
    // relocation index in mergeable_section_piece
    std::size_t sym_idx;
    std::string_view name;
    Elf64_Addr val;
};