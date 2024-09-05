#pragma once

#include "Linking_context.h"

namespace nLinking_context_helper
{
    uint64_t to_phdr_flags(Linking_context &ctx, const Chunk &chunk);
    uint64_t Get_eflags(const Linking_context &ctx);

    inline uint64_t to_phdr_flags(Linking_context &ctx, const Chunk &chunk)
    {
        bool write = (chunk.shdr.sh_flags & SHF_WRITE);
        bool exec = (chunk.shdr.sh_flags & SHF_EXECINSTR);

        return   (uint64_t)eSegment_flag::PF_R 
               | (write ? (uint64_t)eSegment_flag::PF_W : (uint64_t)eSegment_flag::PF_NONE) 
               | (exec ? (uint64_t)eSegment_flag::PF_X : (uint64_t)eSegment_flag::PF_NONE);
    }

    inline uint64_t Get_eflags(const Linking_context &ctx)
    {
        if (ctx.input_file_list().empty() == true)
            return 0;

        uint32_t ret =  ctx.input_file_list()[0].src().elf_hdr().e_flags;

        for (uint64_t i = 1; i < ctx.input_file_list().size(); i++)
        {
            uint32_t flags = ctx.input_file_list()[i].src().elf_hdr() .e_flags;

            if (flags & gRISC_V_RVC_MASK)
                ret |= gRISC_V_RVC_MASK;

            if ((flags & gRISC_V_FLOAT_MASK) != (ret & gRISC_V_FLOAT_MASK))
            {
                std::cout << ctx.input_file_list()[i].name() \
                        << "cannot link object files with different floating-point ABI from " \
                        << ctx.input_file_list()[0].name()
                        << "\n";
                abort();
            }


            if ((flags & gRISC_V_RVE_MASK) != (ret & gRISC_V_RVE_MASK))
            {
                std::cout << ctx.input_file_list()[i].name() \
                        << "cannot link object files with different EF_RISCV_RVE from " \
                        << ctx.input_file_list()[0].name()
                        << "\n";
                abort();
            }
        }
    return ret;
    }
}