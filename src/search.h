#ifndef SEARCH_H
#define SEARCH_H

#include "types.h"
#include "position.h"
#include <cstddef>

constexpr i32 INF_SCORE = 32000;
constexpr i32 MATE_SCORE = 31000;

void resize_tt(std::size_t mib);
void clear_tt();

i32 evaluate(const Position& pos);
Move search(Position& pos, i32 max_depth, i64 max_time_ms, u64& total_nodes,
	const u64* history, i32 history_size);

#endif // SEARCH_H
