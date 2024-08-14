#pragma once
#include <string_view>
#include "elf/ELF.h"
#include "Relocatable_file.h"

struct Input_section
{
public:
    Input_section(Relocatable_file &rel_file, std::size_t shndx) 
                : rel_file(&rel_file), 
                  shndx(shndx), 
                  data(rel_file.section(shndx),
                  rel_file.section_hdr(shndx).sh_size),
                  m_relsec_idx(-1),
                  m_rel_count(0){}


    auto const& shdr() const {return rel_file->section_hdr(shndx);}

    void Set_relsec_idx(std::size_t idx)
    {
        m_relsec_idx = idx;

        if (m_relsec_idx == (std::size_t)-1)
            m_rel_count = 0;
        else
            m_rel_count = rel_file->section_hdr(m_relsec_idx).sh_size / rel_file->section_hdr(m_relsec_idx).sh_entsize;
    }
    

    std::size_t rel_count() const 
    {
        return m_rel_count;
    }

    nELF_util::ELF_Rel rela_at(std::size_t idx) const;
    std::string_view name() const ;

    Relocatable_file *rel_file;
    std::size_t shndx;
    std::string_view data;
    

private:
    std::size_t m_relsec_idx;
    std::size_t m_rel_count;
};


inline nELF_util::ELF_Rel Input_section::rela_at(std::size_t idx) const
{
    char *rel_section = rel_file->section(m_relsec_idx);

    if (rel_file->section_hdr(m_relsec_idx).sh_type == SHT_REL)
    {
        auto *rel = &reinterpret_cast<Elf64_Rel*>(rel_section)[idx];
        return nELF_util::ELF_Rel{*rel};
    }
    else if (rel_file->section_hdr(m_relsec_idx).sh_type == SHT_RELA)
    {
        auto *rel = &reinterpret_cast<Elf64_Rela*>(rel_section)[idx];
        return nELF_util::ELF_Rel{*rel};
    }

    FATALF("at %lu, wrong sh_type", idx);

    return nELF_util::ELF_Rel{};
}


inline std::string_view Input_section::name() const 
{
    if (rel_file->section_hdr_table().header_count()  <= shndx)
        return (shdr().sh_flags & SHF_TLS) ? ".tls_common" : ".common";
  return rel_file->section_hdr_table().string_table().data() + rel_file->section_hdr_table().headers()[shndx].sh_name;
}