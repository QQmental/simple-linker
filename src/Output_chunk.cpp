#include <iostream>
#include <functional>

#include "Output_chunk.h"
#include "Linking_context.h"
#include "elf/ELF.h"
#include "Linking_context_helper.h"
#include "Linking_passes.h"

using nLinking_context_helper::to_phdr_flags;
using nLinking_context_helper::Get_eflags;

static std::vector<Elf64_phdr_t> Create_phdrs(Linking_context &ctx);


// just copied from mold
static std::vector<Elf64_phdr_t> Create_phdrs(Linking_context &ctx)
{
    std::vector<Elf64_phdr_t> vec;

    auto append_segment = [&](uint64_t type, uint64_t flags, Chunk *chunk)
    {
        Elf64_phdr_t phdr = {};
        phdr.p_type = type;
        phdr.p_flags = flags;
        phdr.p_align = chunk->shdr.sh_addralign;
        phdr.p_offset = chunk->shdr.sh_offset;

        if (chunk->shdr.sh_type != SHT_NOBITS)
            phdr.p_filesz = chunk->shdr.sh_size;

        phdr.p_vaddr = chunk->shdr.sh_addr;
        phdr.p_paddr = chunk->shdr.sh_addr;

        if (chunk->shdr.sh_flags & SHF_ALLOC)
            phdr.p_memsz = chunk->shdr.sh_size;
        vec.push_back(phdr);
    };

    // put a section into a segment
    auto put_sec_into_seg = [&](Chunk *chunk)
    {
        Elf64_phdr_t &phdr = vec.back();
        phdr.p_align = std::max<uint64_t>(phdr.p_align, chunk->shdr.sh_addralign);
        phdr.p_memsz = chunk->shdr.sh_addr + chunk->shdr.sh_size - phdr.p_vaddr;
        if (chunk->shdr.sh_type != SHT_NOBITS)
            phdr.p_filesz = phdr.p_memsz;
    };

    auto is_bss = [](const Chunk &chunk)
    {
        return chunk.shdr.sh_type == SHT_NOBITS;
    };

    auto is_tbss = [](const Chunk &chunk)
    {
        return chunk.shdr.sh_type == SHT_NOBITS && (chunk.shdr.sh_flags & SHF_TLS);
    };

    auto is_note = [](const Chunk &chunk)
    {
        return chunk.shdr.sh_type == SHT_NOTE;
    };

    
    // When we are creating PT_LOAD segments, we consider only
    // the following chunks.
    std::vector<Output_chunk*> loadable_chunks;
    for (Output_chunk &output_chunk : ctx.output_chunk_list)
    {
        if ((output_chunk.chunk().shdr.sh_flags & SHF_ALLOC) && !is_tbss(output_chunk.chunk()))
            loadable_chunks.push_back(&output_chunk);
    }

    // The ELF spec says that "loadable segment entries in the program
    // header table appear in ascending order, sorted on the p_vaddr
    // member".
    std::sort(loadable_chunks.begin(), 
              loadable_chunks.end(), 
              [](const Output_chunk *a, const Output_chunk *b) {return a->chunk().shdr.sh_addr < b->chunk().shdr.sh_addr;});

    // Create a PT_PHDR for the program header itself.
    if (ctx.phdr && (ctx.phdr->shdr.sh_flags & SHF_ALLOC))
        append_segment(PT_PHDR, (uint64_t)eSegment_flag::PF_R, ctx.phdr);


    // Create a PT_NOTE for SHF_NOTE sections.
    for (auto it = ctx.output_chunk_list.begin() ; it != ctx.output_chunk_list.end() ; )
    {
        ++it;
        Output_chunk *first = &*(std::prev(it));

        if (is_note(first->chunk()))
        {
            uint64_t flags = to_phdr_flags(ctx, first->chunk());
            append_segment(PT_NOTE, flags, &first->chunk());

            while (   it != ctx.output_chunk_list.end()
                   && is_note(it->chunk()) 
                   && to_phdr_flags(ctx, it->chunk()) == flags)
                put_sec_into_seg(&(it++)->chunk());
        }
    }

    // Create PT_LOAD segments.
    for (auto it = loadable_chunks.begin() ; it != loadable_chunks.end() ; )
    {
        ++it;
        Output_chunk *first = *(std::prev(it));
        std::size_t flags = to_phdr_flags(ctx, first->chunk());
        append_segment(PT_LOAD, flags, &first->chunk());
        vec.back().p_align = std::max<uint64_t>(ctx.page_size, vec.back().p_align);

        // Add contiguous ALLOC sections as long as they have the same
        // section flags and there's no on-disk gap in between.
        if (is_bss(first->chunk()) == false)
        {
            while (   it != loadable_chunks.end()
                   && is_bss((*it)->chunk()) == false
                   && to_phdr_flags(ctx, (*it)->chunk()) == flags
                   && (*it)->chunk().shdr.sh_offset - first->chunk().shdr.sh_offset
                      == (*it)->chunk().shdr.sh_addr - first->chunk().shdr.sh_addr)
            {
                put_sec_into_seg(&(*it)->chunk());
                ++it;
            }

        }

        while (   it != loadable_chunks.end()
               && is_bss((*it)->chunk())
               && to_phdr_flags(ctx, (*it)->chunk()) == flags)
        {
            put_sec_into_seg(&(*it)->chunk());
            ++it;
        }
    }

    // Create a PT_TLS.
    for (auto it = ctx.output_chunk_list.begin() ; it != ctx.output_chunk_list.end() ; )
    {
        ++it;
        Output_chunk *first = &*(std::prev(it));
        if (first->chunk().shdr.sh_flags & SHF_TLS)
        {
            append_segment(PT_TLS, (uint64_t)eSegment_flag::PF_R, &first->chunk());
            while (   it != ctx.output_chunk_list.end()
                   && (it->chunk().shdr.sh_flags & SHF_TLS))
                put_sec_into_seg(&(it++)->chunk());
        }
    }

    if (ctx.riscv_attributes_section && ctx.riscv_attributes_section->shdr.sh_size)
        append_segment(PT_RISC_V_ATTRIBUTES, (uint64_t)eSegment_flag::PF_R, ctx.riscv_attributes_section);

    // Set p_paddr if --physical-image-base was given. --physical-image-base
    // is typically used in embedded programming to specify the base address
    // of a memory-mapped ROM area. In that environment, paddr refers to a
    // segment's initial location in ROM and vaddr refers the its run-time
    // address.
    //
    // When a device is turned on, it start executing code at a fixed
    // location in the ROM area. At that location is a startup routine that
    // copies data or code from ROM to RAM before using them.
    //
    // .data must have different paddr and vaddr because ROM is not writable.
    // paddr of .rodata and .text may or may be equal to vaddr. They can be
    // directly read or executed from ROM, but oftentimes they are copied
    // from ROM to RAM because Flash or EEPROM are usually much slower than
    // DRAM.
    //
    // We want to keep vaddr == pvaddr for as many segments as possible so
    // that they can be directly read/executed from ROM. If a gap between
    // two segments is two page size or larger, we give up and pack segments
    // tightly so that we don't waste too much ROM area.
    if (ctx.physical_image_base != (uint64_t)-1)
    {
        for (std::size_t i = 0; i < vec.size(); i++)
        {
            if (vec[i].p_type != PT_LOAD)
                continue;

            uint64_t addr = ctx.physical_image_base;
            bool in_sync = (vec[i].p_vaddr == addr);

            vec[i].p_paddr = addr;
            addr += vec[i].p_memsz;

            for (i++; i < vec.size() && vec[i].p_type == PT_LOAD; i++)
            {
                Elf64_phdr_t &p = vec[i];
                if (in_sync && addr <= p.p_vaddr && p.p_vaddr < addr + ctx.page_size * 2)
                {
                    p.p_paddr = p.p_vaddr;
                    addr = p.p_vaddr + p.p_memsz;
                } 
                else
                {
                    in_sync = false;
                    p.p_paddr = addr;
                    addr += p.p_memsz;
                }
            }
            break;
        }
    }
    return vec;
}



template<>
Output_chunk::Output_chunk<Chunk>(Chunk *chunk, Linking_context &ctx):m_chunk(chunk), m_ctx(&ctx)
{

}


template<>
Output_chunk::Output_chunk<Output_ehdr>(Output_ehdr *ehdr, Linking_context &ctx):m_chunk(ehdr), m_ctx(&ctx)
{
    m_copy_chunk = [](Chunk *_ehdr, Linking_context &ctx)
    {
        auto *ehdr = (Output_ehdr*)_ehdr;
        Elf64_Ehdr &hdr = *(Elf64_Ehdr*)(ctx.buf + ehdr->shdr.sh_offset);
        memset(&hdr, 0, sizeof(hdr));
        memcpy(&hdr.e_ident, "\177ELF", 4);  // write magic
        hdr.e_ident[EI_CLASS] = ELFCLASS64 ; // only 64 bit target supported now
        hdr.e_ident[EI_DATA] = ELFDATA2LSB;  // only LSB supported now
        hdr.e_ident[EI_VERSION] = EV_CURRENT;
        hdr.e_machine = (Elf64_Half)Linking_context::eLink_machine_optinon::elf64lriscv;
        hdr.e_version = EV_CURRENT;
        hdr.e_entry = nLinking_passes::Get_symbol_addr(ctx, *ctx.special_symbols.entry) ;
        hdr.e_flags = Get_eflags(ctx);
        hdr.e_ehsize = sizeof(Elf64_Ehdr);


        // If e_shstrndx is too large, a dummy value is set to e_shstrndx.
        // The real value is stored to the zero'th section's sh_link field.
        if (ctx.shstrtab_section)
        {
            if (ctx.shstrtab_section->shndx < SHN_LORESERVE)
                hdr.e_shstrndx = ctx.shstrtab_section->shndx;
            else
                hdr.e_shstrndx = SHN_XINDEX;
        }

        hdr.e_type = ET_EXEC; // only executable output is supported now

        if (ctx.phdr)
        {
            hdr.e_phoff = ctx.phdr->shdr.sh_offset;
            hdr.e_phentsize = sizeof(Elf64_phdr_t);
            hdr.e_phnum = ctx.phdr->shdr.sh_size / sizeof(Elf64_phdr_t);
        }

        if (ctx.shdr)
        {
            hdr.e_shoff = ctx.shdr->shdr.sh_offset;
            hdr.e_shentsize = sizeof(Elf64_Shdr);

            // Since e_shnum is a 16-bit integer field, we can't store a very
            // large value there. If it is >65535, the real value is stored to
            // the zero'th section's sh_size field.
            int64_t shnum = ctx.shdr->shdr.sh_size / sizeof(Elf64_Shdr);
            hdr.e_shnum = (shnum <= UINT16_MAX) ? shnum : 0;
        }
    }; // m_copy_chunk
}

template<>
Output_chunk::Output_chunk<Output_phdr>(Output_phdr *phdr, Linking_context &ctx):m_chunk(phdr), m_ctx(&ctx)
{
    m_update_shdr = [](Chunk *_phdr, Linking_context &ctx)
    {
        auto *phdr = (Output_phdr*)_phdr;
        phdr->phdrs = Create_phdrs(ctx);
        phdr->shdr.sh_size = phdr->phdrs.size() * sizeof(Elf64_phdr_t);
    };


    m_copy_chunk = [](Chunk *_phdr, Linking_context &ctx)
    {
        auto *phdr = (Output_phdr*)_phdr;
        memcpy(ctx.buf + phdr->shdr.sh_offset, 
               phdr->phdrs.data(), 
               phdr->phdrs.size() * sizeof(Elf64_phdr_t));
    };


}



template<>
Output_chunk::Output_chunk<Output_shdr>(Output_shdr *shdr, Linking_context &ctx):m_chunk(shdr), m_ctx(&ctx)
{
    m_copy_chunk = [](Chunk *_shdr, Linking_context &ctx)
    {
        auto *shdr = (Output_shdr*)_shdr;
        elf64_shdr *hdr = (elf64_shdr*)(ctx.buf + shdr->shdr.sh_offset);
        memset(hdr, 0, shdr->shdr.sh_size);

        //the first entry in section header table is a special entry

        if (ctx.shstrtab_section && SHN_LORESERVE <= ctx.shstrtab_section->shndx)
            hdr[0].sh_link = ctx.shstrtab_section->shndx;

        uint64_t shnum = ctx.shdr->shdr.sh_size / sizeof(elf64_shdr);
        if (UINT16_MAX < shnum)
            hdr[0].sh_size = shnum;

        for (Output_chunk &output_chunk : ctx.output_chunk_list)
        {
            if (output_chunk.chunk().shndx)
                hdr[output_chunk.chunk().shndx] = output_chunk.chunk().shdr;
        }
    };    
}

template<>
Output_chunk::Output_chunk<Output_section>(Output_section *osec, Linking_context &ctx):m_chunk(osec), m_ctx(&ctx), m_is_osec(true)
{
    m_copy_chunk = [](Chunk *_output_section, Linking_context &ctx)
    {
        auto *output_section = (Output_section*)_output_section;

        if (output_section->shdr.sh_type == SHT_NOBITS)
            return;

        nLinking_passes::Relocate_symbols(ctx, *output_section);

        uint8_t *buf = ctx.buf + output_section->shdr.sh_offset;

        for(std::size_t i = 0 ; i < output_section->member_list.size() ; i++)
        {
            auto &isec = *output_section->member_list[i];
            auto offset = output_section->input_section_offset_list[i];

            memcpy(buf + offset, isec.data.begin(), isec.data.length());
            
            // Clear trailing padding. We write trap or nop instructions for
            // an executable segment so that a disassembler wouldn't try to
            // disassemble garbage as instructions.
            uint64_t this_end = offset + isec.shdr().sh_size;
            uint64_t next_start;
            if (i + 1 < output_section->member_list.size())
                next_start = output_section->input_section_offset_list[i + 1];
            else
                next_start = output_section->shdr.sh_size;

            uint8_t *loc = buf + this_end;
            uint64_t size = next_start - this_end;     

            // 'filler' is a machine-dependant varriable
            constexpr uint8_t filler[] = { 0x02, 0x90 }; // c.ebreak, risc-v instruction

            if (output_section->shdr.sh_flags & SHF_EXECINSTR)
            {
                for (uint64_t i = 0; i + sizeof(filler) <= size; i += sizeof(filler))
                    memcpy(loc + i, filler, sizeof(filler));
            }
            else
                memset(loc, 0, size); 
        }
    };// m_copy_chunk
}


template<>
Output_chunk::Output_chunk<Merged_section>(Merged_section *msec, Linking_context &ctx):m_chunk(msec), m_ctx(&ctx)
{
    m_copy_chunk = [](Chunk *_msec, Linking_context &ctx)
    {
        auto *msec = (Merged_section*)_msec;

        uint8_t *buf = ctx.buf + msec->shdr.sh_offset;

        auto vec = msec->Get_ordered_span();
        for(std::size_t i = 0 ; i < vec.size() ; i++)
        {
            if (vec[i].second->is_alive == false)
                continue;

            // There might be gaps between strings to satisfy alignment requirements.
            // If that's the case, we need to zero-clear them.    
            if (i + 1 != vec.size())
            {
                memset(buf + vec[i].second->offset + vec[i].first.length(), 
                       0, 
                       vec[i+1].second->offset - vec[i].first.length());
            }

            memcpy(buf + vec[i].second->offset, vec[i].first.data(), vec[i].first.length());
        }
        
    };
}

template<>
Output_chunk::Output_chunk<Riscv_attributes_section>(Riscv_attributes_section *riscv_sec, Linking_context &ctx):m_chunk(riscv_sec), m_ctx(&ctx)
{
    
}


template<>
Output_chunk::Output_chunk<Shstrtab_section>(Shstrtab_section *sec, Linking_context &ctx):m_chunk(sec), m_ctx(&ctx)
{
    
}

template<>
Output_chunk::Output_chunk<Strtab_section>(Strtab_section *str_sec, Linking_context &ctx):m_chunk(str_sec), m_ctx(&ctx)
{
    
}

template<>
Output_chunk::Output_chunk<Symtab_section>(Symtab_section *sec, Linking_context &ctx):m_chunk(sec), m_ctx(&ctx)
{
    
}

template<>
Output_chunk::Output_chunk<Symtab_shndx_section>(Symtab_shndx_section *sec, Linking_context &ctx):m_chunk(sec), m_ctx(&ctx)
{
    
}