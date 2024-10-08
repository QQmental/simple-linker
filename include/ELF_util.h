#pragma once
#include "elf/ELF.h"
#include "Relocatable_file.h"
#include <string_view>


namespace nELF_util
{

struct ELF_Rel
{
    ELF_Rel() = default;
    ELF_Rel(Elf64_Rel &rel_src)
    {
        src = reinterpret_cast<char*>(&rel_src);

        r_addend = 0;

        r_sym = ELF64_R_SYM(info());
        r_type = ELF64_R_TYPE(info());
    }

    ELF_Rel(Elf64_Rela &rel_src)
    {
        src = reinterpret_cast<char*>(&rel_src);

        auto self = reinterpret_cast<Elf64_Rela*>(src);

        memcpy(&r_addend, &self->r_addend, sizeof(r_addend));

        r_sym = ELF64_R_SYM(info());
        r_type = ELF64_R_TYPE(info());
    }

    std::size_t type() const {return r_type;}
    std::size_t sym() const {return r_sym;}
    void Set_type(std::size_t t) 
    {
        auto self = reinterpret_cast<Elf64_Rel*>(src);
        decltype(Elf64_Rel::r_info) new_info = (info() & 0x0000'0000'FFFF'FFFF) | (t << 32);
        memcpy(&self->r_info, &new_info, sizeof(new_info));
        r_type = ELF64_R_TYPE(info());
    }
    void Set_sym(std::size_t s) 
    {
        auto self = reinterpret_cast<Elf64_Rel*>(src);
        decltype(Elf64_Rel::r_info) new_info = (info() & 0xFFFF'FFFF'0000'0000) | s ;
        memcpy(&self->r_info, &new_info, sizeof(new_info));
        r_sym = ELF64_R_SYM(info());
    }

    decltype(Elf64_Rela::r_addend) r_addend;


    decltype(Elf64_Rel::r_info) info() const 
    {
        auto self = reinterpret_cast<Elf64_Rel*>(src);
        decltype(Elf64_Rel::r_info) r_info;
        memcpy(&r_info, &self->r_info, sizeof(r_info));
        return r_info;
    }

    decltype(Elf64_Rel::r_offset) offset() const 
    {
        auto self = reinterpret_cast<Elf64_Rel*>(src);
        decltype(Elf64_Rel::r_info) r_offset;
        memcpy(&r_offset, &self->r_offset, sizeof(r_offset));
        return r_offset;
    }
    
private:
    char* src;
    std::size_t r_sym;
    std::size_t r_type;
};

inline std::size_t Get_st_type(const elf64_sym &elf_sym) {return ELF64_ST_TYPE(elf_sym.st_info);}

inline std::string_view Get_symbol_name(const Relocatable_file &rel_file, std::size_t sym_idx)
{
    auto &esym = rel_file.symbol_table()->data(sym_idx);
    if (Get_st_type(esym) == STT_SECTION)
        return rel_file.section_hdr_table().string_table().data() 
                + rel_file.section_hdr(rel_file.get_shndx(esym)).sh_name ;
    else
        return rel_file.symbol_table()->string_table().data() + esym.st_name;
}

inline bool Is_sym_abs(const elf64_sym &elf_sym)
{
    return elf_sym.st_shndx == SHN_ABS;
}

inline bool Is_sym_common(const elf64_sym &elf_sym)
{
    return elf_sym.st_shndx == SHN_COMMON;
}

inline bool Is_sym_undef(const elf64_sym &elf_sym)
{
    return elf_sym.st_shndx == SHN_UNDEF;
}

inline bool Is_sym_weak(const elf64_sym &elf_sym)
{
    return ELF64_ST_BIND(elf_sym.st_info) == STB_WEAK;
}

inline bool Is_sym_undef_weak(const elf64_sym &elf_sym)
{
    return Is_sym_weak(elf_sym) && Is_sym_undef(elf_sym);
}

inline bool Is_sym_global(const elf64_sym &elf_sym)
{
    return ELF64_ST_BIND(elf_sym.st_info) == STB_GLOBAL;
}

inline bool Is_sym_local(const elf64_sym &elf_sym)
{
    return ELF64_ST_BIND(elf_sym.st_info) == STB_LOCAL;
}

inline void Set_sym_bind(elf64_sym &elf_sym, uint8_t bind)
{
    elf_sym.st_info = ELF64_ST_INFO(bind, ELF64_ST_TYPE(elf_sym.st_info));
}

//copy from https://github.com/rui314/mold
inline std::string_view get_output_name(std::string_view name, uint64_t flags)
{
    if (flags & SHF_MERGE)
        return name;

    static const std::string_view prefixes[] = 
    {
        ".text.", ".data.rel.ro.", ".data.", ".rodata.", ".bss.rel.ro.", ".bss.",
        ".init_array.", ".fini_array.", ".tbss.", ".tdata.", ".gcc_except_table.",
        ".ctors.", ".dtors.", ".gnu.warning.", ".openbsd.randomdata.",
    };

    for (std::string_view prefix : prefixes)
    {
        std::string_view stem = prefix.substr(0, prefix.size() - 1);

        // name is equal to 'stem' or has a prefix 'stem'-1
        if (name == stem || name.rfind(prefix, 0) == 0)
            return stem;
    }

    return name;
}


// return true if merged output name is same as 'name',
// otherwise return false and the new output name is set in 'dst'
// modified from https://github.com/rui314/mold 
inline bool get_merged_output_name(std::unique_ptr<char[]> *dst, 
                                   std::string_view name, 
                                   int64_t flags,
                                   int64_t entsize, 
                                   int64_t addralign) 
{

  // GCC seems to create sections named ".rodata.strN.<mangled-symbol-name>.M".
  // We want to eliminate the symbol name part from the section name.
  if ((flags & SHF_STRINGS) && name.rfind(".rodata.", 0) == 0)
  {
    std::string name2 = std::string(".rodata.str") 
                        + std::to_string(entsize) 
                        + "." 
                        + std::to_string(addralign);
    if (name == name2)
        return false;

    *dst = std::make_unique<char[]>(name2.size() + 1);
    memcpy(dst->get(), name2.c_str(), name2.size() + 1);

    return true;
  }

  return false;
}

}



