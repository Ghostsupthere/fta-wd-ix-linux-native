#include "Engine/Source/Gx/ShaderConstantUpdate.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <vector>

using Update = decltype(&GxShaderConstantUpdate::Scalar);

static float FloatBits(uint32_t bits) {
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

static void CheckResult(const std::array<float, 32>& initial, const float* independent,
                        uint32_t destinationOffset, uint32_t sourceOffset, uint32_t count) {
    auto scalar = initial;
    auto simd = initial;
    uint32_t scalarMin = 255;
    uint32_t scalarMax = 0;
    uint32_t simdMin = scalarMin;
    uint32_t simdMax = scalarMax;
    GxShaderConstantUpdate::Scalar(scalar.data() + destinationOffset,
        independent ? independent : scalar.data() + sourceOffset, 19, count, scalarMin, scalarMax);
    GxShaderConstantUpdate::Simd(simd.data() + destinationOffset,
        independent ? independent : simd.data() + sourceOffset, 19, count, simdMin, simdMax);
    assert(scalarMin == simdMin && scalarMax == simdMax);
    assert(std::memcmp(scalar.data(), simd.data(), sizeof(scalar)) == 0);
}

struct Upload {
    std::array<float, 48> destination;
    std::array<float, 48> first;
    std::array<float, 48> second;
    uint32_t dirtyMin = 255;
    uint32_t dirtyMax = 0;
};

[[gnu::noinline]] static double Measure(Update update, std::vector<Upload>& uploads, uint32_t count) {
    using Clock = std::chrono::steady_clock;
    const auto start = Clock::now();
    constexpr uint32_t rounds = 1024;
    for (uint32_t i = 0; i < rounds; ++i) {
        for (auto& upload : uploads) {
            const auto& values = i & 1u ? upload.second : upload.first;
            update(upload.destination.data(), values.data(), 31, count, upload.dirtyMin, upload.dirtyMax);
        }
    }
    const double totalNs = std::chrono::duration<double, std::nano>(Clock::now() - start).count();
    return totalNs / (rounds * uploads.size());
}

static void Benchmark(uint32_t count, bool changed) {
    std::vector<Upload> reference(128);
    for (uint32_t i = 0; i < reference.size(); ++i) {
        auto& upload = reference[i];
        upload.first.fill(static_cast<float>(i));
        upload.second = upload.first;
        upload.destination = upload.first;
        if (changed) {
            for (uint32_t reg = 0; reg < count; ++reg) {
                upload.second[reg * 4 + i % 4] += 1.0f;
            }
        }
    }

    std::array<double, 7> scalarTimes;
    std::array<double, 7> simdTimes;
    for (uint32_t i = 0; i < scalarTimes.size(); ++i) {
        auto scalar = reference;
        auto simd = reference;
        if (i & 1u) {
            simdTimes[i] = Measure(GxShaderConstantUpdate::Simd, simd, count);
            scalarTimes[i] = Measure(GxShaderConstantUpdate::Scalar, scalar, count);
        } else {
            scalarTimes[i] = Measure(GxShaderConstantUpdate::Scalar, scalar, count);
            simdTimes[i] = Measure(GxShaderConstantUpdate::Simd, simd, count);
        }
        for (uint32_t j = 0; j < scalar.size(); ++j) {
            assert(scalar[j].destination == simd[j].destination);
            assert(scalar[j].dirtyMin == simd[j].dirtyMin && scalar[j].dirtyMax == simd[j].dirtyMax);
        }
    }
    std::sort(scalarTimes.begin(), scalarTimes.end());
    std::sort(simdTimes.begin(), simdTimes.end());
    std::printf("%2u registers, %-9s %.2f -> %.2f ns/call\n", count, changed ? "changed" : "unchanged",
        scalarTimes[3], simdTimes[3]);
}

int main() {
    std::array<float, 32> initial{};
    for (uint32_t i = 0; i < initial.size(); ++i) initial[i] = static_cast<float>(i);
    initial[1] = -0.0f;
    initial[4] = FloatBits(0x7fc17721);
    auto source = initial;
    source[1] = +0.0f;
    source[2] = -2.0f;
    source[7] = FloatBits(0x7f800001);

    CheckResult(initial, nullptr, 1, 0, 4);
    CheckResult(initial, nullptr, 0, 1, 4);
    CheckResult(initial, nullptr, 0, 0, 4);
    CheckResult(initial, source.data(), 0, 0, 4);
    CheckResult(initial, source.data() + 1, 1, 0, 4);
    CheckResult(initial, source.data(), 0, 0, 0);

    uint32_t low = 12, high = 25;
    auto output = initial;
    GxShaderConstantUpdate::Simd(output.data(), source.data(), 19, 4, low, high);
    assert(low == 12 && high == 25 && std::memcmp(&output[1], &initial[1], sizeof(float)) == 0);
    std::puts("Constant bytes and dirty ranges matched the scalar path.");
    for (uint32_t count : {1u, 2u, 4u, 12u}) {
        Benchmark(count, false);
        Benchmark(count, true);
    }
}
