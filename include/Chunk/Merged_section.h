#pragma once
#include <string_view>
#include <unordered_map>
#include <memory>

#include "elf/ELF.h"
#include "Chunk/Chunk.h"

class Merged_section : public Chunk
{
public:
    Merged_section(std::string_view name, int64_t flags, int64_t type, int64_t entsize) : Chunk(name, false)
    {
        this->shdr.sh_flags = flags;
        this->shdr.sh_type = type;
        this->shdr.sh_entsize = entsize;
    }

    struct Piece;
    
    Piece* Insert(std::string_view key, uint64_t hash, uint32_t p2align);

    void Assign_offset();

    struct Piece
    {
        Piece(Merged_section &output_section, bool is_alive)
            : output_section(output_section),
              is_alive(is_alive){}
                         
        uint64_t Get_addr() const {return output_section.shdr.sh_addr + offset;}
        
        Merged_section &output_section;
        uint32_t offset = -1;
        uint32_t p2align = 0;
        bool is_alive = false;
    };
    
private:
    std::unordered_map<std::string_view, std::unique_ptr<Piece>> m_map;
};


inline Merged_section::Piece* 
Merged_section::Insert(std::string_view key, uint64_t hash, uint32_t p2align)
{
    bool is_alive = !(this->shdr.sh_flags & SHF_ALLOC);

    auto *frag = m_map.insert(std::make_pair(key, std::make_unique<Piece>(*this, is_alive))).first->second.get();

    frag->p2align = std::max(frag->p2align, p2align);

    return frag;
}