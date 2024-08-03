#pragma once
#include <string_view>
#include "elf/ELF.h"
#include "Relocatable_file.h"

struct Input_section
{
public:
    Input_section() = default;

    Input_section(const Relocatable_file &rel_file, std::size_t shndx) 
                : rel_file(&rel_file), 
                  shndx(shndx), 
                  data(rel_file.section(shndx), rel_file.section_hdr(shndx).sh_size){}


    auto const& shdr() const {return rel_file->section_hdr(shndx);}

    std::size_t rel_count() const 
    {
        if (relsec_idx == (std::size_t)-1)
            return 0;
        else
            return rel_file->section_hdr(relsec_idx).sh_size / rel_file->section_hdr(relsec_idx).sh_entsize;
    }

    nELF_util::ELF_Rel rela_at(std::size_t idx) const;

    const Relocatable_file *rel_file;
    std::size_t shndx;
    std::string_view data;
    std::size_t relsec_idx = -1;
};


inline nELF_util::ELF_Rel Input_section::rela_at(std::size_t idx) const
{
    char *rel_section = rel_file->section(relsec_idx);

    if (rel_file->section_hdr(relsec_idx).sh_type == SHT_REL)
    {
        auto *rel = &reinterpret_cast<Elf64_Rel*>(rel_section)[idx];
        return nELF_util::ELF_Rel{*rel};
    }
    else if (rel_file->section_hdr(relsec_idx).sh_type == SHT_RELA)
    {
        auto *rel = &reinterpret_cast<Elf64_Rela*>(rel_section)[idx];
        return nELF_util::ELF_Rel{*rel};
    }

    FATALF("at %lu, wrong sh_type", idx);

    return nELF_util::ELF_Rel{};
}