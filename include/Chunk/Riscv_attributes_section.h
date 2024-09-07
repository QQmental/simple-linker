#pragma once
#include "Chunk/Chunk.h"

class Riscv_attributes_section final : public Chunk
{
public:
    Riscv_attributes_section():Chunk(".riscv.attributes", false)
    {
        this->shdr.sh_type = SHT_RISC_V_ATTRIBUTES;
    }
};
