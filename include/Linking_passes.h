#pragma once
#include <functional>
#include "Linking_context.h"

namespace nLinking_passes
{
    using Link_option_args = Linking_context::Link_option_args;

    void Reference_dependent_file(Input_file &input_file, 
                                  Linking_context &ctx,
                                  const std::function<void(Input_file&)> &reference_file);

    void Check_duplicate_smbols(const Input_file &file);

    void Create_output_sections(Linking_context &ctx);
}