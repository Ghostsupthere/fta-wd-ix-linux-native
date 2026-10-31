#pragma once
// Bounded request ownership for the accepted-shot handoff of WarcraftXL's wxl-lone-wolf
// (GPL-3.0-or-later, Copyright (C) 2026 WarcraftXL). Unchanged apart from the packet readers, which
// the native port does not need: it reads the reconstructed cast structures. Covered by
// tests/wxl_lone_wolf_intent_test.cpp.

#include <array>
#include <cstddef>
#include <cstdint>

namespace wxl::lonewolf::intent
{
    struct Request
    {
        std::uintptr_t cast = 0;
        std::uint64_t caster = 0;
        std::uint64_t target = 0;
        std::uint32_t spell = 0;
        std::uint8_t count = 0;

        bool operator==(Request const& other) const
        {
            return cast == other.cast && caster == other.caster && target == other.target &&
                spell == other.spell && count == other.count;
        }
    };

    struct Stamp
    {
        Request request;
        std::uint64_t serial = 0;
    };

    // The runtime owns synchronization. Native cast storage is never extended or retained here.
    class State
    {
        std::array<Stamp, 64> requests{};
        std::uint64_t serial = 1;
        std::uint64_t authority = 0;
        std::uint64_t selectionOwner = 0;
        std::size_t next = 0;

        std::uint64_t Advance()
        {
            if (!++serial)
                ++serial;
            return serial;
        }

    public:
        std::uint64_t Revision() const { return serial; }
        void Invalidate()
        {
            Advance();
            requests = {};
        }
        void Reset()
        {
            Invalidate();
            authority = selectionOwner = 0;
            next = 0;
        }
        void Authority(std::uint64_t guid)
        {
            if (authority != guid)
            {
                authority = guid;
                // Keep an in-flight selection's ownership until its callback can safely roll back.
                Invalidate();
            }
        }
        void Release(std::uintptr_t cast)
        {
            for (auto& entry : requests)
                if (entry.request.cast == cast)
                    entry = {};
        }
        Stamp Sent(Request const& request)
        {
            if (!request.cast || !request.caster || request.caster != authority || !request.target ||
                !request.spell || !request.count)
                return {};
            Release(request.cast);
            Stamp const stamp{request, Advance()};
            requests[next] = stamp;
            next = (next + 1) % requests.size();
            return stamp;
        }
        Stamp Consume(Request const& request)
        {
            Stamp result;
            for (auto& entry : requests)
            {
                if (entry.request.cast != request.cast)
                    continue;
                if (entry.request == request)
                    result = entry;
                entry = {}; // Consume before native completion can signal Lua or recycle this node.
            }
            return result;
        }
        bool Current(Stamp const& stamp) const
        {
            return stamp.serial && stamp.serial == serial && stamp.request.caster == authority;
        }
        std::uint64_t Select()
        {
            Invalidate(); // An explicit repeated zero is still a newer user/script decision.
            selectionOwner = serial;
            return selectionOwner;
        }
        bool OwnsSelection(std::uint64_t token) const
        {
            return token && selectionOwner == token;
        }
        bool CurrentSelection(std::uint64_t token) const
        {
            return OwnsSelection(token) && serial == token;
        }
        bool ReleaseSelection(std::uint64_t token)
        {
            if (!OwnsSelection(token))
                return false;
            selectionOwner = 0;
            return true;
        }
    };

    struct Completion
    {
        Stamp stamp;
        std::uint64_t target = 0;
        bool candidate = false;
        bool normal = false;
        bool cancelled = false;

        Completion() = default;
        Completion(Stamp const& sent, std::uint32_t flags, bool parsed, std::uint64_t authoritativeTarget) :
            stamp(sent), target(authoritativeTarget), candidate(sent.serial && !(flags & 1) && parsed) { }
        void Finish(bool normalReturn)
        {
            if (normalReturn)
                normal = true;
            else
                cancelled = true;
        }
        void Released()
        {
            if (!normal)
                cancelled = true;
        }
        bool Accepted(State const& state) const
        {
            return candidate && normal && !cancelled && state.Current(stamp);
        }
    };
}
