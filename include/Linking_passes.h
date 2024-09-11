#pragma once
#include <functional>
#include "Linking_context.h"

namespace nLinking_passes
{
    using Link_option_args = Linking_context::Link_option_args;

    void Resolve_symbols(Linking_context &ctx, std::vector<Input_file> &input_file_list, std::vector<bool> &is_alive);

    void Reference_dependent_file(Input_file &input_file, 
                                  Linking_context &ctx,
                                  const std::function<void(const Input_file&)> &reference_file);

    void Check_duplicate_smbols(const Input_file &file);
    
    // combine Input_section into Output_section
    void Combined_input_sections(Linking_context &ctx);

    void Bind_special_symbols(Linking_context &ctx);

    void Create_synthetic_sections(Linking_context &ctx);

    // after assigning input section offsets of Output_section, output section sizes are calculated
    void Assign_input_section_offset(Linking_context &ctx);

    void Sort_output_sections(Linking_context &ctx);

    void Compute_section_headers(Linking_context &ctx);

    // assign virtual addresses and file offsets to ouput chunks.
    [[nodiscard]] std::size_t Set_output_chunk_locations(Linking_context &ctx);

    void Copy_chunk(Linking_context &ctx);
}