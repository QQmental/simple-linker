#pragma once
#include <cstddef>
#include <math.h>
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

}

