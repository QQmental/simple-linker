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
        symbol_list = std::move(src.symbol_list);
        m_section_relocate_state_list = std::move(src.m_section_relocate_state_list);
        m_input_section_list = std::move(src.m_input_section_list);
        m_local_sym_list = std::move(src.m_local_sym_list);
        m_n_local_sym = src.m_n_local_sym;
        m_mergeable_section_list = std::move(src.m_mergeable_section_list);
        m_src = src.m_src;
        return *this;
    }
    ~Input_file();

    void Put_global_symbol(Linking_context &ctx);
    void Init_mergeable_section(Linking_context &ctx);
    void Collect_mergeable_section();
    void Resolve_sesction_pieces(Linking_context &ctx);

    const std::vector<eRelocate_state>& section_relocate_needed_list() const {return m_section_relocate_state_list;}
    const std::vector<Input_section> input_section_list() const {return m_input_section_list;}
    const Symbol* local_sym_list() const {return m_local_sym_list.get();}
    std::size_t n_local_sym() const {return m_n_local_sym;}
    Relocatable_file& src() const {return *m_src;}
    std::string_view name() const {return m_src->name();} 

    std::vector<Symbol*> symbol_list;
    std::vector<Symbol> mergeable_section_symbol_list;
private:
    std::vector<eRelocate_state> m_section_relocate_state_list;
    std::vector<Input_section> m_input_section_list;
    std::unique_ptr<Symbol[]> m_local_sym_list;
    std::size_t m_n_local_sym;
    std::vector<std::unique_ptr<Mergeable_section>> m_mergeable_section_list;
    Relocatable_file *m_src;
};