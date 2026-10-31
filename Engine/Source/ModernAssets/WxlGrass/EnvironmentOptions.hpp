#pragma once
// Pure logic for the WarcraftXL control add-on's Environment page (the supplied
// wxl-living-azeroth Options.cpp): the option table, clamping, and the WTF\WXL\Environment.ini
// text format. No I/O here; WxlEnvironmentOptions.cpp owns the file and the Lua functions.
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace wxl::grass::environment
{
    struct Option
    {
        const char* name;
        double low, high, initial;
        bool integer;
    };

    // Same names, ranges and order as the supplied module. Defaults match it except lights.enabled and
    // lights.models: surface lights are OFF by default in the native client.
    constexpr Option kOptions[] = {
        {"grass.instanced", 0, 1, 1, true},
        {"grass.memoryMB", 16, 128, 64, true},
        {"grass.buildMs", 0.5, 4, 2, false},
        {"grass.windStrength", 0, 2, 1, false},
        {"grass.windSpeed", 0, 2, 1, false},
        {"grass.interaction", 0, 1, 1, true},
        {"lights.enabled", 0, 1, 0, true},
        {"lights.models", 0, 1, 0, true},
        {"lights.brightness", 0, 2, 1, false},
        {"lights.radius", 4, 32, 16, false},
        {"lights.range", 32, 256, 160, true},
        {"lights.perCell", 2, 16, 8, true},
        {"lights.flicker", 0, 0.5, 0.15, false},
        {"lights.flickerSpeed", 0.1, 3, 1, false},
        {"lights.occlusion", 0, 1, 1, true},
        {"lights.raysPerFrame", 64, 2048, 256, true},
        {"lights.indoorIsolation", 0, 1, 1, true},
    };
    constexpr size_t kCount = sizeof(kOptions) / sizeof(kOptions[0]);

    enum Index : size_t
    {
        kGrassInstanced, kGrassMemoryMB, kGrassBuildMs, kGrassWindStrength, kGrassWindSpeed, kGrassInteraction,
        kLightsEnabled, kLightsModels, kLightsBrightness, kLightsRadius, kLightsRange, kLightsPerCell,
        kLightsFlicker, kLightsFlickerSpeed, kLightsOcclusion, kLightsRaysPerFrame, kLightsIndoorIsolation,
    };

    struct Values
    {
        double v[kCount];
        Values() { for (size_t i = 0; i < kCount; ++i) v[i] = kOptions[i].initial; }
    };

    inline double Normalize(size_t index, double value)
    {
        const Option& option = kOptions[index];
        if (!std::isfinite(value)) return option.initial;
        value = std::clamp(value, option.low, option.high);
        return option.integer ? std::floor(value + 0.5) : value;
    }

    // Index of the option called `name` (exact, length-checked), or kCount.
    inline size_t Find(std::string_view name)
    {
        for (size_t i = 0; i < kCount; ++i)
            if (name == kOptions[i].name) return i;
        return kCount;
    }

    namespace detail
    {
        inline std::string_view Trim(std::string_view s)
        {
            while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r')) s.remove_prefix(1);
            while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
            return s;
        }
    }

    // Reads `key=value` lines of the [Environment] section. Unknown keys, malformed or non-finite
    // numbers and other sections are ignored; every accepted value is clamped. `values` keeps its
    // prior contents for anything absent. Returns how many options were read.
    inline size_t Parse(std::string_view text, Values& values)
    {
        bool inSection = false;
        size_t read = 0;
        while (!text.empty())
        {
            const size_t end = text.find('\n');
            std::string_view line = detail::Trim(text.substr(0, end));
            text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
            if (line.empty() || line.front() == ';' || line.front() == '#') continue;
            if (line.front() == '[')
            {
                inSection = line == "[Environment]";
                continue;
            }
            if (!inSection) continue;
            const size_t equals = line.find('=');
            if (equals == std::string_view::npos) continue;
            const size_t index = Find(detail::Trim(line.substr(0, equals)));
            if (index == kCount) continue;
            const std::string raw(detail::Trim(line.substr(equals + 1)));
            if (raw.empty() || raw.size() > 63) continue;
            char* stop = nullptr;
            const double value = std::strtod(raw.c_str(), &stop);
            if (stop == raw.c_str() || *stop != '\0' || !std::isfinite(value)) continue;
            values.v[index] = Normalize(index, value);
            ++read;
        }
        return read;
    }

    inline std::string Format(const Values& values)
    {
        std::string out = "; Live WarcraftXL grass and surface-light settings.\r\n[Environment]\r\n";
        char number[64];
        for (size_t i = 0; i < kCount; ++i)
        {
            std::snprintf(number, sizeof(number), "=%.6g\r\n", values.v[i]);
            out += kOptions[i].name;
            out += number;
        }
        return out;
    }

    // Scale factors the grass motion applies on top of the compile-time wind block.
    struct Motion
    {
        float strength = 1.0f;
        float speed = 1.0f;
        bool interaction = true;
    };

    inline Motion MotionOf(const Values& values)
    {
        return {static_cast<float>(values.v[kGrassWindStrength]), static_cast<float>(values.v[kGrassWindSpeed]),
                values.v[kGrassInteraction] != 0.0};
    }

    // Frame-time summary for the page's 30-second performance sample.
    struct FrameSummary
    {
        size_t frames = 0;
        double medianMs = 0, p95Ms = 0, p99Ms = 0, worstMs = 0;
    };

    // Nearest-rank percentile of an ascending-sorted, non-empty list; `fraction` in [0, 1].
    inline double Percentile(const std::vector<double>& sorted, double fraction)
    {
        const size_t rank = static_cast<size_t>(std::ceil(std::clamp(fraction, 0.0, 1.0) * sorted.size()));
        return sorted[rank == 0 ? 0 : rank - 1];
    }

    inline FrameSummary Summarize(std::vector<double> frameMs)
    {
        FrameSummary out;
        frameMs.erase(std::remove_if(frameMs.begin(), frameMs.end(), [](double v) { return !std::isfinite(v) || v < 0; }),
                      frameMs.end());
        if (frameMs.empty()) return out;
        std::sort(frameMs.begin(), frameMs.end());
        out.frames = frameMs.size();
        out.medianMs = Percentile(frameMs, 0.5);
        out.p95Ms = Percentile(frameMs, 0.95);
        out.p99Ms = Percentile(frameMs, 0.99);
        out.worstMs = frameMs.back();
        return out;
    }
}
