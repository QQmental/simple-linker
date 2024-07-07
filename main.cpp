#include <iostream>
#include <assert.h>
#include <vector>
#include <utility>
#include <algorithm>
#include <string.h>
#include <memory>
#include <math.h>
#include <fstream>

#include "RISC_V_linking_data.h"
#include "Archive_file.h"
#include "util.h"
#include "Relocatable_file.h"

constexpr std::size_t gARCHIVE_MAGIC_LEN = nUtil::const_expr_STR_len(ARCHIVE_FILE_MAGIC);

enum class eLink_machine_optinon : uint16_t
{
    unknown = 0,
    elf64lriscv
};

enum class eFile_type :decltype(elf64_hdr::e_type)
{
    ET_NONE   = 0,
    ET_REL    = 1,
    ET_EXEC   = 2,
    ET_DYN    = 3,
    ET_CORE   = 4,
    ET_LOPROC = 0xff00,
    ET_HIPROC = 0xffff
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
};



static void Parse_args(Link_option_args *dst, int argc, char* argv[]);
static bool Is_archived_file(FILE *file);
static eFile_type Get_file_type(const elf64_hdr &hdr);
static std::vector<Link_option_args::path_of_file_t> Find_libraries(const Link_option_args &link_option_args);

static std::string Read_archive_scetion_name(const Archive_file_header &ar_fhdr, const char* str_tbl);
static void Load_archived_file_section(std::vector<Relocatable_file> &object_file, const std::vector<Link_option_args::path_of_file_t> &lib_path) ;
static void Collect_rel_file_content(std::vector<Relocatable_file> &object_file, const Link_option_args &link_option_args);

static void Show_link_option_args(const Link_option_args &link_option_args);




int main(int argc, char* argv[])
{
    Link_option_args link_option_args ;

    Parse_args(&link_option_args, argc - 1, argv+1);

    Show_link_option_args(link_option_args);

    link_option_args.library = Find_libraries(link_option_args);

    std::vector<Relocatable_file> rel_file;

    Collect_rel_file_content(rel_file, link_option_args);
}

//the first few bytes are read from the file and compared with ARCHIVE_FILE_MAGIC, 
//if they are not equal, return false

static bool Is_archived_file(FILE *file)
{
    char archiv_header[gARCHIVE_MAGIC_LEN]{};

    if (fread(archiv_header, 1, gARCHIVE_MAGIC_LEN, file) != gARCHIVE_MAGIC_LEN)
        return false;

    return memcmp(archiv_header, ARCHIVE_FILE_MAGIC, gARCHIVE_MAGIC_LEN) == 0;
}

static eFile_type Get_file_type(const elf64_hdr &hdr)
{
    decltype(hdr.e_type) ret;
    // why memcpy? because it probably violates strict pointer aliasing rule with the arg 'hdr'
    memcpy(&ret, &hdr.e_type, sizeof(ret));
    return (eFile_type)ret;
}

// return path of the included libraries
static std::vector<Link_option_args::path_of_file_t> Find_libraries(const Link_option_args &link_option_args)
{
    std::vector<std::string> ret;
    std::vector<bool> is_lib_found(link_option_args.library_name.size());

    for(const auto &lib_path : link_option_args.library_search_path)
    {
        for(const auto &lib_name : link_option_args.library_name)
        {
            // -LXXX -lYYY => XXX/libYYY.a
            auto library = lib_path.substr(2, lib_path.length()) 
                         + "/lib"
                         + lib_name.substr(2, lib_name.length())
                         + ".a";

            if (is_lib_found[&lib_name - &link_option_args.library_name[0]])
                continue;
                
            FILE *file = fopen(library.c_str(), "rb");

            if (file != nullptr)
            {
                if (Is_archived_file(file) == false)
                {
                    fclose(file);
                    FATALF("%s is not an archiv file!\n", library.c_str());
                }
 
                fclose(file);

                ret.push_back(std::move(library));
                is_lib_found[&lib_name - &link_option_args.library_name[0]] = true;
            }
        }
    }

    if (std::any_of(is_lib_found.begin(), is_lib_found.end(), [](auto val){return val == false;}) == true)
        FATALF("some libs are not found !");
    return ret;
}

static std::string Read_archive_scetion_name(const Archive_file_header &ar_fhdr, const char* str_tbl)
{
    std::string ret;
    std::string name(ar_fhdr.file_identifier, sizeof(ar_fhdr.file_identifier));

    if (name.substr(0, 1) == "/") //long file name
    {
        // skip the first char '/'
        // the following chars are numbers and spaces
        // exp: "123  "
        int offset = atoi(&name.at(1));

        const char *p = &str_tbl[offset];
        while(*p != '\0' && *p != '/')
            ret.append(std::string(const_cast<char*>(p++), 1)) ;
    }
    else //short file name
    {
        auto name_end = name.find_first_of("/");
        assert(name_end != std::string::npos);
        ret = name.substr(0, name_end);
    }
    return ret;

}

static void Load_archived_file_section(std::vector<Relocatable_file> &rel_file, const std::vector<Link_option_args::path_of_file_t> &lib_path)
{
    for(const auto &path : lib_path)
    {
        FILE *fptr = fopen(path.c_str(), "rb");
        if (fptr == nullptr)
            FATALF("fail to open the file\n");

        if (Is_archived_file(fptr) == false)
            FATALF("%s is not an archived file !\n", path.c_str());

        std::unique_ptr<char[]> str_tbl_sec;

        while(feof(fptr) == 0)
        {
            Archive_file_header ar_fhdr;
                    
            if (fread(&ar_fhdr, sizeof(ar_fhdr), 1, fptr) != 1)
            {
                if (feof(fptr) == 0)
                    FATALF("fail to load the header\n");                
                else // it's the end of the file, stop reading it
                    break;
            }
                      
            auto sz = std::atoi(reinterpret_cast<const char*>(&ar_fhdr.file_size));

            // align read size because sections are aligned with 2
            sz += sz%2;

            std::unique_ptr<char[]> section(new char[sz]);

            if (fread(section.get(), sizeof(char), sz, fptr) != (std::size_t)sz)
                FATALF("fail to load the sections, only %u is read\n", sz);

            if(Archive_file_header::Is_symtab(ar_fhdr) == true)
                continue;
            else if (Archive_file_header::Is_strtab(ar_fhdr) == true)
            {
                str_tbl_sec = std::move(section);
            }
            else // object file
            {
                if (Get_file_type(*reinterpret_cast<const elf64_hdr*>(section.get())) != eFile_type::ET_REL)        
                    FATALF("%s expected to be a relocation type but it is actully not a rel file !", path.c_str());

                std::string name = Read_archive_scetion_name(ar_fhdr, reinterpret_cast<const char*>(str_tbl_sec.get()));
                //rel_file.push_back(std::move(section));
            }
        }
        fclose(fptr);
    }
}

static void Collect_rel_file_content(std::vector<Relocatable_file> &rel_file, const Link_option_args &link_option_args)
{
    for(const auto &path : link_option_args.obj_file)
    {
        std::ifstream file(path, std::ios::binary);

        if (file.is_open() == false)
            FATALF("fail to open %s", path.c_str());
        
        file.seekg(0, std::ios::end);
        std::size_t file_size =  file.tellg();
        
        auto new_obj_file = std::unique_ptr<char[]>(new char[file_size]);

        file.seekg(0, std::ios::beg);

        file.read(reinterpret_cast<char*>(new_obj_file.get()), file_size);
        
        file.close();

        if (Get_file_type(*reinterpret_cast<const elf64_hdr*>(new_obj_file.get())) != eFile_type::ET_REL)        
            FATALF("%s expected to be a relocation type but it is actully not a rel file !", path.c_str());
        
        rel_file.push_back(std::move(new_obj_file));
    } 
    
    Load_archived_file_section(rel_file, link_option_args.library);

    int x = 0;
    for(auto &item : rel_file)
    {
        for(std::size_t i = 0 ; i < item.section_hdr_table().header_count() ; i++)
        {
            auto &shdr = item.section_hdr_table().headers()[i];
            if (shdr.sh_type == SHT_RISC_V_ATTRIBUTES)
            {
                x++;
            }
        }

    }
}

static void Parse_args(Link_option_args *dst, int argc, char* argv[])
{
    *dst = {};
    Link_option_args &link_option_args = *dst;

    const char *output_file = nullptr;

    for(int i = 0 ; i < argc ; i++)
    {
        if (   strcmp(argv[i], "-o") == 0
            || strcmp(argv[i], "-output") == 0
            || strcmp(argv[i], "--output") == 0)
        {
            if (i + 1 == argc)
            {
                printf("output file name is not specified after the output flag!!\n");
                abort();
            }
            output_file = argv[i+1];
            i++; // skip the next one--- output file name
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
            if (strcmp(argv[i], "-melf64lriscv") == 0)
                link_option_args.link_machine_optinon = eLink_machine_optinon::elf64lriscv;
            else
                link_option_args.link_machine_optinon = eLink_machine_optinon::unknown;

            printf("m option %s\n", &argv[i][2]);
        }
        else if (memcmp(argv[i], "-L", 2) == 0 && argv[i][2] != '\0') // it should not be just "-L")
        {
            link_option_args.library_search_path.push_back(argv[i]);
        }
        else if (memcmp(argv[i], "-l", 2) == 0 && argv[i][2] != '\0') // it should not be just "-l")
        {
            link_option_args.library_name.push_back(argv[i]);
        }
        else if (memcmp(argv[i], "-", 1) == 0) // skip other the flags
        {
            while(i + 1 < argc && memcmp(argv[i + 1], "-", 1) != 0) // skip the following args of this flag
                i++;
        }
        // if an arg has no leading "-" and is not the output_file, 
        // then it is an object file
        else
        {
            link_option_args.obj_file.push_back(argv[i]);
        }
    }

    if (output_file != nullptr)
        link_option_args.output_file = output_file;
}

static void Show_link_option_args(const Link_option_args &link_option_args)
{
    printf("out: %s\n", link_option_args.output_file.c_str());
    
    if (link_option_args.library_search_path.size() > 0)
    {
        std::cout << "library path\n";
        for(const auto &path : link_option_args.library_search_path)
        {
            std::cout << path << "\n";
        }
    }
    if (link_option_args.library_name.size() > 0)
    {
        std::cout << "library name\n";
        for(const auto &name : link_option_args.library_name)
        {
            std::cout << name << "\n";
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