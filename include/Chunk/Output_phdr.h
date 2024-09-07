#pragma once
#include "Chunk/Chunk.h"

class Output_phdr final : public Chunk 
{
public:
    Output_phdr(uint32_t sh_flags):Chunk("PHDR", true)
    {
        this->shdr.sh_flags = sh_flags;
        this->shdr.sh_addralign = sizeof(Elf64_Addr);
    }
    std::vector<Elf64_phdr_t> phdrs;
};
