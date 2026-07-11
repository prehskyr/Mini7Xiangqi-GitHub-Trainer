#include "mini7net.h"
#include "rules.h"
#include "uci_process.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <random>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

namespace {

fs::path pathFromUtf8(const std::string& text) {
    return fs::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}

std::atomic<bool> gStop{false};

void onSignal(int) { gStop.store(true); }

struct Options {
    std::string engine = "engine/fairy-stockfish.exe";
    std::string variants = "config/variants.ini";
    std::string model = "models/mini7-current.m7nnue";
    std::string gameLog = "logs/games.jsonl";
    std::string summary = "state/latest.json";
    int durationSeconds = 20400;
    int maxGames = 1000000;
    int maxPlies = 240;
    int movetimeMs = 120;
    int threads = 4;
    int hashMb = 1024;
    int multiPv = 5;
    int netBlendPercent = 20;
    int checkpointPlies = 20;
    float learningRate = 0.002f;
    std::uint64_t seed = 0;
    std::uint64_t maxTrainingSteps = 0;
};

struct RunStats {
    std::uint64_t startingSteps = 0;
    std::uint64_t endingSteps = 0;
    std::uint64_t teacherUpdates = 0;
    std::uint64_t outcomeUpdates = 0;
    int gamesCompleted = 0;
    int draws = 0;
    int redWins = 0;
    int blackWins = 0;
    int currentGame = 0;
    int currentPly = 0;
    std::string stopReason = "completed";
};

std::string jsonEscape(const std::string& text) {
    std::string out;
    out.reserve(text.size() + 16);
    for (unsigned char ch : text) {
        switch (ch) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (ch < 0x20) {
                    std::ostringstream hex;
                    hex << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(ch);
                    out += hex.str();
                } else {
                    out.push_back(static_cast<char>(ch));
                }
        }
    }
    return out;
}

std::string narrow(const std::wstring& text) {
    std::string out;
    out.reserve(text.size());
    for (wchar_t ch : text) out.push_back(ch >= 0 && ch <= 0x7f ? static_cast<char>(ch) : '?');
    return out;
}

void usage() {
    std::cout << R"(Mini7TrainerCLI - headless 7x7 Xiangqi self-play trainer

Required runtime files:
  engine/fairy-stockfish.exe
  config/variants.ini

Options:
  --engine PATH                 Fairy-Stockfish executable
  --variants PATH               variant configuration
  --model PATH                  load/save .m7nnue checkpoint
  --game-log PATH               append completed games as JSONL
  --summary PATH                rewrite latest run state as JSON
  --duration-seconds N          wall-clock training limit (default 20400)
  --max-games N                 maximum completed games
  --max-plies N                 maximum plies per game
  --movetime-ms N               Fairy-Stockfish time per move
  --threads N                   Fairy-Stockfish Threads
  --hash-mb N                   Fairy-Stockfish Hash in MB
  --multipv N                   teacher candidates, 1..5
  --net-blend-percent N         client-network move-selection weight, 0..100
  --learning-rate X             SGD learning rate
  --checkpoint-plies N          save model every N plies
  --seed N                      RNG seed; 0 uses clock/random_device
  --max-training-steps N        stop after N new updates; 0 means unlimited
  --help                        show this help
)";
}

template<class T>
bool parseInteger(const std::string& text, T& value) {
    try {
        size_t used = 0;
        if constexpr (std::is_unsigned_v<T>) {
            const auto parsed = std::stoull(text, &used, 10);
            if (used != text.size()) return false;
            value = static_cast<T>(parsed);
        } else {
            const auto parsed = std::stoll(text, &used, 10);
            if (used != text.size()) return false;
            value = static_cast<T>(parsed);
        }
        return true;
    } catch (...) {
        return false;
    }
}

bool parseFloat(const std::string& text, float& value) {
    try {
        size_t used = 0;
        value = std::stof(text, &used);
        return used == text.size() && std::isfinite(value);
    } catch (...) {
        return false;
    }
}

bool parseArgs(int argc, char** argv, Options& options, std::string& error) {
    auto next = [&](int& index, const std::string& name) -> const char* {
        if (index + 1 >= argc) {
            error = "missing value for " + name;
            return nullptr;
        }
        return argv[++index];
    };

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            usage();
            std::exit(0);
        }
        const char* value = next(i, arg);
        if (!value) return false;
        const std::string text = value;

        if (arg == "--engine") options.engine = text;
        else if (arg == "--variants") options.variants = text;
        else if (arg == "--model") options.model = text;
        else if (arg == "--game-log") options.gameLog = text;
        else if (arg == "--summary") options.summary = text;
        else if (arg == "--duration-seconds") { if (!parseInteger(text, options.durationSeconds)) return error = "invalid duration", false; }
        else if (arg == "--max-games") { if (!parseInteger(text, options.maxGames)) return error = "invalid max-games", false; }
        else if (arg == "--max-plies") { if (!parseInteger(text, options.maxPlies)) return error = "invalid max-plies", false; }
        else if (arg == "--movetime-ms") { if (!parseInteger(text, options.movetimeMs)) return error = "invalid movetime-ms", false; }
        else if (arg == "--threads") { if (!parseInteger(text, options.threads)) return error = "invalid threads", false; }
        else if (arg == "--hash-mb") { if (!parseInteger(text, options.hashMb)) return error = "invalid hash-mb", false; }
        else if (arg == "--multipv") { if (!parseInteger(text, options.multiPv)) return error = "invalid multipv", false; }
        else if (arg == "--net-blend-percent") { if (!parseInteger(text, options.netBlendPercent)) return error = "invalid net-blend-percent", false; }
        else if (arg == "--checkpoint-plies") { if (!parseInteger(text, options.checkpointPlies)) return error = "invalid checkpoint-plies", false; }
        else if (arg == "--learning-rate") { if (!parseFloat(text, options.learningRate)) return error = "invalid learning-rate", false; }
        else if (arg == "--seed") { if (!parseInteger(text, options.seed)) return error = "invalid seed", false; }
        else if (arg == "--max-training-steps") { if (!parseInteger(text, options.maxTrainingSteps)) return error = "invalid max-training-steps", false; }
        else {
            error = "unknown option: " + arg;
            return false;
        }
    }

    options.durationSeconds = std::clamp(options.durationSeconds, 1, 20700);
    options.maxGames = std::clamp(options.maxGames, 1, 100000000);
    options.maxPlies = std::clamp(options.maxPlies, 20, 2000);
    options.movetimeMs = std::clamp(options.movetimeMs, 20, 600000);
    options.threads = std::clamp(options.threads, 1, 256);
    options.hashMb = std::clamp(options.hashMb, 1, 65536);
    options.multiPv = std::clamp(options.multiPv, 1, 5);
    options.netBlendPercent = std::clamp(options.netBlendPercent, 0, 100);
    options.checkpointPlies = std::clamp(options.checkpointPlies, 1, 10000);
    options.learningRate = std::clamp(options.learningRate, 1e-6f, 0.1f);
    return true;
}

bool ensureParent(const fs::path& path, std::string& error) {
    std::error_code ec;
    const fs::path parent = path.parent_path();
    if (!parent.empty()) fs::create_directories(parent, ec);
    if (ec) {
        error = "cannot create directory " + parent.string() + ": " + ec.message();
        return false;
    }
    return true;
}

bool saveModel(mini7::SparseNet& net, const fs::path& path, std::string& error) {
    if (!ensureParent(path, error)) return false;
    std::wstring wideError;
    if (!net.save(path.wstring(), wideError)) {
        error = narrow(wideError);
        return false;
    }
    return true;
}

void writeSummary(const fs::path& path,
                  const Options& options,
                  const RunStats& stats,
                  int elapsedSeconds,
                  bool final) {
    std::string ignored;
    if (!ensureParent(path, ignored)) return;
    const fs::path temp = path.string() + ".tmp";
    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    if (!out) return;
    out << "{\n"
        << "  \"final\": " << (final ? "true" : "false") << ",\n"
        << "  \"stop_reason\": \"" << jsonEscape(stats.stopReason) << "\",\n"
        << "  \"elapsed_seconds\": " << elapsedSeconds << ",\n"
        << "  \"configured_duration_seconds\": " << options.durationSeconds << ",\n"
        << "  \"games_completed\": " << stats.gamesCompleted << ",\n"
        << "  \"red_wins\": " << stats.redWins << ",\n"
        << "  \"black_wins\": " << stats.blackWins << ",\n"
        << "  \"draws\": " << stats.draws << ",\n"
        << "  \"current_game\": " << stats.currentGame << ",\n"
        << "  \"current_ply\": " << stats.currentPly << ",\n"
        << "  \"starting_steps\": " << stats.startingSteps << ",\n"
        << "  \"ending_steps\": " << stats.endingSteps << ",\n"
        << "  \"teacher_updates\": " << stats.teacherUpdates << ",\n"
        << "  \"outcome_updates\": " << stats.outcomeUpdates << ",\n"
        << "  \"threads\": " << options.threads << ",\n"
        << "  \"hash_mb\": " << options.hashMb << ",\n"
        << "  \"movetime_ms\": " << options.movetimeMs << ",\n"
        << "  \"max_plies\": " << options.maxPlies << ",\n"
        << "  \"multipv\": " << options.multiPv << ",\n"
        << "  \"net_blend_percent\": " << options.netBlendPercent << ",\n"
        << "  \"learning_rate\": " << std::setprecision(8) << options.learningRate << "\n"
        << "}\n";
    out.close();
    std::error_code ec;
    fs::remove(path, ec);
    ec.clear();
    fs::rename(temp, path, ec);
}

void appendGameLog(const fs::path& path,
                   int game,
                   int plies,
                   const mini7::Status& status,
                   std::uint64_t steps) {
    std::string ignored;
    if (!ensureParent(path, ignored)) return;
    std::ofstream out(path, std::ios::binary | std::ios::app);
    if (!out) return;
    const char* winner = status.draw ? "draw" : status.winner == mini7::Side::Red ? "red" : "black";
    out << "{\"game\":" << game
        << ",\"plies\":" << plies
        << ",\"draw\":" << (status.draw ? "true" : "false")
        << ",\"winner\":\"" << winner << "\""
        << ",\"steps\":" << steps
        << ",\"reason\":\"" << jsonEscape(narrow(status.message)) << "\"}\n";
}

std::uint64_t chooseSeed(std::uint64_t configured) {
    if (configured != 0) return configured;
    std::random_device device;
    const auto now = static_cast<std::uint64_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count());
    return now ^ (static_cast<std::uint64_t>(device()) << 32) ^ device();
}

bool deadlineReached(const Clock::time_point& deadline,
                     const RunStats& stats,
                     const Options& options) {
    if (gStop.load()) return true;
    if (Clock::now() >= deadline) return true;
    if (options.maxTrainingSteps > 0 &&
        stats.endingSteps - stats.startingSteps >= options.maxTrainingSteps) return true;
    return false;
}

}  // namespace

int main(int argc, char** argv) {
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    Options options;
    std::string error;
    if (!parseArgs(argc, argv, options, error)) {
        std::cerr << "argument error: " << error << "\n";
        usage();
        return 2;
    }

    options.engine = fs::absolute(pathFromUtf8(options.engine)).string();
    options.variants = fs::absolute(pathFromUtf8(options.variants)).string();
    options.model = fs::absolute(pathFromUtf8(options.model)).string();
    options.gameLog = fs::absolute(pathFromUtf8(options.gameLog)).string();
    options.summary = fs::absolute(pathFromUtf8(options.summary)).string();

    if (!fs::exists(pathFromUtf8(options.engine))) {
        std::cerr << "engine not found: " << options.engine << "\n";
        return 3;
    }
    if (!fs::exists(pathFromUtf8(options.variants))) {
        std::cerr << "variants file not found: " << options.variants << "\n";
        return 3;
    }

    mini7::SparseNet net;
    if (fs::exists(pathFromUtf8(options.model))) {
        std::wstring loadError;
        if (!net.load(pathFromUtf8(options.model).wstring(), loadError)) {
            std::cerr << "model load failed: " << narrow(loadError) << "\n";
            return 4;
        }
        std::cout << "loaded checkpoint: " << options.model << " (steps=" << net.steps() << ")\n";
    } else {
        std::cout << "no checkpoint found; starting a new model\n";
    }

    mini7::UciProcess engine;
    if (!engine.start(options.engine, options.variants, options.threads, options.hashMb, error)) {
        std::cerr << "engine startup failed: " << error << "\n";
        return 5;
    }

    RunStats stats;
    stats.startingSteps = net.steps();
    stats.endingSteps = net.steps();
    const std::uint64_t seed = chooseSeed(options.seed);
    std::mt19937_64 rng(seed);
    const auto started = Clock::now();
    const auto deadline = started + std::chrono::seconds(options.durationSeconds);
    auto lastProgress = started;
    std::uint64_t pliesSinceCheckpoint = 0;

    std::cout << "training start: duration=" << options.durationSeconds
              << "s threads=" << options.threads
              << " hash=" << options.hashMb
              << "MB movetime=" << options.movetimeMs
              << "ms seed=" << seed << "\n";

    for (int game = 1; game <= options.maxGames; ++game) {
        if (deadlineReached(deadline, stats, options)) break;
        stats.currentGame = game;
        stats.currentPly = 0;

        std::vector<mini7::Position> positions{mini7::initialPosition()};
        std::vector<mini7::Move> moves;
        mini7::Status finalStatus;
        bool gameCompleted = false;

        for (int ply = 0; ply < options.maxPlies; ++ply) {
            stats.currentPly = ply;
            if (deadlineReached(deadline, stats, options)) break;

            finalStatus = mini7::statusWithHistory(positions, moves);
            if (finalStatus.gameOver) {
                gameCompleted = true;
                break;
            }

            std::vector<mini7::SearchLine> lines;
            if (!engine.search(positions.back(), options.movetimeMs, options.multiPv,
                               lines, error, gStop)) {
                if (gStop.load()) break;
                std::cerr << "search failed at game " << game << " ply " << ply << ": " << error << "\n";
                stats.stopReason = "engine_error";
                saveModel(net, pathFromUtf8(options.model), error);
                stats.endingSteps = net.steps();
                writeSummary(pathFromUtf8(options.summary), options, stats,
                             static_cast<int>(std::chrono::duration_cast<std::chrono::seconds>(Clock::now() - started).count()), true);
                return 6;
            }

            struct Candidate {
                mini7::Move move;
                int engineScore = 0;
                int netScore = 0;
                int blended = 0;
            };
            std::vector<Candidate> candidates;
            const auto legal = mini7::legalMoves(positions.back());
            const bool redToMove = positions.back().turn == mini7::Side::Red;
            int teacherTarget = redToMove ? std::numeric_limits<int>::min() : std::numeric_limits<int>::max();
            bool hasTeacher = false;

            for (const auto& line : lines) {
                mini7::Move move;
                if (!mini7::moveFromUci(line.move, move)) continue;
                if (std::find(legal.begin(), legal.end(), move) == legal.end()) continue;
                if (mini7::wouldLoseByRule(positions, moves, move)) continue;

                mini7::Position child = positions.back();
                if (!mini7::applyMove(child, move)) continue;
                const int netScore = net.evaluateCp(child);
                const int blended = (line.scoreRed * (100 - options.netBlendPercent) +
                                     netScore * options.netBlendPercent) / 100;
                candidates.push_back({move, line.scoreRed, netScore, blended});
                teacherTarget = redToMove ? std::max(teacherTarget, line.scoreRed)
                                          : std::min(teacherTarget, line.scoreRed);
                hasTeacher = true;
            }

            if (candidates.empty()) {
                for (const auto& move : legal) {
                    if (mini7::wouldLoseByRule(positions, moves, move)) continue;
                    mini7::Position child = positions.back();
                    if (!mini7::applyMove(child, move)) continue;
                    const int netScore = net.evaluateCp(child);
                    candidates.push_back({move, 0, netScore, netScore});
                }
            }
            if (candidates.empty()) {
                if (legal.empty()) {
                    finalStatus = mini7::statusWithHistory(positions, moves);
                    gameCompleted = true;
                    break;
                }
                candidates.push_back({legal.front(), 0, 0, 0});
            }

            std::sort(candidates.begin(), candidates.end(), [redToMove](const Candidate& a, const Candidate& b) {
                return redToMove ? a.blended > b.blended : a.blended < b.blended;
            });

            size_t selected = 0;
            if (candidates.size() >= 2 && rng() % 100 < 18) selected = 1;
            if (candidates.size() >= 3 && rng() % 100 < 6) selected = 2;

            const mini7::Position before = positions.back();
            if (hasTeacher) {
                net.trainOne(before, teacherTarget, options.learningRate);
                ++stats.teacherUpdates;
                stats.endingSteps = net.steps();
            }

            mini7::Position next = before;
            if (!mini7::applyMove(next, candidates[selected].move)) {
                std::cerr << "internal error: selected move became illegal\n";
                return 7;
            }
            moves.push_back(candidates[selected].move);
            positions.push_back(next);
            ++pliesSinceCheckpoint;

            if (pliesSinceCheckpoint >= static_cast<std::uint64_t>(options.checkpointPlies)) {
                if (!saveModel(net, pathFromUtf8(options.model), error)) {
                    std::cerr << "checkpoint save failed: " << error << "\n";
                    return 8;
                }
                pliesSinceCheckpoint = 0;
                const int elapsed = static_cast<int>(std::chrono::duration_cast<std::chrono::seconds>(Clock::now() - started).count());
                writeSummary(pathFromUtf8(options.summary), options, stats, elapsed, false);
            }

            const auto now = Clock::now();
            if (now - lastProgress >= std::chrono::seconds(30)) {
                const int elapsed = static_cast<int>(std::chrono::duration_cast<std::chrono::seconds>(now - started).count());
                std::cout << "progress: elapsed=" << elapsed << "s game=" << game
                          << " ply=" << (ply + 1) << " steps=" << net.steps() << "\n";
                lastProgress = now;
            }
        }

        if (deadlineReached(deadline, stats, options)) break;

        if (!gameCompleted) {
            finalStatus = mini7::statusWithHistory(positions, moves);
            if (!finalStatus.gameOver) {
                finalStatus.gameOver = true;
                finalStatus.draw = true;
                finalStatus.reason = mini7::EndReason::NaturalMoveDraw;
                finalStatus.message = L"training max plies reached; treated as draw";
            }
            gameCompleted = true;
        }

        if (gameCompleted) {
            const int outcome = finalStatus.draw ? 0 : finalStatus.winner == mini7::Side::Red ? 1600 : -1600;
            for (const auto& sample : positions) {
                if (deadlineReached(deadline, stats, options)) break;
                net.trainOne(sample, outcome, options.learningRate * 0.20f);
                ++stats.outcomeUpdates;
                stats.endingSteps = net.steps();
            }

            ++stats.gamesCompleted;
            if (finalStatus.draw) ++stats.draws;
            else if (finalStatus.winner == mini7::Side::Red) ++stats.redWins;
            else ++stats.blackWins;
            appendGameLog(pathFromUtf8(options.gameLog), game, static_cast<int>(moves.size()), finalStatus, net.steps());

            if (!saveModel(net, pathFromUtf8(options.model), error)) {
                std::cerr << "game checkpoint save failed: " << error << "\n";
                return 8;
            }
            pliesSinceCheckpoint = 0;
            std::cout << "game " << game << " complete: plies=" << moves.size()
                      << " steps=" << net.steps() << "\n";
        }
    }

    if (gStop.load()) stats.stopReason = "signal";
    else if (Clock::now() >= deadline) stats.stopReason = "time_limit";
    else if (options.maxTrainingSteps > 0 &&
             stats.endingSteps - stats.startingSteps >= options.maxTrainingSteps) stats.stopReason = "step_limit";
    else if (stats.gamesCompleted >= options.maxGames) stats.stopReason = "game_limit";

    if (!saveModel(net, pathFromUtf8(options.model), error)) {
        std::cerr << "final checkpoint save failed: " << error << "\n";
        return 8;
    }
    stats.endingSteps = net.steps();
    const int elapsed = static_cast<int>(std::chrono::duration_cast<std::chrono::seconds>(Clock::now() - started).count());
    writeSummary(pathFromUtf8(options.summary), options, stats, elapsed, true);
    engine.close();

    std::cout << "training stopped cleanly: reason=" << stats.stopReason
              << " elapsed=" << elapsed << "s games=" << stats.gamesCompleted
              << " steps=" << stats.startingSteps << "->" << stats.endingSteps << "\n";
    return 0;
}
