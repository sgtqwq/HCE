#ifndef EVALUATION_H
#define EVALUATION_H

#include "types.h"

struct Position;
using Score = i64;

constexpr Score S(i32 mg, i32 eg) {
	return static_cast<Score>(
		(static_cast<u64>(static_cast<u32>(eg)) << 32) +
		static_cast<u64>(static_cast<i64>(mg)));
}

constexpr i32 mg_of(Score s) { return static_cast<i32>(static_cast<u32>(s)); }
constexpr i32 eg_of(Score s) {
	return static_cast<i32>(static_cast<u64>(s + 0x80000000LL) >> 32);
}

extern const Score Psqt[6][64];
extern const i32 PhaseValue[6];

void refresh_evaluation(Position& pos);
i32 evaluate(const Position& pos);

#endif // EVALUATION_H
