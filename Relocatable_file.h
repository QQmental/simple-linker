#pragma once
#include <string_view>
#include "RISC_V_linking_data.h"
#include "elf/RISC_V_Section_attribute.h"

class Relocatable_file
{
public:
    struct Linking_mdata
    {
        std::size_t symtab_idx = -1, symtab_shndx = -1, first_global = -1;
        nRISC_V_Section::Attributes attr;        
    };

    Relocatable_file(std::unique_ptr<char[]> data, std::string name);

    Relocatable_file(const Relocatable_file &src) = delete;

    Relocatable_file(Relocatable_file &&src) : m_section_hdr_table(std::move(src.m_section_hdr_table)), 
                                               m_symbol_table(std::move(src.m_symbol_table)), 
                                               m_data(std::move(src.m_data)),
                                               m_linking_mdata(std::move(src.m_linking_mdata)),
                                               m_name(std::move(src.m_name)){}

    Relocatable_file& operator= (Relocatable_file &&src) noexcept
    {
        m_section_hdr_table = std::move(src.m_section_hdr_table); 
        m_symbol_table = std::move(src.m_symbol_table);
        m_data = std::move(src.m_data);
        m_linking_mdata = std::move(src.m_linking_mdata);
        m_name = std::move(src.m_name);
        return *this;
    }
    ~Relocatable_file() = default;

    const elf64_hdr elf_hdr() const {return *reinterpret_cast<const elf64_hdr*>(m_data.get());}
    
    const nRSIC_V_linking_data::Section_hdr_table& section_hdr_table() const {return m_section_hdr_table;}
    const elf64_shdr&  section_hdr(std::size_t idx) const {return section_hdr_table().headers()[idx];}
    char* section(std::size_t idx) const {return m_data.get() + section_hdr(idx).sh_offset;}
    std::size_t section_size(std::size_t idx) const {return section_hdr(idx).sh_size;}

    // return nullptr if this object file has no symbol table
    const nRSIC_V_linking_data::Symbol_table* symbol_table() const {return m_symbol_table.get();}
    const elf64_shdr& symbol_table_hdr() const {return section_hdr(m_linking_mdata.symtab_idx);}
    std::size_t get_shndx(const Elf64_Sym &sym) const;
    const Linking_mdata& linking_mdata () const {return m_linking_mdata;}
    std::string_view name() const {return m_name;}

private:
    nRSIC_V_linking_data::Section_hdr_table m_section_hdr_table;
    std::unique_ptr<nRSIC_V_linking_data::Symbol_table> m_symbol_table;
    std::unique_ptr<char[]> m_data;
    Linking_mdata m_linking_mdata;
    std::string m_name;
};

inline std::size_t Relocatable_file::get_shndx(const Elf64_Sym &sym) const
{

    assert(&m_symbol_table->data(0) <= &sym);
    assert(&m_symbol_table->data(0) + m_symbol_table->count() > &sym);

    
    const Elf32_Word *vals = reinterpret_cast<const Elf32_Word *>(section(linking_mdata().symtab_shndx));

    if (sym.st_shndx == SHN_XINDEX)
        return vals[&sym - &m_symbol_table->data(0)];

    return sym.st_shndx;
}