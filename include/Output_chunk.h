#pragma once
#include <functional>
#include <stdlib.h>

#include "Chunk/Chunk.h"
#include "Chunk/Merged_section.h"
#include "Chunk/Output_section.h"
#include "Chunk/Output_phdr.h"
#include "Chunk/Output_ehdr.h"
#include "Chunk/Output_shdr.h"
#include "Chunk/Symtab_shndx_section.h"
#include "Chunk/Shstrtab_section.h"
#include "Chunk/Strtab_section.h"
#include "Chunk/Symtab_section.h"
#include "Chunk/Riscv_attributes_section.h"
#include "Chunk/Got_section.h"

class Linking_context;


struct Output_chunk
{

    template<typename chunk_t = Chunk>
    Output_chunk(chunk_t *chunk, Linking_context &ctx);

    ~Output_chunk() = default;

    void Update_shdr() const
    {
        if (m_update_shdr)
            m_update_shdr();
    }

    void Copy_buf() const
    {
        if (m_copy_buf)
            m_copy_buf();
    }

    Chunk& chunk() const {return *m_chunk;}
    bool is_osec() const {return m_is_osec;}
    

private:
    Chunk *m_chunk;
    std::function<void()> m_update_shdr;
    std::function<void()>  m_copy_buf;
    bool m_is_osec = false;


};

template<>
Output_chunk::Output_chunk<Chunk>(Chunk *chunk, Linking_context &ctx);

template<>
Output_chunk::Output_chunk<Output_ehdr>(Output_ehdr *ehdr, Linking_context &ctx);

template<>
Output_chunk::Output_chunk<Output_phdr>(Output_phdr *phdr, Linking_context &ctx);

template<>
Output_chunk::Output_chunk<Output_shdr>(Output_shdr *shdr, Linking_context &ctx);

template<>
Output_chunk::Output_chunk<Output_section>(Output_section *osec, Linking_context &ctx);

template<>
Output_chunk::Output_chunk<Merged_section>(Merged_section *msec, Linking_context &ctx);

template<>
Output_chunk::Output_chunk<Riscv_attributes_section>(Riscv_attributes_section *riscv_sec, Linking_context &ctx);

template<>
Output_chunk::Output_chunk<Shstrtab_section>(Shstrtab_section *sec, Linking_context &ctx);

template<>
Output_chunk::Output_chunk<Strtab_section>(Strtab_section *str_sec, Linking_context &ctx);

template<>
Output_chunk::Output_chunk<Symtab_section>(Symtab_section *sec, Linking_context &ctx);

template<>
Output_chunk::Output_chunk<Symtab_shndx_section>(Symtab_shndx_section *sec, Linking_context &ctx);