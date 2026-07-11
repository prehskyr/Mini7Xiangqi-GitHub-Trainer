#include "../src/rules.h"

#include <algorithm>
#include <cassert>
#include <iostream>
#include <set>

using namespace mini7;

std::set<std::string> moveSet(const Position& position) {
    std::set<std::string> result;
    for (const auto& move : legalMoves(position)) result.insert(moveToUci(move));
    return result;
}

Position fen(const std::string& text) {
    Position position;
    std::wstring error;
    assert(parseFen(text, position, error));
    return position;
}

void append(std::vector<Position>& positions, std::vector<Move>& moves, const char* uci) {
    Move move;
    assert(moveFromUci(uci, move));
    Position next = positions.back();
    if (!applyMove(next, move)) { std::cerr << "failed " << uci << " from " << toFen(positions.back()) << "\n"; for (auto x: legalMoves(positions.back())) std::cerr << moveToUci(x) << " "; std::cerr << "\n"; std::abort(); }
    moves.push_back(move);
    positions.push_back(next);
}

int main() {
    Position start = initialPosition();
    assert(toFen(start) == "rchkhcr/p1ppp1p/7/7/7/P1PPP1P/RCHKHCR w - - 0 1");
    assert(legalMoves(start).size() == 19);
    assert(validate(start).empty());

    auto soldier = moveSet(fen("3k3/7/7/3P3/7/2P4/2K4 w - - 0 1"));
    assert(soldier.contains("c2c3"));
    assert(soldier.contains("c2b2"));
    assert(soldier.contains("c2d2"));
    assert(!soldier.contains("c2c1"));

    auto blackSoldier = moveSet(fen("3k3/2p4/7/7/7/7/2K4 b - - 0 1"));
    assert(blackSoldier.contains("c6c5"));
    assert(blackSoldier.contains("c6b6"));
    assert(blackSoldier.contains("c6d6"));
    assert(!blackSoldier.contains("c6c7"));

    auto blackKingEdge = moveSet(fen("2k4/7/7/6P/7/7/4K2 b - - 0 1"));
    assert(!blackKingEdge.contains("c7b7"));
    assert(blackKingEdge.contains("c7d7"));
    assert(blackKingEdge.contains("c7c6"));

    auto horse = moveSet(fen("3k3/7/7/3P3/7/2P4/2HK3 w - - 0 1"));
    assert(!horse.contains("c1b3"));
    assert(!horse.contains("c1d3"));

    auto cannon = moveSet(fen("3k3/3r3/7/3P3/7/3C3/2K4 w - - 0 1"));
    assert(cannon.contains("d2d6"));
    assert(!cannon.contains("d2d7"));

    Position face = fen("3k3/7/7/7/7/7/3K3 w - - 0 1");
    assert(!validate(face).empty());
    assert(isInCheck(face, Side::Red));
    assert(isInCheck(face, Side::Black));

    Move move;
    assert(moveFromUci("a2a3", move));
    assert(applyMove(start, move));
    assert(start.turn == Side::Black);
    assert(start.at(4, 0) == 'P');

    // Three consecutive rook checks by the same side, with legal black replies.
    // Red rook toggles a6/b6 checking the black king on d7 along rank 7? Build a
    // direct controlled sequence and verify the history adjudicator.
    std::vector<Position> ps{fen("3k3/R6/7/7/7/6p/2K4 w - - 0 1")};
    std::vector<Move> ms;
    append(ps, ms, "a6d6"); // rook checks vertically? d6-d7 adjacent
    append(ps, ms, "d7e7");
    append(ps, ms, "d6e6");
    append(ps, ms, "e7d7");
    append(ps, ms, "e6d6");
    Status strict = statusWithHistory(ps, ms, RuleProfile{3, 3, 120});
    assert(strict.gameOver && strict.reason == EndReason::RookPerpetualCheck);
    assert(strict.winner == Side::Black);

    // A harmless horse shuttle reaches the same board and side three times.
    // Neither side checks or chases, so it is a repetition draw.
    std::vector<Position> rp{fen("h1k4/7/7/2P4/7/7/H1K4 w - - 0 1")};
    std::vector<Move> rm;
    for (int repeat = 0; repeat < 2; ++repeat) {
        append(rp, rm, "a1b3");
        append(rp, rm, "a7b5");
        append(rp, rm, "b3a1");
        append(rp, rm, "b5a7");
    }
    Status repetition = statusWithHistory(rp, rm);
    assert(repetition.gameOver && repetition.draw);
    assert(repetition.reason == EndReason::RepetitionDraw);

    std::vector<Position> natural{fen("3k3/7/7/7/7/7/2K4 w - - 120 61")};
    Status naturalDraw = statusWithHistory(natural, {});
    assert(naturalDraw.gameOver && naturalDraw.draw);
    assert(naturalDraw.reason == EndReason::NaturalMoveDraw);

    std::cout << "all native rule tests passed\n";
}
