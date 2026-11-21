#include "Engine/Source/Model2/M2TransparentSort.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <numeric>
#include <random>

struct CGxShader {};
struct Effect {
    CGxShader* m_vertexShaders[2];
    CGxShader* m_pixelShaders[2];
};
struct Batch { uint16_t m_materialLayer; };
struct M2Element {
    int32_t m_type = 0;
    const int* m_model = nullptr;
    uint32_t m_flags = 0;
    float m_sortDepth = 0;
    int32_t m_priorityPlane = 0;
    float m_depth = 0;
    const Batch* m_batch = nullptr;
    const Effect* m_effect = nullptr;
    uint32_t m_vertexPermute = 0;
    uint32_t m_pixelPermute = 0;
    char padding[32]{};
};
struct CM2Scene {
    static inline uint32_t s_optFlags = 0x4000;
    struct { M2Element* m_data; } m_elements;
    static int32_t SortTransparent(uint32_t a, uint32_t b, const void* param);
    static int32_t SortOpaque(uint32_t, uint32_t, const void*) { return 0; }
};

#include "legacy.inc"

using Clock = std::chrono::steady_clock;

static void Run(uint32_t count, bool manyTies, bool specialValues, bool measure) {
    std::mt19937 random(317 + count);
    int models[8]{};
    CGxShader shaders[4];
    Effect effects[] = {{{&shaders[0], &shaders[1]}, {&shaders[2], &shaders[3]}},
                        {{&shaders[1], &shaders[2]}, {&shaders[0], &shaders[3]}}};
    Batch batches[] = {{0}, {1}};
    std::vector<M2Element> elements(2 * count);
    CM2Scene scene{{elements.data()}};

    for (auto& element : elements) {
        element.m_sortDepth = static_cast<float>(static_cast<int32_t>(random() % (manyTies ? 4 : 16000)) - 2000);
        element.m_depth = static_cast<float>(static_cast<int32_t>(random() % (manyTies ? 2 : 16000)) - 2000);
        element.m_flags = random() & 1;
        element.m_priorityPlane = static_cast<int32_t>(random() % (manyTies ? 2 : 32)) - 16;
        element.m_batch = &batches[random() % 2];
        element.m_model = &models[random() % 8];
        element.m_type = random() % 4;
        element.m_effect = &effects[random() % 2];
        element.m_vertexPermute = random() % 2;
        element.m_pixelPermute = random() % 2;
    }
    if (specialValues) {
        auto& first = elements[0];
        first.m_sortDepth = std::numeric_limits<float>::quiet_NaN();
        elements[2].m_depth = -std::numeric_limits<float>::quiet_NaN();
        elements[4].m_sortDepth = std::numeric_limits<float>::infinity();
        elements[6].m_sortDepth = -std::numeric_limits<float>::infinity();
        elements[8].m_sortDepth = 0.0f;
        elements[10].m_sortDepth = -0.0f;
        elements[8].m_priorityPlane = std::numeric_limits<int32_t>::min();
        elements[10].m_priorityPlane = std::numeric_limits<int32_t>::max();
    }

    std::vector<uint32_t> input(count), baseline(count), candidate(count);
    std::iota(input.begin(), input.end(), 0);
    for (auto& index : input) {
        index *= 2;
    }
    std::vector<M2TransparentSort::Key> keys;
    auto original = [&] { M2HeapSort(&CM2Scene::SortTransparent, baseline.data(), count, &scene); };
    auto keyed = [&] {
        return M2TransparentSort::TrySort(candidate.data(), count, elements.data(), elements.size(), keys,
            [&scene](uint32_t a, uint32_t b) { return CM2Scene::SortTransparent(a, b, &scene); });
    };

    std::vector<double> baselineTimes, candidateTimes;
    auto microseconds = [](Clock::time_point start) {
        return std::chrono::duration<double, std::micro>(Clock::now() - start).count();
    };
    const unsigned rounds = measure ? 500 : 16;
    for (unsigned round = 0; round < rounds; ++round) {
        std::shuffle(input.begin(), input.end(), random);
        baseline = input;
        candidate = input;
        auto timeBaseline = [&] {
            const auto start = Clock::now();
            original();
            baselineTimes.push_back(microseconds(start));
        };
        auto timeCandidate = [&] {
            const auto start = Clock::now();
            const bool ran = keyed();
            candidateTimes.push_back(microseconds(start));
            assert(ran == (count >= 64));
        };
        if (round & 1) {
            timeCandidate();
            timeBaseline();
        } else {
            timeBaseline();
            timeCandidate();
        }
        assert(candidate == (count >= 64 ? baseline : input));
    }

    if (measure) {
        std::sort(baselineTimes.begin(), baselineTimes.end());
        std::sort(candidateTimes.begin(), candidateTimes.end());
        std::printf("%4u entries, %-10s median %.2f -> %.2f us\n", count, manyTies ? "many ties" : "mixed depth",
            baselineTimes[rounds / 2], candidateTimes[rounds / 2]);
    }
}

int main() {
    Run(32, false, false, false);
    Run(64, true, true, false);
    Run(512, true, true, false);
    CM2Scene::s_optFlags = 0;
    Run(128, true, true, false);
    CM2Scene::s_optFlags = 0x4000;

    for (const uint32_t size : {64u, 512u, 4096u}) {
        Run(size, false, false, true);
        Run(size, true, false, true);
    }
    std::puts("Exact draw order matched the original heap sort.");
}
