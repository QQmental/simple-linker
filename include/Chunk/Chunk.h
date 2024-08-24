#pragma once
#include <string_view>
#include <assert.h>

#include "elf/ELF.h"

struct Chunk
{
    Chunk(std::string_view name) : name(name), shdr(), is_relro(false)
    {
        assert(shdr.sh_addr == 0);
        assert(shdr.sh_flags == 0);
        assert(shdr.sh_info == 0);
        shdr.sh_addralign = 1;
    }
    std::string_view name;
    Elf64_Shdr shdr;
    std::size_t shndx;
    bool is_relro;
};