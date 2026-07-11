#include "../src/mini7net.h"

#include <cassert>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <system_error>

int main() {
    mini7::SparseNet net;
    const auto position = mini7::initialPosition();
    const int before = net.evaluateCp(position);
    const float loss = net.trainOne(position, 320, 0.01f);
    const int after = net.evaluateCp(position);
    assert(net.steps() == 1);
    assert(std::isfinite(loss));
    assert(after != before);

    std::error_code fsError;
    const std::filesystem::path tempDirectory = std::filesystem::temp_directory_path(fsError);
    if (fsError) {
        std::cerr << "failed to resolve the system temporary directory: "
                  << fsError.message() << '\n';
        return 1;
    }

    const std::filesystem::path path =
        tempDirectory / "mini7_net_test_one_step.m7nnue";
    std::filesystem::path temporaryPath = path;
    temporaryPath += L".tmp";

    // Remove leftovers from an interrupted earlier test run.
    std::filesystem::remove(path, fsError);
    fsError.clear();
    std::filesystem::remove(temporaryPath, fsError);
    fsError.clear();

    std::wstring error;
    if (!net.save(path.wstring(), error)) {
        std::wcerr << L"model save failed: " << error << L'\n';
        return 1;
    }

    // The editor can create a position with a piece on every square.
    // Exercise 49 piece features plus the side-to-move feature.
    mini7::Position dense;
    assert(mini7::parseFen(
        "ppkpppp/ppppppp/ppppppp/PPPPPPP/PPPPPPP/PPPPPPP/PPPKPPP w - - 0 1",
        dense, error));
    (void)net.evaluateCp(dense);

    mini7::SparseNet loaded;
    if (!loaded.load(path.wstring(), error)) {
        std::wcerr << L"model load failed: " << error << L'\n';
        return 1;
    }
    assert(loaded.steps() == 1);
    assert(std::abs(loaded.evaluateCp(position) - after) <= 1);

    std::filesystem::remove(path, fsError);
    fsError.clear();
    std::filesystem::remove(temporaryPath, fsError);

    std::cout << "one training step saved and reloaded: "
              << before << " -> " << after << " cp\n";
}