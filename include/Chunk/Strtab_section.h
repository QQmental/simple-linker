#pragma once
#include "Chunk/Chunk.h"

class Strtab_section : public Chunk
{
public:
    Strtab_section():Chunk(".strtab", false)
    {

        this->shdr.sh_type = SHT_STRTAB;
    }
};
