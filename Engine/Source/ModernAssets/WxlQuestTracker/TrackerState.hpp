// Native port of the state and maths of the supplied wxl-quest-marker (QuestMarker.cpp) and the
// quest-objective ring half of wxl-selcircle-poc (SelCirclePoc.cpp). Both are GPL-3.0-or-later,
// Copyright (C) 2026 WarcraftXL. The Windows modules hooked Wow.exe and drew with the proxy's D3D9
// device; this header is the engine-independent part (validation, bounds, projection, billboard and
// ring parameters) so it can be tested without the game. Drawing lives in
// WoW/Source/WorldClient/Linux/WxlQuestTracker.cpp and WoW/Source/Object/ObjectClient/Linux/.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace wxl::questtracker {

constexpr size_t kMaxMarkers = 25;               // the 3.3.5a quest log has 25 slots
constexpr uint32_t kMaxObjectiveEntries = 16;    // direct objectives plus item-drop creature sources
constexpr float kMaxMarkerDistance = 100000.0f;
constexpr float kMarkerPixelSize = 16.0f;
constexpr float kMatrixEpsilon = 0.000001f;

struct Marker {
    uint32_t questId = 0;
    bool active = false;
    float x = 0, y = 0, z = 0;
};

struct Settings {
    bool enabled = true;
    bool showMarker = true;
    uint8_t alpha = 165;
};

struct RingSettings {
    bool enabled = true;
    uint8_t alpha = 217;
};

inline bool AllFinite(const float* values, size_t count) {
    if (!values) return false;
    for (size_t i = 0; i < count; ++i)
        if (!std::isfinite(values[i])) return false;
    return true;
}

// Row-vector convention (as the D3D9 matrices the camera globals hold): clip = [x y z 1] * view * proj.
struct Projected {
    float ndcX = 0, ndcY = 0;
    float viewZ = 0;     // distance along the view axis
    bool inFront = false;  // viewZ >= 0.1
    bool wValid = false;   // w >= 0.0001, so ndc is a true projection rather than a parked off-screen value
    bool Usable() const { return inFront && wValid; }
};

// `Usable()` is what the Lua distance queries need (the supplied ProjectCurrentAnchor): in front and w valid.
inline bool Project(const float* view, const float* proj, float x, float y, float z, Projected& out) {
    if (!AllFinite(view, 16) || !AllFinite(proj, 16)) return false;
    const float vx = x * view[0] + y * view[4] + z * view[8] + view[12];
    const float vy = x * view[1] + y * view[5] + z * view[9] + view[13];
    const float vz = x * view[2] + y * view[6] + z * view[10] + view[14];
    const float vw = x * view[3] + y * view[7] + z * view[11] + view[15];
    const float px = vx * proj[0] + vy * proj[4] + vz * proj[8] + vw * proj[12];
    const float py = vx * proj[1] + vy * proj[5] + vz * proj[9] + vw * proj[13];
    const float pw = vx * proj[3] + vy * proj[7] + vz * proj[11] + vw * proj[15];
    out.viewZ = vz;
    out.inFront = std::isfinite(vz) && vz >= 0.1f;
    out.wValid = pw >= 0.0001f;
    if (out.wValid) {
        out.ndcX = px / pw;
        out.ndcY = py / pw;
    } else {
        // Behind the eye: park the point off-screen on its side, as the supplied code does for the corpse label.
        out.ndcX = vx >= 0.0f ? 2.0f : -2.0f;
        out.ndcY = vy >= 0.0f ? 2.0f : -2.0f;
    }
    return std::isfinite(out.ndcX) && std::isfinite(out.ndcY) && std::isfinite(vz);
}

// Screen-facing quad transform for a world-space anchor: right and up rows of the view matrix scaled
// so the icon keeps a constant on-screen size (kMarkerPixelSize). Returns false for a degenerate camera.
inline bool BillboardWorld(const float* view, const float* proj, float viewportHeight, float x, float y,
                           float z, float viewZ, float out[16]) {
    if (!AllFinite(view, 16) || !std::isfinite(viewportHeight) || viewportHeight <= 0.0f) return false;
    const float yScale = proj[5];
    if (!std::isfinite(yScale) || std::abs(yScale) <= kMatrixEpsilon) return false;
    const float rightX = view[0], rightY = view[4], rightZ = view[8];
    const float upX = view[1], upY = view[5], upZ = view[9];
    if (rightX == 0.0f && rightY == 0.0f && rightZ == 0.0f) return false;
    const float s = kMarkerPixelSize * 2.0f * viewZ / (yScale * viewportHeight);
    if (!std::isfinite(s)) return false;
    for (int i = 0; i < 16; ++i) out[i] = 0.0f;
    out[0] = rightX * s; out[1] = rightY * s; out[2] = rightZ * s;
    out[4] = upX * s;    out[5] = upY * s;    out[6] = upZ * s;
    out[12] = x; out[13] = y; out[14] = z; out[15] = 1.0f;
    return true;
}

// Projection with the far plane pushed out so distant markers are not clipped (the supplied draw does
// this): z' = zf / (zf - zn), w' = -zn * zf / (zf - zn) with zn recovered from the engine's matrix.
inline bool ExtendFarPlane(const float* proj, float out[16]) {
    if (!AllFinite(proj, 16) || std::abs(proj[10]) <= kMatrixEpsilon || std::abs(proj[5]) <= kMatrixEpsilon)
        return false;
    for (int i = 0; i < 16; ++i) out[i] = proj[i];
    const float zn = -proj[14] / proj[10];
    const float zf = 100000.0f;
    if (!std::isfinite(zn) || std::abs(zf - zn) <= kMatrixEpsilon) return false;
    out[10] = zf / (zf - zn);
    out[14] = -zn * zf / (zf - zn);
    return true;
}

// Atlas cells of interface\navigation\ingamenavigationui.blp (64x64): quest arrow, corpse stone.
struct Uv { float u0, v0, u1, v1; };
constexpr Uv kQuestIconUv{0.4395f, 0.002f, 0.8725f, 0.543875f};
constexpr Uv kCorpseIconUv{0.002f, 0.002f, 0.4355f, 0.52825f};
constexpr float kQuestIconAspect = 28.0f / 35.0f;
constexpr float kCorpseIconAspect = 28.0f / 34.0f;

// A Lua number is accepted as a finite value within +/- limit.
inline bool FiniteWithin(double value, double limit) { return std::isfinite(value) && std::abs(value) <= limit; }

class State {
public:
    // --- markers (WarcraftXL_TrackerSetMarker / ClearMarkers) ---
    bool SetMarker(double questRaw, double activeRaw, double x, double y, double z) {
        if (!std::isfinite(questRaw) || !std::isfinite(activeRaw) || questRaw < 0.0 || questRaw > 4294967295.0 ||
            !FiniteWithin(x, 1000000.0) || !FiniteWithin(y, 1000000.0) || !FiniteWithin(z, 1000000.0))
            return false;
        const uint32_t id = static_cast<uint32_t>(questRaw);
        const float fx = static_cast<float>(x), fy = static_cast<float>(y), fz = static_cast<float>(z);
        if (!std::isfinite(fx) || !std::isfinite(fy) || !std::isfinite(fz)) return true;   // refused quietly, as upstream
        if (activeRaw != 0.0) {
            for (Marker& m : markers_)
                if (m.questId == id) { m.active = true; m.x = fx; m.y = fy; m.z = fz; return true; }
            // Refuse unbounded unique ids from malformed add-on messages.
            if (markers_.size() < kMaxMarkers) markers_.push_back({id, true, fx, fy, fz});
        } else {
            for (auto it = markers_.begin(); it != markers_.end(); ++it)
                if (it->questId == id) { markers_.erase(it); break; }
        }
        return true;
    }
    void ClearMarkers() { markers_.clear(); markerValid_ = false; }
    const std::vector<Marker>& Markers() const { return markers_; }

    // --- corpse marker ---
    bool SetCorpse(double x, double y, double z) {
        if (!FiniteWithin(x, 1000000.0) || !FiniteWithin(y, 1000000.0) || !FiniteWithin(z, 1000000.0)) return false;
        corpseActive_ = true;
        corpse_[0] = static_cast<float>(x); corpse_[1] = static_cast<float>(y); corpse_[2] = static_cast<float>(z);
        return true;
    }
    void ClearCorpse() { corpseActive_ = false; }
    bool CorpseActive() const { return corpseActive_; }
    const float* Corpse() const { return corpse_; }

    bool SetDead(double raw) {
        if (!std::isfinite(raw)) return false;
        dead_ = raw != 0.0;
        return true;
    }
    bool Dead() const { return dead_; }

    // --- settings (WarcraftXL_TrackerSetSettings: enabled, showMarker, alpha) ---
    bool SetSettings(double enabled, double show, double alpha) {
        if (!std::isfinite(enabled) || !std::isfinite(show) || !std::isfinite(alpha)) return false;
        settings_.enabled = enabled != 0.0;
        settings_.showMarker = show != 0.0;
        settings_.alpha = static_cast<uint8_t>(std::clamp(alpha, 0.0, 255.0));
        if (!settings_.enabled || !settings_.showMarker) markerValid_ = false;
        return true;
    }
    bool SetMarkerAlpha(double raw) {
        if (!std::isfinite(raw)) return false;
        settings_.alpha = static_cast<uint8_t>(std::clamp(raw, 0.0, 255.0));
        return true;
    }
    const Settings& GetSettings() const { return settings_; }

    // --- screen-space diamond (WarcraftXL_TrackerSetDiamond: active, x, y, halfW, halfH in NDC) ---
    bool SetDiamond(double active, double x, double y, double halfW, double halfH) {
        if (!std::isfinite(active) || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(halfW) ||
            !std::isfinite(halfH) || std::abs(x) > 4.0 || std::abs(y) > 4.0 || halfW < 0.0 || halfW > 2.0 ||
            halfH < 0.0 || halfH > 2.0)
            return false;
        diamondActive_ = active != 0.0;
        diamond_[0] = static_cast<float>(x); diamond_[1] = static_cast<float>(y);
        diamond_[2] = static_cast<float>(halfW); diamond_[3] = static_cast<float>(halfH);
        return true;
    }
    bool DiamondActive() const { return diamondActive_; }
    const float* Diamond() const { return diamond_; }

    // --- objective rings (WarcraftXL_TrackerSetObjectiveEntries / Clear / SetRingSettings) ---
    // Arguments: count, then `count` creature entries. Entries outside 1..0x00FFFFFF are skipped and
    // duplicates collapsed; a count outside 0..16 or a missing/non-finite entry rejects the whole call.
    bool SetObjectiveEntries(const double* args, size_t argc) {
        // A rejected call leaves the previous entries in place, as upstream does.
        if (!args || argc < 1 || !std::isfinite(args[0]) || args[0] < 0.0 || args[0] > kMaxObjectiveEntries)
            return false;
        const uint32_t count = static_cast<uint32_t>(args[0]);
        if (argc < 1 + static_cast<size_t>(count)) return false;
        uint32_t entries[kMaxObjectiveEntries] = {};
        uint32_t real = 0;
        for (uint32_t i = 0; i < count; ++i) {
            const double raw = args[1 + i];
            if (!std::isfinite(raw)) return false;
            if (raw < 1.0 || raw > 16777215.0) continue;
            const uint32_t entry = static_cast<uint32_t>(raw);
            if (std::find(entries, entries + real, entry) == entries + real) entries[real++] = entry;
        }
        for (uint32_t i = 0; i < kMaxObjectiveEntries; ++i) objective_[i] = entries[i];
        objectiveCount_ = real;
        return true;
    }
    void ClearObjectiveEntries() {
        objectiveCount_ = 0;
        for (uint32_t& e : objective_) e = 0;
    }
    bool ObjectiveMatches(uint32_t entry) const {
        return entry && std::find(objective_, objective_ + objectiveCount_, entry) != objective_ + objectiveCount_;
    }
    uint32_t ObjectiveCount() const { return objectiveCount_; }
    bool SetRingSettings(double enabled, double alpha) {
        if (!std::isfinite(enabled) || !std::isfinite(alpha)) return false;
        ring_.enabled = enabled != 0.0;
        ring_.alpha = static_cast<uint8_t>(std::clamp(alpha, 0.0, 255.0));
        return true;
    }
    const RingSettings& GetRingSettings() const { return ring_; }
    bool RingsActive() const { return ring_.enabled && objectiveCount_ > 0; }

    // A new world session starts clean: markers surviving from the previous one are stale.
    void ResetSession() {
        ClearMarkers();
        ClearCorpse();
        dead_ = false;
        diamondActive_ = false;
        ClearObjectiveEntries();
    }

    // Cached label data from the last draw (what GetMarkerInfo reports).
    bool markerValid_ = false;

private:
    std::vector<Marker> markers_;
    bool corpseActive_ = false;
    float corpse_[3] = {};
    bool dead_ = false;
    Settings settings_;
    bool diamondActive_ = false;
    float diamond_[4] = {};
    uint32_t objective_[kMaxObjectiveEntries] = {};
    uint32_t objectiveCount_ = 0;
    RingSettings ring_;
};

// ---- objective rings ----
constexpr float kRingMaxDistance = 60.0f;      // player distance cap in yards
constexpr uint32_t kRingMaxPerFrame = 24;
constexpr float kRingBrightness = 0.40f;       // the custom ring texture reads as too bright at full strength
constexpr uint32_t kRingColor = 0xFFFFC800;    // AARRGGBB, yellow; replaces the unit's own selection colour

// Quest-ring colour as a D3DCOLOR: fixed hue, RGB scaled by the brightness knob, alpha by the user's
// ring opacity (WarcraftXL_TrackerSetRingSettings alpha / 255).
inline uint32_t RingColour(uint8_t ringAlpha) {
    const float opacity = static_cast<float>(ringAlpha) / 255.0f;
    const uint32_t a = static_cast<uint32_t>(static_cast<float>(kRingColor >> 24) * opacity) & 0xFFu;
    const uint32_t r = static_cast<uint32_t>(static_cast<float>((kRingColor >> 16) & 0xFFu) * kRingBrightness) & 0xFFu;
    const uint32_t g = static_cast<uint32_t>(static_cast<float>((kRingColor >> 8) & 0xFFu) * kRingBrightness) & 0xFFu;
    const uint32_t b = static_cast<uint32_t>(static_cast<float>(kRingColor & 0xFFu) * kRingBrightness) & 0xFFu;
    return (a << 24) | (r << 16) | (g << 8) | b;
}

// Plausibility of the radius the engine reports (the native renderer sizes the ring from it).
constexpr float kRingMinRadius = 0.5f, kRingMaxRadius = 30.0f;
inline bool RingRadiusUsable(float r) { return std::isfinite(r) && r >= kRingMinRadius && r <= kRingMaxRadius; }

}  // namespace wxl::questtracker
