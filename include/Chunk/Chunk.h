#pragma once
#include <string_view>
#include <assert.h>

#include "elf/ELF.h"

class Chunk
{
public:
    Chunk(std::string_view name, bool is_header) : name(name), shdr(), is_relro(false), m_is_header(is_header)
    {
        assert(shdr.sh_addr == 0);
        assert(shdr.sh_flags == 0);
        assert(shdr.sh_info == 0);
        shdr.sh_addralign = 1;
    }
    virtual ~Chunk() = default;
    bool is_header() const {return m_is_header;}
    std::string_view name;

    //this section header is used for recording the info of the corresponding section
    Elf64_Shdr shdr;
    std::size_t shndx;
    bool is_relro;

private:
    bool m_is_header;
};