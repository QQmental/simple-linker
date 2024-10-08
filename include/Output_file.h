#pragma once
#include <fstream>
#include <bitset>
#include <iostream>
#include <filesystem>
#include <memory>

#include "Linking_context.h"

class Output_file
{
public:

    Output_file() = default;

    Output_file(Linking_context &ctx, std::string in_path, uint64_t filesize, std::filesystem::perms permission) 
              : path(in_path), filesize(filesize), m_buf(std::make_unique<uint8_t[]>(filesize))
    {
        namespace fs = std::filesystem;
        
        if (fs::exists(path.c_str()))
            fs::remove(path.c_str());

        fout.open(path, std::ios_base::binary);

        if (fout.is_open() == false)
            FATALF("failed to open the file: %s", path.c_str());

        fs::path fp(path.c_str());
        fs::permissions(fp, permission);
    }

    Output_file(const Output_file &src) = delete;

    Output_file(Output_file &&src) noexcept
              : buf2(std::move(src.buf2)), 
                path(std::move(src.path)),
                filesize(src.filesize),
                m_buf(std::move(src.m_buf)),
                fout(std::move(src.fout))
    {
        src.filesize = 0;
    }

    Output_file& operator=(Output_file &&src)
    {
        if (this != &src)
        {
            this->~Output_file();
            new (this) Output_file(std::move(src));
        }
        return *this;
    }

    ~Output_file()
    {
        if (fout.is_open())
            fout.close();
    }

    void Serialize()
    {
        fout.write(reinterpret_cast<char*>(buf()), filesize);
    }

    uint8_t* buf() const {return m_buf.get();}

    std::vector<char> buf2;
    std::string path;
    uint64_t filesize = 0;

private:
    std::unique_ptr<uint8_t[]> m_buf = nullptr;
    std::ofstream fout;
};