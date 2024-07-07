#pragma once
#include <cstddef>

#define FATALF(fmt, ...) (fprintf(stderr, "fatal: %s:%d\n" fmt, __FILE__, __LINE__, ##__VA_ARGS__), abort())

namespace nUtil
{

template<std::size_t N>
constexpr std::size_t const_expr_STR_len(const char (&str)[N])
{
    return N-1;
}

}

