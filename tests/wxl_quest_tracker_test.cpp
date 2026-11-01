#include "Engine/Source/ModernAssets/WxlQuestTracker/TrackerState.hpp"
#include <cstring>
#include <cassert>
#include <cmath>
#include <limits>

using namespace wxl::questtracker;

static const float kNaN = std::numeric_limits<float>::quiet_NaN();
static const double kInf = std::numeric_limits<double>::infinity();

// A look-down-the-+Y-axis camera at the origin with a simple perspective projection (row-vector convention).
static void Camera(float view[16], float proj[16]) {
    // x -> view x, z -> view y (up), y -> view z (forward): [x y z 1] * M with M[row][col] = m[row*4+col].
    for (int i = 0; i < 16; ++i) view[i] = proj[i] = 0;
    view[0] = 1;   // x -> vx
    view[9] = 1;   // z -> vy  (row 2, column 1)
    view[6] = 1;   // y -> vz  (row 1, column 2)
    view[15] = 1;
    const float zn = 0.5f, zf = 1000.0f, f = 2.0f;
    proj[0] = f; proj[5] = f; proj[10] = zf / (zf - zn); proj[11] = 1; proj[14] = -zn * zf / (zf - zn);
}

int main() {
    // ---- markers ----
    State s;
    assert(s.SetMarker(7, 1, 10, 20, 30) && s.Markers().size() == 1);
    assert(s.SetMarker(7, 1, 11, 21, 31) && s.Markers().size() == 1 && s.Markers()[0].x == 11);   // update in place
    assert(s.SetMarker(8, 1, 0, 0, 0) && s.Markers().size() == 2);
    assert(s.SetMarker(7, 0, 0, 0, 0) && s.Markers().size() == 1 && s.Markers()[0].questId == 8);   // inactive removes
    assert(s.SetMarker(99, 0, 1, 1, 1) && s.Markers().size() == 1);                                  // removing an unknown id is fine
    // invalid inputs are rejected and change nothing
    assert(!s.SetMarker(-1, 1, 0, 0, 0) && !s.SetMarker(4294967296.0, 1, 0, 0, 0) && !s.SetMarker(1, 1, kInf, 0, 0) &&
           !s.SetMarker(1, 1, 0, 2e6, 0) && !s.SetMarker(1, kNaN, 0, 0, 0));
    assert(s.Markers().size() == 1);
    for (uint32_t i = 100; i < 200; ++i) s.SetMarker(i, 1, 1, 2, 3);
    assert(s.Markers().size() == kMaxMarkers);   // capped at the quest log size
    s.ClearMarkers();
    assert(s.Markers().empty());

    // ---- corpse, dead state, settings, diamond ----
    assert(!s.SetCorpse(kInf, 0, 0) && !s.CorpseActive() && s.SetCorpse(1, 2, 3) && s.CorpseActive() && s.Corpse()[2] == 3);
    s.ClearCorpse();
    assert(!s.CorpseActive() && !s.SetDead(kNaN) && s.SetDead(1) && s.Dead() && s.SetDead(0) && !s.Dead());
    assert(s.SetSettings(1, 0, 300) && s.GetSettings().enabled && !s.GetSettings().showMarker && s.GetSettings().alpha == 255);
    assert(s.SetSettings(0, 1, -5) && s.GetSettings().alpha == 0);
    assert(s.SetMarkerAlpha(77) && s.GetSettings().alpha == 77 && !s.SetMarkerAlpha(kInf) && s.GetSettings().alpha == 77);
    assert(!s.SetSettings(1, 1, kNaN));
    assert(s.SetDiamond(1, 0.5, -0.5, 0.1, 0.2) && s.DiamondActive() && s.Diamond()[3] == 0.2f);
    assert(!s.SetDiamond(1, 5, 0, 0.1, 0.1) && !s.SetDiamond(1, 0, 0, -1, 0.1) && !s.SetDiamond(1, 0, 0, 0.1, 3) && s.Diamond()[0] == 0.5f);

    // ---- objective entries ----
    {
        const double ok[] = {3, 100, 200, 100};   // duplicate collapsed
        assert(s.SetObjectiveEntries(ok, 4) && s.ObjectiveCount() == 2 && s.ObjectiveMatches(100) && s.ObjectiveMatches(200));
        assert(!s.ObjectiveMatches(300) && !s.ObjectiveMatches(0));
        const double outOfRange[] = {2, 0, 17000000};   // both skipped
        assert(s.SetObjectiveEntries(outOfRange, 3) && s.ObjectiveCount() == 0 && !s.RingsActive());
        const double again[] = {1, 55};
        assert(s.SetObjectiveEntries(again, 2) && s.RingsActive());
        // rejected calls keep the previous entries
        const double tooMany[] = {17, 1};
        const double missing[] = {3, 1, 2};
        const double notFinite[] = {2, 1, kInf};
        assert(!s.SetObjectiveEntries(tooMany, 2) && !s.SetObjectiveEntries(missing, 3) && !s.SetObjectiveEntries(notFinite, 3) &&
               !s.SetObjectiveEntries(nullptr, 0));
        assert(s.ObjectiveCount() == 1 && s.ObjectiveMatches(55));
        double full[1 + kMaxObjectiveEntries];
        full[0] = kMaxObjectiveEntries;
        for (uint32_t i = 0; i < kMaxObjectiveEntries; ++i) full[1 + i] = 1000 + i;
        assert(s.SetObjectiveEntries(full, 1 + kMaxObjectiveEntries) && s.ObjectiveCount() == kMaxObjectiveEntries);
        assert(s.SetRingSettings(0, 300) && !s.GetRingSettings().enabled && s.GetRingSettings().alpha == 255 && !s.RingsActive());
        assert(!s.SetRingSettings(kInf, 0));
        s.ClearObjectiveEntries();
        assert(s.ObjectiveCount() == 0 && !s.ObjectiveMatches(1000));
    }
    s.SetMarker(1, 1, 1, 2, 3); s.SetCorpse(1, 2, 3); s.SetDead(1); s.SetDiamond(1, 0, 0, 0.1, 0.1);
    s.ResetSession();
    assert(s.Markers().empty() && !s.CorpseActive() && !s.Dead() && !s.DiamondActive());

    // ---- ring colour ----
    assert(RingColour(255) == 0xFF665000u);   // yellow at 40% brightness (0xFF,0xC8,0x00 -> 0x66,0x50,0x00)
    assert((RingColour(255) >> 24) == 255 && (RingColour(0) >> 24) == 0 && (RingColour(128) >> 24) == 128);
    assert(RingRadiusUsable(1.0f) && !RingRadiusUsable(0.1f) && !RingRadiusUsable(31.0f) && !RingRadiusUsable(kNaN));

    // ---- projection and billboard ----
    float view[16], proj[16];
    Camera(view, proj);
    Projected p;
    assert(Project(view, proj, 0, 10, 0, p) && p.inFront && p.wValid && p.Usable() && std::abs(p.ndcX) < 1e-5f && std::abs(p.viewZ - 10) < 1e-4f);
    assert(Project(view, proj, 2, 10, 0, p) && p.ndcX > 0.0f && p.ndcY == 0.0f);          // right of centre
    assert(Project(view, proj, 0, 10, 3, p) && p.ndcY > 0.0f);                              // above centre
    assert(Project(view, proj, 0, -5, 0, p) && !p.inFront && !p.wValid && !p.Usable());                                 // behind the eye
    assert(Project(view, proj, 4, -5, 0, p) && p.ndcX == 2.0f && !p.inFront);              // parked off-screen on its side
    assert(Project(view, proj, -4, -5, 0, p) && p.ndcX == -2.0f);
    float bad[16]; std::memcpy(bad, view, sizeof(bad)); bad[3] = kNaN;
    assert(!Project(bad, proj, 0, 10, 0, p) && !Project(nullptr, proj, 0, 10, 0, p));
    float world[16];
    assert(BillboardWorld(view, proj, 1080.0f, 5, 6, 7, 6.0f, world));
    assert(world[12] == 5 && world[13] == 6 && world[14] == 7 && world[15] == 1);
    // At distance d the icon spans 2 * 16 pixels: half-height in world units = 16 * d / (yScale * H)
    assert(std::abs(world[0] - 16.0f * 2.0f * 6.0f / (2.0f * 1080.0f)) < 1e-6f);
    assert(!BillboardWorld(view, proj, 0.0f, 0, 0, 0, 1.0f, world) && !BillboardWorld(bad, proj, 1080.0f, 0, 0, 0, 1.0f, world));
    float zero[16] = {}; zero[5] = 1; zero[15] = 1;
    assert(!BillboardWorld(zero, proj, 1080.0f, 0, 0, 0, 1.0f, world));   // degenerate camera (no right vector)

    // ---- far plane extension keeps depth finite and the near plane ----
    float far[16];
    assert(ExtendFarPlane(proj, far));
    assert(std::abs(-far[14] / far[10] - 0.5f) < 1e-3f);   // same near plane
    float flat[16] = {}; flat[5] = 1;
    assert(!ExtendFarPlane(flat, far) && !ExtendFarPlane(bad, far));
    return 0;
}
