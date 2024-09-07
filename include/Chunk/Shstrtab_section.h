#pragma once
#include "Chunk/Chunk.h"

class Shstrtab_section final : public Chunk
{
public:
    Shstrtab_section():Chunk(".shstrtab", false)
    {
        this->shdr.sh_type = SHT_STRTAB;
    }
};

