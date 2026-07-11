#pragma once

#include "rules.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace mini7 {

class SparseNet {
public:
    static constexpr int PieceChannels = 10;
    static constexpr int InputSize = PieceChannels * N * N + 2;
    static constexpr int HiddenSize = 64;

    SparseNet();
    void reset(uint64_t seed = 0x7A11C0DEULL);
    float evaluate(const Position& position) const;      // normalized red score in [-1, 1]
    int evaluateCp(const Position& position) const;      // approximate red centipawns
    float trainOne(const Position& position, int targetCp, float learningRate);
    bool save(const std::wstring& path, std::wstring& error) const;
    bool load(const std::wstring& path, std::wstring& error);
    uint64_t steps() const { return steps_; }

private:
    std::array<float, HiddenSize> hiddenBias_{};
    std::array<float, HiddenSize> outputWeight_{};
    std::vector<float> inputWeight_;
    float outputBias_ = 0.0f;
    uint64_t steps_ = 0;

    static int channel(char piece);
    static void activeFeatures(const Position& position, std::array<int, 64>& out, int& count);
};

}  // namespace mini7
