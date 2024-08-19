#pragma once
#include "Chunk.h"

struct Output_shdr : public Chunk
{
    Output_shdr(uint32_t sh_flags):Chunk("SHDR")
    {
        this->shdr.sh_size = 1;
        this->shdr.sh_addralign = 8;// 4 byte for 32 bit target, and 8 for 64 bit target
    }
};