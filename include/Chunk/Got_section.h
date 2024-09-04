#pragma once
#include "Chunk/Chunk.h"

class Got_section : public Chunk
{
public:
    Got_section():Chunk(".got", false)
    {
        this->is_relro = true;
        this->shdr.sh_type = SHT_PROGBITS;
        this->shdr.sh_flags = SHF_ALLOC | SHF_WRITE;
        this->shdr.sh_addralign = sizeof(Elf64_Addr); // 8 for 64 bit target, 4 for 32 bit

        // We always create a .got so that _GLOBAL_OFFSET_TABLE_ has
        // something to point to. s390x psABI define GOT[1] as a
        // reserved slot, so we allocate one more for them.
        this->shdr.sh_size = 8;
    }
};