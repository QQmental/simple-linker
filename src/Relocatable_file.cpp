#include <stdint.h>
#include <stddef.h>
#include <string>
#include <assert.h>
#include <string.h>
#include "elf/ELF.h"
#include "elf/RISC_V_Section_attribute.h"
#include "third_party/uleb128.h"
#include "Relocatable_file.h"

using namespace nRISC_V_Section;

static std::size_t Parse_uleb128(const uint8_t* src, std::size_t max_len, uint32_t &val);
static void Init_guest_RISC_V_attributes(Attributes &attr, const uint8_t *sh_RISC_V_attr);



Relocatable_file::Relocatable_file(std::unique_ptr<char[]> data, std::string name) 
                                   : m_section_hdr_table(data.get()), m_data(std::move(data)), m_name(std::move(name))
{
    m_linking_mdata = Linking_mdata{(std::size_t)-1, (std::size_t)-1, (std::size_t)-1, {}};

    for(std::size_t i = 0 ; i < m_section_hdr_table.header_count() ; i++)
    {
        auto &shdr = section_hdr_table().headers()[i];

        switch (shdr.sh_type)
        {
            case SHT_SYMTAB:
                m_linking_mdata.symtab_idx = i;
            break;

            case SHT_SYMTAB_SHNDX:
                m_linking_mdata.symtab_shndx = i;
            break;

            case SHT_RISC_V_ATTRIBUTES:
                Init_guest_RISC_V_attributes(m_linking_mdata.attr, reinterpret_cast<const uint8_t *>(section(i)));
            break;

            default:
            break;
        }
    }

    if (m_linking_mdata.symtab_idx != (std::size_t)-1)
    {
        m_symbol_table = std::unique_ptr<nLinking_data::Symbol_table>(new nLinking_data::Symbol_table(
                                                                             m_data.get(), 
                                                                             m_section_hdr_table.headers()[m_linking_mdata.symtab_idx], 
                                                                             m_section_hdr_table));
        m_linking_mdata.first_global =  m_section_hdr_table.headers()[m_linking_mdata.symtab_idx].sh_info;                                                                             
    }
}







static std::size_t Parse_uleb128(const uint8_t* src, std::size_t max_len, uint32_t &val)
{
    val = 0;
    return bfs::DecodeUleb128(src, max_len, &val);
}

// ref: https://github.com/RISC_V-non-isa/RISC_V-elf-psabi-doc/blob/master/RISC_V-elf.adoc#rv-section-type

// null-terminated byte string : (NTBS)

// The attributes section start with a format-version (uint8 = 'A')
// followed by vendor specific sub-section(s). 
// A sub-section starts with sub-section length (uint32), vendor name (NTBS)
// and one or more sub-sub-section(s).

// A sub-sub-section consists of a tag (uleb128), sub-sub-section 
// length (uint32) followed by actual attribute tag,value pair(s).

// If tag of an attribute is an odd number, then the value is 
// a null-terminated byte string (NTBS), 
// otherwise, it's a uleb128 encoded integer.

static void Init_guest_RISC_V_attributes(Attributes &attr, const uint8_t *sh_RISC_V_attr)
{
    assert(sh_RISC_V_attr[0] == static_cast<uint8_t>('A'));

    uint32_t sub_section_length = 0;

    memcpy(&sub_section_length, &sh_RISC_V_attr[1], sizeof(uint32_t));
  
    std::string vendor_name = reinterpret_cast<const char*>(  sh_RISC_V_attr 
                                                            + sizeof(sh_RISC_V_attr[0]) 
                                                            + sizeof(sub_section_length));
    
    assert(vendor_name == "riscv");
  
    auto sub_sub_section_offset = sizeof(sh_RISC_V_attr[0]) 
                                + sizeof(sub_section_length) 
                                + vendor_name.length()      /*vendor_name name*/
                                + 1;                        /*NULL character */
    
    uint32_t fst_sub_sub_section_tag = 0, fst_sub_sub_section_tag_len = 0;

    fst_sub_sub_section_tag_len = Parse_uleb128(sh_RISC_V_attr+sub_sub_section_offset , 5, fst_sub_sub_section_tag);

    uint32_t sub_sub_section_len = 0;
    assert(fst_sub_sub_section_tag == Tag_file);

    memcpy(&sub_sub_section_len, sh_RISC_V_attr + sub_sub_section_offset + fst_sub_sub_section_tag_len, sizeof(uint32_t));

    auto cur_actual_attr_offset = sub_sub_section_offset + fst_sub_sub_section_tag_len + sizeof(sub_sub_section_len);

    // smaller than file size of RISC_V_attributes segment
    while(cur_actual_attr_offset < sizeof(sh_RISC_V_attr[0]) + sub_section_length)
    {
        uint32_t sub_sub_section_tag = 0;

        auto sub_sub_section_tag_len 
        = Parse_uleb128(sh_RISC_V_attr + cur_actual_attr_offset , 5, sub_sub_section_tag);

        cur_actual_attr_offset += sub_sub_section_tag_len;
        
        std::size_t tag_val_len = 0;
        
        switch(sub_sub_section_tag)
        {
            // used for read Deprecated tag
            uint32_t garbage;

            case Tag_RISCV_stack_align:
                 attr.Tag_RISCV_stack_align.first = true;
                 tag_val_len = Parse_uleb128(sh_RISC_V_attr + cur_actual_attr_offset, 5, attr.Tag_RISCV_stack_align.second);
            break;

            case Tag_RISCV_arch:
                 attr.Tag_RISCV_arch.first = true;            
                 attr.Tag_RISCV_arch.second = reinterpret_cast<const char*>(sh_RISC_V_attr + cur_actual_attr_offset);
                 tag_val_len = attr.Tag_RISCV_arch.second.length() + 1;
            break;

            case Tag_RISCV_unaligned_access:
                 attr.Tag_RISCV_unaligned_access.first = true;                  
                 tag_val_len = Parse_uleb128(sh_RISC_V_attr + cur_actual_attr_offset, 5, attr.Tag_RISCV_unaligned_access.second);           
            break;

            case Tag_RISCV_priv_spec:
            case Tag_RISCV_priv_spec_minor:
            case Tag_RISCV_priv_spec_revision:
                 tag_val_len = Parse_uleb128(sh_RISC_V_attr + cur_actual_attr_offset, 5, garbage);
            break;
            
            case Tag_RISCV_atomic_abi:
                 attr.Tag_RISCV_atomic_abi.first = true;                        
                 tag_val_len = Parse_uleb128(sh_RISC_V_attr + cur_actual_attr_offset, 5, attr.Tag_RISCV_atomic_abi.second);
            break;                

            case Tag_RISCV_x3_reg_usage:  
                 attr.Tag_RISCV_x3_reg_usage.first = true;                        
                 tag_val_len = Parse_uleb128(sh_RISC_V_attr + cur_actual_attr_offset, 5, attr.Tag_RISCV_x3_reg_usage.second); 
            break;             
            
            default :
                printf("??? tag %d cur_offset %lu\n", sub_sub_section_tag, cur_actual_attr_offset);
                assert(0);
        }
        cur_actual_attr_offset += tag_val_len;
    }
}