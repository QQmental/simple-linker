#pragma once
#include <vector>
#include <unordered_map>
#include <memory>
#include "Symbol.h"
#include "Mergeable_section.h"
#include "Input_section.h"

class Linking_context;
class Relocatable_file;

class Input_file
{
public:
    enum class eRelocate_state : uint32_t
    {
        no_need = 0,
        relocatable,
        mergeable
    };

    Input_file(Relocatable_file &src);
    Input_file(const Input_file &src) = delete;
    Input_file(Input_file &&src) = default;
    Input_file& operator= (Input_file &&src) noexcept
    {
        if (this != &src)
        {
            symbol_list = std::move(src.symbol_list);
            input_section_list = std::move(src.input_section_list);
            has_init_array = src.has_init_array;
            has_ctors = src.has_ctors;

            m_mergeable_section_symbol_list = std::move(src.m_mergeable_section_symbol_list);
            m_relocate_state_list = std::move(src.m_relocate_state_list);
            m_local_sym_list = std::move(src.m_local_sym_list);
            m_n_local_sym = src.m_n_local_sym;
            m_mergeable_section_list = std::move(src.m_mergeable_section_list);
            m_src = src.m_src;
        }

        return *this;
    }
    ~Input_file();

    void Put_global_symbol(Linking_context &ctx);
    void Init_mergeable_section(Linking_context &ctx);
    // there are some entries in mergeable section
    // they are reffered as 'mergeable section piece' or 'fragment' in this project
    // pieces are merged if they have same property
    void Collect_mergeable_section_piece();
    void Resolve_sesction_pieces(Linking_context &ctx);

    const std::vector<eRelocate_state>& relocate_state_list() const {return m_relocate_state_list;}
    Input_section* Get_input_section(std::size_t shndx);
    Input_section* Get_symbol_input_section(const Symbol &sym)
    {
        if (sym.piece() != nullptr)
            return nullptr;
        return Get_input_section(sym.file()->get_shndx(sym.elf_sym()));
    }
    const Symbol* local_sym_list() const {return m_local_sym_list.get();}
    std::size_t n_local_sym() const {return m_n_local_sym;}
    Relocatable_file& src() const {return *m_src;}
    std::string_view name() const {return m_src->name();} 

    std::vector<Symbol*> symbol_list;

    //input sections are sorted by shndx
    std::vector<Input_section> input_section_list;
    bool has_init_array = false;
    bool has_ctors = false;

private:
    std::vector<Symbol> m_mergeable_section_symbol_list;
    std::vector<eRelocate_state> m_relocate_state_list;
    std::unique_ptr<Symbol[]> m_local_sym_list;
    std::size_t m_n_local_sym;
    std::vector<std::unique_ptr<Mergeable_section>> m_mergeable_section_list;
    Relocatable_file *m_src;

};

inline Input_section* Input_file::Get_input_section(std::size_t shndx)
{
    auto cmp = [](const Input_section &cur, size_t shndx)
    {
        return cur.shndx < shndx;
    };

    auto it = std::lower_bound(input_section_list.begin(), input_section_list.end(), shndx, cmp);
    
    if (it == input_section_list.end() || it->shndx != shndx)
        return nullptr;

    return &*it;
}