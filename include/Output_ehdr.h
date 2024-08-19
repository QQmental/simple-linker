#pragma once
#include "Chunk.h"

struct Output_ehdr : public Chunk
{
    Output_ehdr(uint32_t sh_flags):Chunk("EHDR")
    {
        this->shdr.sh_flags = sh_flags;
        this->shdr.sh_size = sizeof(Elf64_Ehdr);
        this->shdr.sh_addralign = 8;// 4 byte for 32 bit target, and 8 for 64 bit target
    }
};