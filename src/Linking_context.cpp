#include <vector>
#include <algorithm>
#include <numeric>
#include <iostream>
#include <fstream>
#include "Linking_context.h"
#include "Relocatable_file.h"
#include "util.h"
#include "Archive_file.h"

using Link_option_args = Linking_context::Link_option_args;

constexpr std::size_t gARCHIVE_MAGIC_LEN = nUtil::const_expr_STR_len(ARCHIVE_FILE_MAGIC);

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

static bool Is_archived_file(FILE *file);

static eFile_type Get_file_type(const elf64_hdr &hdr);

static std::vector< Link_option_args::path_of_file_t> Find_libraries(const Link_option_args &link_option_args);

static std::string_view Read_archive_scetion_name(const Archive_file_header &ar_fhdr, const char* str_tbl);

static void Load_archived_file_section(Linking_context &linking_ctx, const std::vector<Link_option_args::path_of_file_t> &lib_path) ;

static void Collect_rel_file_content(Linking_context &linking_ctx, const Link_option_args &link_option_args);

static void Reference_dependent_file(Input_file &input_file, 
                                     Linking_context &ctx, 
                                     std::vector<bool> &alive_list, 
                                     const std::pair<Input_file*, Input_file*> input_file_range);


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
        FATALF("%s", "some libs are not found !");
    return ret;
}

static std::string_view Read_archive_scetion_name(const Archive_file_header &ar_fhdr, const char* str_tbl)
{
    std::string_view ret;
    std::string_view name(ar_fhdr.file_identifier, sizeof(ar_fhdr.file_identifier));

    if (name.substr(0, 1) == "/") //long file name
    {
        // skip the first char '/'
        // the following chars are numbers and spaces
        // exp: "123  "
        int offset = atoi(&name.at(1));

        const char *p = &str_tbl[offset];
        while(*p != '\0' && *p != '/')
            p++;
        ret = std::string_view(&str_tbl[offset], p-&str_tbl[offset]);
    }
    else //short file name
    {
        auto name_end = name.find_first_of("/");
        assert(name_end != std::string::npos);
        ret = name.substr(0, name_end);
    }
    return ret;

}

static void Load_archived_file_section(Linking_context &linking_ctx, const std::vector<Link_option_args::path_of_file_t> &lib_path)
{
    for(const auto &path : lib_path)
    {
        FILE *fptr = fopen(path.c_str(), "rb");
        if (fptr == nullptr)
            FATALF("%s", "fail to open the file\n");

        if (Is_archived_file(fptr) == false)
            FATALF("%s is not an archived file !\n", path.c_str());

        std::unique_ptr<const char[]> str_tbl_sec;

        while(feof(fptr) == 0)
        {
            Archive_file_header ar_fhdr;
                    
            if (fread(&ar_fhdr, sizeof(ar_fhdr), 1, fptr) != 1)
            {
                if (feof(fptr) == 0)
                    FATALF("%s", "fail to load the header\n");                
                else // it's the end of the file, stop reading it
                    break;
            }
                      
            auto sz = std::atoi(reinterpret_cast<const char*>(&ar_fhdr.file_size));

            // align read size because sections are aligned with 2
            sz += sz%2;

            auto mem = std::make_unique<char[]>(sz);

            if (fread(mem.get(), sizeof(char), sz, fptr) != (std::size_t)sz)
                FATALF("fail to load the sections, only %u is read\n", sz);

            if(Archive_file_header::Is_symtab(ar_fhdr) == true)
                continue;
            else if (Archive_file_header::Is_strtab(ar_fhdr) == true)
            {
                str_tbl_sec = std::move(mem);
            }
            else // object file
            {
                if (Get_file_type(*reinterpret_cast<const elf64_hdr*>(mem.get())) != eFile_type::ET_REL)        
                    FATALF("%s expected to be a relocation type but it is actully not a rel file !", path.c_str());

                std::string_view name = Read_archive_scetion_name(ar_fhdr, reinterpret_cast<const char*>(str_tbl_sec.get()));
                //std::cout << name << "\n";
                linking_ctx.insert_object_file(Relocatable_file(std::move(mem), std::string(name)), true);
            }
        }
        fclose(fptr);
    }
}

static void Collect_rel_file_content(Linking_context &linking_ctx, const Link_option_args &link_option_args)
{
    // read .o files
    for(const auto &path : link_option_args.obj_file)
    {
        std::ifstream file(path, std::ios::binary);

        if (file.is_open() == false)
            FATALF("fail to open %s", path.c_str());
        
        file.seekg(0, std::ios::end);
        std::size_t file_size =  file.tellg();

        auto mem = new char[file_size];

        file.seekg(0, std::ios::beg);

        file.read(mem, file_size);
        
        file.close();

        if (Get_file_type(*reinterpret_cast<const elf64_hdr*>(mem)) != eFile_type::ET_REL)        
            FATALF("%s expected to be a relocation type but it is actully not a rel file !", path.c_str());
        
        linking_ctx.insert_object_file({std::unique_ptr<char[]>(mem), path}, false);
    }
    
    Load_archived_file_section(linking_ctx, link_option_args.library);
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
                link_option_args.link_machine_optinon = Linking_context::eLink_machine_optinon::elf64lriscv;
            else
                link_option_args.link_machine_optinon = Linking_context::eLink_machine_optinon::unknown;

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


// Bind undef global symbols in symbol list of each input file to defined global symbols.
// If a file is not referenced by other files through global symbols, then
// content of this file is no need to be linked into the output file.
// a file with no reference will be discarded from the file list
// input_file_range.first: addr of the first input_file
// input_file_range.second: addr of the last input_file + 1
static void Reference_dependent_file(Input_file &input_file, 
                                     Linking_context &ctx, 
                                     std::vector<bool> &alive_list, 
                                     const std::pair<Input_file*, Input_file*> input_file_range)
{        
    for(std::size_t sym_idx = input_file.src().linking_mdata().first_global ; sym_idx < input_file.src().symbol_table()->count() ; sym_idx++)
    {
        if (input_file.symbol_list[sym_idx] != nullptr) //not nullptr, no need to be binded to the global symbal 
            continue;

        auto &esym = input_file.src().symbol_table()->data(sym_idx);

        // a file would be referenced when it has a defined symbol 
        // which is not defined in 'the other' file

        // it's defined, don't mark alive its source, because it's itself
        if (   !nELF_util::Is_sym_undef(esym) 
            && (nELF_util::Is_sym_common(esym) && !nELF_util::Is_sym_common(input_file.symbol_list[sym_idx]->elf_sym())))
            continue;
        
        auto it = ctx.Find_symbol(nELF_util::Get_symbol_name(input_file.src(), sym_idx));
        
        if (it == ctx.global_symbol_map().end())
        {
/*             std::cout << nELF_util::Get_symbol_name(input_file.src(), sym_idx) << \
            " " << esym.st_size << " " << ELF64_ST_TYPE(esym.st_info) << " not found \n"; */
            continue;
        }

        // bind the undef symbol with the defined global symbol which is from the other file
        input_file.symbol_list[sym_idx] = it->second.symbol.get();
        
        assert(it->second.input_file >= input_file_range.first && it->second.input_file < input_file_range.second);
        
        alive_list[it->second.input_file - input_file_range.first] = true;
    }
}

template <typename File_list_t>
void Remove_unused_file(File_list_t &dst, const std::vector<bool> &is_alive)
{
    auto cmp_gen = [&is_alive](auto begin) 
    {
        return [&is_alive, begin](auto &item) -> bool
        {
            return is_alive[&item - &(*begin)] == false;
        };
    };

    auto remove_start = std::remove_if(dst.begin(), dst.end(), cmp_gen(dst.begin()));
    dst.erase(remove_start, dst.end());
}

Linking_context::Linking_context(Link_option_args link_option_args, eLink_machine_optinon maching_opt)
                               : m_link_option_args(std::move(link_option_args)), 
                                 m_link_machine_optinon(maching_opt)
{
    Parse_args(&m_link_option_args, m_link_option_args.argc, m_link_option_args.argv);

    m_link_option_args.library = Find_libraries(m_link_option_args);

    Collect_rel_file_content(*this, m_link_option_args);
}


void Linking_context::Link()
{
    for(std::size_t i = 0 ; i < m_rel_file.size() ; i++)
    {
        m_input_file.push_back(Input_file(*m_rel_file[i].get()));
    }

    for(auto &file : m_input_file)
        file.Put_global_symbol(*this);

    std::vector<bool> is_not_from_lib = m_is_alive;
    
    for(std::size_t i = 0 ; i < m_input_file.size() ; i++)
    {
        if (is_not_from_lib[i] == true)
            Reference_dependent_file(m_input_file[i], *this, m_is_alive, {m_input_file.data(), m_input_file.data() + m_input_file.size()});
    }

    //mark object files needed by other in the same archive file, and not other which is not from the archive file
    for(std::size_t i = 0 ; i < m_input_file.size() ; i++)
    {
        if (is_not_from_lib[i] == false && m_is_alive[i] == true) // is in lib, and referenced by obj files which is not in archive file
            Reference_dependent_file(m_input_file[i], *this, m_is_alive, {m_input_file.data(), m_input_file.data() + m_input_file.size()});
    }

    Remove_unused_file(m_input_file, m_is_alive);
    Remove_unused_file(m_rel_file, m_is_alive);
    m_is_alive.clear();
    
    for(std::size_t i = 0 ; i < m_input_file.size() ; i++)
        m_input_file[i].Init_mergeable_section(*this);

    for(std::size_t i = 0 ; i < m_input_file.size() ; i++)
        m_input_file[i].Collect_mergeable_section();

    for(std::size_t i = 0 ; i < m_input_file.size() ; i++)
        m_input_file[i].Resolve_sesction_pieces(*this);

    for(auto &item : merged_section_map)
        item.second->Aggregate_section_fragment();
}

