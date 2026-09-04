#include "types.h"
#include "bitboard.h"
#include "position.h"
#include "movegen.h"
#include "search.h"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <sstream>
#include <string>

namespace {
	constexpr i32 MAX_HISTORY = 512;
	u64 g_history[MAX_HISTORY];
	i32 g_history_size = 0;

	inline void push_history(u64 key) {
		if (g_history_size < MAX_HISTORY) g_history[g_history_size++] = key;
	}

	Move move_from_uci(Position& pos, const std::string& str) {
		Move movelist[256];
		const i32 n = generate_moves(pos, movelist, MoveGenType::All);
		for (i32 i = 0; i < n; ++i)
			if (move_to_string(movelist[i], pos.flipped) == str) return movelist[i];
		return NullMove;
	}

	void handle_position(Position& pos, std::istringstream& ss) {
		std::string token;
		ss >> token;
		g_history_size = 0;

		if (token == "startpos") {
			pos = Position();
			push_history(pos.key());
			ss >> token;
		} else if (token == "fen") {
			std::string fen;
			bool first = true;
			while (ss >> token && token != "moves") {
				if (!first) fen += ' ';
				fen += token;
				first = false;
			}
			pos.set_fen(fen);
			push_history(pos.key());
		}

		if (token == "moves") {
			while (ss >> token) {
				const Move mv = move_from_uci(pos, token);
				if (!mv.is_none()) {
					pos.make_move(mv);
					push_history(pos.key());
				}
			}
		}
	}

	void handle_setoption(std::istringstream& ss) {
		std::string token, name;
		ss >> token; // "name"
		while (ss >> token && token != "value") {
			if (!name.empty()) name += ' ';
			name += token;
		}

		if (name == "Hash") {
			std::size_t mib = 64;
			if (token == "value") ss >> mib;
			resize_tt(std::max<std::size_t>(1, std::min<std::size_t>(65536, mib)));
		} else if (name == "Clear Hash") {
			clear_tt();
		}
	}

	void handle_go(Position& pos, std::istringstream& ss) {
		i32 depth = 0;
		i64 movetime = -1;
		i64 wtime = -1, btime = -1, winc = 0, binc = 0;

		std::string tok;
		while (ss >> tok) {
			if      (tok == "depth")    ss >> depth;
			else if (tok == "movetime") ss >> movetime;
			else if (tok == "wtime")    ss >> wtime;
			else if (tok == "btime")    ss >> btime;
			else if (tok == "winc")     ss >> winc;
			else if (tok == "binc")     ss >> binc;
		}

		const i32 max_depth = depth > 0 ? depth : 64;
		i64 max_time_ms;
		if (movetime > 0) {
			max_time_ms = movetime;
		} else if (depth > 0) {
			max_time_ms = static_cast<i64>(1) << 40;
		} else {
			const i64 my_time = pos.flipped ? btime : wtime;
			const i64 my_inc  = pos.flipped ? binc  : winc;
			max_time_ms = my_time / 25 + my_inc / 2;
		}

		u64 nodes = 0;
		const Move best = search(pos, max_depth, max_time_ms, nodes, g_history, g_history_size);
		std::cout << "bestmove " << move_to_string(best, pos.flipped) << '\n';
	}
} // namespace

int main() {
	BB::init();
	Zobrist::init();
	resize_tt(64);

	Position pos;
	g_history_size = 0;
	push_history(pos.key());
	std::string line;

	while (std::getline(std::cin, line)) {
		std::istringstream ss(line);
		std::string cmd;
		ss >> cmd;

		if (cmd == "quit") {
			break;
		} else if (cmd == "uci") {
			std::cout << "option name Hash type spin default 64 min 1 max 65536\n"
				<< "option name Clear Hash type button\n"
				<< "uciok\n";
		} else if (cmd == "isready") {
			std::cout << "readyok\n";
		} else if (cmd == "setoption") {
			handle_setoption(ss);
		} else if (cmd == "ucinewgame") {
			clear_tt();
			pos = Position();
			g_history_size = 0;
			push_history(pos.key());
		} else if (cmd == "position") {
			handle_position(pos, ss);
		} else if (cmd == "go") {
			handle_go(pos, ss);
		} else if (cmd == "perft") {
			i32 d = 0;
			ss >> d;
			const auto t0 = std::chrono::steady_clock::now();
			const u64 nodes = perft(pos, d);
			const i64 ms = std::max<i64>(1,
				std::chrono::duration_cast<std::chrono::milliseconds>(
					std::chrono::steady_clock::now() - t0).count());
			std::cout << "nodes " << nodes
				<< " time " << ms
				<< " nps " << nodes * 1000ULL / static_cast<u64>(ms) << '\n';
		} else if (cmd == "divide") {
			i32 d = 0;
			ss >> d;
			perft_divide(pos, d);
		}
	}

	return 0;
}
