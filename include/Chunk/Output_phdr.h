#pragma once
#include "Chunk/Chunk.h"

struct Output_phdr : public Chunk
{
    Output_phdr(uint32_t sh_flags):Chunk("PHDR")
    {
        this->shdr.sh_flags = sh_flags;
        this->shdr.sh_addralign = sizeof(Elf64_Addr);
    }
};
