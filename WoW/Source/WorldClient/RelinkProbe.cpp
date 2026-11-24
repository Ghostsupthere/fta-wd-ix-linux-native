#include "RelinkProbe.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace RelinkProbe {
namespace {
constexpr uint64_t SamplePeriod = 64;
constexpr size_t MaxGroups = 24;
struct Group {
    uint32_t placement, group, visited, triangles;
    double length;
    Faces faces;
    char asset[260];
};
struct Sample {
    uint64_t frame = 0;
    uint64_t mover = 0;
    uint32_t count = 0, dropped = 0;
    std::array<Group, MaxGroups> groups;
};

// Only the movement updater selects a sample. WMO visits occur synchronously on that thread.
thread_local bool sampling;
thread_local uint64_t lastSampleFrame;
thread_local uint32_t previousMoverCount;
Sample first{}, second{};
Sample* current;

void Report() {
    char buffer[2 * MaxGroups * 384 + 256];
    int used = std::snprintf(buffer, sizeof(buffer),
        "WarcraftXL relink probe: frames=%llu/%llu groups=%u/%u dropped=%u/%u",
        static_cast<unsigned long long>(first.frame), static_cast<unsigned long long>(second.frame),
        first.count, second.count, first.dropped, second.dropped);
    // Match the nth occurrence of a placement/group across frames. The fallback segment can query it twice.
    std::array<bool, MaxGroups> matched{};
    for (uint32_t i = 0; i < first.count && used > 0 && static_cast<size_t>(used) < sizeof(buffer); ++i) {
        const auto& a = first.groups[i];
        const Group* b = nullptr;
        for (uint32_t j = 0; j < second.count; ++j) {
            const auto& candidate = second.groups[j];
            if (!matched[j] && candidate.placement == a.placement && candidate.group == a.group &&
                std::strcmp(candidate.asset, a.asset) == 0) {
                b = &candidate;
                matched[j] = true;
                break;
            }
        }
        used += std::snprintf(buffer + used, sizeof(buffer) - used,
            " | %.160s group=%u placement=%u yd=%.2f leaves=%u digests=%u visited=%u tris=%u admitted=%u repeat=%s",
            a.asset, a.group, a.placement, a.length, a.faces.leaves, a.faces.digests, a.visited, a.triangles,
            a.faces.admitted, !b ? "absent" :
                (a.faces.admitted == b->faces.admitted && a.faces.fingerprint == b->faces.fingerprint)
                ? "same-sequence" : "changed-sequence");
        if (b && used > 0 && static_cast<size_t>(used) < sizeof(buffer)) {
            used += std::snprintf(buffer + used, sizeof(buffer) - used,
                " nextYd=%.2f nextLeaves=%u nextVisited=%u nextTris=%u nextAdmitted=%u",
                b->length, b->faces.leaves, b->visited, b->triangles, b->faces.admitted);
        }
    }
    for (uint32_t i = 0; i < second.count && used > 0 && static_cast<size_t>(used) < sizeof(buffer); ++i) {
        if (matched[i]) continue;
        const auto& b = second.groups[i];
        used += std::snprintf(buffer + used, sizeof(buffer) - used,
            " | %.160s group=%u placement=%u yd=%.2f leaves=%u digests=%u visited=%u tris=%u admitted=%u repeat=new",
            b.asset, b.group, b.placement, b.length, b.faces.leaves, b.faces.digests, b.visited, b.triangles, b.faces.admitted);
    }
    std::fprintf(stderr, "%s\n", buffer);
}
}

bool Enabled() {
    static const bool enabled = [] {
        const char* value = std::getenv("WXL_PROFILE_RELINK_DETAIL");
        return value && std::strcmp(value, "1") == 0;
    }();
    return enabled;
}

bool BeginMovementFrame(uint64_t frame) {
    if (!frame || lastSampleFrame == frame) return false;
    const uint64_t phase = frame % SamplePeriod;
    if (phase == 0) first.frame = 0;
    return phase <= 1;
}

void EndMovementFrame(uint32_t movers) {
    previousMoverCount = movers;
}

void MoverScope::Begin(uint64_t frame, uint32_t ordinal, uint64_t mover) {
    if (!mover || sampling || frame == lastSampleFrame) return;
    const uint64_t phase = frame % SamplePeriod;
    if (phase == 0 && ordinal == (frame / SamplePeriod) % std::max(previousMoverCount, 1u)) {
        current = &first;
    } else if (phase == 1 && first.frame == frame - 1 && first.mover == mover) {
        current = &second;
    } else {
        return;
    }
    current->frame = frame;
    current->mover = mover;
    current->count = current->dropped = 0;
    lastSampleFrame = frame;
    sampling = m_sampled = true;
}

void MoverScope::Finish() {
    sampling = false;
    if (current == &second) Report();
    current = nullptr;
}

bool Sampling() { return sampling; }

Faces Fingerprint(const uint16_t* indices, uint32_t count) {
    uint64_t fingerprint = 14695981039346656037ull;
    for (uint32_t i = 0; i < count; ++i)
        fingerprint = (fingerprint ^ indices[i]) * 1099511628211ull;
    return {count, fingerprint};
}

void RecordGroup(uint32_t placement, uint32_t group, const char* asset, double length,
                 Faces faces, uint32_t visited, uint32_t triangles) {
    if (!sampling) return;
    if (current->count == current->groups.size()) {
        ++current->dropped;
        return;
    }
    auto& sample = current->groups[current->count++];
    sample.placement = placement;
    sample.group = group;
    sample.visited = visited;
    sample.triangles = triangles;
    sample.length = length;
    sample.faces = faces;
    std::snprintf(sample.asset, sizeof(sample.asset), "%.259s", asset);
}

} // namespace RelinkProbe
