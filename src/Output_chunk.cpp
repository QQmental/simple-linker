#include "Linking_context.h"



template<>
Output_chunk::Output_chunk(Chunk *chunk, Linking_context &ctx):chunk(chunk)
{

}


template<>
Output_chunk::Output_chunk(Output_ehdr *ehdr, Linking_context &ctx):chunk(ehdr)
{
/*
    copy_buf = [ehdr, &ctx]()
    {

        Elf64_Ehdr &hdr = *(Elf64_Ehdr*)(ctx.buf.get() + ehdr->shdr.sh_offset);
        memset(&hdr, 0, sizeof(hdr));
        memcpy(&hdr.e_ident, "\177ELF", 4); // write magic
        hdr.e_ident[EI_CLASS] = ELFCLASS64 ; // only 64 bit target supported now
        hdr.e_ident[EI_DATA] = ELFDATA2LSB; // only LSB supported now
        hdr.e_ident[EI_VERSION] = EV_CURRENT;
*/

/*           ElfEhdr<E> &hdr = *(ElfEhdr<E> *)(ctx.buf + this->shdr.sh_offset);
  memset(&hdr, 0, sizeof(hdr));

  memcpy(&hdr.e_ident, "\177ELF", 4);
  hdr.e_ident[EI_CLASS] = E::is_64 ? ELFCLASS64 : ELFCLASS32;
  hdr.e_ident[EI_DATA] = E::is_le ? ELFDATA2LSB : ELFDATA2MSB;
  hdr.e_ident[EI_VERSION] = EV_CURRENT;
  hdr.e_machine = E::e_machine;
  hdr.e_version = EV_CURRENT;
  hdr.e_entry = get_entry_addr(ctx);
  hdr.e_flags = get_eflags(ctx);
  hdr.e_ehsize = sizeof(ElfEhdr<E>);

  // If e_shstrndx is too large, a dummy value is set to e_shstrndx.
  // The real value is stored to the zero'th section's sh_link field.
  if (ctx.shstrtab) {
    if (ctx.shstrtab->shndx < SHN_LORESERVE)
      hdr.e_shstrndx = ctx.shstrtab->shndx;
    else
      hdr.e_shstrndx = SHN_XINDEX;
  }

  if (ctx.arg.relocatable)
    hdr.e_type = ET_REL;
  else if (ctx.arg.pic)
    hdr.e_type = ET_DYN;
  else
    hdr.e_type = ET_EXEC;

  if (ctx.phdr) {
    hdr.e_phoff = ctx.phdr->shdr.sh_offset;
    hdr.e_phentsize = sizeof(ElfPhdr<E>);
    hdr.e_phnum = ctx.phdr->shdr.sh_size / sizeof(ElfPhdr<E>);
  }

  if (ctx.shdr) {
    hdr.e_shoff = ctx.shdr->shdr.sh_offset;
    hdr.e_shentsize = sizeof(ElfShdr<E>);

    // Since e_shnum is a 16-bit integer field, we can't store a very
    // large value there. If it is >65535, the real value is stored to
    // the zero'th section's sh_size field.
    i64 shnum = ctx.shdr->shdr.sh_size / sizeof(ElfShdr<E>);
    hdr.e_shnum = (shnum <= UINT16_MAX) ? shnum : 0;
  } */


/*
    };// copy_buf
*/

}

template<>
Output_chunk::Output_chunk(Output_phdr *phdr, Linking_context &ctx):chunk(phdr)
{

}



template<>
Output_chunk::Output_chunk(Output_shdr *shdr, Linking_context &ctx):chunk(shdr)
{
    
}


template<>
Output_chunk::Output_chunk(Output_section *osec, Linking_context &ctx):chunk(osec), m_is_osec(true)
{
    
}


template<>
Output_chunk::Output_chunk(Merged_section *msec, Linking_context &ctx):chunk(msec)
{
    
}

template<>
Output_chunk::Output_chunk(Riscv_attributes_section *riscv_sec, Linking_context &ctx):chunk(riscv_sec)
{
    
}


template<>
Output_chunk::Output_chunk(Shstrtab_section *sec, Linking_context &ctx):chunk(sec)
{
    
}

template<>
Output_chunk::Output_chunk(Strtab_section *str_sec, Linking_context &ctx):chunk(str_sec)
{
    
}

template<>
Output_chunk::Output_chunk(Symtab_section *sec, Linking_context &ctx):chunk(sec)
{
    
}

template<>
Output_chunk::Output_chunk(Symtab_shndx_section *sec, Linking_context &ctx):chunk(sec)
{
    
}