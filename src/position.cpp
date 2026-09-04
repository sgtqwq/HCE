
#include "position.h"
#include "evaluation.h"
#include <iostream>
#include <sstream>
#include <cstdlib>

#if defined(__AVX2__)
#include <immintrin.h>
#endif

namespace {
	u64 piece_keys[2][6][64];
	u64 castling_piece_keys[4];
	u64 castling_keys[16];
	u64 ep_keys[64];
	u64 side_key;
	constexpr u8 FLIPPED_CASTLING[16] = {
		0, 4, 8, 12, 1, 5, 9, 13, 2, 6, 10, 14, 3, 7, 11, 15
	};

	inline i32 absolute_square(i32 sq, bool flipped) {
		return flipped ? (sq ^ 56) : sq;
	}

	inline i32 absolute_color(i32 color, bool flipped) {
		return flipped ? (color ^ 1) : color;
	}

	constexpr i32 SEE_PIECE_VALUES[6] = {100, 450, 450, 675, 1300, 0};

	bool has_legal_ep_capture(const Position& pos) {
		if (pos.ep_square >= 64) return false;
		
		const u64 target = BB::square_bb(pos.ep_square);
		const u64 captured_pawn = BB::south(target);
		if ((pos.all_pieces() & target) ||
			!(pos.colour[1] & pos.pieces[Pawn] & captured_pawn))
			return false;
		
		u64 candidates = pos.colour[0] & pos.pieces[Pawn] &
			BB::PawnAttackers[1][pos.ep_square];
		while (candidates) {
			const i32 from = BB::pop_lsb(candidates);
			const u64 from_bb = BB::square_bb(from);
			Position child = pos;
			child.colour[0] ^= from_bb | target;
			child.pieces[Pawn] ^= from_bb | target;
			child.colour[1] ^= captured_pawn;
			child.pieces[Pawn] ^= captured_pawn;
			const i32 king_sq = BB::lsb(child.colour[0] & child.pieces[King]);
			if (!child.is_attacked(king_sq)) return true;
		}
		return false;
	}

	inline i32 see_piece_value(PieceType piece) {
		return SEE_PIECE_VALUES[static_cast<i32>(piece)];
	}

	inline u64 least_absolute_square(u64 bb, bool flipped) {
		if (!flipped) return bb & (~bb + 1);
		return BB::square_bb(BB::lsb(BB::flip(bb)) ^ 56);
	}

	u64 attackers_to(const Position& pos, i32 sq, u64 occupied) {
		const u64 queens = pos.pieces[Queen];
		const u64 pawns =
			(pos.colour[0] & pos.pieces[Pawn] & BB::PawnAttackers[1][sq]) |
			(pos.colour[1] & pos.pieces[Pawn] & BB::PawnAttackers[0][sq]);
		const u64 non_pawns =
			(pos.pieces[Knight] & BB::knight_attacks(sq)) |
			(pos.pieces[King] & BB::king_attacks(sq)) |
			((pos.pieces[Bishop] | queens) & BB::bishop_attacks(sq, occupied)) |
			((pos.pieces[Rook] | queens) & BB::rook_attacks(sq, occupied));
		return pawns | non_pawns;
	}

	u64 squares_between(i32 a, i32 b) {
		const i32 rank_delta = rank_of(b) - rank_of(a);
		const i32 file_delta = file_of(b) - file_of(a);
		i32 step = 0;
		if (file_delta == 0) step = rank_delta > 0 ? 8 : -8;
		else if (rank_delta == 0) step = file_delta > 0 ? 1 : -1;
		else if (rank_delta == file_delta) step = rank_delta > 0 ? 9 : -9;
		else if (rank_delta == -file_delta) step = rank_delta > 0 ? 7 : -7;
		else return 0;

		u64 between = 0;
		for (i32 sq = a + step; sq != b; sq += step)
			between |= BB::square_bb(sq);
		return between;
	}

	u64 aligned_squares(i32 a, i32 b) {
		const u64 endpoints = BB::square_bb(a) | BB::square_bb(b);
		if (rank_of(a) == rank_of(b) || file_of(a) == file_of(b))
			return (BB::rook_attacks(a, 0) & BB::rook_attacks(b, 0)) | endpoints;
		if (std::abs(rank_of(a) - rank_of(b)) == std::abs(file_of(a) - file_of(b)))
			return (BB::bishop_attacks(a, 0) & BB::bishop_attacks(b, 0)) | endpoints;
		return 0;
	}

	void pin_data(const Position& pos, i32 color, u64& pinned, u64& pinners) {
		const i32 king_sq = BB::lsb(pos.colour[color] & pos.pieces[King]);
		const u64 enemy = pos.colour[color ^ 1];
		const u64 queens = pos.pieces[Queen];
		u64 sliders = enemy &
			((BB::rook_attacks(king_sq, 0) & (pos.pieces[Rook] | queens)) |
			 (BB::bishop_attacks(king_sq, 0) & (pos.pieces[Bishop] | queens)));
		const u64 occupied_without_sliders = pos.all_pieces() ^ sliders;

		pinned = 0;
		pinners = 0;
		while (sliders) {
			const i32 slider_sq = BB::pop_lsb(sliders);
			const u64 between = squares_between(king_sq, slider_sq) & occupied_without_sliders;
			if (BB::popcount(between) == 1 && (between & pos.colour[color])) {
				pinned |= between;
				pinners |= BB::square_bb(slider_sq);
			}
		}
	}

	// Tiny, dependency-free PRNG (xorshift64*) so we don't drag in <random>.
	u64 next_rand(u64& state) {
		state ^= state >> 12;
		state ^= state << 25;
		state ^= state >> 27;
		return state * 0x2545F4914F6CDD1DULL;
	}
}

namespace Zobrist {
	void init() {
		u64 seed = 0x9E3779B97F4A7C15ULL;
		for (int c = 0; c < 2; c++)
			for (int pt = 0; pt < 6; pt++)
				for (int sq = 0; sq < 64; sq++)
					piece_keys[c][pt][sq] = next_rand(seed);
		for (int i = 0; i < 4; ++i) castling_piece_keys[i] = next_rand(seed);
		castling_keys[0] = 0;
		for (int rights = 1; rights < 16; ++rights) {
			u64 key = 0;
			for (int i = 0; i < 4; ++i)
				if (rights & (1 << i)) key ^= castling_piece_keys[i];
			castling_keys[rights] = key;
		}
		for (int sq = 0; sq < 64; sq++) ep_keys[sq] = next_rand(seed);
		side_key = next_rand(seed);
	}
}

u64 Position::key() const {
	if (hash_valid) return hash_key;

	u64 h = flipped ? side_key : 0;
	for (int c = 0; c < 2; ++c) {
		const int abs_color = absolute_color(c, flipped);
		for (int pt = Pawn; pt <= King; ++pt) {
			u64 bb = colour[c] & pieces[pt];
			while (bb) {
				const int sq = absolute_square(BB::pop_lsb(bb), flipped);
				h ^= piece_keys[abs_color][pt][sq];
			}
		}
	}
	int absolute_rights = castling;
	if (flipped) absolute_rights = ((absolute_rights & 3) << 2) | ((absolute_rights >> 2) & 3);
	h ^= castling_keys[absolute_rights];
	if (ep_square < 64) h ^= ep_keys[absolute_square(ep_square, flipped)];

	hash_key = h;
	hash_valid = true;
	return h;
}

Position::Position() {
	colour[0] = 0x000000000000FFFFULL;
	colour[1] = 0xFFFF000000000000ULL;
	pieces[Pawn]   = 0x00FF00000000FF00ULL;  
	pieces[Knight] = 0x4200000000000042ULL;  
	pieces[Bishop] = 0x2400000000000024ULL;  
	pieces[Rook]   = 0x8100000000000081ULL;  
	pieces[Queen]  = 0x0800000000000008ULL;  
	pieces[King]   = 0x1000000000000010ULL;  
	castling = 0xF;
	ep_square = NoSquare;
	flipped = false;
	hash_key = 0;
	hash_valid = false;
	refresh_evaluation(*this);
}

void Position::set_fen(const std::string& fen) {
	colour[0] = colour[1] = 0;
	for (int i = 0; i < 6; i++) pieces[i] = 0;
	castling = 0;
	ep_square = NoSquare;
	flipped = false;
	psqt_score = 0;
	phase_score = 0;
	hash_valid = false;
	
	std::istringstream ss(fen);
	std::string board_str, side_str, castle_str, ep_str;
	ss >> board_str >> side_str >> castle_str >> ep_str;

	i32 sq = 56; 
	for (char c : board_str) {
		if (c == '/') {
			sq -= 16; 
		} else if (c >= '1' && c <= '8') {
			sq += (c - '0');  
		} else {
			bool is_black = (c >= 'a' && c <= 'z');
			PieceType pt;
			char lower = is_black ? c : (c + 32);
			
			switch (lower) {
				case 'p': pt = Pawn; break;
				case 'n': pt = Knight; break;
				case 'b': pt = Bishop; break;
				case 'r': pt = Rook; break;
				case 'q': pt = Queen; break;
				case 'k': pt = King; break;
				default: pt = None;
			}
			
			if (pt != None) {
				u64 bb = BB::square_bb(sq);
				colour[is_black ? 1 : 0] |= bb;
				pieces[pt] |= bb;
			}
			sq++;
		}
	}
	
	bool black_to_move = (side_str == "b");
	for (char c : castle_str) {
		switch (c) {
			case 'K': castling |= 1; break;
			case 'Q': castling |= 2; break;
			case 'k': castling |= 4; break;
			case 'q': castling |= 8; break;
		}
	}
	if (ep_str != "-" && ep_str.length() >= 2) {
		i32 file = ep_str[0] - 'a';
		i32 rank = ep_str[1] - '1';
		ep_square = static_cast<u8>(make_square(rank, file));
	}
	if (black_to_move) {
		flip();
	}

	// An en-passant square changes the legal move set only when the side to
	// move has at least one legal capture. Keeping uncapturable EP squares in
	// the Zobrist key makes otherwise identical positions compare different
	// for repetition detection.
	if (ep_square < 64 && !has_legal_ep_capture(*this))
		ep_square = NoSquare;
	refresh_evaluation(*this);
}

void Position::flip() {
	hash_valid = false;
	flipped = !flipped;
	psqt_score = -psqt_score;
#if defined(__AVX2__)
	const __m256i byte_reverse = _mm256_setr_epi8(
		7, 6, 5, 4, 3, 2, 1, 0, 15, 14, 13, 12, 11, 10, 9, 8,
		7, 6, 5, 4, 3, 2, 1, 0, 15, 14, 13, 12, 11, 10, 9, 8);
	__m256i first = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(colour));
	__m256i second = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(pieces + 2));
	first = _mm256_shuffle_epi8(first, byte_reverse);
	second = _mm256_shuffle_epi8(second, byte_reverse);
	first = _mm256_permute4x64_epi64(first, _MM_SHUFFLE(3, 2, 0, 1));
	_mm256_storeu_si256(reinterpret_cast<__m256i*>(colour), first);
	_mm256_storeu_si256(reinterpret_cast<__m256i*>(pieces + 2), second);
#else
	colour[0] = BB::flip(colour[0]);
	colour[1] = BB::flip(colour[1]);
	std::swap(colour[0], colour[1]);
	for (int i = 0; i < 6; ++i) pieces[i] = BB::flip(pieces[i]);
#endif
	if (ep_square < 64) ep_square ^= 56;
	castling = static_cast<u8>(((castling & 3) << 2) | ((castling >> 2) & 3));
}
void Position::make_null_move() {
	u64 h = key();
	if (ep_square < 64) h ^= ep_keys[ep_square ^ (flipped ? 56 : 0)];
	h ^= side_key;
	ep_square = NoSquare;
	flip();
	hash_key = h;
	hash_valid = true;
}

PieceType Position::piece_on(i32 sq) const {
	const u64 bb = BB::square_bb(sq);
	if (!(all_pieces() & bb)) return None;
	if (pieces[Pawn]   & bb) return Pawn;
	if (pieces[Knight] & bb) return Knight;
	if (pieces[Bishop] & bb) return Bishop;
	if (pieces[Rook]   & bb) return Rook;
	if (pieces[Queen]  & bb) return Queen;
	return King;
}

bool Position::is_attacked(i32 sq, bool by_enemy) const {
	int attacker = by_enemy ? 1 : 0;
	u64 attackers = colour[attacker];
	u64 all = all_pieces();
	const u64 pawns = attackers & pieces[Pawn];
	if (BB::PawnAttackers[by_enemy ? 0 : 1][sq] & pawns) return true;
	if (BB::knight_attacks(sq) & attackers & pieces[Knight]) return true;
	if (BB::bishop_attacks(sq, all) & attackers & (pieces[Bishop] | pieces[Queen])) return true;
	if (BB::rook_attacks(sq, all) & attackers & (pieces[Rook] | pieces[Queen])) return true;
	if (BB::king_attacks(sq) & attackers & pieces[King]) return true;
	
	return false;
}

bool Position::see(const Move& move, i32 margin) const {
	const i32 src = move.from;
	const i32 dst = move.to;
	const u64 src_bb = BB::square_bb(src);

	// Castling does not lose material in orthodox chess.
	if (move.moving_piece() == King && std::abs(dst - src) == 2)
		return margin <= 0;

	const bool ep_capture = move.moving_piece() == Pawn && dst == ep_square &&
		!(colour[1] & BB::square_bb(dst));

	u64 occupied = all_pieces() ^ src_bb;
	if (ep_capture)
		occupied ^= BB::south(BB::square_bb(dst));
	u64 attackers = attackers_to(*this, dst, occupied) & ~src_bb;

	i32 value = 0;
	if (move.promo != None) {
		value = see_piece_value(static_cast<PieceType>(move.promo)) - see_piece_value(Pawn);
		if (move.captured_piece() == None)
			value -= margin;
		else
			value += see_piece_value(move.captured_piece()) - margin;
		if (value < 0) return false;
		value = see_piece_value(static_cast<PieceType>(move.promo)) - value;
	} else if (ep_capture) {
		value = see_piece_value(Pawn) - margin;
		if (value < 0) return false;
		value = see_piece_value(Pawn) - value;
	} else {
		if (move.captured_piece() == None)
			value = -margin;
		else
			value = see_piece_value(move.captured_piece()) - margin;
		if (value < 0) return false;
		value = see_piece_value(move.moving_piece()) - value;
	}

	if (value <= 0) return true;

	const u64 diagonal_sliders = pieces[Bishop] | pieces[Queen];
	const u64 straight_sliders = pieces[Rook] | pieces[Queen];

	u64 pinned_by_color[2];
	u64 pinners_by_color[2];
	pin_data(*this, 0, pinned_by_color[0], pinners_by_color[0]);
	pin_data(*this, 1, pinned_by_color[1], pinners_by_color[1]);
	const u64 pinned = pinned_by_color[0] | pinned_by_color[1];

	const i32 king0 = BB::lsb(colour[0] & pieces[King]);
	const i32 king1 = BB::lsb(colour[1] & pieces[King]);
	const u64 pinned_aligned =
		(aligned_squares(dst, king0) & pinned_by_color[0]) |
		(aligned_squares(dst, king1) & pinned_by_color[1]);

	i32 side = 0;
	bool us = false;

	while (true) {
		side ^= 1;
		u64 side_attackers = attackers & colour[side];
		if (pinners_by_color[side] & occupied)
			side_attackers &= ~pinned | pinned_aligned;

		if (!side_attackers) return !us;

		u64 candidates = side_attackers & pieces[Pawn];
		if (candidates) {
			const u64 attacker = least_absolute_square(candidates, flipped);
			occupied ^= attacker;
			attackers ^= attacker;
			attackers |= BB::bishop_attacks(dst, occupied) & occupied & diagonal_sliders;
			value = see_piece_value(Pawn) - value;
			if (value < static_cast<i32>(us)) return us;
		} else if ((candidates = side_attackers & pieces[Knight])) {
			const u64 attacker = least_absolute_square(candidates, flipped);
			occupied ^= attacker;
			attackers ^= attacker;
			value = see_piece_value(Knight) - value;
			if (value < static_cast<i32>(us)) return us;
		} else if ((candidates = side_attackers & pieces[Bishop])) {
			const u64 attacker = least_absolute_square(candidates, flipped);
			occupied ^= attacker;
			attackers ^= attacker;
			attackers |= BB::bishop_attacks(dst, occupied) & occupied & diagonal_sliders;
			value = see_piece_value(Bishop) - value;
			if (value < static_cast<i32>(us)) return us;
		} else if ((candidates = side_attackers & pieces[Rook])) {
			const u64 attacker = least_absolute_square(candidates, flipped);
			occupied ^= attacker;
			attackers ^= attacker;
			attackers |= BB::rook_attacks(dst, occupied) & occupied & straight_sliders;
			value = see_piece_value(Rook) - value;
			if (value < static_cast<i32>(us)) return us;
		} else if ((candidates = side_attackers & pieces[Queen])) {
			const u64 attacker = least_absolute_square(candidates, flipped);
			occupied ^= attacker;
			attackers ^= attacker;
			attackers |= (BB::bishop_attacks(dst, occupied) & occupied & diagonal_sliders) |
				(BB::rook_attacks(dst, occupied) & occupied & straight_sliders);
			value = see_piece_value(Queen) - value;
			if (value < static_cast<i32>(us)) return us;
		} else if (side_attackers & pieces[King]) {
			return (attackers & colour[side ^ 1]) ? !us : us;
		}

		us = !us;
	}
}

bool Position::make_move(const Move& move) {
	u64 h = key();
	const bool was_flipped = flipped;
	const i32 us = static_cast<i32>(was_flipped);
	const i32 them = us ^ 1;
	const i32 square_xor = was_flipped ? 56 : 0;
	const i32 abs_from = move.from ^ square_xor;
	const i32 abs_to = move.to ^ square_xor;

	if (castling) h ^= castling_keys[was_flipped ? FLIPPED_CASTLING[castling] : castling];
	if (ep_square < 64) h ^= ep_keys[ep_square ^ square_xor];

	const u64 from_bb = BB::square_bb(move.from);
	const u64 to_bb = BB::square_bb(move.to);
	const u64 move_mask = from_bb | to_bb;
	const PieceType piece = move.moving_piece();
	const PieceType captured = move.captured_piece();
	const bool promotion = move.promo != None;

	psqt_score += Psqt[piece][move.to] - Psqt[piece][move.from];

	colour[0] ^= move_mask;
	pieces[piece] ^= move_mask;

	if (promotion) {
		h ^= piece_keys[us][Pawn][abs_from];
		h ^= piece_keys[us][move.promo][abs_to];
	} else {
		h ^= piece_keys[us][piece][abs_from];
		h ^= piece_keys[us][piece][abs_to];
	}

	const bool ep_capture = piece == Pawn && move.to == ep_square;
	if (captured != None) {
		if (ep_capture) {
			const u64 captured_pawn = BB::south(to_bb);
			const i32 captured_sq = move.to - 8;
			psqt_score += Psqt[Pawn][captured_sq ^ 56];
			colour[1] ^= captured_pawn;
			pieces[Pawn] ^= captured_pawn;
			h ^= piece_keys[them][Pawn][captured_sq ^ square_xor];
		} else {
			psqt_score += Psqt[captured][move.to ^ 56];
			colour[1] ^= to_bb;
			pieces[captured] ^= to_bb;
			h ^= piece_keys[them][captured][abs_to];
		}
		phase_score -= PhaseValue[captured];
	}

	ep_square = NoSquare;
	if (piece == Pawn) {
		if (move.to - move.from == 16) {
			ep_square = static_cast<u8>(move.to - 8);
			Position ep_probe = *this;
			ep_probe.flip();
			if (has_legal_ep_capture(ep_probe))
				h ^= ep_keys[ep_square ^ square_xor];
			else
				ep_square = NoSquare;
		}
		if (promotion) {
			psqt_score += Psqt[move.promo][move.to] - Psqt[Pawn][move.to];
			phase_score += PhaseValue[move.promo];
			pieces[Pawn] ^= to_bb;
			pieces[move.promo] ^= to_bb;
		}
	} else if (piece == King) {
		if (move.to - move.from == 2) {
			psqt_score += Psqt[Rook][F1] - Psqt[Rook][H1];
			const u64 rook_move = BB::square_bb(H1) | BB::square_bb(F1);
			colour[0] ^= rook_move;
			pieces[Rook] ^= rook_move;
			h ^= piece_keys[us][Rook][H1 ^ square_xor];
			h ^= piece_keys[us][Rook][F1 ^ square_xor];
		} else if (move.from - move.to == 2) {
			psqt_score += Psqt[Rook][D1] - Psqt[Rook][A1];
			const u64 rook_move = BB::square_bb(A1) | BB::square_bb(D1);
			colour[0] ^= rook_move;
			pieces[Rook] ^= rook_move;
			h ^= piece_keys[us][Rook][A1 ^ square_xor];
			h ^= piece_keys[us][Rook][D1 ^ square_xor];
		}
	}

	if (move_mask & BB::square_bb(E1)) castling &= static_cast<u8>(~3);
	if (move_mask & BB::square_bb(H1)) castling &= static_cast<u8>(~1);
	if (move_mask & BB::square_bb(A1)) castling &= static_cast<u8>(~2);
	if (move_mask & BB::square_bb(E8)) castling &= static_cast<u8>(~12);
	if (move_mask & BB::square_bb(H8)) castling &= static_cast<u8>(~4);
	if (move_mask & BB::square_bb(A8)) castling &= static_cast<u8>(~8);

	if (castling) h ^= castling_keys[was_flipped ? FLIPPED_CASTLING[castling] : castling];
	h ^= side_key;

	flip();
	hash_key = h;
	hash_valid = true;

	const i32 my_king_sq = BB::lsb(colour[1] & pieces[King]);
	return !is_attacked(my_king_sq, false);
}

void Position::print() const {
	const char* piece_chars = "PNBRQKpnbrqk";
	
	std::cout << "\n";
	for (i32 rank = 7; rank >= 0; rank--) {
		i32 display_rank = flipped ? (7 - rank) : rank;
		std::cout << (display_rank + 1) << " | ";
		
		for (i32 file = 0; file < 8; file++) {
			i32 sq = make_square(rank, file);
			u64 bb = BB::square_bb(sq);
			
			char c = '.';
			for (int pt = Pawn; pt < None; pt++) {
				if (pieces[pt] & bb) {
					bool is_enemy = colour[1] & bb;
					c = piece_chars[pt + (is_enemy ? 6 : 0)];
					break;
				}
			}
			std::cout << c << ' ';
		}
		std::cout << "\n";
	}
	std::cout << "  +----------------\n";
	std::cout << "    a b c d e f g h\n\n";
	
	std::cout << "  Flipped: " << (flipped ? "yes (black to move)" : "no (white to move)") << "\n";
	std::cout << "  Castling: ";
	if (castling & 1) std::cout << "K";
	if (castling & 2) std::cout << "Q";
	if (castling & 4) std::cout << "k";
	if (castling & 8) std::cout << "q";
	if (!castling) std::cout << "-";
	std::cout << "\n";
	
	if (ep_square < 64) {
		std::cout << "  En passant: " << char('a' + file_of(ep_square))
			<< (rank_of(ep_square) + 1) << "\n";
	}
	std::cout << "\n";
}

