#ifndef WOW_SOURCE_NET_NETCLIENT_NETWORKPOLLPROFILE_H
#define WOW_SOURCE_NET_NETCLIENT_NETWORKPOLLPROFILE_H

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>

namespace NetworkPollProfile {

constexpr uint32_t EventBase = 0x10000;
constexpr uint32_t ShortPacket = 0x20000;
constexpr uint32_t Idle = ShortPacket + 1;

inline uint32_t PacketKey(const void* data, uint32_t size) {
    if (!data || size < 2) return ShortPacket;
    const auto* bytes = static_cast<const unsigned char*>(data);
    return bytes[0] | (static_cast<uint32_t>(bytes[1]) << 8);
}

class Sample {
    using Clock = std::chrono::steady_clock;
    struct Totals {
        uint32_t count = 0;
        uint64_t ns = 0;

        void Add(uint64_t elapsed) {
            ++count;
            ns += elapsed;
        }
    };
    struct Bucket {
        uint32_t key = 0;
        Totals totals;
    };

    std::array<Bucket, 64> m_buckets{};
    Totals m_callbacks;
    Totals m_other;
    size_t m_used = 0;
    uint64_t m_frame;
    Clock::time_point m_started = Clock::now();

public:
    explicit Sample(uint64_t frame) : m_frame(frame) {}
    ~Sample() {
        Log(stderr, std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - m_started).count());
    }
    Sample(const Sample&) = delete;
    Sample& operator=(const Sample&) = delete;

    void Record(uint32_t key, uint64_t elapsed) {
        m_callbacks.Add(elapsed);
        for (size_t i = 0; i < m_used; ++i) {
            if (m_buckets[i].key == key) {
                m_buckets[i].totals.Add(elapsed);
                return;
            }
        }
        if (m_used < m_buckets.size()) {
            Bucket& bucket = m_buckets[m_used++];
            bucket.key = key;
            bucket.totals.Add(elapsed);
        } else {
            m_other.Add(elapsed);
        }
    }

    void Log(FILE* out, uint64_t elapsed) const {
        if (elapsed < 10'000'000) return;
        const uint64_t outside = elapsed > m_callbacks.ns ? elapsed - m_callbacks.ns : 0;
        std::fprintf(out, "WarcraftXL network poll: frame=%llu poll=%.3f ms callbacks=%.3f ms count=%u outside=%.3f ms",
            static_cast<unsigned long long>(m_frame), elapsed * 1.0e-6,
            m_callbacks.ns * 1.0e-6, m_callbacks.count, outside * 1.0e-6);
        for (size_t i = 0; i < m_used; ++i) {
            const auto& [key, totals] = m_buckets[i];
            if (key < EventBase) {
                std::fprintf(out, " | opcode=0x%04x", key);
            } else if (key == ShortPacket || key == Idle) {
                std::fprintf(out, " | %s", key == Idle ? "idle" : "short-packet");
            } else {
                std::fprintf(out, " | event=%u", key - EventBase);
            }
            std::fprintf(out, " count=%u ms=%.3f", totals.count, totals.ns * 1.0e-6);
        }
        if (m_other.count) {
            std::fprintf(out, " | other count=%u ms=%.3f", m_other.count, m_other.ns * 1.0e-6);
        }
        std::fputc('\n', out);
    }

    class Callback {
        Sample* m_sample;
        uint32_t m_key;
        Clock::time_point m_started;

    public:
        Callback(Sample* sample, uint32_t key)
            : m_sample(sample), m_key(key), m_started(sample ? Clock::now() : Clock::time_point{}) {}
        ~Callback() {
            if (!m_sample) return;
            const auto elapsed = Clock::now() - m_started;
            m_sample->Record(m_key, std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count());
        }
        Callback(const Callback&) = delete;
        Callback& operator=(const Callback&) = delete;
    };
};

}

#endif
