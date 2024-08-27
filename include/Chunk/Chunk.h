#pragma once
#include <string_view>
#include <assert.h>

#include "elf/ELF.h"

struct Chunk
{
    Chunk(std::string_view name, bool is_header) : name(name), shdr(), is_relro(false), is_header(is_header)
    {
        assert(shdr.sh_addr == 0);
        assert(shdr.sh_flags == 0);
        assert(shdr.sh_info == 0);
        shdr.sh_addralign = 1;
    }
    std::string_view name;

    //this section header is used for recording the info of the corresponding section
    Elf64_Shdr shdr;
    std::size_t shndx;
    bool is_relro;
    const bool is_header;
};