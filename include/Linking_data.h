#pragma once
#include <assert.h>
#include <vector>
#include <utility>
#include <string.h>
#include <memory>
#include "elf/ELF.h"
#include "util.h"




namespace nLinking_data
{

class String_table;

class Section_hdr_table;

class Symbol_table;

class String_table
{
public:
    String_table(std::vector<const char*> str_table_ptr, const char *str_tbl) 
               : m_str_table_ptr(std::move(str_table_ptr)), 
                 m_str_tbl(str_tbl)
    {

    }
    

    using str_ptr_list_t = std::vector<const char*>;

    const char* data() const {return m_str_tbl;}
    const char* const* begin_str_itor() const {return m_str_table_ptr.data();}
    const char* const* end_str_itor() const {return m_str_table_ptr.data() + m_str_table_ptr.size();}
    const char* const* itor_at(std::size_t idx) const {return m_str_table_ptr.data() + idx;}
    const str_ptr_list_t& str_ptr_list() const {return m_str_table_ptr;}


private:
    str_ptr_list_t m_str_table_ptr;
    const char *m_str_tbl;
};

std::unique_ptr<String_table> Create_symbol_string_table(const char *data, 
                                                         const elf64_shdr &shdr, 
                                                         const Elf64_Sym *symbol_list, 
                                                         std::size_t n_symbol);

// a list of section headers and section names
class Section_hdr_table
{
public:

    Section_hdr_table(const char *data) : m_data(data), m_shdr_list(nullptr), m_str_tbl(nullptr)
    {
        elf64_hdr hdr ;
        
        memcpy(&hdr, data, sizeof(hdr));

        if (hdr.e_type != ET_REL)
            FATALF("%sit is not a relocatable file", "");

        m_shdr_list = reinterpret_cast<const Elf64_Shdr*>(data + hdr.e_shoff);

        std::size_t shstrtab_idx = (hdr.e_shstrndx == SHN_XINDEX) ? m_shdr_list[0].sh_link : hdr.e_shstrndx;

        const char *str_tbl = data + m_shdr_list[shstrtab_idx].sh_offset;

        auto str_table_ptr = String_table::str_ptr_list_t(header_count());

        for(std::size_t i = 0 ; i < header_count() ; i++)
            str_table_ptr[i] = &str_tbl[m_shdr_list[i].sh_name];

        m_str_tbl = new String_table(std::move(str_table_ptr), str_tbl);
    }

    Section_hdr_table(const Section_hdr_table &src) : m_data(src.m_data), m_shdr_list(src.m_shdr_list), m_str_tbl(new String_table(*src.m_str_tbl)) {}
    Section_hdr_table(Section_hdr_table &&src) : m_data(src.m_data), m_shdr_list(src.m_shdr_list), m_str_tbl(src.m_str_tbl)
    {
        src.m_str_tbl = nullptr;
    }
    Section_hdr_table& operator= (Section_hdr_table src)
    {
        std::swap(m_data, src.m_data);
        std::swap(m_shdr_list, src.m_shdr_list);
        std::swap(m_str_tbl, src.m_str_tbl);
        return *this;
    }

    ~Section_hdr_table() {delete m_str_tbl;}

    const elf64_shdr* headers() const {return m_shdr_list;}
    std::size_t header_count() const 
    {
        /*
            If the number of sections is greater than or equal to SHN_LORESERVE (0xff00), 
            elfXX_hdr::e_shnum has the value zero and the actual number of section header table entries 
            is contained in the sh_size field of the section header at index 0. 
            Otherwise, the sh_size member of the initial entry contains 0.
            ref: https://docs.oracle.com/cd/E19683-01/817-3677/6mj8mbtc9/index.html
        */
        elf64_hdr hdr;
        memcpy(&hdr, m_data, sizeof(hdr));
        return (hdr.e_shnum == 0) ? headers()[0].sh_size : hdr.e_shnum;
    }

    //name of each section
    const String_table& string_table() const {return *m_str_tbl;}

private:
    const char *m_data;
    const elf64_shdr *m_shdr_list;
    String_table *m_str_tbl;
};


//given the symbol table section header, create the symbol table and the string table (name of each symbol)
class Symbol_table
{
public:
    Symbol_table(const char *data, const elf64_shdr &shdr, const Section_hdr_table &sec_hdr_tbl) : m_sym_tbl(nullptr)
    {
        if (shdr.sh_type != SHT_SYMTAB)
            FATALF("the given section header is not for symbol table, its type is %u", shdr.sh_type);
        m_sym_tbl = reinterpret_cast<const Elf64_Sym *>(data + shdr.sh_offset);
        auto symbol_cnt = shdr.sh_size / sizeof(Elf64_Sym);

        auto &str_tbl_sec_hdr = sec_hdr_tbl.headers()[shdr.sh_link];

        m_str_tbl = Create_symbol_string_table(data, str_tbl_sec_hdr, m_sym_tbl, symbol_cnt);
    }

    ~Symbol_table() = default;

    const Elf64_Sym& data(std::size_t idx) const {return m_sym_tbl[idx];}
    std::size_t count() const {return m_str_tbl->str_ptr_list().size();}
    const String_table& string_table() const {return *m_str_tbl.get();}
    const String_table::str_ptr_list_t& str_table_ptr() const {return m_str_tbl->str_ptr_list();}

private:
    const Elf64_Sym *m_sym_tbl;
    //name of each symbol
    std::unique_ptr<String_table> m_str_tbl;
};





// shdr should be string table section header
inline std::unique_ptr<String_table> Create_symbol_string_table(const char *data, const elf64_shdr &shdr, const Elf64_Sym *symbol_list, std::size_t n_symbol)
{
    assert(shdr.sh_type == SHT_STRTAB);

    const char *str_tbl = data + shdr.sh_offset;

    auto str_table_ptr = std::vector<const char*>(n_symbol);

    for(std::size_t i = 0 ; i < n_symbol ; i++)
        str_table_ptr[i] = &str_tbl[symbol_list[i].st_name];
    
    auto ret = std::make_unique<String_table>(std::move(str_table_ptr), str_tbl);

    return ret;
}




}


