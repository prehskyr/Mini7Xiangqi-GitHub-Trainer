#include <iostream>
#include <string>

int main() {
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);

    std::string line;
    while (std::getline(std::cin, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line == "uci") {
            std::cout << "id name Mini7 Mock Engine\n"
                      << "id author trainer smoke test\n"
                      << "option name UCI_Variant type combo default mini7xiangqi var mini7xiangqi\n"
                      << "option name Use NNUE type check default false\n"
                      << "option name Threads type spin default 1 min 1 max 256\n"
                      << "option name Hash type spin default 16 min 1 max 65536\n"
                      << "option name MultiPV type spin default 1 min 1 max 5\n"
                      << "uciok\n" << std::flush;
        } else if (line == "isready") {
            std::cout << "readyok\n" << std::flush;
        } else if (line.rfind("go ", 0) == 0) {
            std::cout << "info depth 1 multipv 1 score cp 25 pv a2a3\n"
                      << "bestmove a2a3\n" << std::flush;
        } else if (line == "stop") {
            std::cout << "bestmove a2a3\n" << std::flush;
        } else if (line == "quit") {
            break;
        }
    }
    return 0;
}
