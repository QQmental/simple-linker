#pragma once
#include <assert.h>
#include <vector>
#include <utility>
#include <string.h>
#include <memory>
#include "../RVemu/elf.h"


// although ARCHIVE_FILE_MAGIC is represented as a ascii string here, it's not followed by a '\0' in the archive file
// just compare the first 8 byte
#define ARCHIVE_FILE_MAGIC "!<arch>\n"


namespace nRSIC_V_linking_data
{

class String_table;

class Section_hdr_table;

class Symbol_table;

class Relocatable_file;

class String_table
{
public:
    String_table(std::vector<const char*> str_table_ptr, std::vector<char> str_tbl) 
               : m_str_table_ptr(std::move(str_table_ptr)), 
                 m_str_tbl(std::move(str_tbl))
    {

    }

    using str_ptr_list_t = std::vector<const char*>;

    const char* const* begin_str_ptr() const {return m_str_table_ptr.data();}
    const char* const* end_str_ptr() const {return m_str_table_ptr.data() + m_str_table_ptr.size();}
    const char* const* str_at(std::size_t idx) const {return m_str_table_ptr.data() + idx;}
    const str_ptr_list_t& str_ptr_list() const {return m_str_table_ptr;}


private:
    str_ptr_list_t m_str_table_ptr;
    std::vector<char> m_str_tbl;
};

std::unique_ptr<String_table> Create_symbol_string_table(FILE *file, const elf64_shdr &shdr, const std::vector<Elf64_Sym> &symbol_vec);

class Section_hdr_table
{
public:

    Section_hdr_table(FILE *file, const elf64_hdr &hdr) : m_shdr_vec(hdr.e_shnum), m_tbl(nullptr)
    {
        if (hdr.e_type != ET_REL)
            abort();

        fseek(file, hdr.e_shoff, 0);
        fread(m_shdr_vec.data(), sizeof(elf64_shdr), hdr.e_shnum, file);

        auto str_tbl = std::vector<char>(m_shdr_vec[hdr.e_shstrndx].sh_size);

        fseek(file, m_shdr_vec[hdr.e_shstrndx].sh_offset, 0);
        fread(str_tbl.data(), 1, m_shdr_vec[hdr.e_shstrndx].sh_size, file);

        auto str_table_ptr = String_table::str_ptr_list_t(hdr.e_shnum);

        for(auto &shdr : m_shdr_vec)
            str_table_ptr[&shdr - m_shdr_vec.data()] = &str_tbl[shdr.sh_name];

        m_tbl = new String_table(std::move(str_table_ptr), std::move(str_tbl));
    }

    ~Section_hdr_table() {delete m_tbl;}

    const std::vector<elf64_shdr>& headers() const {return m_shdr_vec;}

    //name of each section
    const String_table& string_table() const {return *m_tbl;}

private:
    std::vector<elf64_shdr> m_shdr_vec;
    String_table *m_tbl;
};


//given the symbol table section header, create the symbol table and the string table
class Symbol_table
{
public:
    Symbol_table(FILE *file, const elf64_shdr &shdr, const Section_hdr_table &sec_hdr_tbl) : m_sym_tbl(shdr.sh_size / sizeof(Elf64_Sym)) 
    {
        assert(shdr.sh_type == SHT_SYMTAB);
        fseek(file, shdr.sh_offset, 0);
        fread(m_sym_tbl.data(), 1, shdr.sh_size, file);

        auto &str_tbl_sec_hdr = sec_hdr_tbl.headers()[shdr.sh_link];

        m_str_tbl = Create_symbol_string_table(file, str_tbl_sec_hdr, m_sym_tbl);
    }

    ~Symbol_table() = default;

    const std::vector<Elf64_Sym>& data() const {return m_sym_tbl;}
    const String_table& string_table() const {return *m_str_tbl.get();}
    const String_table::str_ptr_list_t& str_table_ptr() const {return m_str_tbl.get()->str_ptr_list();}
private:
    std::vector<Elf64_Sym> m_sym_tbl;
    //name of each symbol
    std::unique_ptr<String_table> m_str_tbl;
};





// shdr should be string table section header
inline std::unique_ptr<String_table> Create_symbol_string_table(FILE *file, const elf64_shdr &shdr, const std::vector<Elf64_Sym> &symbol_vec)
{
    assert(shdr.sh_type == SHT_STRTAB);

    auto str_tbl = std::vector<char>(shdr.sh_size);

    fseek(file, shdr.sh_offset, 0);
    fread(str_tbl.data(), 1, shdr.sh_size, file);
    
    auto str_table_ptr = std::vector<const char*>(symbol_vec.size());

    for(auto &symbol : symbol_vec)
        str_table_ptr[&symbol - symbol_vec.data()] = &str_tbl[symbol.st_name];
    
    auto ret = std::make_unique<String_table>(std::move(str_table_ptr), std::move(str_tbl));

    return ret;
}


class Relocatable_file
{



};

}


