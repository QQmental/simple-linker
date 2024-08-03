#pragma once
#include <vector>
#include <stdint.h>
#include <string_view>
#include <algorithm>
#include <stdint.h>
struct Mergeable_section
{
    std::string_view data;
    std::vector<uint32_t> frag_offset_list;
    std::vector<std::size_t> frag_hash_list; // is it really needed?
    std::size_t p2_align = 0;

// first: the fragment which holds the offset
// second: the offset from the returned fragment
std::pair<std::string_view, std::size_t> 
get_fragment(std::size_t offset)
{
  std::vector<uint32_t> &vec = frag_offset_list;
  auto it = std::upper_bound(vec.begin(), vec.end(), offset);
  std::size_t idx = it - 1 - vec.begin();
  
  uint32_t len = it == vec.end() ? data.size() - *vec.rbegin() : *it - *(it-1);
  return {data.substr(vec[idx], len), offset - vec[idx]};

}
};

