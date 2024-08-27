#pragma once
#include "Chunk/Chunk.h"

struct Riscv_attributes_section : public Chunk
{
    Riscv_attributes_section():Chunk(".riscv.attributes", false)
    {
        this->shdr.sh_type = SHT_RISC_V_ATTRIBUTES;
    }
};
