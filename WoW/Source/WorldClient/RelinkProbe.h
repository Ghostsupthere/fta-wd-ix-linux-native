#ifndef WOW_SOURCE_WORLDCLIENT_RELINKPROBE_H
#define WOW_SOURCE_WORLDCLIENT_RELINKPROBE_H

#include <cstdint>

namespace RelinkProbe {

struct Faces {
    uint32_t admitted = 0;
    uint64_t fingerprint = 0;
    uint32_t leaves = 0;
    uint32_t digests = 0;
};

bool Enabled();
bool BeginMovementFrame(uint64_t frame);
void EndMovementFrame(uint32_t movers);

// Samples the same mover in two consecutive frames, rotating the mover between pairs.
struct MoverScope {
    MoverScope(uint64_t frame, uint32_t ordinal, uint64_t mover) {
        if (frame) Begin(frame, ordinal, mover);
    }
    ~MoverScope() {
        if (m_sampled) Finish();
    }
    MoverScope(const MoverScope&) = delete;
    MoverScope& operator=(const MoverScope&) = delete;
private:
    void Begin(uint64_t frame, uint32_t ordinal, uint64_t mover);
    void Finish();
    bool m_sampled = false;
};

bool Sampling();
Faces Fingerprint(const uint16_t* indices, uint32_t count);
void RecordGroup(uint32_t placement, uint32_t group, const char* asset, double length,
                 Faces faces, uint32_t visited, uint32_t triangles);

} // namespace RelinkProbe
#endif
