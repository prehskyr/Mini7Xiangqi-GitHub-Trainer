#pragma once

#include "rules.h"

#include <atomic>
#include <optional>
#include <string>
#include <vector>

namespace mini7 {

struct SearchLine {
    int multipv = 1;
    int scoreRed = 0;
    bool mate = false;
    std::string move;
};

class UciProcess {
public:
    UciProcess() = default;
    UciProcess(const UciProcess&) = delete;
    UciProcess& operator=(const UciProcess&) = delete;
    ~UciProcess();

    bool start(const std::string& enginePath,
               const std::string& variantsPath,
               int threads,
               int hashMb,
               std::string& error);

    bool search(const Position& position,
                int movetimeMs,
                int multiPv,
                std::vector<SearchLine>& lines,
                std::string& error,
                const std::atomic<bool>& stopRequested);

    void close();

private:
#ifdef _WIN32
    void* process_ = nullptr;
    void* inputWrite_ = nullptr;
    void* outputRead_ = nullptr;
#else
    int childPid_ = -1;
    int inputFd_ = -1;
    int outputFd_ = -1;
#endif

    bool writeLine(const std::string& line);
    bool readLine(std::string& line);
    bool waitFor(const std::string& wanted);
    static std::optional<SearchLine> parseInfo(const std::string& text, bool redToMove);
};

}  // namespace mini7
