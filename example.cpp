#include <iostream>
#include <assert.h>
#include <utility>
#include <string.h>
#include <fstream>

#include "RISC_V_linking_data.h"

int main(int argc, char* argv[])
{
    for(int i = 1 ; i < argc ; i++)
        printf("%s\n", argv[i]);

    //auto file = fopen("test3.o", "rb");

    //assert(file != nullptr);

    elf64_hdr hdr;

    std::string path = "test3.o";

    std::ifstream file(path, std::ios::binary);

    if (file.is_open() == false)
        FATALF("fail to open %s", path.c_str());
    
    file.seekg(0, std::ios::end);
    std::size_t file_size =  file.tellg();
    auto data = std::unique_ptr<char[]>(new char[file_size]);    
    file.seekg(0, std::ios::beg);
    file.read(data.get(), file_size);
    nRSIC_V_linking_data::Relocatable_file rel(std::move(data));

    file.close();
}