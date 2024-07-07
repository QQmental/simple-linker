#pragma once
#include "RISC_V_linking_data.h"
#include "elf/RISC_V_Section_attribute.h"

class Relocatable_file
{
public:
    Relocatable_file(std::unique_ptr<char[]> data);

    Relocatable_file(const Relocatable_file &src) = delete;
    
    Relocatable_file(Relocatable_file &&src) : m_section_hdr_table(std::move(src.m_section_hdr_table)), 
                                               m_symbol_table(std::move(src.m_symbol_table)), 
                                               m_data(std::move(src.m_data)){}

    const nRSIC_V_linking_data::Section_hdr_table& section_hdr_table() const {return m_section_hdr_table;}
    const elf64_shdr&  section_hdr(std::size_t idx) const {return section_hdr_table().headers()[idx];}
    const char* section(std::size_t idx) const {return m_data.get() + section_hdr(idx).sh_offset;}
    const nRSIC_V_linking_data::Symbol_table& symbol_table() const {return *m_symbol_table;}
    const elf64_shdr& symbol_table_hdr() const {return section_hdr(m_symtab_idx);}
    
private:
    std::size_t get_shndx(const Elf64_Sym &sym) const;

    // be aware of the initilization order
    nRSIC_V_linking_data::Section_hdr_table m_section_hdr_table;
    std::unique_ptr<nRSIC_V_linking_data::Symbol_table> m_symbol_table;
    std::unique_ptr<char[]> m_data;
    std::size_t m_symtab_idx = -1, m_symtab_shndx = -1;
    nRISC_V_Section::Attributes m_attr;
};

inline std::size_t Relocatable_file::get_shndx(const Elf64_Sym &sym) const
{

    assert(m_symbol_table->data() <= &sym);
    assert(m_symbol_table->data() + m_symbol_table->count() > &sym);

    
    const Elf32_Word *vals = reinterpret_cast<const Elf32_Word *>(section(m_symtab_shndx));

    if (sym.st_shndx == SHN_XINDEX)
        return vals[&sym - m_symbol_table->data()];

    return sym.st_shndx;
}