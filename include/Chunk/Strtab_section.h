#pragma once
#include "Chunk/Chunk.h"

struct Strtab_section : public Chunk
{
    Strtab_section():Chunk(".strtab")
    {

        this->shdr.sh_type = SHT_STRTAB;
    }
};
