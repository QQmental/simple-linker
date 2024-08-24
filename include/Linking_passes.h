#pragma once
#include <functional>
#include "Linking_context.h"

namespace nLinking_passes
{
    using Link_option_args = Linking_context::Link_option_args;

    void Reference_dependent_file(Input_file &input_file, 
                                  Linking_context &ctx,
                                  const std::function<void(const Input_file&)> &reference_file);

    void Check_duplicate_smbols(const Input_file &file);
    
    // combine Input_section into Output_section
    void Combined_input_sections(Linking_context &ctx);

    void Bind_special_symbols(Linking_context &ctx);

    void Create_synthetic_sections(Linking_context &ctx);

    // after assigning input section offset of Output_section, size of the Output_section is calculated
    void Assign_input_section_offset(Linking_context &ctx);

    void Sort_output_sections(Linking_context &ctx);
}