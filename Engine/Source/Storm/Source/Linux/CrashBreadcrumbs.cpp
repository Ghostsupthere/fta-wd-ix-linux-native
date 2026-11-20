#include "CrashBreadcrumbs.h"
#include <atomic>
#include <cstddef>
#include <cstring>
#include <ctime>
#include <initializer_list>
#include <sys/syscall.h>
#include <unistd.h>

namespace native_crash
{
    namespace
    {
        struct Payload
        {
            uint64_t sequence{}, milliseconds{};
            uint32_t event{}, thread{}, value{}, extra{};
            char asset[184]{};
        };
        struct alignas(64) Slot
        {
            std::atomic_flag busy = ATOMIC_FLAG_INIT;
            Payload payload;
        };
        static_assert(sizeof(Slot) == 256, "crash breadcrumb slot budget");
        constexpr unsigned RingSize = 64;
        Slot ring[RingSize];
        std::atomic<uint64_t> next{};
        std::atomic<uint32_t> backend{static_cast<uint32_t>(Backend::Unknown)};
        std::atomic<uint32_t> backendSamples{};

        uint64_t Milliseconds() noexcept
        {
            timespec now{};
            clock_gettime(CLOCK_MONOTONIC, &now);
            return static_cast<uint64_t>(now.tv_sec) * 1000 + static_cast<uint64_t>(now.tv_nsec) / 1000000;
        }

        char const* BackendName(uint32_t value) noexcept
        {
            switch (static_cast<Backend>(value))
            {
                case Backend::Vulkan: return "Vulkan";
                case Backend::D3d9: return "Direct3D 9";
                case Backend::D3d9Ex: return "Direct3D 9Ex";
                case Backend::D3d11: return "Direct3D 11";
                case Backend::Other: return "other graphics backend";
                default: return "not initialized";
            }
        }

        char const* EventName(uint32_t value) noexcept
        {
            switch (static_cast<Event>(value))
            {
                case Event::Backend: return "graphics backend initialized";
                case Event::MapLoad: return "map load requested";
                case Event::MapUnload: return "map unload requested";
                case Event::ModelLoad: return "model load requested";
                case Event::ModelFailed: return "model load rejected";
                case Event::IndexGuard: return "invalid model picking index rejected safely";
                default: return "unknown operation";
            }
        }

        // Only a bounded virtual asset path is kept (no absolute paths, no "..", no quoting characters).
        void Asset(char (&out)[184], char const* input) noexcept
        {
            out[0] = 0;
            if (!input || !*input)
                return;
            std::size_t length = 0;
            for (; length < sizeof(out) && input[length]; ++length)
            {
                unsigned char const value = static_cast<unsigned char>(input[length]);
                if (value < 32 || value == 127 || input[length] == ':' || input[length] == '"' ||
                    input[length] == '=' || input[length] == '\'')
                    break;
            }
            bool allowed = length > 3 && length < sizeof(out) && input[length] == 0 &&
                input[0] != '/' && input[0] != '\\';
            bool prefix = false;
            for (char const* root : {"character", "creature", "item", "spells", "world", "environments", "dungeons", "wxl"})
            {
                std::size_t const size = std::strlen(root);
                if (length > size && (input[size] == '/' || input[size] == '\\'))
                {
                    bool equal = true;
                    for (std::size_t i = 0; i < size; ++i)
                    {
                        char const character = input[i] >= 'A' && input[i] <= 'Z' ? input[i] + ('a' - 'A') : input[i];
                        if (character != root[i])
                            equal = false;
                    }
                    prefix = prefix || equal;
                }
            }
            allowed = allowed && prefix;
            for (std::size_t i = 1; i < length; ++i)
                if (input[i - 1] == '.' && input[i] == '.')
                    allowed = false;
            if (!allowed)
            {
                std::strcpy(out, "[asset reference omitted: not a bounded virtual asset path]");
                return;
            }
            std::memcpy(out, input, length);
            out[length] = 0;
        }

        // Small formatter: no stdio, no allocation.
        struct Line
        {
            char text[320];
            std::size_t used = 0;
            void Add(char const* value) noexcept
            {
                for (; *value && used + 1 < sizeof(text); ++value)
                    text[used++] = static_cast<unsigned char>(*value) >= 32 && *value != 127 ? *value : '?';
                text[used] = 0;
            }
            void Newline() noexcept
            {
                if (used + 1 < sizeof(text))
                    text[used++] = '\n';
                text[used] = 0;
            }
            void Add(uint64_t value) noexcept
            {
                char digits[24];
                int at = sizeof(digits) - 1;
                digits[at] = 0;
                do
                {
                    digits[--at] = static_cast<char>('0' + value % 10);
                    value /= 10;
                } while (value && at > 0);
                Add(&digits[at]);
            }
        };

        void Write(Payload const& payload, uint64_t origin, LineWriter write) noexcept
        {
            Line line;
            line.text[0] = 0;
            line.Add("  [");
            uint64_t const since = payload.milliseconds >= origin ? payload.milliseconds - origin : 0;
            line.Add(since / 1000);
            line.Add(".");
            uint64_t const part = since % 1000;
            if (part < 100) line.Add("0");
            if (part < 10) line.Add("0");
            line.Add(part);
            line.Add("s thread ");
            line.Add(payload.thread);
            line.Add("] ");
            line.Add(EventName(payload.event));
            switch (static_cast<Event>(payload.event))
            {
                case Event::MapLoad:
                    line.Add(" zone=");
                    line.Add(payload.value);
                    break;
                case Event::IndexGuard:
                    line.Add(" index=");
                    line.Add(payload.value);
                    line.Add(" sectionStart=");
                    line.Add(payload.extra);
                    break;
                default:
                    break;
            }
            if (payload.asset[0])
            {
                line.Add(" ");
                line.Add(payload.asset);
            }
            line.Newline();
            write(line.text);
        }
    }

    void Record(Event event, char const* virtualAsset, uint32_t value, uint32_t extra) noexcept
    {
        uint64_t const sequence = next.fetch_add(1, std::memory_order_relaxed) + 1;
        Slot& slot = ring[sequence % RingSize];
        if (slot.busy.test_and_set(std::memory_order_acquire))
            return; // another writer is lapping this slot; losing one breadcrumb is fine
        Payload& payload = slot.payload;
        payload.sequence = sequence;
        payload.milliseconds = Milliseconds();
        payload.event = static_cast<uint32_t>(event);
        payload.thread = static_cast<uint32_t>(syscall(SYS_gettid));
        payload.value = value;
        payload.extra = extra;
        Asset(payload.asset, virtualAsset);
        slot.busy.clear(std::memory_order_release);
    }

    void SetBackend(Backend selected, uint32_t samples) noexcept
    {
        backend.store(static_cast<uint32_t>(selected), std::memory_order_relaxed);
        backendSamples.store(samples, std::memory_order_relaxed);
        Record(Event::Backend, nullptr, static_cast<uint32_t>(selected), samples);
    }

    void RecordIndexGuard(char const* virtualAsset, uint32_t index, uint32_t start) noexcept
    {
        Record(Event::IndexGuard, virtualAsset, index, start);
    }

    void WriteRecent(LineWriter write) noexcept
    {
        if (!write)
            return;
        Line head;
        head.text[0] = 0;
        head.Add("Graphics backend: ");
        head.Add(BackendName(backend.load(std::memory_order_relaxed)));
        head.Newline();
        head.Add("Recent engine events (oldest first):");
        head.Newline();
        write(head.text);

        uint64_t const end = next.load(std::memory_order_relaxed);
        unsigned const count = static_cast<unsigned>(end > RingSize ? RingSize : end);
        if (!count)
        {
            write("  (none recorded)\n");
            return;
        }
        uint64_t const first = end - count + 1;
        uint64_t origin = 0;
        bool haveOrigin = false;
        for (unsigned i = 0; i < count; ++i)
        {
            uint64_t const sequence = first + i;
            Slot& slot = ring[sequence % RingSize];
            if (slot.busy.test_and_set(std::memory_order_acquire))
                continue;
            Payload const payload = slot.payload;
            slot.busy.clear(std::memory_order_release);
            if (payload.sequence != sequence)
                continue;
            if (!haveOrigin)
            {
                origin = payload.milliseconds;
                haveOrigin = true;
            }
            Write(payload, origin, write);
        }
    }
}
