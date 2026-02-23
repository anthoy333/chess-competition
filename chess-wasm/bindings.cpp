#include <emscripten/bind.h>
#include "chess-simulator.h"
#include <string>

// SFINAE: T must be a dependent template parameter so that
// invalid expressions are a substitution failure, not a hard error.

// Preferred overload (int): two-arg Move(fen, timeLimitMs) — new signature
template <typename T = ChessSimulator>
auto call_move(const std::string& fen, int timeLimitMs, int)
    -> decltype(T::Move(fen, timeLimitMs)) {
    return T::Move(fen, timeLimitMs);
}

// Fallback overload (long): one-arg Move(fen) — old forks without timeLimitMs
template <typename T = ChessSimulator>
auto call_move(const std::string& fen, int /*timeLimitMs*/, long)
    -> decltype(T::Move(fen)) {
    return T::Move(fen);
}

std::string safe_move(const std::string& fen, int timeLimitMs) {
    return call_move(fen, timeLimitMs, 0);
}

EMSCRIPTEN_BINDINGS(chess_module) {
    emscripten::function("move", &safe_move);
}
