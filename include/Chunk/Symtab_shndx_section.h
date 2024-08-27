#pragma once
#include "Chunk/Chunk.h"

class Symtab_shndx_section : public Chunk
{
public:
    Symtab_shndx_section():Chunk(".symtab_shndx", false)
    {
        this->shdr.sh_type = SHT_SYMTAB_SHNDX;
        this->shdr.sh_entsize = 4;
        this->shdr.sh_addralign = sizeof(Elf64_Addr);
    }
};