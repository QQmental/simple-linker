#include <vector>
#include <algorithm>
#include <numeric>
#include <iostream>
#include <fstream>
#include <filesystem>
#include "Linking_context.h"
#include "Relocatable_file.h"
#include "util.h"
#include "Archive_file.h"
#include "Linking_passes.h"

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

static void Clear_unused_resources(Linking_context &ctx, 
                            std::unordered_map<std::string_view, Linking_context::linking_package> &global_symbol_map,
                            std::vector<std::unique_ptr<Relocatable_file>> &rel_files,
                            std::vector<Input_file> &input_file_list,
                            std::vector<bool> &is_alive);


static void Add_synthetic_symbols(Linking_context &ctx);

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
            auto library = lib_path.substr(2, lib_path.length()) /* skip "-L" */
                         + "/lib"
                         + lib_name.substr(2, lib_name.length()) /* skip "-l" */
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
                linking_ctx.insert_object_file(Relocatable_file(std::move(mem), std::string(name) + path), true);
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

Linking_context::Linking_context(Link_option_args link_option_args):m_link_option_args(std::move(link_option_args))
{
    Parse_args(&m_link_option_args, m_link_option_args.argc, m_link_option_args.argv);

    m_link_option_args.library = Find_libraries(m_link_option_args);

    Collect_rel_file_content(*this, m_link_option_args);
}

static void Clear_unused_resources(Linking_context &ctx, 
                            std::unordered_map<std::string_view, Linking_context::linking_package> &global_symbol_map,
                            std::vector<std::unique_ptr<Relocatable_file>> &rel_files,
                            std::vector<Input_file> &input_file_list,
                            std::vector<bool> &is_alive)
{
    auto *input_file_data = input_file_list.data();
    std::vector<uint32_t> offset_list(input_file_list.size());
    for(std::size_t i = 0, new_place = 0 ; i < offset_list.size() ; i++)
    {
        if (is_alive[i] == true)
            offset_list[i] = new_place++;
    }
    Remove_unused_file(input_file_list, is_alive);
    Remove_unused_file(rel_files, is_alive);
    
    for(auto it = global_symbol_map.begin() ; it != global_symbol_map.end() ;)
    {
        if (is_alive[it->second.input_file - input_file_data] == false)
        {
            it = global_symbol_map.erase(it); // this symbol is from a dead input file, so remove it
            continue;
        }
        auto new_offset = offset_list[it->second.input_file - input_file_data];
        it->second.input_file =  &input_file_list[new_offset]; // move Input_file* to the correct Input_file*
        ++it;
    }
    is_alive.clear();
}

static void Add_synthetic_symbols(Linking_context &ctx)
{
    elf64_hdr elf_hdr{};
    elf_hdr.e_type = ET_REL;
    elf_hdr.e_shstrndx = 3;
    elf_hdr.e_shentsize = sizeof(Elf64_Shdr);
    elf_hdr.e_shnum = 4;
    elf_hdr.e_shoff = nUtil::align_to(sizeof(elf64_hdr), std::alignment_of_v<elf64_shdr>) ;
    elf_hdr.e_machine = (Elf64_Half)ctx.maching_option();

    elf64_shdr dummy_shdr{};

    elf64_shdr symtab_shdr{};
    symtab_shdr.sh_type = SHT_SYMTAB;
    symtab_shdr.sh_addralign = std::alignment_of_v<elf64_sym>;
    symtab_shdr.sh_entsize = sizeof(elf64_sym);
    symtab_shdr.sh_type = SHT_SYMTAB;
    symtab_shdr.sh_info = 1; // set the start of global symbols at 1
    symtab_shdr.sh_link = 2;
    
    elf64_shdr sym_strtab_shdr{};
    sym_strtab_shdr.sh_type = SHT_SYMTAB;
    sym_strtab_shdr.sh_addralign = std::alignment_of_v<char>;
    sym_strtab_shdr.sh_entsize = sizeof(char);
    sym_strtab_shdr.sh_type = SHT_STRTAB;

    elf64_shdr sec_strtab_shdr{};
    sec_strtab_shdr.sh_type = SHT_SYMTAB;
    sec_strtab_shdr.sh_addralign = std::alignment_of_v<char>;
    sec_strtab_shdr.sh_entsize = sizeof(char);
    sec_strtab_shdr.sh_type = SHT_STRTAB;

    std::vector<elf64_sym> esyms;
    std::vector<char> sym_strtab;
    auto add_esym = [&symtab_shdr, &esyms, &sym_strtab_shdr, &sym_strtab](std::string_view name)
    {
        elf64_sym esym{};
        esym.st_shndx = SHN_ABS;
        esym.st_info = ELF64_ST_INFO(STB_GLOBAL, STT_NOTYPE);
        esym.st_name = sym_strtab_shdr.sh_size;

        sym_strtab_shdr.sh_size += name.size() + 1; // plus one null character

        for(std::size_t i = 0 ; i < name.size() + 1 ; i++)
            sym_strtab.push_back(name[i]);

        esyms.push_back(esym);

        symtab_shdr.sh_size += sizeof(elf64_sym);

    };
    add_esym(""); // put the first symbol, which is a dummy symbol
    add_esym(ctx.special_symbols.global_pointer_name);
    add_esym(ctx.special_symbols.bss_start_name);
    add_esym(ctx.special_symbols.end_name);
    add_esym(ctx.special_symbols._end_name);
    add_esym(ctx.special_symbols.etext_name);
    add_esym(ctx.special_symbols._etext_name);
    add_esym(ctx.special_symbols.edata_name);
    add_esym(ctx.special_symbols._edata_name);
    add_esym(ctx.special_symbols.libc_fini_name);
    add_esym(ctx.special_symbols.preinit_array_end_name);
    add_esym(ctx.special_symbols.preinit_array_start_name);
    add_esym(ctx.special_symbols.init_array_start_name);
    add_esym(ctx.special_symbols.init_array_end_name);
    add_esym(ctx.special_symbols.fini_array_start_name);
    add_esym(ctx.special_symbols.fini_array_end_name);

    std::vector<char> sec_strtab;
    auto add_sec_str = [&sec_strtab_shdr, &sec_strtab](auto &str, elf64_shdr &shdr)
    {
        for(std::size_t idx = 0 ; idx < nUtil::const_expr_STR_len(str) + 1 ; idx++)
            sec_strtab.push_back(str[idx]);
        shdr.sh_name = sec_strtab_shdr.sh_size;
        sec_strtab_shdr.sh_size += nUtil::const_expr_STR_len(str) + 1;
    };

    add_sec_str("", dummy_shdr);
    add_sec_str(".symtab", symtab_shdr);
    add_sec_str(".strtab", sym_strtab_shdr);
    add_sec_str(".shstrtab", sec_strtab_shdr);

    symtab_shdr.sh_offset = nUtil::align_to(elf_hdr.e_shoff + elf_hdr.e_shnum*elf_hdr.e_shentsize, symtab_shdr.sh_addralign) ;
    sym_strtab_shdr.sh_offset = nUtil::align_to(symtab_shdr.sh_offset + symtab_shdr.sh_size, sym_strtab_shdr.sh_addralign);
    sec_strtab_shdr.sh_offset = nUtil::align_to(sym_strtab_shdr.sh_offset + sym_strtab_shdr.sh_size, sec_strtab_shdr.sh_addralign);

    // data ends with data of sec_strtab_shdr, so the allocated size is equal to its offset plus its size
    std::unique_ptr<char[]> mem = std::make_unique<char[]>(sec_strtab_shdr.sh_offset + sec_strtab_shdr.sh_size);
    memcpy(mem.get(), &elf_hdr, sizeof(elf_hdr));                                                                    // copy elf header
    char *dst = (char*)memcpy(mem.get() + elf_hdr.e_shoff, &dummy_shdr, elf_hdr.e_shentsize) + elf_hdr.e_shentsize;  // copy section headers
    dst = (char*)memcpy(dst, &symtab_shdr, elf_hdr.e_shentsize) + elf_hdr.e_shentsize;
    dst = (char*)memcpy(dst, &sym_strtab_shdr, elf_hdr.e_shentsize) + elf_hdr.e_shentsize;;
    dst = (char*)memcpy(dst, &sec_strtab_shdr, elf_hdr.e_shentsize) + elf_hdr.e_shentsize;;
    memcpy(mem.get() + symtab_shdr.sh_offset, esyms.data(), esyms.size() * symtab_shdr.sh_entsize);                   // copy elf symbols
    memcpy(mem.get() + sym_strtab_shdr.sh_offset, sym_strtab.data(), sym_strtab.size() * sym_strtab_shdr.sh_entsize); // copy name of elf syms
    memcpy(mem.get() + sec_strtab_shdr.sh_offset, sec_strtab.data(), sec_strtab.size() * sec_strtab_shdr.sh_entsize); // copy name of sections

    ctx.insert_object_file(Relocatable_file(std::move(mem), "linker-synthetic_obj"), false);

}

void Linking_context::Link()
{
    Add_synthetic_symbols(*this);

    for(std::size_t i = 0 ; i < m_rel_file.size() ; i++)
    {
        m_input_file.push_back(Input_file(*m_rel_file[i].get()));
    }
    assert(m_input_file.size() == m_rel_file.size());

    for(auto &file : m_input_file)
        file.Put_global_symbol(*this);

    nLinking_passes::Bind_special_symbols(*this);

    nLinking_passes::Resolve_symbols(*this, m_input_file, m_is_alive);

    Clear_unused_resources(*this, m_global_symbol_map, m_rel_file, m_input_file, m_is_alive);

    for(std::size_t i = 0 ; i < m_input_file.size() ; i++)
        m_input_file[i].Init_mergeable_section(*this);

    for(std::size_t i = 0 ; i < m_input_file.size() ; i++)
        m_input_file[i].Collect_mergeable_section_piece();

    for(std::size_t i = 0 ; i < m_input_file.size() ; i++)
        m_input_file[i].Resolve_sesction_pieces(*this);
    
    for(auto &item : merged_section_map())
    {
        item.second->Assign_offset();
    }

    nLinking_passes::Create_synthetic_sections(*this);

     for(std::size_t i = 0 ; i < m_input_file.size() ; i++)
        nLinking_passes::Check_duplicate_smbols(m_input_file[i]);

    nLinking_passes::Combined_input_sections(*this);

    nLinking_passes::Assign_input_section_offset(*this);

    nLinking_passes::Sort_output_sections(*this);

    Create_output_symtab();

    nLinking_passes::Compute_section_headers(*this);

    filesize = nLinking_passes::Set_output_chunk_locations(*this);

    nLinking_passes::Fix_up_synthetic_symbols(*this);
    
    using perm_t = std::filesystem::perms;
    
    auto perms = perm_t::owner_all 
               | perm_t::group_exec  | perm_t::group_read 
               | perm_t::others_exec | perm_t::others_read;
    
    output_file = Output_file(*this, m_link_option_args.output_file, filesize, perms);
    
    this->buf = output_file.buf();
    
    nLinking_passes::Copy_chunks(*this); 
    
    for(auto &osec : osec_pool())
        std::cout << osec.first.name << "\n";
    for(auto &[sym_name, pkg] : global_symbol_map())
    {
        std::cout << sym_name << " " << pkg.input_file->name() << "\n";
    }
    output_file.Serialize();
}

void Linking_context::Create_output_symtab()
{
    for(auto &file : m_input_file)
        file.Compute_symtab_size(*this);

    for(auto &output_chunk : output_chunk_list)
        output_chunk.Compute_symtab_size();
}