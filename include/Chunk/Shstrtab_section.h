#pragma once
#include "Chunk/Chunk.h"

struct Shstrtab_section : Chunk
{
    Shstrtab_section():Chunk(".shstrtab")
    {
        this->shdr.sh_type = SHT_STRTAB;
    }
};

