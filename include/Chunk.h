#pragma once
#include <string_view>

#include "elf/ELF.h"

struct Chunk
{
    Chunk(std::string_view name) : name(name){}
    Elf64_Shdr shdr;
    std::string_view name;
};