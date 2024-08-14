#pragma once
#include <string_view>
#include <unordered_map>
#include <memory>

#include "elf/ELF.h"


struct Merged_section
{
    Merged_section(std::string_view name, int64_t flags, int64_t type, int64_t entsize) 
    {
        this->name = name;
        this->shdr.sh_flags = flags;
        this->shdr.sh_type = type;
        this->shdr.sh_entsize = entsize;
    }

    struct Section_fragment;
    
    Section_fragment* Insert(std::string_view key, uint64_t hash, uint32_t p2align);

    void Aggregate_section_fragment();

    struct Section_fragment
    {
        Section_fragment(Merged_section &output_section, bool is_alive)
                       : output_section(output_section),
                         is_alive(is_alive){}
                         

        Merged_section &output_section;
        uint32_t offset = -1;
        uint32_t p2align = 0;
        bool is_alive = false;
    };

    Elf64_Shdr shdr;
    std::string_view name;

private:
    std::unordered_map<std::string_view, std::unique_ptr<Section_fragment>> m_map;
};


inline Merged_section::Section_fragment* 
Merged_section::Insert(std::string_view key, uint64_t hash, uint32_t p2align)
{
    bool is_alive = !(this->shdr.sh_flags & SHF_ALLOC);

    auto *frag = m_map.insert(std::make_pair(key, std::make_unique<Section_fragment>(*this, is_alive))).first->second.get();

    frag->p2align = std::max(frag->p2align, p2align);

    return frag;
}