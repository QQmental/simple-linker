#include <algorithm>
#include <vector>
#include <string.h>

#include "Chunk/Merged_section.h"
#include "util.h"

// sort mergeable section pieces in some order,
// then assign offest in this order.
std::vector<std::pair<std::string_view, Merged_section::Piece*>> Merged_section::Get_ordered_span() const
{
    using item_t = std::pair<std::string_view, Piece*>;

    std::vector<item_t> vec(m_map.size());

    auto map_it = m_map.begin();

    for(item_t &item : vec)
    {
        item.first = map_it->first;
        item.second = map_it->second.get();
        map_it++;
    }

    auto cmp = [](const item_t &a, const item_t &b)->bool
    {
        if (a.first.size() != b.first.size())
            return a.first.size() < b.first.size();
        return memcmp(a.first.data(), b.first.data(), a.first.size()) < 0;
    };
    
    std::sort(vec.begin(), vec.end(), cmp);

    return vec;
}

void Merged_section::Assign_offset()
{
    auto vec = Get_ordered_span();
    
    using item_t = std::decay_t<decltype(*vec.begin())>;

    std::size_t offset = 0;
    uint32_t p2align = 0;

    for(item_t &item : vec)
    {
        auto *piece = item.second;

        if (piece->is_alive == false)
            continue;

        offset = nUtil::align_to(offset, 1 << p2align);
        piece->offset = offset;
        offset += item.first.size();
        p2align = std::max(p2align, piece->p2align);
    }

    shdr.sh_size = offset;
    shdr.sh_addralign = 1<<p2align;
}

