#include <iostream>
#include <assert.h>
#include <utility>
#include <string.h>


#include "RISC_V_linking_data.h"

int main(int argc, char* argv[])
{
    for(int i = 1 ; i < argc ; i++)
        printf("%s\n", argv[i]);

    auto file = fopen("test3.o", "rb");

    assert(file != nullptr);

    elf64_hdr hdr;

    fread(&hdr, 1, sizeof(elf64_hdr), file);

    nRSIC_V_linking_data::Section_hdr_table sec_hdr_tbl(file, hdr);

    for(auto it = sec_hdr_tbl.string_table().begin_str_ptr() ; it != sec_hdr_tbl.string_table().end_str_ptr() ; it++)
        printf("%s\n",*it);


    std::size_t symtab_idx = -1;

    for(auto &item : sec_hdr_tbl.headers())
    {
        if (item.sh_type == SHT_SYMTAB)
        {
            symtab_idx = &item - &(*sec_hdr_tbl.headers().begin());

        }
    }

    assert(symtab_idx != (std::size_t)-1);

    nRSIC_V_linking_data::Symbol_table sym_tbl(file, sec_hdr_tbl.headers()[symtab_idx], sec_hdr_tbl);

    std::cout << sym_tbl.data().size() << " symbols\n";

    for(std::size_t i = 0 ; i < sym_tbl.data().size() ; i++)
    {
        std::cout << sym_tbl.str_table_ptr()[i] << " " << sym_tbl.data()[i].st_value << "\n";
    }
/*     for(auto &item : sym_tbl.str_table_ptr())
        printf("%s\n",item); */

    fclose(file);
}