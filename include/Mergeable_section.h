#pragma once
#include <vector>
#include <stdint.h>
#include <string_view>
#include <algorithm>
#include <stdint.h>

#include "Chunk/Merged_section.h"

// a mergeable section can be sliced into pieces, and they will be aggregated 
struct Mergeable_section
{
	std::size_t size() const {return piece_offset_list.size();}

	std::string_view data;
	std::vector<uint32_t> piece_offset_list;
	std::vector<uint64_t> piece_hash_list; // is it really needed?
	std::size_t p2_align = 0;
	std::vector<Merged_section::Piece*> piece_list;
	// all of mergeable section piece is merged into 'final_dst'
	Merged_section *final_dst;


	// first: a mergeable section piece
	// second: the offset from the returned mergeable piece
	std::pair<Merged_section::Piece*, std::size_t> 
	Get_mergeable_piece(std::size_t offset) const
	{
		const std::vector<uint32_t> &vec = piece_offset_list;
		auto it = std::upper_bound(vec.begin(), vec.end(), offset);
		assert(it != vec.begin());
		std::size_t idx = it - 1 - vec.begin();
		return std::make_pair(piece_list[idx], offset - vec[idx]);
	}

	std::string_view Get_contents(uint64_t piece_idx) const
	{
		uint64_t cur = piece_offset_list[piece_idx];
		assert(piece_idx < piece_offset_list.size());
		
		if (piece_idx == piece_offset_list.size() - 1)
			return data.substr(cur);
		return data.substr(cur, piece_offset_list[piece_idx + 1] - cur);
	}

};

