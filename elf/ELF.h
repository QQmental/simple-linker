#pragma once

// copied from here with some modification
/*
https://github.com/torvalds/linux/blob/16f73eb02d7e1765ccab3d2018e0bd98eb93d973/include/uapi/linux/elf.h#L220
https://github.com/lattera/glibc/blob/master/elf/elf.h
*/

#include <stdint.h>

typedef uint32_t	Elf32_Addr;
typedef uint16_t	Elf32_Half;
typedef uint32_t	Elf32_Off;
typedef int32_t	Elf32_Sword;
typedef uint32_t	Elf32_Word;


using Elf64_Addr = uint64_t;
using Elf64_Half = uint16_t;
using Elf64_SHalf = int16_t;
using Elf64_Off = uint64_t;
using Elf64_Sword = int32_t;
using Elf64_Word = uint32_t;
using Elf64_Xword = uint64_t;
using Elf64_Sxword = int64_t;



/* These constants are for the segment types stored in the image headers */
constexpr uint64_t PT_NULL    = 0;
constexpr uint64_t PT_LOAD    = 1;
constexpr uint64_t PT_DYNAMIC = 2;
constexpr uint64_t PT_INTERP  = 3;
constexpr uint64_t PT_NOTE    = 4;
constexpr uint64_t PT_SHLIB   = 5;
constexpr uint64_t PT_PHDR    = 6;
constexpr uint64_t PT_TLS     = 7;               /* Thread local storage segment */
constexpr uint64_t PT_LOOS    = 0x60000000;      /* OS-specific */
constexpr uint64_t PT_HIOS    = 0x6fffffff;      /* OS-specific */
constexpr uint64_t PT_LOPROC  = 0x70000000;
constexpr uint64_t PT_HIPROC  = 0x7fffffff;


/* These constants define the different elf file types */
constexpr uint64_t ET_NONE   = 0;
constexpr uint64_t ET_REL    = 1;
constexpr uint64_t ET_EXEC   = 2;
constexpr uint64_t ET_DYN    = 3;
constexpr uint64_t ET_CORE   = 4;
constexpr uint64_t ET_LOPROC = 0xff00;
constexpr uint64_t ET_HIPROC = 0xffff;

enum class eSegment_flag: uint32_t
{
  PF_NONE = 0,
  PF_X = 1,  // executable
  PF_W = 2,  // writable
  PF_R = 4,  // readable
};


constexpr uint64_t EI_NIDENT = 16;

typedef struct elf32_sym{
  Elf32_Word	st_name;
  Elf32_Addr	st_value;
  Elf32_Word	st_size;
  unsigned char	st_info;
  unsigned char	st_other;
  Elf32_Half	st_shndx;
} Elf32_Sym;

typedef struct elf64_sym {
  Elf64_Word st_name;		/* Symbol name, index in string tbl */
  unsigned char	st_info;	/* Type and binding attributes */
  unsigned char	st_other;	/* No defined meaning, 0 */
  Elf64_Half st_shndx;		/* Associated section index */
  Elf64_Addr st_value;		/* Value of the symbol */
  Elf64_Xword st_size;		/* Associated symbol size */
} Elf64_Sym;

typedef struct elf32_hdr{
  unsigned char	e_ident[EI_NIDENT];
  Elf32_Half	e_type;
  Elf32_Half	e_machine;
  Elf32_Word	e_version;
  Elf32_Addr	e_entry;  /* Entry point */
  Elf32_Off	e_phoff;
  Elf32_Off	e_shoff;
  Elf32_Word	e_flags;
  Elf32_Half	e_ehsize;
  Elf32_Half	e_phentsize;
  Elf32_Half	e_phnum;
  Elf32_Half	e_shentsize;
  Elf32_Half	e_shnum;
  Elf32_Half	e_shstrndx;
} Elf32_Ehdr;

typedef struct elf64_hdr {
  unsigned char	e_ident[EI_NIDENT];	/* ELF "magic number" */
  Elf64_Half e_type;
  Elf64_Half e_machine;
  Elf64_Word e_version;
  Elf64_Addr e_entry;		/* Entry point virtual address */
  Elf64_Off e_phoff;		/* Program header table file offset */
  Elf64_Off e_shoff;		/* Section header table file offset */
  Elf64_Word e_flags;
  Elf64_Half e_ehsize;
  Elf64_Half e_phentsize;
  Elf64_Half e_phnum;
  Elf64_Half e_shentsize;
  Elf64_Half e_shnum;
  Elf64_Half e_shstrndx;
} Elf64_Ehdr;

#define	EI_MAG0		0		/* e_ident[] indexes */
#define	EI_MAG1		1
#define	EI_MAG2		2
#define	EI_MAG3		3
#define	EI_CLASS	4
#define	EI_DATA		5
#define	EI_VERSION	6
#define	EI_OSABI	7
#define	EI_PAD		8

#define	ELFMAG0		0x7f		/* EI_MAG */
#define	ELFMAG1		'E'
#define	ELFMAG2		'L'
#define	ELFMAG3		'F'
#define	ELFMAG		"\177ELF"
#define	SELFMAG		4

#define	ELFCLASSNONE	0		/* EI_CLASS */
#define	ELFCLASS32	1
#define	ELFCLASS64	2
#define	ELFCLASSNUM	3

#define ELFDATANONE	0		/* e_ident[EI_DATA] */
#define ELFDATA2LSB	1
#define ELFDATA2MSB	2

#define EV_NONE		0		/* e_version, EI_VERSION */
#define EV_CURRENT	1
#define EV_NUM		2

#define ELFOSABI_NONE	0
#define ELFOSABI_LINUX	3

#define EM_X86_64	62	/* AMD x86-64 architecture */
#define EM_RISC_V 243

typedef struct elf32_phdr{
  Elf32_Word	p_type;
  Elf32_Off	p_offset;
  Elf32_Addr	p_vaddr;
  Elf32_Addr	p_paddr;
  Elf32_Word	p_filesz;
  Elf32_Word	p_memsz;
  Elf32_Word	p_flags;
  Elf32_Word	p_align;
} Elf32_Phdr;

typedef struct {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} Elf64_phdr_t;

/* sh_type */
#define SHT_NULL	  0		/* Section header table entry unused */
#define SHT_PROGBITS	  1		/* Program data */
#define SHT_SYMTAB	  2		/* Symbol table */
#define SHT_STRTAB	  3		/* String table */
#define SHT_RELA	  4		/* Relocation entries with addends */
#define SHT_HASH	  5		/* Symbol hash table */
#define SHT_DYNAMIC	  6		/* Dynamic linking information */
#define SHT_NOTE	  7		/* Notes */
#define SHT_NOBITS	  8		/* Program space with no data (bss) */
#define SHT_REL		  9		/* Relocation entries, no addends */
#define SHT_SHLIB	  10		/* Reserved */
#define SHT_DYNSYM	  11		/* Dynamic linker symbol table */
#define SHT_INIT_ARRAY	  14		/* Array of constructors */
#define SHT_FINI_ARRAY	  15		/* Array of destructors */
#define SHT_PREINIT_ARRAY 16		/* Array of pre-constructors */
#define SHT_GROUP	  17		/* Section group */
#define SHT_SYMTAB_SHNDX  18		/* Extended section indeces */
#define	SHT_NUM		  19		/* Number of defined types.  */
#define SHT_LOOS	  0x60000000	/* Start OS-specific.  */
#define SHT_GNU_ATTRIBUTES 0x6ffffff5	/* Object attributes.  */
#define SHT_GNU_HASH	  0x6ffffff6	/* GNU-style hash table.  */
#define SHT_GNU_LIBLIST	  0x6ffffff7	/* Prelink library list */
#define SHT_CHECKSUM	  0x6ffffff8	/* Checksum for DSO content.  */
#define SHT_LOSUNW	  0x6ffffffa	/* Sun-specific low bound.  */
#define SHT_SUNW_move	  0x6ffffffa
#define SHT_SUNW_COMDAT   0x6ffffffb
#define SHT_SUNW_syminfo  0x6ffffffc
#define SHT_GNU_verdef	  0x6ffffffd	/* Version definition section.  */
#define SHT_GNU_verneed	  0x6ffffffe	/* Version needs section.  */
#define SHT_GNU_versym	  0x6fffffff	/* Version symbol table.  */
#define SHT_HISUNW	  0x6fffffff	/* Sun-specific high bound.  */
#define SHT_HIOS	  0x6fffffff	/* End OS-specific type */
#define SHT_LOPROC	  0x70000000	/* Start of processor-specific */
#define SHT_HIPROC	  0x7fffffff	/* End of processor-specific */
#define SHT_LOUSER	  0x80000000	/* Start of application-specific */
#define SHT_HIUSER	  0x8fffffff	/* End of application-specific */


// section flags
#define SHF_WRITE	           (1 << 0)	/* Writable */
#define SHF_ALLOC	           (1 << 1)	/* Occupies memory during execution */
#define SHF_EXECINSTR	       (1 << 2)	/* Executable */
#define SHF_MERGE	           (1 << 4)	/* Might be merged */
#define SHF_STRINGS	         (1 << 5)	/* Contains nul-terminated strings */
#define SHF_INFO_LINK	       (1 << 6)	/* `sh_info' contains SHT index */
#define SHF_LINK_ORDER	     (1 << 7)	/* Preserve order after combining */
#define SHF_OS_NONCONFORMING (1 << 8)	/* Non-standard OS specific handling required */
#define SHF_GROUP	           (1 << 9)	/* Section is member of a group.  */
#define SHF_TLS		           (1 << 10)	/* Section hold thread-local data.  */
#define SHF_COMPRESSED	     (1 << 11)	/* Section with compressed data. */
#define SHF_MASKOS	         0x0ff00000	/* OS-specific.  */
#define SHF_MASKPROC	       0xf0000000	/* Processor-specific */
#define SHF_ORDERED	         (1 << 30)	/* Special ordering requirement (Solaris).  */
#define SHF_EXCLUDE	         (1U << 31)	/* Section is excluded unless referenced or allocated (Solaris).*/

/* special section indexes */
#define SHN_UNDEF	0		/* Undefined section */
#define SHN_LORESERVE	0xff00		/* Start of reserved indices */
#define SHN_LOPROC	0xff00		/* Start of processor-specific */
#define SHN_BEFORE	0xff00		/* Order section before all others (Solaris).  */
#define SHN_AFTER	0xff01		/* Order section after all others (Solaris).  */
#define SHN_HIPROC	0xff1f		/* End of processor-specific */
#define SHN_LOOS	0xff20		/* Start of OS-specific */
#define SHN_HIOS	0xff3f		/* End of OS-specific */
#define SHN_ABS		0xfff1		/* Associated symbol is absolute */
#define SHN_COMMON	0xfff2		/* Associated symbol is common */
#define SHN_XINDEX	0xffff		/* Index is in extra table.  */
#define SHN_HIRESERVE	0xffff		/* End of reserved indices */

/* Possible values for si_boundto.  */
#define SYMINFO_BT_SELF		0xffff	/* Symbol bound to self */
#define SYMINFO_BT_PARENT	0xfffe	/* Symbol bound to parent */
#define SYMINFO_BT_LOWRESERVE	0xff00	/* Beginning of reserved entries */

/* Possible bitmasks for si_flags.  */
#define SYMINFO_FLG_DIRECT	0x0001	/* Direct bound symbol */
#define SYMINFO_FLG_PASSTHRU	0x0002	/* Pass-thru symbol for translator */
#define SYMINFO_FLG_COPY	0x0004	/* Symbol is a copy-reloc */
#define SYMINFO_FLG_LAZYLOAD	0x0008	/* Symbol bound to object to be lazy loaded */


/* Syminfo version values.  */
#define SYMINFO_NONE		0
#define SYMINFO_CURRENT		1
#define SYMINFO_NUM		2


/* How to extract and insert information held in the st_info field.  */
#define ELF32_ST_BIND(val)		(((unsigned char) (val)) >> 4)
#define ELF32_ST_TYPE(val)		((val) & 0xf)
#define ELF32_ST_INFO(bind, type)	(((bind) << 4) + ((type) & 0xf))

/* Both Elf32_Sym and Elf64_Sym use the same one-byte st_info field.  */
#define ELF64_ST_BIND(val)		ELF32_ST_BIND (val)
#define ELF64_ST_TYPE(val)		ELF32_ST_TYPE (val)
#define ELF64_ST_INFO(bind, type)	ELF32_ST_INFO ((bind), (type))

/* Legal values for ST_BIND subfield of st_info (symbol binding).  */

#define STB_LOCAL	0		/* Local symbol */
#define STB_GLOBAL	1		/* Global symbol */
#define STB_WEAK	2		/* Weak symbol */
#define	STB_NUM		3		/* Number of defined types.  */
#define STB_LOOS	10		/* Start of OS-specific */
#define STB_GNU_UNIQUE	10		/* Unique symbol.  */
#define STB_HIOS	12		/* End of OS-specific */
#define STB_LOPROC	13		/* Start of processor-specific */
#define STB_HIPROC	15		/* End of processor-specific */

/* Legal values for ST_TYPE subfield of st_info (symbol type).  */

#define STT_NOTYPE	0		/* Symbol type is unspecified */
#define STT_OBJECT	1		/* Symbol is a data object */
#define STT_FUNC	2		/* Symbol is a code object */
#define STT_SECTION	3		/* Symbol associated with a section */
#define STT_FILE	4		/* Symbol's name is file name */
#define STT_COMMON	5		/* Symbol is a common data object */
#define STT_TLS		6		/* Symbol is thread-local data object*/
#define	STT_NUM		7		/* Number of defined types.  */
#define STT_LOOS	10		/* Start of OS-specific */
#define STT_GNU_IFUNC	10		/* Symbol is indirect code object */
#define STT_HIOS	12		/* End of OS-specific */
#define STT_LOPROC	13		/* Start of processor-specific */
#define STT_HIPROC	15		/* End of processor-specific */

enum class eLinkg_type : uint32_t
{
  R_RISCV_NONE = 0,
  R_RISCV_32 = 1,
  R_RISCV_64 = 2,
  R_RISCV_RELATIVE = 3,
  R_RISCV_COPY = 4,
  R_RISCV_JUMP_SLOT = 5,
  R_RISCV_TLS_DTPMOD32 = 6,
  R_RISCV_TLS_DTPMOD64 = 7,
  R_RISCV_TLS_DTPREL32 = 8,
  R_RISCV_TLS_DTPREL64 = 9,
  R_RISCV_TLS_TPREL32 = 10,
  R_RISCV_TLS_TPREL64 = 11,
  R_RISCV_TLSDESC = 12,
  R_RISCV_BRANCH = 16,
  R_RISCV_JAL = 17,
  R_RISCV_CALL = 18,
  R_RISCV_CALL_PLT = 19,
  R_RISCV_GOT_HI20 = 20,
  R_RISCV_TLS_GOT_HI20 = 21,
  R_RISCV_TLS_GD_HI20 = 22,
  R_RISCV_PCREL_HI20 = 23,
  R_RISCV_PCREL_LO12_I = 24,
  R_RISCV_PCREL_LO12_S = 25,
  R_RISCV_HI20 = 26,
  R_RISCV_LO12_I = 27,
  R_RISCV_LO12_S = 28,
  R_RISCV_TPREL_HI20 = 29,
  R_RISCV_TPREL_LO12_I = 30,
  R_RISCV_TPREL_LO12_S = 31,
  R_RISCV_TPREL_ADD = 32,
  R_RISCV_ADD8 = 33,
  R_RISCV_ADD16 = 34,
  R_RISCV_ADD32 = 35,
  R_RISCV_ADD64 = 36,
  R_RISCV_SUB8 = 37,
  R_RISCV_SUB16 = 38,
  R_RISCV_SUB32 = 39,
  R_RISCV_SUB64 = 40,
  R_RISCV_ALIGN = 43,
  R_RISCV_RVC_BRANCH = 44,
  R_RISCV_RVC_JUMP = 45,
  R_RISCV_RVC_LUI = 46,
  R_RISCV_GPREL_LO12_I = 47,
  R_RISCV_GPREL_LO12_S = 48,
  R_RISCV_GPREL_HI20 = 49,
  R_RISCV_RELAX = 51,
  R_RISCV_SUB6 = 52,
  R_RISCV_SET6 = 53,
  R_RISCV_SET8 = 54,
  R_RISCV_SET16 = 55,
  R_RISCV_SET32 = 56,
  R_RISCV_32_PCREL = 57,
  R_RISCV_IRELATIVE = 58,
  R_RISCV_PLT32 = 59,
  R_RISCV_SET_ULEB128 = 60,
  R_RISCV_SUB_ULEB128 = 61,
  R_RISCV_TLSDESC_HI20 = 62,
  R_RISCV_TLSDESC_LOAD_LO12 = 63,
  R_RISCV_TLSDESC_ADD_LO12 = 64,
  R_RISCV_TLSDESC_CALL = 65,
};

typedef struct
{
  Elf64_Addr	r_offset;		/* Address */
  Elf64_Xword	r_info;			/* Relocation type and symbol index */
} Elf64_Rel;

/* Relocation table entry with addend (in section of type SHT_RELA).  */

typedef struct
{
  Elf32_Addr	r_offset;		/* Address */
  Elf32_Word	r_info;			/* Relocation type and symbol index */
  Elf32_Sword	r_addend;		/* Addend */
} Elf32_Rela;

typedef struct
{
  Elf64_Addr	r_offset;		/* Address */
  Elf64_Xword	r_info;			/* Relocation type and symbol index */
  Elf64_Sxword	r_addend;		/* Addend */
} Elf64_Rela;

/* How to extract and insert information held in the r_info field.  */

#define ELF32_R_SYM(val)		((val) >> 8)
#define ELF32_R_TYPE(val)		((val) & 0xff)
#define ELF32_R_INFO(sym, type)		(((sym) << 8) + ((type) & 0xff))

#define ELF64_R_SYM(i)			((i) >> 32)
#define ELF64_R_TYPE(i)			((i) & 0xffffffff)
#define ELF64_R_INFO(sym,type)		((((Elf64_Xword) (sym)) << 32) + (type))

typedef struct elf32_shdr {
  Elf32_Word	sh_name;
  Elf32_Word	sh_type;
  Elf32_Word	sh_flags;
  Elf32_Addr	sh_addr;
  Elf32_Off	sh_offset;
  Elf32_Word	sh_size;
  Elf32_Word	sh_link;
  Elf32_Word	sh_info;
  Elf32_Word	sh_addralign;
  Elf32_Word	sh_entsize;
} Elf32_Shdr;

typedef struct elf64_shdr {
  Elf64_Word sh_name;		/* Section name, index in string tbl */
  Elf64_Word sh_type;		/* Type of section */
  Elf64_Xword sh_flags;		/* Miscellaneous section attributes */
  Elf64_Addr sh_addr;		/* Section virtual addr at execution */
  Elf64_Off sh_offset;		/* Section file offset */
  Elf64_Xword sh_size;		/* Size of section in bytes */
  Elf64_Word sh_link;		/* Index of another section */
  Elf64_Word sh_info;		/* Additional section information */
  Elf64_Xword sh_addralign;	/* Section alignment */
  Elf64_Xword sh_entsize;	/* Entry size if section holds table */
} Elf64_Shdr;


// RISC-V-specific section types.
constexpr uint32_t SHT_RISC_V_ATTRIBUTES = 0x70000003;

// RISC-V-specific segment types.
constexpr uint32_t PT_RISC_V_ATTRIBUTES = 0x70000003; // meaning: RISC-V ELF attribute section.

constexpr Elf64_Word gRISC_V_RVC_MASK = (Elf64_Word)1<<0;

// 0b00 soft, 0b01 single, 0b10 double, 0b11 quad, the maximum is used 
constexpr Elf64_Word gRISC_V_FLOAT_MASK = (Elf64_Word)0b11<<1;

constexpr Elf64_Word gRISC_V_RVE_MASK = (Elf64_Word)1<<3;

constexpr Elf64_Word gRISC_V_TSO_MASK = (Elf64_Word)1<<4;

constexpr Elf64_Word gRISC_V_RESERVED_MASK = (Elf64_Word)0x3ffff<<5;

