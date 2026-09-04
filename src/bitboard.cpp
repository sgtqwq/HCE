
#include "bitboard.h"
#include <iostream>
#include <algorithm>

namespace BB {

	u64 DiagMask[64];
	u64 AntiDiagMask[64];
	u64 KnightAttacks[64];
	u64 KingAttacks[64];
	u64 PawnAttackers[2][64];
	u64 RookMasks[64];
	u64 BishopMasks[64];
	u32 RookOffsets[64];
	u32 BishopOffsets[64];
	u8 RookShifts[64];
	u8 BishopShifts[64];
	u64 RookAttackTable[102400];
	u64 BishopAttackTable[5248];

	const u64 RookMagics[64] = {
		0x1080004008801020ULL, 0x0840092002c03000ULL, 0x1900200010400900ULL, 0x0880100008000480ULL,
		0x4200100420080200ULL, 0x8100020100080400ULL, 0x0200040110886200ULL, 0x0200008040220411ULL,
		0x0404800084400220ULL, 0x0000401000402000ULL, 0x0086001081220440ULL, 0x0408800800100280ULL,
		0x000a001201040820ULL, 0x8848800200840080ULL, 0x4001000100040200ULL, 0x0442000102105084ULL,
		0x9080010020804100ULL, 0x0040404000201009ULL, 0x0000808010002009ULL, 0x2200090021d00100ULL,
		0x0008008008040080ULL, 0x0004004002010040ULL, 0x0011040008015042ULL, 0x00000a0001768104ULL,
		0x0000800080204009ULL, 0x2010004140002001ULL, 0x9800200280100080ULL, 0x1000100080080080ULL,
		0x0442000a00049020ULL, 0x2100040080020080ULL, 0x0800120400900148ULL, 0x0010040a00128541ULL,
		0x2800804000800030ULL, 0x1010002000400041ULL, 0x4000200011004100ULL, 0x0610008410800800ULL,
		0x0400802402800800ULL, 0xc100020080800400ULL, 0x0002000802000401ULL, 0x0182085882000401ULL,
		0x0220204000808000ULL, 0x2860100040024022ULL, 0x0001002004110040ULL, 0x99101042000a0020ULL,
		0x0004080004008080ULL, 0x0010040002008080ULL, 0x2012004881020004ULL, 0x8300842444820011ULL,
		0x0088403882010200ULL, 0x0820400080210100ULL, 0x0110910040a00300ULL, 0x0801100280080480ULL,
		0x0242009008200600ULL, 0x1002000489500200ULL, 0x0040800200010080ULL, 0x0091800041000080ULL,
		0x0000209300488001ULL, 0x04c1002414824001ULL, 0x020020000b001041ULL, 0x7000100004200901ULL,
		0x8002002004100802ULL, 0x30010002084c0007ULL, 0x0888221800813004ULL, 0x4000002840840112ULL,
	};

	const u64 BishopMagics[64] = {
		0xa010041108003100ULL, 0x006082020a002900ULL, 0x6810010619200000ULL, 0x08281a0520000408ULL,
		0x0001104001000400ULL, 0x0018901008048400ULL, 0x00040a0210245280ULL, 0x000200210808a402ULL,
		0x9140048410821200ULL, 0x0800091010820041ULL, 0x20504804832202c0ULL, 0x0100091401081000ULL,
		0x8021011140000012ULL, 0x0810020804450400ULL, 0x208b0542109008a2ULL, 0x0080084a08040204ULL,
		0x0040e2a80811244cULL, 0x2505022008008108ULL, 0x0430220100420040ULL, 0x010a040420220040ULL,
		0x1105000290400000ULL, 0x0093001200822120ULL, 0x4000a62048043004ULL, 0x280120048a015004ULL,
		0x006090002a020814ULL, 0x44042000240800d0ULL, 0x01102800040a4400ULL, 0x1004080080220040ULL,
		0x0001001011004024ULL, 0x0010044000805040ULL, 0x0914041200820100ULL, 0x0004821012821480ULL,
		0x0024040500c05021ULL, 0x0088611002080200ULL, 0x0116080a00040020ULL, 0x4000020080080080ULL,
		0x2450450140840040ULL, 0x0000880201484100ULL, 0x0222020404020092ULL, 0x8081110600002e00ULL,
		0x2842101105000801ULL, 0x1100809008001025ULL, 0x00020202221c0400ULL, 0x0422014022009020ULL,
		0x0210046102100c00ULL, 0xc004008082029102ULL, 0x00aa461801101200ULL, 0x0404080080201108ULL,
		0x020542108c205002ULL, 0x0410544804100100ULL, 0x0040910841100000ULL, 0x0400200042021100ULL,
		0x00004204850400c0ULL, 0x0200100410a42102ULL, 0x1040020801210102ULL, 0x0805040410420000ULL,
		0x2884804130100200ULL, 0x800c262201242000ULL, 0x1058000194108800ULL, 0x0014221054420204ULL,
		0x0104000012a02200ULL, 0x0200881003300100ULL, 0x0140400202840100ULL, 0x0402020801010201ULL,
	};

	namespace {
		u64 rook_mask(i32 sq) {
			const i32 rank = rank_of(sq);
			const i32 file = file_of(sq);
			u64 mask = 0;
			for (i32 r = rank + 1; r <= 6; ++r) mask |= square_bb(make_square(r, file));
			for (i32 r = rank - 1; r >= 1; --r) mask |= square_bb(make_square(r, file));
			for (i32 f = file + 1; f <= 6; ++f) mask |= square_bb(make_square(rank, f));
			for (i32 f = file - 1; f >= 1; --f) mask |= square_bb(make_square(rank, f));
			return mask;
		}

		u64 bishop_mask(i32 sq) {
			const i32 rank = rank_of(sq);
			const i32 file = file_of(sq);
			u64 mask = 0;
			for (i32 r = rank + 1, f = file + 1; r <= 6 && f <= 6; ++r, ++f)
				mask |= square_bb(make_square(r, f));
			for (i32 r = rank + 1, f = file - 1; r <= 6 && f >= 1; ++r, --f)
				mask |= square_bb(make_square(r, f));
			for (i32 r = rank - 1, f = file + 1; r >= 1 && f <= 6; --r, ++f)
				mask |= square_bb(make_square(r, f));
			for (i32 r = rank - 1, f = file - 1; r >= 1 && f >= 1; --r, --f)
				mask |= square_bb(make_square(r, f));
			return mask;
		}

		u64 rook_attacks_slow(i32 sq, u64 blockers) {
			const i32 rank = rank_of(sq);
			const i32 file = file_of(sq);
			u64 attacks = 0;
			for (i32 r = rank + 1; r < 8; ++r) {
				const u64 bit = square_bb(make_square(r, file));
				attacks |= bit; if (blockers & bit) break;
			}
			for (i32 r = rank - 1; r >= 0; --r) {
				const u64 bit = square_bb(make_square(r, file));
				attacks |= bit; if (blockers & bit) break;
			}
			for (i32 f = file + 1; f < 8; ++f) {
				const u64 bit = square_bb(make_square(rank, f));
				attacks |= bit; if (blockers & bit) break;
			}
			for (i32 f = file - 1; f >= 0; --f) {
				const u64 bit = square_bb(make_square(rank, f));
				attacks |= bit; if (blockers & bit) break;
			}
			return attacks;
		}

		u64 bishop_attacks_slow(i32 sq, u64 blockers) {
			const i32 rank = rank_of(sq);
			const i32 file = file_of(sq);
			u64 attacks = 0;
			for (i32 r = rank + 1, f = file + 1; r < 8 && f < 8; ++r, ++f) {
				const u64 bit = square_bb(make_square(r, f));
				attacks |= bit; if (blockers & bit) break;
			}
			for (i32 r = rank + 1, f = file - 1; r < 8 && f >= 0; ++r, --f) {
				const u64 bit = square_bb(make_square(r, f));
				attacks |= bit; if (blockers & bit) break;
			}
			for (i32 r = rank - 1, f = file + 1; r >= 0 && f < 8; --r, ++f) {
				const u64 bit = square_bb(make_square(r, f));
				attacks |= bit; if (blockers & bit) break;
			}
			for (i32 r = rank - 1, f = file - 1; r >= 0 && f >= 0; --r, --f) {
				const u64 bit = square_bb(make_square(r, f));
				attacks |= bit; if (blockers & bit) break;
			}
			return attacks;
		}
	}

	void init() {
		u32 rook_offset = 0;
		u32 bishop_offset = 0;
		for (i32 sq = 0; sq < 64; ++sq) {
			const i32 r = rank_of(sq);
			const i32 f = file_of(sq);

			const u64 bb = square_bb(sq);
			KnightAttacks[sq] = ((bb << 17) & NotFileA)
				| ((bb << 15) & NotFileH)
				| ((bb << 10) & ~(FileA | FileB))
				| ((bb << 6) & ~(FileG | FileH))
				| ((bb >> 17) & NotFileH)
				| ((bb >> 15) & NotFileA)
				| ((bb >> 10) & ~(FileG | FileH))
				| ((bb >> 6) & ~(FileA | FileB));
			const u64 sides = east(bb) | west(bb);
			KingAttacks[sq] = north(bb) | south(bb) | sides | north(sides) | south(sides);
			PawnAttackers[0][sq] = north_east(bb) | north_west(bb);
			PawnAttackers[1][sq] = south_east(bb) | south_west(bb);

			DiagMask[sq] = 0;
			for (i32 tr = r + 1, tf = f + 1; tr < 8 && tf < 8; ++tr, ++tf)
				DiagMask[sq] |= square_bb(make_square(tr, tf));
			for (i32 tr = r - 1, tf = f - 1; tr >= 0 && tf >= 0; --tr, --tf)
				DiagMask[sq] |= square_bb(make_square(tr, tf));

			AntiDiagMask[sq] = 0;
			for (i32 tr = r + 1, tf = f - 1; tr < 8 && tf >= 0; ++tr, --tf)
				AntiDiagMask[sq] |= square_bb(make_square(tr, tf));
			for (i32 tr = r - 1, tf = f + 1; tr >= 0 && tf < 8; --tr, ++tf)
				AntiDiagMask[sq] |= square_bb(make_square(tr, tf));

			RookMasks[sq] = rook_mask(sq);
			BishopMasks[sq] = bishop_mask(sq);
			const i32 rook_bits = popcount(RookMasks[sq]);
			const i32 bishop_bits = popcount(BishopMasks[sq]);
			RookShifts[sq] = static_cast<u8>(64 - rook_bits);
			BishopShifts[sq] = static_cast<u8>(64 - bishop_bits);
			RookOffsets[sq] = rook_offset;
			BishopOffsets[sq] = bishop_offset;

			u64 subset = 0;
			do {
				const u32 index = static_cast<u32>((subset * RookMagics[sq]) >> RookShifts[sq]);
				RookAttackTable[rook_offset + index] = rook_attacks_slow(sq, subset);
				subset = (subset - RookMasks[sq]) & RookMasks[sq];
			} while (subset);

			subset = 0;
			do {
				const u32 index = static_cast<u32>((subset * BishopMagics[sq]) >> BishopShifts[sq]);
				BishopAttackTable[bishop_offset + index] = bishop_attacks_slow(sq, subset);
				subset = (subset - BishopMasks[sq]) & BishopMasks[sq];
			} while (subset);

			rook_offset += 1U << rook_bits;
			bishop_offset += 1U << bishop_bits;
		}
	}

	void print(u64 bb) {
		std::cout << "\n";
		for (i32 rank = 7; rank >= 0; --rank) {
			std::cout << (rank + 1) << " | ";
			for (i32 file = 0; file < 8; ++file) {
				const i32 sq = make_square(rank, file);
				std::cout << ((bb & square_bb(sq)) ? "1 " : ". ");
			}
			std::cout << "\n";
		}
		std::cout << "  +----------------\n";
		std::cout << "    a b c d e f g h\n\n";
		std::cout << "  Hex: 0x" << std::hex << bb << std::dec << "\n";
	}

} // namespace BB

