#ifndef WOW_SOURCE_SOUNDINTERFACE_MOUNTSOUNDTIMING_H
#define WOW_SOURCE_SOUNDINTERFACE_MOUNTSOUNDTIMING_H

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <map>
#include <mutex>
#include <string_view>
#include <utility>

namespace MountSoundTiming {

constexpr uint32_t MaxDelayMs = 600000;
constexpr size_t MaxFileBytes = 1024 * 1024;

struct Timing {
    uint32_t delayMin;
    uint32_t delayMax;
    uint32_t cooldownMin;
    uint32_t cooldownMax;
};

constexpr bool DeadlineReached(uint32_t now, uint32_t deadline) {
    return static_cast<int32_t>(now - deadline) >= 0;
}

class Policy {
public:
    bool Parse(std::string_view csv) {
        if (csv.size() > MaxFileBytes || csv.find('\0') != csv.npos) {
            return false;
        }

        auto readLine = [&csv] {
            const auto end = csv.find('\n');
            auto line = csv.substr(0, end);
            csv.remove_prefix(end == csv.npos ? csv.size() : end + 1);
            if (!line.empty() && line.back() == '\r') {
                line.remove_suffix(1);
            }
            return line;
        };

        if (readLine() != "SoundID,DelayMin,DelayMax,CooldownMin,CooldownMax") {
            return false;
        }

        std::map<uint32_t, Timing> parsed;
        while (!csv.empty()) {
            auto row = readLine();
            if (row.empty()) {
                continue;
            }

            uint32_t values[5]{};
            for (size_t i = 0; i < 5; ++i) {
                const auto comma = row.find(',');
                const auto token = row.substr(0, comma);
                if (token.empty() || ((i == 4) != (comma == row.npos))) {
                    return false;
                }

                const auto* end = token.data() + token.size();
                const auto result = std::from_chars(token.data(), end, values[i]);
                if (result.ec != std::errc{} || result.ptr != end) {
                    return false;
                }
                row.remove_prefix(comma == row.npos ? row.size() : comma + 1);
            }

            const auto [kit, delayMin, delayMax, cooldownMin, cooldownMax] = values;
            if (!kit || delayMin > delayMax || cooldownMin > cooldownMax ||
                delayMax > MaxDelayMs || cooldownMax > MaxDelayMs || parsed.size() >= 4096 ||
                !parsed.emplace(kit, Timing{delayMin, delayMax, cooldownMin, cooldownMax}).second) {
                return false;
            }
        }

        std::lock_guard lock(m_lock);
        m_timings = std::move(parsed);
        m_cooldowns.clear();
        return true;
    }

    bool Find(uint32_t kit, Timing& timing) {
        std::lock_guard lock(m_lock);
        const auto found = m_timings.find(kit);
        if (found == m_timings.end()) {
            return false;
        }
        timing = found->second;
        return true;
    }

    uint32_t Remaining(const void* owner, uint32_t kit, uint32_t now) {
        std::lock_guard lock(m_lock);
        const auto found = m_cooldowns.find({reinterpret_cast<uintptr_t>(owner), kit});
        if (found == m_cooldowns.end()) {
            return 0;
        }
        if (DeadlineReached(now, found->second)) {
            m_cooldowns.erase(found);
            return 0;
        }
        return found->second - now;
    }

    void Played(const void* owner, uint32_t kit, uint32_t now, uint32_t cooldown) {
        if (!owner || !cooldown) {
            return;
        }
        std::lock_guard lock(m_lock);
        m_cooldowns[{reinterpret_cast<uintptr_t>(owner), kit}] = now + cooldown;
    }

    void Forget(const void* owner) {
        std::lock_guard lock(m_lock);
        const auto identity = reinterpret_cast<uintptr_t>(owner);
        auto it = m_cooldowns.lower_bound({identity, 0});
        while (it != m_cooldowns.end() && it->first.first == identity) {
            it = m_cooldowns.erase(it);
        }
    }

    void Reset() {
        std::lock_guard lock(m_lock);
        m_timings.clear();
        m_cooldowns.clear();
    }

private:
    std::mutex m_lock;
    std::map<uint32_t, Timing> m_timings;
    std::map<std::pair<uintptr_t, uint32_t>, uint32_t> m_cooldowns;
};

inline Policy& State() {
    static Policy policy;
    return policy;
}

template<class Play, class Random>
bool HandleModelEvent(Policy& policy, const void* model, uint32_t kit, uint32_t time, Play play, Random random) {
    Timing timing;
    if (!model || !policy.Find(kit, timing)) {
        return false;
    }

    if (!policy.Remaining(model, kit, time) && play() == 0) {
        const uint32_t cooldown = std::max(1u, random(timing.cooldownMin, timing.cooldownMax));
        policy.Played(model, kit, time, cooldown);
    }
    return true;
}

}

#endif
