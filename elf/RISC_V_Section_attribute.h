#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string>
#include <utility>

namespace nRISC_V_Section
{

constexpr uint32_t Tag_file = 1;
constexpr uint32_t Tag_RISCV_stack_align = 4;
constexpr uint32_t Tag_RISCV_arch = 5;
constexpr uint32_t Tag_RISCV_unaligned_access = 6;
//Deprecated
constexpr uint32_t Tag_RISCV_priv_spec = 8;
//Deprecated
constexpr uint32_t Tag_RISCV_priv_spec_minor = 10;
//Deprecated
constexpr uint32_t Tag_RISCV_priv_spec_revision = 12;
constexpr uint32_t Tag_RISCV_atomic_abi = 14;
constexpr uint32_t Tag_RISCV_x3_reg_usage = 16;

struct Attributes
{
    std::pair<bool, uint32_t>    Tag_RISCV_stack_align;
    std::pair<bool, std::string> Tag_RISCV_arch;
    std::pair<bool, uint32_t>    Tag_RISCV_unaligned_access;
    std::pair<bool, uint32_t>    Tag_RISCV_atomic_abi;
    std::pair<bool, uint32_t>    Tag_RISCV_x3_reg_usage;                
};

}

