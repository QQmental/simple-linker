#pragma once
#include "Chunk/Chunk.h"

class Output_shdr : public Chunk
{
public:
    Output_shdr():Chunk("SHDR", true)
    {
        this->shdr.sh_size = 1;
        this->shdr.sh_addralign = sizeof(Elf64_Addr);
    }
};