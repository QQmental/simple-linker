#pragma once
#include <functional>
#include "Linking_context.h"
#include "Linking_context_helper.h"

namespace nLinking_passes
{
    using Link_option_args = Linking_context::Link_option_args;

    Output_section_key Get_output_section_key(const Linking_context &ctx, const Input_section &isec, bool ctors_in_init_array);

    uint64_t Get_input_section_addr(const Linking_context &ctx, Input_section *isec);    
    uint64_t Get_symbol_addr(const Linking_context &ctx, const Symbol &sym, uint64_t flags = 0) ;

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

    void Relocate_symbols(Linking_context &ctx, Output_section &osec);

    void Copy_chunks(Linking_context &ctx);
}

inline Output_section_key nLinking_passes::Get_output_section_key(const Linking_context &ctx, const Input_section &isec, bool ctors_in_init_array)
{
    // If .init_array/.fini_array exist, .ctors/.dtors must be merged
    // with them.
    //
    // CRT object files contain .ctors/.dtors sections without any
    // relocations. They contain sentinel values, 0 and -1, to mark the
    // beginning and the end of the initializer/finalizer pointer arrays.
    // We do not place them into .init_array/.fini_array because such
    // invalid pointer values would simply make the program to crash.
    if (ctors_in_init_array && !isec.rel_count() == 0) {
    std::string_view name = isec.name();
    if (name == ".ctors" || name.rfind(".ctors.", 0) == 0)
        return {".init_array", SHT_INIT_ARRAY};
    if (name == ".dtors" || name.rfind(".dtors.", 0) == 0)
        return {".fini_array", SHT_FINI_ARRAY};
    }

    auto &shdr = isec.shdr();
    
    std::string_view name = nELF_util::get_output_name(isec.name(), shdr.sh_flags);
    uint64_t type = ctx.Canonicalize_type(name, shdr.sh_type);

    return Output_section_key{name, type};
}

inline uint64_t nLinking_passes::Get_input_section_addr(const Linking_context &ctx, Input_section *isec)
{
    auto key = Get_output_section_key(ctx, *isec, nLinking_context_helper::Has_ctors_and_init_array(ctx));

    auto it = ctx.osec_pool().find(key);
    
    for(std::size_t i = 0 ; i < it->second->member_list.size() ; i++)
    {
        if (it->second->member_list[i] == isec)
            return it->second->shdr.sh_addr + it->second->input_section_offset_list[i];
    }
    
    FATALF("%s", "unreachable");
    return 0;
}

// copied from mold
inline uint64_t nLinking_passes::Get_symbol_addr(const Linking_context &ctx, const Symbol &sym, uint64_t flags)
{
    if (Merged_section::Piece *piece = sym.piece() ; piece != nullptr)
    {
        if (piece->is_alive == false) {
            // This condition is met if a non-alloc section refers an
            // alloc section and if the referenced piece of data is
            // garbage-collected. Typically, this condition occurs if a
            // debug info section refers a string constant in .rodata.
            return 0;
        }

        return piece->Get_addr() + sym.val;
    }

    Input_file *input_file = ctx.global_symbol_map().find(sym.name)->second.input_file;

    //It's safe because nothing is modified
    Input_section *isec = input_file->Get_symbol_input_section(sym);

    if (isec == nullptr)
        return sym.val; // absolute symbol
    
    return Get_input_section_addr(ctx, isec) + sym.val;
}