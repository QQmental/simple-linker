#pragma once
#include <stdint.h>
#include <string.h>
/*
ref https://en.wikipedia.org/wiki/Ar_(Unix)
Each file stored in an ar archive includes a file header to store information about the file.
Numeric values are encoded in ASCII and all values right-padded with ASCII spaces (0x20). 

archive file format: [ARCHIVE_FILE_MAGIC][ar_fhdr][section][ar_fhdr][section]...[ar_fhdr][section]

each section would be either a string table, symbol table, or relocatable file
if it's a string table, it's used for long files' name
*/
struct Archive_file_header
{
    char file_identifier[16];    // detect type of the following section, ascii
    char file_mod_timestamp[12]; // File modification timestamp (in seconds), decimal
    char onwer_id[6];            // Decimal 
    char group_id[6];            // Decimal 
    char file_mode[8];           // Octal
    char file_size[10];          // size of the following section, Decimal 
    char ending_char[2];         // 0x60 0x0A

    
    static bool Is_strtab(const Archive_file_header &hdr)
    {
        return memcmp(&hdr.file_identifier, "// ", 3) == 0;
    }

    static bool Is_symtab(const Archive_file_header &hdr)
    {
        return     memcmp(&hdr.file_identifier, "/ ", 2) == 0 
                || memcmp(&hdr.file_identifier, "/SYM64/ ", 8) == 0;
    }
    static bool Is_object(const Archive_file_header &hdr)
    {
        return Is_strtab(hdr) == false && Is_symtab(hdr) == false;
    }
};