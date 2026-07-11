#include "mini7net.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <limits>

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace mini7 {
namespace {

constexpr char MAGIC[8] = {'M','7','N','N','U','E','1','\0'};
constexpr uint32_t VERSION = 1;
constexpr float CP_SCALE = 600.0f;

float targetFromCp(int cp) {
    return std::tanh(std::clamp(cp, -4000, 4000) / CP_SCALE);
}

int cpFromValue(float value) {
    value = std::clamp(value, -0.999f, 0.999f);
    return static_cast<int>(std::lround(std::atanh(value) * CP_SCALE));
}

uint64_t nextRand(uint64_t& state) {
    state ^= state << 13;
    state ^= state >> 7;
    state ^= state << 17;
    return state;
}

float randWeight(uint64_t& state) {
    const double unit = static_cast<double>(nextRand(state) >> 11) * (1.0 / 9007199254740992.0);
    return static_cast<float>((unit - 0.5) * 0.02);
}

template<class T>
bool writeRaw(std::ofstream& out, const T& value) {
    out.write(reinterpret_cast<const char*>(&value), sizeof(value));
    return static_cast<bool>(out);
}

template<class T>
bool readRaw(std::ifstream& in, T& value) {
    in.read(reinterpret_cast<char*>(&value), sizeof(value));
    return static_cast<bool>(in);
}

}  // namespace

SparseNet::SparseNet() : inputWeight_(InputSize * HiddenSize) { reset(); }

void SparseNet::reset(uint64_t seed) {
    uint64_t state = seed ? seed : 1;
    for (float& value : inputWeight_) value = randWeight(state);
    for (float& value : hiddenBias_) value = 0.0f;
    for (float& value : outputWeight_) value = randWeight(state);
    outputBias_ = 0.0f;
    steps_ = 0;
}

int SparseNet::channel(char piece) {
    switch (piece) {
        case 'R': return 0; case 'C': return 1; case 'H': return 2; case 'K': return 3; case 'P': return 4;
        case 'r': return 5; case 'c': return 6; case 'h': return 7; case 'k': return 8; case 'p': return 9;
        default: return -1;
    }
}

void SparseNet::activeFeatures(const Position& position, std::array<int, 64>& out, int& count) {
    count = 0;
    for (int square = 0; square < N * N; ++square) {
        const int ch = channel(position.board[square]);
        if (ch >= 0 && count < static_cast<int>(out.size())) out[count++] = ch * N * N + square;
    }
    out[count++] = PieceChannels * N * N + (position.turn == Side::Red ? 0 : 1);
}

float SparseNet::evaluate(const Position& position) const {
    std::array<int, 64> active{};
    int count = 0;
    activeFeatures(position, active, count);
    std::array<float, HiddenSize> hidden = hiddenBias_;
    for (int i = 0; i < count; ++i) {
        const float* row = inputWeight_.data() + active[i] * HiddenSize;
        for (int h = 0; h < HiddenSize; ++h) hidden[h] += row[h];
    }
    float output = outputBias_;
    for (int h = 0; h < HiddenSize; ++h) output += std::max(0.0f, hidden[h]) * outputWeight_[h];
    return std::tanh(output);
}

int SparseNet::evaluateCp(const Position& position) const { return cpFromValue(evaluate(position)); }

float SparseNet::trainOne(const Position& position, int targetCp, float learningRate) {
    learningRate = std::clamp(learningRate, 1e-6f, 0.2f);
    std::array<int, 64> active{};
    int count = 0;
    activeFeatures(position, active, count);
    std::array<float, HiddenSize> pre = hiddenBias_;
    for (int i = 0; i < count; ++i) {
        const float* row = inputWeight_.data() + active[i] * HiddenSize;
        for (int h = 0; h < HiddenSize; ++h) pre[h] += row[h];
    }
    std::array<float, HiddenSize> hidden{};
    float raw = outputBias_;
    for (int h = 0; h < HiddenSize; ++h) {
        hidden[h] = std::max(0.0f, pre[h]);
        raw += hidden[h] * outputWeight_[h];
    }
    const float prediction = std::tanh(raw);
    const float target = targetFromCp(targetCp);
    const float error = prediction - target;
    const float gradRaw = 2.0f * error * (1.0f - prediction * prediction);

    const auto oldOutput = outputWeight_;
    for (int h = 0; h < HiddenSize; ++h) outputWeight_[h] -= learningRate * gradRaw * hidden[h];
    outputBias_ -= learningRate * gradRaw;
    for (int h = 0; h < HiddenSize; ++h) {
        if (pre[h] <= 0.0f) continue;
        const float grad = gradRaw * oldOutput[h];
        hiddenBias_[h] -= learningRate * grad;
        for (int i = 0; i < count; ++i) inputWeight_[active[i] * HiddenSize + h] -= learningRate * grad;
    }
    ++steps_;
    return error * error;
}

bool SparseNet::save(const std::wstring& path, std::wstring& error) const {
    const std::filesystem::path target(path);
    const std::filesystem::path temporary = target.wstring() + L".tmp";
    {
        std::ofstream out{temporary, std::ios::binary | std::ios::trunc};
        if (!out) { error = L"无法创建训练模型临时文件"; return false; }
        out.write(MAGIC, sizeof(MAGIC));
        const uint32_t input = InputSize, hidden = HiddenSize;
        if (!writeRaw(out, VERSION) || !writeRaw(out, input) || !writeRaw(out, hidden) || !writeRaw(out, steps_) ||
            !writeRaw(out, outputBias_)) { error = L"写入模型头失败"; return false; }
        out.write(reinterpret_cast<const char*>(hiddenBias_.data()), sizeof(float) * hiddenBias_.size());
        out.write(reinterpret_cast<const char*>(outputWeight_.data()), sizeof(float) * outputWeight_.size());
        out.write(reinterpret_cast<const char*>(inputWeight_.data()), sizeof(float) * inputWeight_.size());
        out.flush();
        if (!out) { error = L"写入模型权重失败"; return false; }
    }

#ifdef _WIN32
    if (!MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        error = L"替换训练模型失败，Win32 错误码 " + std::to_wstring(GetLastError());
        return false;
    }
#else
    std::error_code ec;
    std::filesystem::rename(temporary, target, ec);
    if (ec) {
        std::filesystem::remove(temporary, ec);
        error = L"替换训练模型失败";
        return false;
    }
#endif
    return true;
}

bool SparseNet::load(const std::wstring& path, std::wstring& error) {
    std::ifstream in{std::filesystem::path(path), std::ios::binary};
    if (!in) { error = L"无法打开训练模型"; return false; }
    char magic[8]{};
    uint32_t version = 0, input = 0, hidden = 0;
    in.read(magic, sizeof(magic));
    if (!readRaw(in, version) || !readRaw(in, input) || !readRaw(in, hidden) ||
        std::memcmp(magic, MAGIC, sizeof(MAGIC)) != 0 || version != VERSION ||
        input != InputSize || hidden != HiddenSize) {
        error = L"模型格式或网络结构不兼容";
        return false;
    }
    if (!readRaw(in, steps_) || !readRaw(in, outputBias_)) { error = L"模型头损坏"; return false; }
    in.read(reinterpret_cast<char*>(hiddenBias_.data()), sizeof(float) * hiddenBias_.size());
    in.read(reinterpret_cast<char*>(outputWeight_.data()), sizeof(float) * outputWeight_.size());
    in.read(reinterpret_cast<char*>(inputWeight_.data()), sizeof(float) * inputWeight_.size());
    if (!in) { error = L"模型权重不完整"; return false; }
    return true;
}

}  // namespace mini7
