CXX      := g++
CXXFLAGS := -std=c++17 -O3 -march=native -flto -DNDEBUG
SRC      := main.cpp bitboard.cpp position.cpp movegen.cpp evaluation.cpp search.cpp
EXE      := hce

$(EXE): $(SRC)
	$(CXX) $(CXXFLAGS) $^ -o $@

clean:
	rm -f $(EXE)