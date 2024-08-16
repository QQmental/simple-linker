#pragma once
#include <string_view>

#include "elf/ELF.h"


struct Output_section
{

    Elf64_Shdr shdr;
    std::string_view name;
};