#pragma once

#include <array>
#include <string>
#include <vector>

namespace mini7 {

constexpr int N = 7;
constexpr char EMPTY = '.';

enum class Side { Red, Black };

struct Move {
    int fr = 0, fc = 0, tr = 0, tc = 0;
    bool operator==(const Move&) const = default;
};

struct Position {
    std::array<char, N * N> board{};
    Side turn = Side::Red;
    int halfmove = 0;
    int fullmove = 1;

    char at(int row, int col) const { return board[row * N + col]; }
    char& at(int row, int col) { return board[row * N + col]; }
};

enum class EndReason {
    None,
    Checkmate,
    Stalemate,
    RookPerpetualCheck,
    PerpetualCheck,
    PerpetualChase,
    RepetitionDraw,
    NaturalMoveDraw,
};

struct Status {
    bool valid = true;
    bool check = false;
    bool gameOver = false;
    bool draw = false;
    Side winner = Side::Red;
    EndReason reason = EndReason::None;
    std::wstring message;
    std::vector<std::wstring> errors;
};

// positions[0] is the initial position and positions[i + 1] is the position
// after moves[i]. The function adjudicates the custom Chinese-rule profile:
// - three consecutive rook checks by one side: checking side loses;
// - unilateral perpetual check in a threefold cycle: checking side loses;
// - unilateral repeated chase of the same rook/cannon/horse through a cycle: chasing side loses;
// - other threefold repetitions: draw;
// - 120 plies without capture or soldier move: draw.
struct RuleProfile {
    int consecutiveRookChecksToLose = 3;
    int repetitionCount = 3;
    int naturalDrawHalfmoves = 120;
};

Position initialPosition();
Side opposite(Side side);
bool isRed(char piece);
bool isBlack(char piece);
bool belongsTo(char piece, Side side);
bool inside(int row, int col);
bool palaceContains(Side side, int row, int col);

std::string moveToUci(const Move& move);
bool moveFromUci(const std::string& text, Move& move);
std::wstring moveLabel(const Position& position, const Move& move);

std::string toFen(const Position& position);
std::string positionKey(const Position& position);
bool parseFen(const std::string& fen, Position& out, std::wstring& error);

std::vector<Move> pseudoMovesFor(const Position& position, int row, int col);
std::vector<Move> legalMoves(const Position& position);
bool isInCheck(const Position& position, Side side);
bool moveGivesCheck(const Position& before, const Move& move);
char movedPiece(const Position& before, const Move& move);
std::vector<std::wstring> validate(const Position& position);
Status status(const Position& position);
Status statusWithHistory(const std::vector<Position>& positions,
                         const std::vector<Move>& moves,
                         RuleProfile profile = {});
bool wouldLoseByRule(const std::vector<Position>& positions,
                     const std::vector<Move>& moves,
                     const Move& candidate,
                     RuleProfile profile = {});
bool applyMove(Position& position, const Move& move, bool requireLegal = true);

}  // namespace mini7
