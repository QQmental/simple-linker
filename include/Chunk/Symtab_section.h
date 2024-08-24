#pragma once
#include "Chunk/Chunk.h"

struct Symtab_section : public Chunk
{
    Symtab_section():Chunk(".symtab")
    {
        this->shdr.sh_type = SHT_SYMTAB;
        this->shdr.sh_entsize = sizeof(elf64_sym);
        this->shdr.sh_addralign = sizeof(Elf64_Addr);
    }
};