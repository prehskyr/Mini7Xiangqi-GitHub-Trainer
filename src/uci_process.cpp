#include "uci_process.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <sstream>

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <csignal>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace mini7 {
namespace {

#ifdef _WIN32
std::filesystem::path pathFromUtf8(const std::string& text) {
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}

std::wstring utf8ToWide(const std::string& text) {
    if (text.empty()) return {};
    const int count = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (count <= 0) return {};
    std::wstring result(static_cast<size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), count);
    return result;
}
#endif

}  // namespace

UciProcess::~UciProcess() { close(); }

bool UciProcess::start(const std::string& enginePath,
                       const std::string& variantsPath,
                       int threads,
                       int hashMb,
                       std::string& error) {
    close();

#ifdef _WIN32
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE childOutRead = nullptr, childOutWrite = nullptr;
    HANDLE childInRead = nullptr, childInWrite = nullptr;
    if (!CreatePipe(&childOutRead, &childOutWrite, &security, 0) ||
        !SetHandleInformation(childOutRead, HANDLE_FLAG_INHERIT, 0) ||
        !CreatePipe(&childInRead, &childInWrite, &security, 0) ||
        !SetHandleInformation(childInWrite, HANDLE_FLAG_INHERIT, 0)) {
        error = "cannot create UCI pipes, Win32 error " + std::to_string(GetLastError());
        if (childOutRead) CloseHandle(childOutRead);
        if (childOutWrite) CloseHandle(childOutWrite);
        if (childInRead) CloseHandle(childInRead);
        if (childInWrite) CloseHandle(childInWrite);
        return false;
    }

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    startup.hStdInput = childInRead;
    startup.hStdOutput = childOutWrite;
    startup.hStdError = childOutWrite;
    PROCESS_INFORMATION info{};

    const std::wstring engineWide = utf8ToWide(enginePath);
    const std::wstring variantsWide = utf8ToWide(variantsPath);
    std::wstring command = L"\"" + engineWide + L"\" load \"" + variantsWide + L"\"";
    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(L'\0');
    const std::filesystem::path engineFs = pathFromUtf8(enginePath);
    const std::wstring currentDir = engineFs.parent_path().wstring();

    const BOOL created = CreateProcessW(nullptr,
                                        mutableCommand.data(),
                                        nullptr,
                                        nullptr,
                                        TRUE,
                                        CREATE_NO_WINDOW,
                                        nullptr,
                                        currentDir.empty() ? nullptr : currentDir.c_str(),
                                        &startup,
                                        &info);
    CloseHandle(childInRead);
    CloseHandle(childOutWrite);
    if (!created) {
        error = "cannot start engine, Win32 error " + std::to_string(GetLastError());
        CloseHandle(childOutRead);
        CloseHandle(childInWrite);
        return false;
    }
    CloseHandle(info.hThread);
    process_ = info.hProcess;
    outputRead_ = childOutRead;
    inputWrite_ = childInWrite;
#else
    int toChild[2] = {-1, -1};
    int fromChild[2] = {-1, -1};
    if (pipe(toChild) != 0 || pipe(fromChild) != 0) {
        error = std::string("cannot create UCI pipes: ") + std::strerror(errno);
        if (toChild[0] >= 0) ::close(toChild[0]);
        if (toChild[1] >= 0) ::close(toChild[1]);
        if (fromChild[0] >= 0) ::close(fromChild[0]);
        if (fromChild[1] >= 0) ::close(fromChild[1]);
        return false;
    }

    const pid_t pid = fork();
    if (pid < 0) {
        error = std::string("cannot fork engine: ") + std::strerror(errno);
        ::close(toChild[0]); ::close(toChild[1]); ::close(fromChild[0]); ::close(fromChild[1]);
        return false;
    }
    if (pid == 0) {
        dup2(toChild[0], STDIN_FILENO);
        dup2(fromChild[1], STDOUT_FILENO);
        dup2(fromChild[1], STDERR_FILENO);
        ::close(toChild[0]); ::close(toChild[1]); ::close(fromChild[0]); ::close(fromChild[1]);
        const std::filesystem::path engineFs(enginePath);
        if (!engineFs.parent_path().empty()) chdir(engineFs.parent_path().c_str());
        const std::string executable = engineFs.filename().string();
        execl(engineFs.is_absolute() ? enginePath.c_str() : executable.c_str(),
              executable.c_str(), "load", variantsPath.c_str(), static_cast<char*>(nullptr));
        _exit(127);
    }
    ::close(toChild[0]);
    ::close(fromChild[1]);
    childPid_ = static_cast<int>(pid);
    inputFd_ = toChild[1];
    outputFd_ = fromChild[0];
#endif

    writeLine("uci");
    if (!waitFor("uciok")) {
        error = "engine did not return uciok";
        close();
        return false;
    }
    writeLine("setoption name UCI_Variant value mini7xiangqi");
    writeLine("setoption name Use NNUE value false");
    writeLine("setoption name Threads value " + std::to_string(std::max(1, threads)));
    writeLine("setoption name Hash value " + std::to_string(std::max(1, hashMb)));
    writeLine("isready");
    if (!waitFor("readyok")) {
        error = "engine did not become ready after loading mini7xiangqi";
        close();
        return false;
    }
    return true;
}

bool UciProcess::search(const Position& position,
                        int movetimeMs,
                        int multiPv,
                        std::vector<SearchLine>& lines,
                        std::string& error,
                        const std::atomic<bool>& stopRequested) {
    lines.clear();
    multiPv = std::clamp(multiPv, 1, 5);
    writeLine("setoption name MultiPV value " + std::to_string(multiPv));
    writeLine("position fen " + toFen(position));
    writeLine("go movetime " + std::to_string(std::max(20, movetimeMs)));

    std::array<std::optional<SearchLine>, 6> latest;
    std::string bestMove;
    std::string line;
    bool stopSent = false;
    while (readLine(line)) {
        if (stopRequested.load() && !stopSent) {
            writeLine("stop");
            stopSent = true;
        }
        if (line.rfind("bestmove ", 0) == 0) {
            size_t end = line.find(' ', 9);
            if (end == std::string::npos) end = line.size();
            bestMove = line.substr(9, end - 9);
            break;
        }
        auto parsed = parseInfo(line, position.turn == Side::Red);
        if (parsed && parsed->multipv >= 1 && parsed->multipv <= 5) latest[parsed->multipv] = *parsed;
    }

    for (int i = 1; i <= multiPv; ++i) {
        if (latest[i]) lines.push_back(*latest[i]);
    }
    if (lines.empty() && !bestMove.empty() && bestMove != "0000" && bestMove != "(none)") {
        lines.push_back({1, 0, false, bestMove});
    }
    if (lines.empty() && !stopRequested.load()) {
        error = "engine returned no trainable move";
        return false;
    }
    return true;
}

void UciProcess::close() {
#ifdef _WIN32
    HANDLE input = static_cast<HANDLE>(inputWrite_);
    HANDLE output = static_cast<HANDLE>(outputRead_);
    HANDLE process = static_cast<HANDLE>(process_);
    if (input) {
        writeLine("quit");
        CloseHandle(input);
        inputWrite_ = nullptr;
    }
    if (output) {
        CloseHandle(output);
        outputRead_ = nullptr;
    }
    if (process) {
        if (WaitForSingleObject(process, 1000) == WAIT_TIMEOUT) TerminateProcess(process, 0);
        CloseHandle(process);
        process_ = nullptr;
    }
#else
    if (inputFd_ >= 0) {
        writeLine("quit");
        ::close(inputFd_);
        inputFd_ = -1;
    }
    if (outputFd_ >= 0) {
        ::close(outputFd_);
        outputFd_ = -1;
    }
    if (childPid_ > 0) {
        int status = 0;
        for (int i = 0; i < 20; ++i) {
            const pid_t result = waitpid(childPid_, &status, WNOHANG);
            if (result == childPid_) {
                childPid_ = -1;
                return;
            }
            usleep(50000);
        }
        kill(childPid_, SIGTERM);
        waitpid(childPid_, &status, 0);
        childPid_ = -1;
    }
#endif
}

bool UciProcess::writeLine(const std::string& line) {
    const std::string payload = line + "\n";
#ifdef _WIN32
    HANDLE input = static_cast<HANDLE>(inputWrite_);
    if (!input) return false;
    DWORD written = 0;
    return WriteFile(input, payload.data(), static_cast<DWORD>(payload.size()), &written, nullptr) &&
           written == payload.size();
#else
    if (inputFd_ < 0) return false;
    size_t done = 0;
    while (done < payload.size()) {
        const ssize_t written = ::write(inputFd_, payload.data() + done, payload.size() - done);
        if (written < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        done += static_cast<size_t>(written);
    }
    return true;
#endif
}

bool UciProcess::readLine(std::string& line) {
    line.clear();
    char ch = 0;
    while (true) {
#ifdef _WIN32
        HANDLE output = static_cast<HANDLE>(outputRead_);
        if (!output) return false;
        DWORD read = 0;
        if (!ReadFile(output, &ch, 1, &read, nullptr) || read == 0) return false;
#else
        if (outputFd_ < 0) return false;
        const ssize_t readCount = ::read(outputFd_, &ch, 1);
        if (readCount == 0) return false;
        if (readCount < 0) {
            if (errno == EINTR) continue;
            return false;
        }
#endif
        if (ch == '\n') return true;
        if (ch != '\r') line.push_back(ch);
    }
}

bool UciProcess::waitFor(const std::string& wanted) {
    std::string line;
    while (readLine(line)) {
        if (line == wanted || line.rfind(wanted + " ", 0) == 0) return true;
    }
    return false;
}

std::optional<SearchLine> UciProcess::parseInfo(const std::string& text, bool redToMove) {
    if (text.rfind("info ", 0) != 0 || text.find(" pv ") == std::string::npos) return std::nullopt;
    std::istringstream input(text);
    std::vector<std::string> tokens;
    for (std::string token; input >> token;) tokens.push_back(token);

    SearchLine line;
    bool hasScore = false;
    int raw = 0;
    for (size_t i = 1; i < tokens.size(); ++i) {
        if (tokens[i] == "multipv" && i + 1 < tokens.size()) {
            line.multipv = std::atoi(tokens[++i].c_str());
        } else if (tokens[i] == "score" && i + 2 < tokens.size()) {
            line.mate = tokens[++i] == "mate";
            raw = std::atoi(tokens[++i].c_str());
            if (line.mate) raw = raw > 0 ? 30000 - std::min(raw, 1000) : -30000 - std::max(raw, -1000);
            hasScore = true;
        } else if (tokens[i] == "pv" && i + 1 < tokens.size()) {
            line.move = tokens[i + 1];
            break;
        }
    }
    if (!hasScore || line.move.empty()) return std::nullopt;
    line.scoreRed = redToMove ? raw : -raw;
    return line;
}

}  // namespace mini7
