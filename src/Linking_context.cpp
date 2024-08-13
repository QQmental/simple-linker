#include <vector>
#include <algorithm>
#include <numeric>
#include "Linking_context.h"
#include "Relocatable_file.h"
#include <iostream>

static void Reference_dependent_file(Input_file &input_file, 
                                     Linking_context &ctx, 
                                     std::vector<bool> &alive_list, 
                                     const std::pair<Input_file*, Input_file*> input_file_range);


// Bind undef global symbols in symbol list of each input file to defined global symbols.
// If a file is not referenced by other files through global symbols, then
// content of this file is no need to be linked into the output file.
// a file with no reference will be discarded from the file list
// input_file_range.first: addr of the first input_file
// input_file_range.second: addr of the last input_file + 1
static void Reference_dependent_file(Input_file &input_file, 
                                     Linking_context &ctx, 
                                     std::vector<bool> &alive_list, 
                                     const std::pair<Input_file*, Input_file*> input_file_range)
{        
    for(std::size_t sym_idx = input_file.src().linking_mdata().first_global ; sym_idx < input_file.src().symbol_table()->count() ; sym_idx++)
    {
        if (input_file.symbol_list[sym_idx] != nullptr) //not nullptr, no need to be binded to the global symbal 
            continue;

        auto &esym = input_file.src().symbol_table()->data(sym_idx);

        // a file would be referenced when it has a defined symbol 
        // which is not defined in 'the other' file

        // it's defined, don't mark alive its source, because it's itself
        if (esym.st_shndx != SHN_UNDEF)
            continue;
        
        auto it = ctx.Find_symbol(nELF_util::Get_symbol_name(input_file.src(), sym_idx));
        
        if (it == ctx.global_symbol_map().end())
            continue;;
        
        // bind the undef symbol with the defined global symbol which is from the other file
        input_file.symbol_list[sym_idx] = it->second.symbol.get();
        
        assert(it->second.input_file >= input_file_range.first && it->second.input_file < input_file_range.second);
        
        alive_list[it->second.input_file - input_file_range.first] = true;
    }
}

template <typename File_list_t>
void Remove_unused_file(File_list_t &dst, const std::vector<bool> &is_alive)
{
    auto cmp_gen = [&is_alive](auto begin) 
    {
        return [&is_alive, begin](auto &item) -> bool
        {
            return is_alive[&item - &(*begin)] == false;
        };
    };

    auto remove_start = std::remove_if(dst.begin(), dst.end(), cmp_gen(dst.begin()));
    dst.erase(remove_start, dst.end());
}

void Linking_context::link()
{
    for(std::size_t i = 0 ; i < m_rel_file.size() ; i++)
    {
        m_input_file.push_back(Input_file(*m_rel_file[i].get()));
    }

    for(auto &file : m_input_file)
        file.Put_global_symbol(*this);
    
    for(std::size_t i = 0 ; i < m_input_file.size() ; i++)
    {
        if (m_is_alive[i] == true)
            Reference_dependent_file(m_input_file[i], *this, m_is_alive, {m_input_file.data(), m_input_file.data() + m_input_file.size()});
    }

    Remove_unused_file(m_input_file, m_is_alive);
    Remove_unused_file(m_rel_file, m_is_alive);
    m_is_alive.clear();
    
    for(std::size_t i = 0 ; i < m_input_file.size() ; i++)
        m_input_file[i].Init_mergeable_section(*this);

    for(std::size_t i = 0 ; i < m_input_file.size() ; i++)
        m_input_file[i].Collect_mergeable_section();

    for(std::size_t i = 0 ; i < m_input_file.size() ; i++)
        m_input_file[i].Resolve_sesction_pieces(*this);

    for(auto &item : merged_section_map)
        item.second->Aggregate_section_fragment();
}

