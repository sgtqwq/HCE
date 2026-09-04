
#ifndef BITBOARD_H
#define BITBOARD_H

#include "types.h"

namespace BB {
	constexpr u64 FileA = 0x0101010101010101ULL;
	constexpr u64 FileB = FileA << 1;
	constexpr u64 FileC = FileA << 2;
	constexpr u64 FileD = FileA << 3;
	constexpr u64 FileE = FileA << 4;
	constexpr u64 FileF = FileA << 5;
	constexpr u64 FileG = FileA << 6;
	constexpr u64 FileH = FileA << 7;
	
	constexpr u64 Rank1 = 0x00000000000000FFULL;
	constexpr u64 Rank2 = Rank1 << 8;
	constexpr u64 Rank3 = Rank1 << 16;
	constexpr u64 Rank4 = Rank1 << 24;
	constexpr u64 Rank5 = Rank1 << 32;
	constexpr u64 Rank6 = Rank1 << 40;
	constexpr u64 Rank7 = Rank1 << 48;
	constexpr u64 Rank8 = Rank1 << 56;
	
	constexpr u64 NotFileA = ~FileA;
	constexpr u64 NotFileH = ~FileH;
	
	inline i32 lsb(u64 bb) {
		return __builtin_ctzll(bb);
	}
	
	inline i32 pop_lsb(u64& bb) {
		i32 sq = lsb(bb);
		bb &= bb - 1;
		return sq;
	}
	
	inline i32 popcount(u64 bb) {
		return __builtin_popcountll(bb);
	}
	
	inline u64 flip(u64 bb) {
		return __builtin_bswap64(bb);
	}
	
	inline u64 square_bb(i32 sq) {
		return 1ULL << sq;
	}
	

	inline u64 north(u64 bb) { return bb << 8; }
	inline u64 south(u64 bb) { return bb >> 8; }
	inline u64 east(u64 bb)  { return (bb << 1) & NotFileA; }
	inline u64 west(u64 bb)  { return (bb >> 1) & NotFileH; }
	
	inline u64 north_east(u64 bb) { return (bb << 9) & NotFileA; }
	inline u64 north_west(u64 bb) { return (bb << 7) & NotFileH; }
	inline u64 south_east(u64 bb) { return (bb >> 7) & NotFileA; }
	inline u64 south_west(u64 bb) { return (bb >> 9) & NotFileH; }
	
	extern u64 KnightAttacks[64];
	extern u64 KingAttacks[64];
	extern u64 PawnAttackers[2][64];

	inline u64 knight_attacks(i32 sq) { return KnightAttacks[sq]; }
	inline u64 king_attacks(i32 sq) { return KingAttacks[sq]; }

	extern u64 DiagMask[64];
	extern u64 AntiDiagMask[64];
	extern u64 RookMasks[64];
	extern u64 BishopMasks[64];
	extern u32 RookOffsets[64];
	extern u32 BishopOffsets[64];
	extern u8 RookShifts[64];
	extern u8 BishopShifts[64];
	extern const u64 RookMagics[64];
	extern const u64 BishopMagics[64];
	extern u64 RookAttackTable[102400];
	extern u64 BishopAttackTable[5248];

	inline u64 rook_attacks(i32 sq, u64 blockers) {
		const u64 occupied = blockers & RookMasks[sq];
		const u32 index = static_cast<u32>((occupied * RookMagics[sq]) >> RookShifts[sq]);
		return RookAttackTable[RookOffsets[sq] + index];
	}

	inline u64 bishop_attacks(i32 sq, u64 blockers) {
		const u64 occupied = blockers & BishopMasks[sq];
		const u32 index = static_cast<u32>((occupied * BishopMagics[sq]) >> BishopShifts[sq]);
		return BishopAttackTable[BishopOffsets[sq] + index];
	}

	inline u64 queen_attacks(i32 sq, u64 blockers) {
		return rook_attacks(sq, blockers) | bishop_attacks(sq, blockers);
	}

	void init();
	
	void print(u64 bb);
	
} // namespace BB

#endif // BITBOARD_H

