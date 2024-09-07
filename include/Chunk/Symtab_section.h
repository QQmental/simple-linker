#pragma once
#include "Chunk/Chunk.h"

class Symtab_section final : public Chunk
{
public:
    Symtab_section():Chunk(".symtab", false)
    {
        this->shdr.sh_type = SHT_SYMTAB;
        this->shdr.sh_entsize = sizeof(elf64_sym);
        this->shdr.sh_addralign = sizeof(Elf64_Addr);
    }
};