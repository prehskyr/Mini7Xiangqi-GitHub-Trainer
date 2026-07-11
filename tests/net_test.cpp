#include "../src/mini7net.h"
#include <cassert>
#include <cmath>
#include <iostream>

int main() {
    mini7::SparseNet net;
    const auto position = mini7::initialPosition();
    const int before = net.evaluateCp(position);
    const float loss = net.trainOne(position, 320, 0.01f);
    const int after = net.evaluateCp(position);
    assert(net.steps() == 1);
    assert(std::isfinite(loss));
    assert(after != before);
    std::wstring error;
    const std::wstring path = L"/mnt/data/work7x/one_step.m7nnue";
    assert(net.save(path, error));

    // The editor can create a position with a piece on every square.
    // Exercise 49 piece features plus the side-to-move feature.
    mini7::Position dense;
    assert(mini7::parseFen("ppkpppp/ppppppp/ppppppp/PPPPPPP/PPPPPPP/PPPPPPP/PPPKPPP w - - 0 1", dense, error));
    (void)net.evaluateCp(dense);

    mini7::SparseNet loaded;
    assert(loaded.load(path, error));
    assert(loaded.steps() == 1);
    assert(std::abs(loaded.evaluateCp(position) - after) <= 1);
    std::cout << "one training step saved and reloaded: " << before << " -> " << after << " cp\n";
}
