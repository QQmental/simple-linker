#include <iostream>

#include "Linking_passes.h"

//copy from https://github.com/rui314/mold

struct Output_section_key
{
    bool operator==(const Output_section_key &src) const
    {
        return name == src.name && type == src.type;
    }

    std::string_view name;
    uint64_t type;
};


static bool Has_ctors_and_init_array(Linking_context &ctx);
static uint64_t Canonicalize_type(std::string_view name, uint64_t type);


static bool Has_ctors_and_init_array(Linking_context &ctx)
{
    bool x = false;
    bool y = false;

    for(const Input_file &file : ctx.input_file_list())
    {
        x |= file.has_ctors;
        y |= file.has_init_array;
    }

    return x && y;
}


static uint64_t Canonicalize_type(std::string_view name, uint64_t type)
{
  // Some old assemblers don't recognize these section names and
  // create them as SHT_PROGBITS.
  if (type == SHT_PROGBITS) {
    if (name == ".init_array" || name.rfind(".init_array.", 0) == 0)
      return SHT_INIT_ARRAY;
    if (name == ".fini_array" || name.rfind(".fini_array.", 0) == 0)
      return SHT_FINI_ARRAY;
  }
    return type;
}
void nLinking_passes::Check_duplicate_smbols(const Input_file &file)
{
    for (std::size_t i = file.n_local_sym(); i < file.src().symbol_table()->count(); i++)
    {
        auto &esym = file.src().symbol_table()->data(i);
        
        // a global symbol could be a nullptr
        auto *sym = file.symbol_list[i];

        if (   sym != nullptr
            && (sym->file() == &file.src()))
            continue;
        if (nELF_util::Is_sym_undef(esym) || nELF_util::Is_sym_common(esym))
            continue;
        
        if (nELF_util::Is_sym_abs(esym) == false)
        {
            std::size_t shndx = file.src().get_shndx(esym);
            if (file.section_relocate_needed_list()[shndx] == Input_file::eRelocate_state::no_need)
                continue;
        }
        std::cout << "duplicate symbols " << file.name() << " " << sym->file()->name() << "\n";
        abort();
    }
}

// Bind undef global symbols in symbol list of each input file to defined global symbols.
// If a file is not referenced by other files through global symbols, then
// content of this file is no need to be linked into the output file.
// a file with no reference will be discarded from the file list
// input_file_range.first: addr of the first input_file
// input_file_range.second: addr of the last input_file + 1
void nLinking_passes::Reference_dependent_file(Input_file &input_file, 
                                               Linking_context &ctx, 
                                               const std::function<void(Input_file&)> &reference_file)
{        
    for(std::size_t sym_idx = input_file.src().linking_mdata().first_global ; sym_idx < input_file.src().symbol_table()->count() ; sym_idx++)
    {
        if (input_file.symbol_list[sym_idx] != nullptr) //not nullptr, no need to be binded to the global symbal 
            continue;

        auto &esym = input_file.src().symbol_table()->data(sym_idx);

        // a file would be referenced when it has a defined symbol 
        // which is not defined in 'the other' file

        // it's defined, don't mark alive its source, because it's itself
        bool undef = nELF_util::Is_sym_undef(esym) == false && nELF_util::Is_sym_weak(esym) == false;
        bool common = nELF_util::Is_sym_common(esym) && nELF_util::Is_sym_common(input_file.symbol_list[sym_idx]->elf_sym()) == false;
        if (undef || common)
            continue;
        
        auto it = ctx.Find_symbol(nELF_util::Get_symbol_name(input_file.src(), sym_idx));
        
        if (it == ctx.global_symbol_map().end())
        {
/*              std::cout << nELF_util::Get_symbol_name(input_file.src(), sym_idx) << \
            " size: " << esym.st_size << " type: " << ELF64_ST_TYPE(esym.st_info) << \
            " bind: " << ELF64_ST_BIND(esym.st_info) << " not found \n";  */
            continue;
        }

        // bind the undef symbol with the defined global symbol which is from the other file
        input_file.symbol_list[sym_idx] = it->second.symbol.get();
        
        reference_file(input_file);
    }
}
void nLinking_passes::Create_output_sections(Linking_context &ctx)
{

}