#pragma once
#include <cstddef>
#include <math.h>
#include <assert.h>

#include "stdlib.h"
#include "stdio.h"

#define FATALF(fmt, ...) (fprintf(stderr, "fatal: %s:%d\n" fmt, __FILE__, __LINE__, ##__VA_ARGS__), abort())

namespace nUtil
{

template<std::size_t N>
constexpr std::size_t const_expr_STR_len(const char (&str)[N])
{
    return N-1;
}

// 0b0->0, 0b1->0, 0b10->1, 0b100->2 
inline std::size_t to_p2align(std::size_t alignment)
{
    std::size_t ret = 0;

    if (alignment == 0)
        return ret;

    while((alignment & 1) == 0)
    {
        ret++;
        alignment >>= 1;
    }

    return ret;
}

inline bool has_single_bit(uint64_t val)
{
    return (val != 0) && ((val & (val-1)) == 0);
}

inline uint64_t align_to(uint64_t val, uint64_t align)
{
    if (align == 0)
        return val;
    assert(has_single_bit(align));
    return (val + align - 1) & ~(align - 1);
}

inline uint64_t align_down(uint64_t val, uint64_t align)
{
    assert(has_single_bit(align));
    return val & ~(align - 1);
}

// it's copied from https://github.com/rui314/mold
inline size_t Find_null(std::string_view data, std::size_t pos, std::size_t entsize)
{
    if (entsize == 1)
    return data.find('\0', pos);

    for (; pos <= data.size() - entsize; pos += entsize)
    {
        if (data.substr(pos, entsize).find_first_not_of('\0') == data.npos)
            return pos;
    }
  return data.npos;
}

}

