#include "rules.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <map>

namespace mini7 {
namespace {

constexpr const char* START_FEN =
    "rchkhcr/p1ppp1p/7/7/7/P1PPP1P/RCHKHCR w - - 0 1";

int sign(int value) { return (value > 0) - (value < 0); }

std::vector<std::string> splitFields(const std::string& text) {
    std::vector<std::string> fields;
    size_t index = 0;
    while (index < text.size()) {
        while (index < text.size() && std::isspace(static_cast<unsigned char>(text[index]))) ++index;
        if (index >= text.size()) break;
        size_t end = index;
        while (end < text.size() && !std::isspace(static_cast<unsigned char>(text[end]))) ++end;
        fields.push_back(text.substr(index, end - index));
        index = end;
    }
    return fields;
}

std::pair<int, int> findKing(const Position& position, Side side) {
    const char wanted = side == Side::Red ? 'K' : 'k';
    for (int row = 0; row < N; ++row) {
        for (int col = 0; col < N; ++col) {
            if (position.at(row, col) == wanted) return {row, col};
        }
    }
    return {-1, -1};
}

void addIfTarget(const Position& position, int fr, int fc, int tr, int tc,
                 Side owner, std::vector<Move>& moves) {
    if (inside(tr, tc) && !belongsTo(position.at(tr, tc), owner)) {
        moves.push_back({fr, fc, tr, tc});
    }
}

std::vector<Move> rayMoves(const Position& position, int row, int col,
                           bool cannon) {
    std::vector<Move> result;
    const Side owner = isRed(position.at(row, col)) ? Side::Red : Side::Black;
    constexpr int directions[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
    for (const auto& direction : directions) {
        int r = row + direction[0], c = col + direction[1];
        bool screen = false;
        while (inside(r, c)) {
            const char target = position.at(r, c);
            if (!cannon) {
                if (target == EMPTY) {
                    result.push_back({row, col, r, c});
                } else {
                    if (!belongsTo(target, owner)) result.push_back({row, col, r, c});
                    break;
                }
            } else if (!screen) {
                if (target == EMPTY) {
                    result.push_back({row, col, r, c});
                } else {
                    screen = true;
                }
            } else if (target != EMPTY) {
                if (!belongsTo(target, owner)) result.push_back({row, col, r, c});
                break;
            }
            r += direction[0];
            c += direction[1];
        }
    }
    return result;
}

constexpr int HORSE_STEPS[8][4] = {
    {-2, -1, -1, 0}, {-2, 1, -1, 0}, {2, -1, 1, 0}, {2, 1, 1, 0},
    {-1, -2, 0, -1}, {1, -2, 0, -1}, {-1, 2, 0, 1}, {1, 2, 0, 1},
};

bool pieceAttacks(const Position& position, int row, int col, int targetRow,
                  int targetCol) {
    const char piece = position.at(row, col);
    if (piece == EMPTY) return false;
    const Side owner = isRed(piece) ? Side::Red : Side::Black;
    const char kind = static_cast<char>(std::toupper(static_cast<unsigned char>(piece)));

    if (kind == 'R' || kind == 'C') {
        if (row != targetRow && col != targetCol) return false;
        const int dr = sign(targetRow - row), dc = sign(targetCol - col);
        int blockers = 0;
        for (int r = row + dr, c = col + dc;
             r != targetRow || c != targetCol; r += dr, c += dc) {
            if (position.at(r, c) != EMPTY) ++blockers;
        }
        return blockers == (kind == 'C' ? 1 : 0);
    }

    if (kind == 'H') {
        for (const auto& step : HORSE_STEPS) {
            if (row + step[0] == targetRow && col + step[1] == targetCol) {
                return position.at(row + step[2], col + step[3]) == EMPTY;
            }
        }
        return false;
    }

    if (kind == 'P') {
        const int forward = owner == Side::Red ? -1 : 1;
        const int dr = targetRow - row, dc = targetCol - col;
        return (dr == forward && dc == 0) || (dr == 0 && (dc == -1 || dc == 1));
    }

    if (kind == 'K') {
        if (std::abs(targetRow - row) + std::abs(targetCol - col) == 1) return true;
        if (col == targetCol &&
            std::toupper(static_cast<unsigned char>(position.at(targetRow, targetCol))) == 'K') {
            const int lo = std::min(row, targetRow), hi = std::max(row, targetRow);
            for (int r = lo + 1; r < hi; ++r) {
                if (position.at(r, col) != EMPTY) return false;
            }
            return true;
        }
    }
    return false;
}

std::wstring pieceName(char piece) {
    switch (piece) {
        case 'R': case 'r': return L"车";
        case 'C': case 'c': return L"炮";
        case 'H': case 'h': return L"马";
        case 'K': return L"帅";
        case 'k': return L"将";
        case 'P': return L"兵";
        case 'p': return L"卒";
        default: return L"?";
    }
}

}  // namespace

Position initialPosition() {
    Position position;
    std::wstring error;
    parseFen(START_FEN, position, error);
    return position;
}

Side opposite(Side side) { return side == Side::Red ? Side::Black : Side::Red; }
bool isRed(char piece) { return piece >= 'A' && piece <= 'Z'; }
bool isBlack(char piece) { return piece >= 'a' && piece <= 'z'; }
bool belongsTo(char piece, Side side) {
    return side == Side::Red ? isRed(piece) : isBlack(piece);
}
bool inside(int row, int col) { return row >= 0 && row < N && col >= 0 && col < N; }

bool palaceContains(Side side, int row, int col) {
    if (col < 2 || col > 4) return false;
    return side == Side::Red ? row >= 4 && row <= 6 : row >= 0 && row <= 2;
}

std::string moveToUci(const Move& move) {
    std::string result;
    result += static_cast<char>('a' + move.fc);
    result += static_cast<char>('0' + (N - move.fr));
    result += static_cast<char>('a' + move.tc);
    result += static_cast<char>('0' + (N - move.tr));
    return result;
}

bool moveFromUci(const std::string& text, Move& move) {
    if (text.size() < 4) return false;
    const int fc = std::tolower(static_cast<unsigned char>(text[0])) - 'a';
    const int fr = N - (text[1] - '0');
    const int tc = std::tolower(static_cast<unsigned char>(text[2])) - 'a';
    const int tr = N - (text[3] - '0');
    if (!inside(fr, fc) || !inside(tr, tc)) return false;
    move = {fr, fc, tr, tc};
    return true;
}

std::wstring moveLabel(const Position& position, const Move& move) {
    const std::string uci = moveToUci(move);
    std::wstring squares;
    squares += static_cast<wchar_t>(uci[0]);
    squares += static_cast<wchar_t>(uci[1]);
    squares += L'–';
    squares += static_cast<wchar_t>(uci[2]);
    squares += static_cast<wchar_t>(uci[3]);
    return pieceName(position.at(move.fr, move.fc)) + L" " + squares;
}

std::string toFen(const Position& position) {
    std::string out;
    for (int row = 0; row < N; ++row) {
        int blanks = 0;
        for (int col = 0; col < N; ++col) {
            const char piece = position.at(row, col);
            if (piece == EMPTY) {
                ++blanks;
            } else {
                if (blanks) out += static_cast<char>('0' + blanks);
                blanks = 0;
                out += piece;
            }
        }
        if (blanks) out += static_cast<char>('0' + blanks);
        if (row + 1 < N) out += '/';
    }
    out += position.turn == Side::Red ? " w - - " : " b - - ";
    out += std::to_string(position.halfmove);
    out += ' ';
    out += std::to_string(position.fullmove);
    return out;
}


std::string positionKey(const Position& position) {
    const std::string fen = toFen(position);
    const size_t first = fen.find(' ');
    if (first == std::string::npos) return fen;
    const size_t second = fen.find(' ', first + 1);
    return second == std::string::npos ? fen : fen.substr(0, second);
}

bool parseFen(const std::string& fen, Position& out, std::wstring& error) {
    const auto fields = splitFields(fen);
    if (fields.size() < 2) {
        error = L"FEN 至少需要棋盘和行棋方";
        return false;
    }
    const std::string& field = fields[0];
    const std::string& side = fields[1];
    const int halfmove = fields.size() >= 5 ? std::atoi(fields[4].c_str()) : 0;
    const int fullmove = fields.size() >= 6 ? std::atoi(fields[5].c_str()) : 1;
    Position result;
    result.board.fill(EMPTY);
    int row = 0, col = 0;
    const std::string allowed = "RCHKPrchkp";
    for (char ch : field) {
        if (ch == '/') {
            if (col != N || ++row >= N) {
                error = L"FEN 每行必须恰好有 7 格";
                return false;
            }
            col = 0;
        } else if (ch >= '1' && ch <= '7') {
            col += ch - '0';
        } else if (allowed.find(ch) != std::string::npos) {
            if (col >= N || row >= N) {
                error = L"FEN 棋盘尺寸超过 7×7";
                return false;
            }
            result.at(row, col++) = ch;
        } else {
            error = L"FEN 含有未知棋子";
            return false;
        }
        if (col > N) {
            error = L"FEN 每行展开后超过 7 格";
            return false;
        }
    }
    if (row != N - 1 || col != N) {
        error = L"7×7 FEN 必须恰好有 7 行";
        return false;
    }
    if (side != "w" && side != "b") {
        error = L"FEN 行棋方只能是 w 或 b";
        return false;
    }
    result.turn = side == "w" ? Side::Red : Side::Black;
    result.halfmove = std::max(0, halfmove);
    result.fullmove = std::max(1, fullmove);
    out = result;
    return true;
}

std::vector<Move> pseudoMovesFor(const Position& position, int row, int col) {
    std::vector<Move> result;
    const char piece = position.at(row, col);
    if (piece == EMPTY) return result;
    const Side owner = isRed(piece) ? Side::Red : Side::Black;
    const char kind = static_cast<char>(std::toupper(static_cast<unsigned char>(piece)));

    if (kind == 'R') return rayMoves(position, row, col, false);
    if (kind == 'C') return rayMoves(position, row, col, true);
    if (kind == 'H') {
        for (const auto& step : HORSE_STEPS) {
            const int tr = row + step[0], tc = col + step[1];
            if (inside(tr, tc) && position.at(row + step[2], col + step[3]) == EMPTY) {
                addIfTarget(position, row, col, tr, tc, owner, result);
            }
        }
    } else if (kind == 'P') {
        const int forward = owner == Side::Red ? -1 : 1;
        addIfTarget(position, row, col, row + forward, col, owner, result);
        addIfTarget(position, row, col, row, col - 1, owner, result);
        addIfTarget(position, row, col, row, col + 1, owner, result);
    } else if (kind == 'K') {
        constexpr int directions[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
        for (const auto& direction : directions) {
            const int tr = row + direction[0], tc = col + direction[1];
            if (palaceContains(owner, tr, tc)) {
                addIfTarget(position, row, col, tr, tc, owner, result);
            }
        }
    }
    return result;
}

bool isInCheck(const Position& position, Side side) {
    const auto [kingRow, kingCol] = findKing(position, side);
    if (kingRow < 0) return true;
    const Side enemy = opposite(side);
    for (int row = 0; row < N; ++row) {
        for (int col = 0; col < N; ++col) {
            if (belongsTo(position.at(row, col), enemy) &&
                pieceAttacks(position, row, col, kingRow, kingCol)) {
                return true;
            }
        }
    }
    return false;
}

std::vector<Move> legalMoves(const Position& position) {
    std::vector<Move> result;
    if (findKing(position, Side::Red).first < 0 ||
        findKing(position, Side::Black).first < 0) return result;

    for (int row = 0; row < N; ++row) {
        for (int col = 0; col < N; ++col) {
            if (!belongsTo(position.at(row, col), position.turn)) continue;
            for (const Move& move : pseudoMovesFor(position, row, col)) {
                if (std::toupper(static_cast<unsigned char>(position.at(move.tr, move.tc))) == 'K') {
                    continue;
                }
                Position next = position;
                next.at(move.tr, move.tc) = next.at(move.fr, move.fc);
                next.at(move.fr, move.fc) = EMPTY;
                if (!isInCheck(next, position.turn)) result.push_back(move);
            }
        }
    }
    return result;
}


char movedPiece(const Position& before, const Move& move) {
    if (!inside(move.fr, move.fc)) return EMPTY;
    return before.at(move.fr, move.fc);
}

bool moveGivesCheck(const Position& before, const Move& move) {
    Position after = before;
    if (!applyMove(after, move, true)) return false;
    return isInCheck(after, after.turn);
}

std::vector<std::wstring> validate(const Position& position) {
    std::vector<std::wstring> errors;
    int redKings = 0, blackKings = 0;
    for (char piece : position.board) {
        redKings += piece == 'K';
        blackKings += piece == 'k';
    }
    if (redKings != 1) errors.push_back(L"红方必须恰好有一个帅");
    if (blackKings != 1) errors.push_back(L"黑方必须恰好有一个将");

    const auto red = findKing(position, Side::Red);
    const auto black = findKing(position, Side::Black);
    if (red.first >= 0 && !palaceContains(Side::Red, red.first, red.second)) {
        errors.push_back(L"红帅不能离开九宫");
    }
    if (black.first >= 0 && !palaceContains(Side::Black, black.first, black.second)) {
        errors.push_back(L"黑将不能离开九宫");
    }
    if (red.first >= 0 && black.first >= 0 && red.second == black.second) {
        bool blocked = false;
        for (int row = std::min(red.first, black.first) + 1;
             row < std::max(red.first, black.first); ++row) {
            if (position.at(row, red.second) != EMPTY) blocked = true;
        }
        if (!blocked) errors.push_back(L"将帅不能在同一路上无遮挡相对");
    }
    return errors;
}

Status status(const Position& position) {
    Status result;
    result.errors = validate(position);
    if (!result.errors.empty()) {
        result.valid = false;
        result.message = result.errors.front();
        return result;
    }
    result.check = isInCheck(position, position.turn);
    const auto moves = legalMoves(position);
    const std::wstring sideName = position.turn == Side::Red ? L"红方" : L"黑方";
    if (moves.empty()) {
        result.gameOver = true;
        result.winner = opposite(position.turn);
        result.reason = result.check ? EndReason::Checkmate : EndReason::Stalemate;
        result.message = result.check ? sideName + L"被将死" : sideName + L"无子可动，判负";
    } else {
        result.message = sideName + L"行棋" + (result.check ? L" · 将军" : L"");
    }
    return result;
}

namespace {

struct EventInfo {
    Side mover = Side::Red;
    char piece = EMPTY;
    bool check = false;
    bool chase = false;
    char chasedPiece = EMPTY;
};

std::vector<EventInfo> buildEvents(const std::vector<Position>& positions,
                                   const std::vector<Move>& moves) {
    std::vector<EventInfo> events;
    const size_t count = std::min(moves.size(), positions.size() > 0 ? positions.size() - 1 : 0);
    events.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        const Position& before = positions[i];
        EventInfo event{before.turn, movedPiece(before, moves[i]),
                        isInCheck(positions[i + 1], positions[i + 1].turn), false, EMPTY};
        // Conservative long-chase recognition for this reduced piece set: after a
        // non-checking move, the moved piece attacks an enemy rook/cannon/horse,
        // and the opponent's immediate reply moves that attacked piece away.
        // Kings and soldiers are deliberately excluded to avoid false penalties.
        if (!event.check && i + 1 < count) {
            const Position& after = positions[i + 1];
            const Move& reply = moves[i + 1];
            const char victim = after.at(reply.fr, reply.fc);
            const char victimKind = static_cast<char>(std::toupper(static_cast<unsigned char>(victim)));
            if (belongsTo(victim, after.turn) &&
                (victimKind == 'R' || victimKind == 'C' || victimKind == 'H') &&
                pieceAttacks(after, moves[i].tr, moves[i].tc, reply.fr, reply.fc)) {
                event.chase = true;
                event.chasedPiece = victim;
            }
        }
        events.push_back(event);
    }
    return events;
}

const wchar_t* sideText(Side side) { return side == Side::Red ? L"红方" : L"黑方"; }

}  // namespace

Status statusWithHistory(const std::vector<Position>& positions,
                         const std::vector<Move>& moves,
                         RuleProfile profile) {
    if (positions.empty()) {
        Status invalid;
        invalid.valid = false;
        invalid.message = L"棋局历史为空";
        return invalid;
    }
    Status result = status(positions.back());
    if (!result.valid || result.gameOver) return result;

    const auto events = buildEvents(positions, moves);

    // Custom strict rule requested for this variant: on three consecutive turns,
    // if the same side uses a rook to check every time, that side loses immediately.
    if (profile.consecutiveRookChecksToLose > 0) {
        for (Side side : {Side::Red, Side::Black}) {
            int streak = 0;
            for (size_t i = events.size(); i-- > 0;) {
                if (events[i].mover != side) continue;
                const char kind = static_cast<char>(std::toupper(static_cast<unsigned char>(events[i].piece)));
                if (events[i].check && kind == 'R') ++streak;
                else break;
                if (streak >= profile.consecutiveRookChecksToLose) {
                    result.gameOver = true;
                    result.winner = opposite(side);
                    result.reason = EndReason::RookPerpetualCheck;
                    result.message = std::wstring(sideText(side)) + L"连续三次用车将军，依七路棋规判负";
                    return result;
                }
            }
        }
    }

    // Locate the last N occurrences of the current board+side key.
    const std::string key = positionKey(positions.back());
    std::vector<size_t> occurrences;
    for (size_t i = 0; i < positions.size(); ++i) {
        if (positionKey(positions[i]) == key) occurrences.push_back(i);
    }
    if (profile.repetitionCount > 1 &&
        occurrences.size() >= static_cast<size_t>(profile.repetitionCount)) {
        const size_t cycleBegin = occurrences[occurrences.size() - profile.repetitionCount];
        const size_t cycleEnd = positions.size() - 1;
        bool allCheck[2] = {true, true};
        int moveCount[2] = {0, 0};
        for (size_t i = cycleBegin; i < cycleEnd && i < events.size(); ++i) {
            const int side = events[i].mover == Side::Red ? 0 : 1;
            ++moveCount[side];
            allCheck[side] = allCheck[side] && events[i].check;
        }
        const bool redLongCheck = moveCount[0] >= 2 && allCheck[0];
        const bool blackLongCheck = moveCount[1] >= 2 && allCheck[1];
        if (redLongCheck != blackLongCheck) {
            const Side offender = redLongCheck ? Side::Red : Side::Black;
            result.gameOver = true;
            result.winner = opposite(offender);
            result.reason = EndReason::PerpetualCheck;
            result.message = std::wstring(sideText(offender)) + L"单方长将形成三次循环，判负";
            return result;
        }

        bool allChase[2] = {true, true};
        int chaseCount[2] = {0, 0};
        char chasedKind[2] = {EMPTY, EMPTY};
        for (size_t i = cycleBegin; i < cycleEnd && i < events.size(); ++i) {
            const int side = events[i].mover == Side::Red ? 0 : 1;
            if (!events[i].chase) {
                allChase[side] = false;
                continue;
            }
            const char kind = static_cast<char>(std::toupper(static_cast<unsigned char>(events[i].chasedPiece)));
            if (chasedKind[side] == EMPTY) chasedKind[side] = kind;
            else if (chasedKind[side] != kind) allChase[side] = false;
            ++chaseCount[side];
        }
        const bool redLongChase = moveCount[0] >= 2 && chaseCount[0] == moveCount[0] && allChase[0];
        const bool blackLongChase = moveCount[1] >= 2 && chaseCount[1] == moveCount[1] && allChase[1];
        if (redLongChase != blackLongChase) {
            const Side offender = redLongChase ? Side::Red : Side::Black;
            result.gameOver = true;
            result.winner = opposite(offender);
            result.reason = EndReason::PerpetualChase;
            result.message = std::wstring(sideText(offender)) + L"单方长捉同类强子形成三次循环，判负";
            return result;
        }
        result.gameOver = true;
        result.draw = true;
        result.reason = EndReason::RepetitionDraw;
        result.message = L"三次重复局面，双方均无单方长将，判和";
        return result;
    }

    if (profile.naturalDrawHalfmoves > 0 &&
        positions.back().halfmove >= profile.naturalDrawHalfmoves) {
        result.gameOver = true;
        result.draw = true;
        result.reason = EndReason::NaturalMoveDraw;
        result.message = L"连续六十回合无吃子且无兵卒移动，判和";
        return result;
    }
    return result;
}

bool wouldLoseByRule(const std::vector<Position>& positions,
                     const std::vector<Move>& moves,
                     const Move& candidate,
                     RuleProfile profile) {
    if (positions.empty()) return true;
    Position next = positions.back();
    const Side mover = next.turn;
    if (!applyMove(next, candidate, true)) return true;
    auto p = positions;
    auto m = moves;
    p.push_back(next);
    m.push_back(candidate);
    const Status result = statusWithHistory(p, m, profile);
    return result.gameOver && !result.draw && result.winner == opposite(mover);
}

bool applyMove(Position& position, const Move& move, bool requireLegal) {
    if (!inside(move.fr, move.fc) || !inside(move.tr, move.tc)) return false;
    if (requireLegal) {
        const auto moves = legalMoves(position);
        if (std::find(moves.begin(), moves.end(), move) == moves.end()) return false;
    }
    const char moving = position.at(move.fr, move.fc);
    const char captured = position.at(move.tr, move.tc);
    if (moving == EMPTY) return false;
    position.at(move.tr, move.tc) = moving;
    position.at(move.fr, move.fc) = EMPTY;
    position.halfmove = (std::toupper(static_cast<unsigned char>(moving)) == 'P' || captured != EMPTY)
                            ? 0
                            : position.halfmove + 1;
    if (position.turn == Side::Black) ++position.fullmove;
    position.turn = opposite(position.turn);
    return true;
}

}  // namespace mini7
