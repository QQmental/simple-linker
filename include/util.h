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

// Extract portion of the instruction
inline uint32_t EPOI(uint32_t targ, uint32_t upper_bit_pos, uint32_t lower_bit_pos)
{
    assert(upper_bit_pos >= lower_bit_pos);

    auto left_shift_len = sizeof(uint32_t) * std::numeric_limits<uint8_t>::digits - 1 - upper_bit_pos;
    auto t = targ << left_shift_len;
    return t >> (sizeof(uint32_t) * std::numeric_limits<uint8_t>::digits - 1 - upper_bit_pos + lower_bit_pos);
}

inline uint64_t bit(uint64_t val, uint64_t pos)
{
    return (val >> pos) & 1;
};

//not a portable sign extend !
inline int64_t sign_extend(uint64_t val, int64_t size)
{
    return (int64_t)(val << (63 - size)) >> (63 - size);
}

inline uint64_t read_uleb(uint8_t **buf)
{
    uint64_t val = 0;
    uint8_t shift = 0;
    uint8_t byte;
    do
    {
        byte = *(*buf)++;
        val |= (byte & 0x7f) << shift;
        shift += 7;
    } while (byte & 0x80);

    return val;
}

inline uint64_t read_uleb(uint8_t *buf)
{
    uint8_t *tmp = buf;
    return read_uleb(&tmp);
}

inline uint64_t read_uleb(std::string_view *str)
{
    uint8_t *start = (uint8_t *)str->data();
    uint8_t *ptr = start;
    uint64_t val = read_uleb(&ptr);
    *str = str->substr(ptr - start);
    return val;
}

inline uint64_t read_uleb(std::string_view str)
{
    std::string_view tmp = str;
    return read_uleb(&tmp);
}

inline uint64_t uleb_size(int64_t val)
{
    for (int i = 1; i < 9; i++)
    {
        if (val < (1LL << (7 * i)))
            return i;
    }

  return 9;
}

inline void Overwrite_uleb(uint8_t *loc, uint64_t val)
{
    while (*loc & 0b1000'0000)
    {
        *loc++ = 0b1000'0000 | (val & 0b0111'1111);
        val >>= 7;
    }
    *loc = val & 0b0111'1111;
}

inline std::size_t Write_string(void *buf, std::string_view str)
{
    memcpy(buf, str.data(), str.size());
    *((uint8_t *)buf + str.size()) = '\0';
    return str.size() + 1;
}

}

