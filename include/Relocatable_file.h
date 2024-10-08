#pragma once
#include <string_view>
#include "Linking_data.h"
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

    Relocatable_file(Relocatable_file &&src) noexcept 
                   : m_section_hdr_table(std::move(src.m_section_hdr_table)), 
                     m_symbol_table(std::move(src.m_symbol_table)), 
                     m_data(std::move(src.m_data)),
                     m_linking_mdata(std::move(src.m_linking_mdata)),
                     m_name(std::move(src.m_name)){}

    Relocatable_file& operator= (Relocatable_file &&src) noexcept
    {
        if (this != &src)
        {
            m_section_hdr_table = std::move(src.m_section_hdr_table); 
            m_symbol_table = std::move(src.m_symbol_table);
            m_data = std::move(src.m_data);
            m_linking_mdata = std::move(src.m_linking_mdata);
            m_name = std::move(src.m_name);
        }
        return *this;
    }
    ~Relocatable_file() = default;

    const elf64_hdr elf_hdr() const {return *reinterpret_cast<const elf64_hdr*>(m_data.get());}
    
    const nLinking_data::Section_hdr_table& section_hdr_table() const {return m_section_hdr_table;}
    const elf64_shdr&  section_hdr(std::size_t idx) const {return section_hdr_table().headers()[idx];}
    char* section(std::size_t idx) {return m_data.get() + section_hdr(idx).sh_offset;}
    std::size_t section_size(std::size_t idx) const {return section_hdr(idx).sh_size;}

    // return nullptr if this object file has no symbol table
    const nLinking_data::Symbol_table* symbol_table() const {return m_symbol_table.get();}
    const elf64_sym& symbol_table(std::size_t sym_idx) const {return m_symbol_table->data(sym_idx);}
    const elf64_shdr& symbol_table_hdr() const {return section_hdr(m_linking_mdata.symtab_idx);}
    std::size_t get_shndx(const Elf64_Sym &sym) const;
    const Linking_mdata& linking_mdata () const {return m_linking_mdata;}
    std::string_view name() const {return m_name;}

private:
    nLinking_data::Section_hdr_table m_section_hdr_table;
    std::unique_ptr<nLinking_data::Symbol_table> m_symbol_table;
    std::unique_ptr<char[]> m_data;
    Linking_mdata m_linking_mdata;
    std::string m_name;
};

inline std::size_t Relocatable_file::get_shndx(const Elf64_Sym &sym) const
{

    assert(&m_symbol_table->data(0) <= &sym);
    assert(&m_symbol_table->data(0) + m_symbol_table->count() > &sym);

    char *sec = const_cast<Relocatable_file*>(this)->section(linking_mdata().symtab_shndx); //nothing is modified, fine

    const Elf32_Word *vals = reinterpret_cast<const Elf32_Word *>(sec);

    if (sym.st_shndx == SHN_XINDEX)
    {
        Elf32_Word ret;
        memcpy(&ret, vals + (&sym - &m_symbol_table->data(0)), sizeof(ret));
        return ret;
    }


    return sym.st_shndx;
}