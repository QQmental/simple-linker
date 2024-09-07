#pragma once
#include <string_view>

#include "Chunk/Chunk.h"
#include "elf/ELF.h"
#include "Input_section.h"

struct Output_section_key
{
    struct Hash_func
    {
        uint64_t operator()(const Output_section_key &key) const
        {
            return std::hash<std::string_view>{}(key.name) ^ std::hash<decltype(key.type)>{}(key.type); 
        }
    };
    bool operator==(const Output_section_key &src) const
    {
        return name == src.name && type == src.type;
    }

    std::string_view name;
    uint64_t type;
};

// Output_section consists of some Input_section and their offset from the begining of the osec
class Output_section final : public Chunk
{
public:
    Output_section(Output_section_key key) : Chunk(key.name, false), type(key.type){}
    uint64_t type;
    std::vector<const Input_section*> member_list;
    std::vector<std::size_t> input_section_offset_list;
};