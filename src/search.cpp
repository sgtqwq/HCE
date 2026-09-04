
#include "search.h"
#include "movegen.h"
#include "bitboard.h"
#include "evaluation.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>

namespace {
	using Clock = std::chrono::steady_clock;
	using TimePoint = Clock::time_point;
	
	constexpr i32 MAX_HISTORY = 512;
	constexpr i32 MAX_DEPTH = 64;
	u64 position_stack[MAX_HISTORY];
	i32 stack_size = 0;
	
	constexpr i32 PieceValue[7] = { 100, 300, 300, 500, 900, 20000, 0 };
	
	constexpr i32 HISTORY_MAX = 16384;
	i32 history_table[2][64][64];
	
	inline void update_history(i32& entry, i32 bonus) {
		entry += bonus - entry * std::abs(bonus) / HISTORY_MAX;
	}
	
	inline i32 history_bonus(i32 depth) {
		return std::min(depth * depth * 16, HISTORY_MAX);
	}
	
	inline i32 move_index(const Move& m) {
		return static_cast<i32>(m.from) | (static_cast<i32>(m.to) << 6);
	}
	
	struct ReductionTable {
		i32 data[MAX_DEPTH + 1][256];
		ReductionTable() {
			for (i32 depth = 0; depth <= MAX_DEPTH; ++depth) {
				for (i32 move_count = 0; move_count < 256; ++move_count) {
					if (depth == 0 || move_count == 0) {
						data[depth][move_count] = 0;
						continue;
					}
					const double r = 1 + std::log(static_cast<double>(depth)) *
					std::log(static_cast<double>(move_count)) * 0.4;
					data[depth][move_count] = static_cast<i32>(r);
				}
			}
		}
	};
	const ReductionTable Reductions;
	
	inline i32 base_reduction(i32 depth, i32 move_count) {
		return Reductions.data[std::min(depth, MAX_DEPTH)][std::min(move_count, 255)];
	}
	
	enum TTFlag : u8 { TT_NONE, TT_EXACT, TT_LOWER, TT_UPPER };
	
	struct TTEntry {
		u64 key;
		Move move;
		i16 score;
		int8_t depth;
		u8 flag;
	};
	static_assert(sizeof(TTEntry) == 16, "TT entry must remain 16 bytes");
	
	std::unique_ptr<TTEntry[]> tt_table;
	std::size_t tt_count = 0;
	std::size_t tt_mask = 0;
	
	inline TTEntry* tt_entry(u64 key) {
		return tt_table ? &tt_table[static_cast<std::size_t>(key) & tt_mask] : nullptr;
	}
	
	inline i32 score_to_tt(i32 score, i32 ply) {
		if (score >= MATE_SCORE - MAX_HISTORY) return score + ply;
		if (score <= -MATE_SCORE + MAX_HISTORY) return score - ply;
		return score;
	}
	
	inline i32 score_from_tt(i32 score, i32 ply) {
		if (score >= MATE_SCORE - MAX_HISTORY) return score - ply;
		if (score <= -MATE_SCORE + MAX_HISTORY) return score + ply;
		return score;
	}
	
	inline void tt_store(u64 key, i32 depth, i32 ply, i32 score, TTFlag flag, const Move& move) {
		TTEntry* entry = tt_entry(key);
		if (!entry) return;
		
		if (entry->flag == TT_NONE || entry->key == key) {
			entry->key = key;
			entry->move = move;
			entry->score = static_cast<i16>(score_to_tt(score, ply));
			entry->depth = static_cast<int8_t>(depth);
			entry->flag = flag;
			return;
		}
		
		if (depth >= entry->depth) {
			entry->key = key;
			entry->move = move;
			entry->score = static_cast<i16>(score_to_tt(score, ply));
			entry->depth = static_cast<int8_t>(depth);
			entry->flag = flag;
		}
	}
	
	inline bool tt_cutoff(u64 key, i32 depth, i32 ply, i32 alpha, i32 beta,
		Move& tt_move, i32& score) {
			TTEntry* entry = tt_entry(key);
			if (!entry || entry->flag == TT_NONE || entry->key != key) return false;
			
			tt_move = entry->move;
			if (entry->depth < depth) return false;
			
			score = score_from_tt(entry->score, ply);
			return entry->flag == TT_EXACT ||
			(entry->flag == TT_LOWER && score >= beta) ||
			(entry->flag == TT_UPPER && score <= alpha);
		}
	
	inline bool is_quiet_move(const Move& move) {
		return move.captured_piece() == None && move.promo == None;
	}
	
	inline i32 score_move(const Move& move, const Move& tt_move, bool flipped) {
		if (!tt_move.is_none() && move == tt_move) return 2000000000;
		
		i32 score = 0;
		if (move.captured_piece() != None)
			score += 1000000 + PieceValue[move.captured_piece()] * 16
			- PieceValue[move.moving_piece()];
		if (move.promo != None)
			score += 2000000 + PieceValue[move.promo] * 16;
		if (move.captured_piece() == None && move.promo == None)
			score += history_table[flipped][move.from][move.to];
		return score;
	}
	
	inline void sort_moves(Move* moves, i32 count, bool flipped, const Move& tt_move = NullMove) {
		i32 scores[256];
		for (i32 i = 0; i < count; ++i) scores[i] = score_move(moves[i], tt_move, flipped);
		for (i32 i = 1; i < count; ++i) {
			const Move key_move = moves[i];
			const i32 key_score = scores[i];
			i32 j = i - 1;
			while (j >= 0 && scores[j] < key_score) {
				moves[j + 1] = moves[j];
				scores[j + 1] = scores[j];
				--j;
			}
			moves[j + 1] = key_move;
			scores[j + 1] = key_score;
		}
	}
	
	inline bool is_repetition(u64 key) {
		i32 count = 0;
		for (i32 i = stack_size - 3; i >= 0; i -= 2) {
			if (position_stack[i] == key && ++count >= 1) return true;
		}
		return false;
	}
	
	inline bool in_check(const Position& pos) {
		const i32 king_sq = BB::lsb(pos.colour[0] & pos.pieces[King]);
		return pos.is_attacked(king_sq, true);
	}
	
	i32 quiescence(Position& pos, i32 alpha, i32 beta, u64& nodes,
		const TimePoint& deadline, bool& stopped) {
			if (stopped) return 0;
			
			++nodes;
			if ((nodes & 4095) == 0 && Clock::now() >= deadline) {
				stopped = true;
				return 0;
			}
			
			const bool check = in_check(pos);
			i32 best = -INF_SCORE;
			if (!check) {
				best = evaluate(pos);
				if (best >= beta) return best;
				if (best > alpha) alpha = best;
			}
			
			Move movelist[256];
			const i32 num_moves = check
			? generate_moves(pos, movelist, MoveGenType::All)
			: generate_moves(pos, movelist, MoveGenType::Noisy);
			sort_moves(movelist, num_moves, pos.flipped);
			
			for (i32 i = 0; i < num_moves; ++i) {
				if (pos.see(movelist[i], 0) == 0) continue;
				Position child = pos;
				if (!child.make_move(movelist[i])) continue;
				
				const i32 score = -quiescence(child, -beta, -alpha, nodes, deadline, stopped);
				if (stopped) return 0;
				if (score > best) best = score;
				if (best > alpha) alpha = best;
				if (alpha >= beta) break;
			}
			
			return check && best == -INF_SCORE ? -MATE_SCORE : best;
		}
	
	i32 negamax(Position& pos, i32 depth, i32 ply, i32 alpha, i32 beta, u64& nodes,
		const TimePoint& deadline, bool& stopped) {
			if (stopped) return 0;
			
			++nodes;
			const u64 pos_key = pos.key();
			if (is_repetition(pos_key)) return 0;
			
			if ((nodes & 4095) == 0 && Clock::now() >= deadline) {
				stopped = true;
				return 0;
			}
			
			const bool pv_node = alpha != beta - 1;
			const i32 original_depth = depth;
			const i32 alpha_orig = alpha;
			Move tt_move = NullMove;
			
			i32 tt_score = 0;
			if (depth > 0 && tt_cutoff(pos_key, depth, ply, alpha, beta, tt_move, tt_score))
				return tt_score;
			
			const bool check = in_check(pos);
			if (check && depth < MAX_DEPTH) ++depth;
			if (depth == 0) return quiescence(pos, alpha, beta, nodes, deadline, stopped);
			
			if (original_depth == 0 &&
				tt_cutoff(pos_key, depth, ply, alpha, beta, tt_move, tt_score))
				return tt_score;
			
			// Reverse futility pruning (a.k.a. static null move pruning): at shallow
			// depths, if the static eval already beats beta by a depth-scaled margin,
			// assume the position will hold above beta and prune without searching.
			if (!pv_node && !check && depth <= 7 &&
				std::abs(beta) < MATE_SCORE - MAX_HISTORY) {
				const i32 static_eval = evaluate(pos);
				const i32 margin = 60 * depth;
				if (static_eval - margin >= beta)
					return (static_eval + beta) / 2;
			}
			
			Move movelist[256];
			const i32 num_moves = generate_moves(pos, movelist, MoveGenType::All);
			sort_moves(movelist, num_moves, pos.flipped, tt_move);
			
			Move quiet_moves_played[256];
			i32 quiet_count = 0;
			
			i32 best = -INF_SCORE;
			Move best_move = NullMove;
			bool any_legal = false;
			i32 move_count = 0;
			const i32 stm = pos.flipped;
			
			for (i32 i = 0; i < num_moves; ++i) {
				Position child = pos;
				if (!child.make_move(movelist[i])) continue;
				any_legal = true;
				++move_count;
				
				const bool quiet = is_quiet_move(movelist[i]);
				
				const u64 child_key = child.key();
				const bool pushed = stack_size < MAX_HISTORY;
				if (pushed) position_stack[stack_size++] = child_key;
				
				// Principal variation search with late move reductions.
				// - Reduced null-window search for late quiet moves.
				// - Full-depth null-window re-search when the reduction fails high.
				// - Full-window search only for the PV move or when a move raises alpha.
				const i32 full_depth = depth - 1;
				i32 new_depth = full_depth;
				bool did_lmr = false;
				
				if (depth >= 3 && move_count > 2 && quiet) {
					i32 r = base_reduction(depth, move_count);
//					r -= history_table[stm][movelist[i].from][movelist[i].to] / 8870;
					r = std::max(0, r);
					new_depth = std::max(1, full_depth - r);
					did_lmr = new_depth < full_depth;
				}
				
				i32 score = -INF_SCORE;
				
				if (did_lmr) {
					score = -negamax(child, new_depth, ply + 1, -alpha - 1, -alpha,
						nodes, deadline, stopped);
					
					if (!stopped && score > alpha)
						score = -negamax(child, full_depth, ply + 1, -alpha - 1, -alpha,
							nodes, deadline, stopped);
				}
				else if (!pv_node || move_count > 1) {
					score = -negamax(child, full_depth, ply + 1, -alpha - 1, -alpha,
						nodes, deadline, stopped);
				}
				
				if (!stopped && pv_node && (move_count == 1 || score > alpha))
					score = -negamax(child, full_depth, ply + 1, -beta, -alpha,
						nodes, deadline, stopped);
				
				if (pushed) --stack_size;
				if (stopped) return 0;
				
				if (score > best) {
					best = score;
					best_move = movelist[i];
				}
				if (best > alpha) alpha = best;
				if (alpha >= beta) {
					if (quiet) {
						const i32 bonus = history_bonus(depth);
						update_history(history_table[stm][movelist[i].from][movelist[i].to], bonus);
						for (i32 j = 0; j < quiet_count; ++j) {
							const Move& qm = quiet_moves_played[j];
							update_history(history_table[stm][qm.from][qm.to], -bonus);
						}
					}
					break;
				}
				
				if (quiet && quiet_count < 256) quiet_moves_played[quiet_count++] = movelist[i];
			}
			
			const i32 tt_depth = original_depth > 0 ? original_depth : depth;
			if (!any_legal) {
				best = check ? -MATE_SCORE + ply : 0;
				tt_store(pos_key, tt_depth, ply, best, TT_EXACT, NullMove);
				return best;
			}
			
			const TTFlag flag = best <= alpha_orig ? TT_UPPER : (best >= beta ? TT_LOWER : TT_EXACT);
			tt_store(pos_key, tt_depth, ply, best, flag, best_move);
			return best;
		}
	
}

void resize_tt(std::size_t mib) {
	const std::size_t requested = (mib ? mib : 1) * 1024ULL * 1024ULL / sizeof(TTEntry);
	std::size_t count = 1;
	while (count <= requested / 2) count <<= 1;
	
	tt_table.reset(new TTEntry[count]());
	tt_count = count;
	tt_mask = count - 1;
	std::memset(history_table, 0, sizeof(history_table));
}

void clear_tt() {
	if (tt_table) std::memset(tt_table.get(), 0, tt_count * sizeof(TTEntry));
	std::memset(history_table, 0, sizeof(history_table));
}

Move search(Position& pos, i32 max_depth, i64 soft_time_ms, i64 hard_time_ms, u64& total_nodes,
	const u64* history, i32 history_size) {
		const TimePoint start = Clock::now();
		const TimePoint deadline = start + std::chrono::milliseconds(hard_time_ms);
		total_nodes = 0;
		Move best_move = NullMove;
		const u64 root_key = pos.key();
		
		Move movelist[256];
		const i32 num_moves = generate_moves(pos, movelist, MoveGenType::All);
		
		i32 prev_score = 0;
		
		// Nodes spent on each root move (from|to encoded), used to scale the soft
		// time limit based on how "settled" the current best move is (c4ke style).
		static thread_local u64 nodes_table[4096];
		std::memset(nodes_table, 0, sizeof(nodes_table));
		const bool use_soft_limit = soft_time_ms < hard_time_ms;
		
		for (i32 depth = 1; depth <= max_depth; ++depth) {
			Move root_tt_move = NullMove;
			if (TTEntry* entry = tt_entry(root_key); entry && entry->flag != TT_NONE && entry->key == root_key)
				root_tt_move = entry->move;
			sort_moves(movelist, num_moves, pos.flipped, root_tt_move);
			
			Move iter_best = NullMove;
			bool stopped = false;
			i32 best_score = -INF_SCORE;
			
			// Aspiration Windows: for depths deep enough to trust the previous
			// iteration's score, start with a narrow window around it and widen
			// (re-searching) whenever the result fails low or high.
			i32 delta = 25;
			i32 alpha = -INF_SCORE;
			i32 beta = INF_SCORE;
			if (depth >= 4 && std::abs(prev_score) < MATE_SCORE - MAX_HISTORY) {
				alpha = std::max(prev_score - delta, -INF_SCORE);
				beta = std::min(prev_score + delta, INF_SCORE);
			}
			
			while (true) {
				iter_best = NullMove;
				stopped = false;
				best_score = -INF_SCORE;
				u64 nodes = 0;
				
				const i32 base = history_size < MAX_HISTORY ? history_size : MAX_HISTORY;
				for (i32 i = 0; i < base; ++i) position_stack[i] = history[i];
				stack_size = base;
				if ((stack_size == 0 || position_stack[stack_size - 1] != root_key) && stack_size < MAX_HISTORY)
					position_stack[stack_size++] = root_key;
				
				i32 search_alpha = alpha;
				i32 move_count = 0;
				const i32 stm = pos.flipped;
				
				for (i32 i = 0; i < num_moves; ++i) {
					Position child = pos;
					if (!child.make_move(movelist[i])) continue;
					++move_count;
					
					const u64 child_key = child.key();
					const bool pushed = stack_size < MAX_HISTORY;
					if (pushed) position_stack[stack_size++] = child_key;
					
					const u64 nodes_before_move = nodes;
					
					// The root is always a PV node: reduced/null-window searches are
					// only used for late quiet moves, and any move that raises alpha
					// is re-searched with the full window.
					const bool quiet = is_quiet_move(movelist[i]);
					const i32 full_depth = depth - 1;
					i32 new_depth = full_depth;
					bool did_lmr = false;
					
					if (depth >= 3 && move_count > 3 && quiet) {
						i32 r = base_reduction(depth, move_count);
//						r -= history_table[stm][movelist[i].from][movelist[i].to] / 8870;
						r = std::max(0, r);
						new_depth = std::max(1, full_depth - r);
						did_lmr = new_depth < full_depth;
					}
					
					i32 score = -INF_SCORE;
					
					if (did_lmr) {
						score = -negamax(child, new_depth, 1, -search_alpha - 1, -search_alpha,
							nodes, deadline, stopped);
						
						if (!stopped && score > search_alpha)
							score = -negamax(child, full_depth, 1, -search_alpha - 1, -search_alpha,
								nodes, deadline, stopped);
					}
					else if (move_count > 1) {
						score = -negamax(child, full_depth, 1, -search_alpha - 1, -search_alpha,
							nodes, deadline, stopped);
					}
					
					if (!stopped && (move_count == 1 || score > search_alpha))
						score = -negamax(child, full_depth, 1, -beta, -search_alpha,
							nodes, deadline, stopped);
					
					if (pushed) --stack_size;
					if (stopped) break;
					
					nodes_table[move_index(movelist[i])] += nodes - nodes_before_move;
					
					if (score > best_score) {
						best_score = score;
						iter_best = movelist[i];
					}
					if (best_score > search_alpha) search_alpha = best_score;
				}
				
				total_nodes += nodes;
				if (stopped) break;
				
				if (best_score <= alpha && alpha > -INF_SCORE) {
					beta = (alpha + beta) / 2;
					alpha = std::max(alpha - delta, -INF_SCORE);
					delta += delta / 2;
					continue;
				}
				if (best_score >= beta && beta < INF_SCORE) {
					beta = std::min(beta + delta, INF_SCORE);
					delta += delta / 2;
					continue;
				}
				break;
			}
			
			if (stopped) break;
			
			if (!iter_best.is_none()) {
				best_move = iter_best;
				prev_score = best_score;
				tt_store(root_key, depth, 0, best_score, TT_EXACT, best_move);
				const i64 elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
					Clock::now() - start).count();
				const i64 ms = elapsed > 0 ? elapsed : 1;
				const u64 nps = total_nodes * 1000ULL / static_cast<u64>(ms);
				std::cout << "info depth " << depth
				<< " score cp " << best_score
				<< " nodes " << total_nodes
				<< " nps " << nps
				<< " time " << elapsed
				<< " pv " << move_to_string(best_move, pos.flipped)
				<< std::endl;
			} else {
				break;
			}
			
			if (Clock::now() >= deadline) break;
			if (best_score >= MATE_SCORE - 100 || best_score <= -MATE_SCORE + 100) break;
			
			if (use_soft_limit) {
				const i32 idx = move_index(best_move);
				const double frac = total_nodes > 0
				? static_cast<double>(nodes_table[idx]) / static_cast<double>(total_nodes)
				: 0.0;
				const double factor = 2.0 - 1.5 * frac;
				const i64 elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
					Clock::now() - start).count();
				if (static_cast<double>(elapsed) > static_cast<double>(soft_time_ms) * factor)
					break;
			}
		}
		
		return best_move;
	}
