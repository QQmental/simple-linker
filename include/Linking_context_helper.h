#pragma once

#include "Linking_context.h"

namespace nLinking_context_helper
{
    uint64_t to_phdr_flags(Linking_context &ctx, const Chunk &chunk);

    inline uint64_t to_phdr_flags(Linking_context &ctx, const Chunk &chunk)
    {
        bool write = (chunk.shdr.sh_flags & SHF_WRITE);
        bool exec = (chunk.shdr.sh_flags & SHF_EXECINSTR);

        return   (uint64_t)eSegment_flag::PF_R 
            | (write ? (uint64_t)eSegment_flag::PF_W : (uint64_t)eSegment_flag::PF_NONE) 
            | (exec ? (uint64_t)eSegment_flag::PF_X : (uint64_t)eSegment_flag::PF_NONE);
    }
}