
#ifndef POSITION_H
#define POSITION_H

#include "types.h"
#include "bitboard.h"
#include <string>

struct Position {
	u64 colour[2];
	u64 pieces[6];
	i64 psqt_score;
	mutable u64 hash_key;
	i32 phase_score;
	u8 ep_square;
	u8 castling;
	bool flipped;
	mutable bool hash_valid;
	Position();
	void set_fen(const std::string& fen);
	void flip();
	void make_null_move();
	PieceType piece_on(i32 sq) const;
	u64 all_pieces() const { return colour[0] | colour[1]; }
	u64 ep_bb() const { return ep_square < 64 ? BB::square_bb(ep_square) : 0; }
	bool is_attacked(i32 sq, bool by_enemy = true) const;
	bool make_move(const Move& move);
	bool see(const Move& move, i32 margin) const;
	void print() const;
	
	// Full absolute-position Zobrist key (pieces, side, castling, and en passant).
	u64 key() const;
};

namespace Zobrist {
	void init();
}

#endif // POSITION_H

