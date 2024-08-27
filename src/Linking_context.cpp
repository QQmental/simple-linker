#include <vector>
#include <algorithm>
#include <numeric>
#include <iostream>
#include <fstream>
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

static void Remove_unused_symbol(Linking_context &ctx, 
                                  std::unordered_map<std::string_view, Linking_context::linking_package> &global_symbol_map,
                                  std::vector<bool> &is_alive);

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


static void Remove_unused_symbol(Linking_context &ctx, 
                                 std::unordered_map<std::string_view, Linking_context::linking_package> &global_symbol_map,
                                 std::vector<bool> &is_alive)
{
    for(auto it = global_symbol_map.begin() ; it != global_symbol_map.end() ;)
    {
        auto idx = it->second.input_file - &*ctx.input_file_list().begin();
        if (is_alive[idx] == false)
            it = global_symbol_map.erase(it);
        else
            ++it;
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

Linking_context::Linking_context(Link_option_args link_option_args):m_link_option_args(std::move(link_option_args))
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
    

    nLinking_passes::Resolve_symbols(*this, m_input_file, m_is_alive);

    Remove_unused_symbol(*this, m_global_symbol_map, m_is_alive);
    Remove_unused_file(m_input_file, m_is_alive);
    Remove_unused_file(m_rel_file, m_is_alive);
    m_is_alive.clear();
    
    for(std::size_t i = 0 ; i < m_input_file.size() ; i++)
        m_input_file[i].Init_mergeable_section(*this);

    for(std::size_t i = 0 ; i < m_input_file.size() ; i++)
        m_input_file[i].Collect_mergeable_section_piece();

    for(std::size_t i = 0 ; i < m_input_file.size() ; i++)
        m_input_file[i].Resolve_sesction_pieces(*this);

    nLinking_passes::Bind_special_symbols(*this);
    
    for(auto &item : merged_section_map)
        item.second->Assign_offset();

    nLinking_passes::Create_synthetic_sections(*this);

    for(std::size_t i = 0 ; i < m_input_file.size() ; i++)
        nLinking_passes::Check_duplicate_smbols(m_input_file[i]);

    nLinking_passes::Combined_input_sections(*this);

    nLinking_passes::Assign_input_section_offset(*this);

    nLinking_passes::Sort_output_sections(*this);

}


template<>
Output_chunk::Output_chunk(Chunk *chunk, Linking_context &ctx):chunk(chunk)
{

}


template<>
Output_chunk::Output_chunk(Output_ehdr *ehdr, Linking_context &ctx):chunk(ehdr)
{
/*
    copy_buf = [ehdr, &ctx]()
    {

        Elf64_Ehdr &hdr = *(Elf64_Ehdr*)(ctx.buf.get() + ehdr->shdr.sh_offset);
        memset(&hdr, 0, sizeof(hdr));
        memcpy(&hdr.e_ident, "\177ELF", 4); // write magic
        hdr.e_ident[EI_CLASS] = ELFCLASS64 ; // only 64 bit target supported now
        hdr.e_ident[EI_DATA] = ELFDATA2LSB; // only LSB supported now
        hdr.e_ident[EI_VERSION] = EV_CURRENT;
*/

/*           ElfEhdr<E> &hdr = *(ElfEhdr<E> *)(ctx.buf + this->shdr.sh_offset);
  memset(&hdr, 0, sizeof(hdr));

  memcpy(&hdr.e_ident, "\177ELF", 4);
  hdr.e_ident[EI_CLASS] = E::is_64 ? ELFCLASS64 : ELFCLASS32;
  hdr.e_ident[EI_DATA] = E::is_le ? ELFDATA2LSB : ELFDATA2MSB;
  hdr.e_ident[EI_VERSION] = EV_CURRENT;
  hdr.e_machine = E::e_machine;
  hdr.e_version = EV_CURRENT;
  hdr.e_entry = get_entry_addr(ctx);
  hdr.e_flags = get_eflags(ctx);
  hdr.e_ehsize = sizeof(ElfEhdr<E>);

  // If e_shstrndx is too large, a dummy value is set to e_shstrndx.
  // The real value is stored to the zero'th section's sh_link field.
  if (ctx.shstrtab) {
    if (ctx.shstrtab->shndx < SHN_LORESERVE)
      hdr.e_shstrndx = ctx.shstrtab->shndx;
    else
      hdr.e_shstrndx = SHN_XINDEX;
  }

  if (ctx.arg.relocatable)
    hdr.e_type = ET_REL;
  else if (ctx.arg.pic)
    hdr.e_type = ET_DYN;
  else
    hdr.e_type = ET_EXEC;

  if (ctx.phdr) {
    hdr.e_phoff = ctx.phdr->shdr.sh_offset;
    hdr.e_phentsize = sizeof(ElfPhdr<E>);
    hdr.e_phnum = ctx.phdr->shdr.sh_size / sizeof(ElfPhdr<E>);
  }

  if (ctx.shdr) {
    hdr.e_shoff = ctx.shdr->shdr.sh_offset;
    hdr.e_shentsize = sizeof(ElfShdr<E>);

    // Since e_shnum is a 16-bit integer field, we can't store a very
    // large value there. If it is >65535, the real value is stored to
    // the zero'th section's sh_size field.
    i64 shnum = ctx.shdr->shdr.sh_size / sizeof(ElfShdr<E>);
    hdr.e_shnum = (shnum <= UINT16_MAX) ? shnum : 0;
  } */


/*
    };// copy_buf
*/

}

template<>
Output_chunk::Output_chunk(Output_phdr *phdr, Linking_context &ctx):chunk(phdr)
{

}



template<>
Output_chunk::Output_chunk(Output_shdr *shdr, Linking_context &ctx):chunk(shdr)
{
    
}


template<>
Output_chunk::Output_chunk(Output_section *osec, Linking_context &ctx):chunk(osec), m_is_osec(true)
{
    
}


template<>
Output_chunk::Output_chunk(Merged_section *msec, Linking_context &ctx):chunk(msec)
{
    
}

template<>
Output_chunk::Output_chunk(Riscv_attributes_section *riscv_sec, Linking_context &ctx):chunk(riscv_sec)
{
    
}


template<>
Output_chunk::Output_chunk(Shstrtab_section *sec, Linking_context &ctx):chunk(sec)
{
    
}

template<>
Output_chunk::Output_chunk(Strtab_section *str_sec, Linking_context &ctx):chunk(str_sec)
{
    
}

template<>
Output_chunk::Output_chunk(Symtab_section *sec, Linking_context &ctx):chunk(sec)
{
    
}

template<>
Output_chunk::Output_chunk(Symtab_shndx_section *sec, Linking_context &ctx):chunk(sec)
{
    
}