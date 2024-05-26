#include <iostream>
#include <assert.h>
#include <vector>
#include <utility>
#include <string.h>
#include <memory>
#include "../my_rvemu/elf.h"


class String_table
{
public:
    String_table(std::vector<const char*> str_table_ptr, std::vector<char> str_tbl) 
               : m_str_table_ptr(std::move(str_table_ptr)), 
                 m_str_tbl(std::move(str_tbl))
    {

    }
    const char* const* begin_str_ptr() const {return m_str_table_ptr.data();}
    const char* const* end_str_ptr() const {return m_str_table_ptr.data() + m_str_table_ptr.size();}
    const char* const* str_at(std::size_t idx) const {return m_str_table_ptr.data() + idx;}
    const std::vector<const char*>& str_table_ptr() const {return m_str_table_ptr;}

private:
    std::vector<const char*> m_str_table_ptr;
    std::vector<char> m_str_tbl;
};

std::unique_ptr<String_table> Create_symbol_string_table(FILE *file, const elf64_shdr &shdr, const std::vector<Elf64_Sym> &symbol_vec);


class Section_hdr_table
{
public:

    Section_hdr_table(FILE *file, const elf64_hdr &hdr) : m_shdr_vec(hdr.e_shnum), m_tbl(nullptr)
    {
        fseek(file, hdr.e_shoff, 0);
        fread(m_shdr_vec.data(), sizeof(elf64_shdr), hdr.e_shnum, file);

        auto str_tbl = std::vector<char>(m_shdr_vec[hdr.e_shstrndx].sh_size);

        fseek(file, m_shdr_vec[hdr.e_shstrndx].sh_offset, 0);
        fread(str_tbl.data(), 1, m_shdr_vec[hdr.e_shstrndx].sh_size, file);

        auto str_table_ptr = std::vector<const char*>(hdr.e_shnum);

        for(auto &shdr : m_shdr_vec)
            str_table_ptr[&shdr - m_shdr_vec.data()] = &str_tbl[shdr.sh_name];

        m_tbl = new String_table(std::move(str_table_ptr), std::move(str_tbl));
    }

    ~Section_hdr_table() {delete m_tbl;}

    const std::vector<elf64_shdr>& headers() const {return m_shdr_vec;}
    const String_table& string_table() const {return *m_tbl;}

private:
    std::vector<elf64_shdr> m_shdr_vec;
    String_table *m_tbl;
};


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
    const std::vector<const char*>& str_table_ptr() const {return m_str_tbl.get()->str_table_ptr();}
private:
    std::vector<Elf64_Sym> m_sym_tbl;
    std::unique_ptr<String_table> m_str_tbl;
};


struct Link_option_args
{

    std::vector<std::string> library_path;
    std::vector<std::string> obj_file;
};

void Parse_args(int argc, char* argv[], Link_option_args &link_option_args)
{
    const char defult_out_name[] = "a.exe";

    const char *output_file = defult_out_name;

    for(std::size_t i = 0 ; i < argc ; i++)
    {
        if (   strcmp(argv[i], "-o") == 0
            || strcmp(argv[i], "-output") == 0
            || strcmp(argv[i], "--output") == 0)
        {
            if (output_file != defult_out_name)
            {
                printf("more than 1 output name !!\n");
                abort();
            }
            output_file = argv[i+1];
            i++; // skip the next arg
        }
        else if (   strcmp(argv[i], "-v") == 0
                 || strcmp(argv[i], "-version") == 0
                 || strcmp(argv[i], "--version") == 0)
        {
            printf("version 9.4.8.7\n");
            return;
        }
        else if (   strcmp(argv[i], "-h") == 0
                 || strcmp(argv[i], "-help") == 0
                 || strcmp(argv[i], "--help") == 0)
        {
            printf("I can help you\n");
            return;
        }
        else if (memcmp(argv[i], "-m", 2) == 0 && argv[i][2] != '\0') // it should not be just "-m"
        {
            printf("m option %s\n", &argv[i][2]);
        }
        else if (memcmp(argv[i], "-L", 2) == 0 && argv[i][2] != '\0') // it should not be just "-L")
        {
            link_option_args.library_path.push_back(argv[i]);
        }
        // if an arg has no leading "-" and is not the output_file, then it is an object file
        else if (memcmp(argv[i], "-", 1) != 0 && argv[i] != output_file) 
        {
            link_option_args.obj_file.push_back(argv[i]);
        }
    }

    printf("out: %s\n", output_file);
    
    if (link_option_args.library_path.size() > 0)
    {
        std::cout << "library path\n";
        for(const auto &path : link_option_args.library_path)
        {
            std::cout << path << "\n";
        }
    }

    if (link_option_args.obj_file.size() > 0)
    {
        std::cout << "object file\n";
        for(const auto &obj_file : link_option_args.obj_file)
        {
            std::cout << obj_file << "\n";
        }
    }
}

int main(int argc, char* argv[])
{
    
    for(int i = 1 ; i < argc ; i++)
        printf("%s\n", argv[i]);
    auto file = fopen("test3.o", "rb");

    assert(file != nullptr);

    elf64_hdr hdr;

    fread(&hdr, 1, sizeof(elf64_hdr), file);

    Section_hdr_table sec_hdr_tbl(file, hdr);

    for(auto it = sec_hdr_tbl.string_table().begin_str_ptr() ; it != sec_hdr_tbl.string_table().end_str_ptr() ; it++)
        printf("%s\n",*it);


    std::size_t symtab_idx = -1, size = 0;

    for(auto &item : sec_hdr_tbl.headers())
    {
        if (item.sh_type == SHT_SYMTAB)
        {
            symtab_idx = &item - &(*sec_hdr_tbl.headers().begin());
            size = item.sh_size;
        }
    }

    assert(symtab_idx != -1);

    Symbol_table sym_tbl(file, sec_hdr_tbl.headers()[symtab_idx], sec_hdr_tbl);

    std::cout << sym_tbl.data().size() << " symbols\n";

    for(auto &item : sym_tbl.str_table_ptr())
        printf("%s\n",item);

    fclose(file);


    Link_option_args link_option_args {};
    Parse_args(argc - 1, argv+1, link_option_args);
}

std::unique_ptr<String_table> Create_symbol_string_table(FILE *file, const elf64_shdr &shdr, const std::vector<Elf64_Sym> &symbol_vec)
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