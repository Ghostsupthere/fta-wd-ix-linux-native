#pragma once
// The bookkeeping behind the nameplate API (see WoW/Source/Ui/Linux/WxlNamePlates.h), independent of the engine so it
// can be tested on its own: which plate frame carries which "nameplateN" token, and which unit it shows now.
// A frame keeps its slot (and so its token) for as long as it exists; the unit shown on it changes.
#include <cstdint>

namespace wxl::nameplates {

class Registry {
public:
    static constexpr uint32_t kMaxPlates = 64;

    struct Shown {
        int slot = -1;         // -1: more than kMaxPlates frames, this one gets no token
        bool created = false;  // the frame was seen for the first time
        bool ownerChanged = false;  // it was already showing a unit and now shows another
    };

    // A unit was assigned to `frame` and the frame is shown.
    Shown Show(const void* frame, uint64_t guid) {
        Shown result;
        int slot = Find(frame);
        if (slot < 0) {
            for (uint32_t i = 0; i < kMaxPlates && slot < 0; ++i)
                if (!m_plates[i].frame) slot = static_cast<int>(i);
            if (slot < 0) return result;
            m_plates[slot] = Plate{frame, 0, false};
            result.created = true;
        }
        Plate& plate = m_plates[slot];
        result.slot = slot;
        result.ownerChanged = plate.active;
        plate.guid = guid;
        plate.active = true;
        return result;
    }

    // The frame stops showing its unit (it is recycled). Returns its slot, or -1 when it was not showing one. The unit
    // stays readable until Release, so event handlers can still resolve the token of the unit that is leaving.
    int Hide(const void* frame) const {
        const int slot = Find(frame);
        return slot >= 0 && m_plates[slot].active ? slot : -1;
    }
    void Release(int slot) {
        if (slot >= 0 && slot < static_cast<int>(kMaxPlates)) {
            m_plates[slot].active = false;
            m_plates[slot].guid = 0;
        }
    }

    // The frame no longer exists.
    void Destroy(const void* frame) {
        const int slot = Find(frame);
        if (slot >= 0) m_plates[slot] = Plate{};
    }

    void Reset() {
        for (Plate& plate : m_plates) plate = Plate{};
    }

    int ActiveSlotForGuid(uint64_t guid) const {
        if (!guid) return -1;
        for (uint32_t i = 0; i < kMaxPlates; ++i)
            if (m_plates[i].frame && m_plates[i].active && m_plates[i].guid == guid) return static_cast<int>(i);
        return -1;
    }

    // "nameplate<oneBasedIndex>": the unit shown on that plate, if it shows one.
    bool GuidAtIndex(uint32_t oneBasedIndex, uint64_t& guid) const {
        if (oneBasedIndex < 1 || oneBasedIndex > kMaxPlates) return false;
        const Plate& plate = m_plates[oneBasedIndex - 1];
        if (!plate.frame || !plate.active) return false;
        guid = plate.guid;
        return true;
    }

    const void* FrameAt(int slot) const { return slot >= 0 && slot < static_cast<int>(kMaxPlates) ? m_plates[slot].frame : nullptr; }
    bool ActiveAt(int slot) const { return slot >= 0 && slot < static_cast<int>(kMaxPlates) && m_plates[slot].frame && m_plates[slot].active; }

private:
    struct Plate {
        const void* frame = nullptr;
        uint64_t guid = 0;
        bool active = false;
    };

    int Find(const void* frame) const {
        if (!frame) return -1;
        for (uint32_t i = 0; i < kMaxPlates; ++i)
            if (m_plates[i].frame == frame) return static_cast<int>(i);
        return -1;
    }

    Plate m_plates[kMaxPlates];
};

}  // namespace wxl::nameplates
