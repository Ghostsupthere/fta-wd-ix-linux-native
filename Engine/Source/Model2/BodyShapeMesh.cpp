#include "Engine/Source/Retail/BodyShapeMesh.h"

#include <cmath>
#include <iterator>
#include <cstring>

#include "Engine/Source/Retail/BodyShapeData/FtaProfiles.hpp"
#include "Engine/Source/Retail/BodyShapeData/Profiles.hpp"

namespace {

// Every audited profile's bounds are the mesh's bind-pose bounds padded by this much on each side.
constexpr float BoundsPad = 0.2f;
constexpr float BoundsTolerance = 0.01f;

body_shape::Vec PositionOf(const float* bind, uint32_t i) {
    return { bind[6 * i], bind[6 * i + 1], bind[6 * i + 2] };
}

} // namespace

namespace BodyShapeMesh {

const void* FindProfile(const float* bind, uint32_t count) {
    if (!bind || !count) {
        return nullptr;
    }

    float lo[3] = { 1e9f, 1e9f, 1e9f };
    float hi[3] = { -1e9f, -1e9f, -1e9f };

    for (uint32_t i = 0; i < count; ++i) {
        for (int a = 0; a < 3; ++a) {
            float v = bind[6 * i + a];

            if (std::isfinite(v)) {
                lo[a] = std::fmin(lo[a], v);
                hi[a] = std::fmax(hi[a], v);
            }
        }
    }

    const body_shape::Profile* best = nullptr;
    size_t bestHits = 0;
    float bestDistance = 1e9f; // bounds distance of the best masked candidate: bodies that share vertices (Draenei, Eredar) still pick their own

    // The retail profiles first, then FTA's own (tools/bodyshape-profiles).
    const body_shape::Profile* tables[2][2] = { { std::begin(body_shape::Profiles), std::end(body_shape::Profiles) },
        { std::begin(body_shape::FtaProfiles), std::end(body_shape::FtaProfiles) } };

    for (auto& table : tables)
    for (const body_shape::Profile* it = table[0]; it != table[1]; ++it) {
        const body_shape::Profile& profile = *it;
        const float* min = &profile.minimum.x;
        const float* max = &profile.maximum.x;
        bool bounds = true;
        float distance = 0.0f;

        for (int a = 0; a < 3; ++a) {
            float dLo = std::fabs(lo[a] - (min[a] + BoundsPad));
            float dHi = std::fabs(hi[a] - (max[a] - BoundsPad));
            bounds = bounds && dLo < BoundsTolerance && dHi < BoundsTolerance;
            distance += dLo + dHi;
        }

        if (!bounds) {
            continue;
        }

        if (!profile.mask) {
            // No per-vertex mask (human, dracthyr): the bounds are the whole fingerprint.
            if (!best) {
                best = &profile;
            }
            continue;
        }

        // Masked profiles match vertices by exact bind position, so count how many of the model's vertices hit.
        size_t hits = 0;

        for (uint32_t i = 0; i < count; ++i) {
            if (body_shape::VertexMask(PositionOf(bind, i), profile)) {
                ++hits;
            }
        }

        bool better = distance < bestDistance - 1e-6f || (std::fabs(distance - bestDistance) <= 1e-6f && hits > bestHits);

        if (hits >= profile.maskSize * 95 / 100 && better) {
            best = &profile;
            bestHits = hits;
            bestDistance = distance;
        }
    }

    return best;
}

const char* Name(const void* profile) {
    return profile ? static_cast<const body_shape::Profile*>(profile)->path : "";
}

bool Apply(const void* profileData, const float* bind, uint32_t count, float breast, float butt, float* out) {
    const body_shape::Profile& profile = *static_cast<const body_shape::Profile*>(profileData);
    bool moved = false;

    memcpy(out, bind, sizeof(float) * 6 * count);

    for (uint32_t i = 0; i < count; ++i) {
        body_shape::Vec p = PositionOf(bind, i);
        uint32_t mask = body_shape::VertexMask(p, profile);
        float strength = static_cast<float>(mask >> 8) / 255.0f;
        float a = (mask & 1) ? breast * strength : 0.0f;
        float b = (mask & 2) ? butt * strength : 0.0f;

        if (a == 0.0f && b == 0.0f) {
            continue;
        }

        body_shape::Vec q = body_shape::Deform(p, a, b, profile);
        body_shape::Vec n = body_shape::Normal(p, { bind[6 * i + 3], bind[6 * i + 4], bind[6 * i + 5] }, a, b, profile);
        out[6 * i] = q.x;
        out[6 * i + 1] = q.y;
        out[6 * i + 2] = q.z;
        out[6 * i + 3] = n.x;
        out[6 * i + 4] = n.y;
        out[6 * i + 5] = n.z;
        moved = moved || q.x != p.x || q.y != p.y || q.z != p.z;
    }

    return moved;
}

} // namespace BodyShapeMesh
