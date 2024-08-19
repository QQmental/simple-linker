#pragma once
#include "Chunk.h"

struct Output_phdr : public Chunk
{
    Output_phdr(uint32_t sh_flags):Chunk("PHDR")
    {
        this->shdr.sh_flags = sh_flags;
        this->shdr.sh_addralign = 8;// 4 byte for 32 bit target, and 8 for 64 bit target
    }
};
