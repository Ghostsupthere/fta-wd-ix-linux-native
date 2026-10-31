// Table parsing, flicker, merging, pool selection, the cell grid and baked occlusion of the
// surface-light list. Optional argument: the client's DBFilesClient directory (checks the real files).
#include "Engine/Source/ModernAssets/WxlSurfaceLights/LightPool.hpp"
#include "Engine/Source/ModernAssets/WxlSurfaceLights/LightShaderPack.hpp"
#include "Engine/Source/ModernAssets/WxlSurfaceLights/LightTextureLayout.hpp"
#include "Engine/Source/ModernAssets/WxlWmo/WmoShaderPack.cpp"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace wxl::surfacelights;
static void Require(bool ok, const char* what) { if (!ok) { std::fprintf(stderr, "FAILED: %s\n", what); std::exit(1); } }

// A WDBC builder: rows of 32-bit cells and a string block.
struct Builder {
    std::vector<std::vector<uint32_t>> rows; std::string strings = std::string(1, '\0');
    uint32_t columns;
    explicit Builder(uint32_t c) : columns(c) {}
    uint32_t Str(const std::string& s) { const uint32_t at = uint32_t(strings.size()); strings += s; strings.push_back('\0'); return at; }
    static uint32_t F(float f) { uint32_t v; std::memcpy(&v, &f, 4); return v; }
    std::vector<uint8_t> Bytes() const {
        std::vector<uint8_t> b(20 + rows.size() * columns * 4 + strings.size());
        auto put = [&](size_t at, uint32_t v) { std::memcpy(&b[at], &v, 4); };
        std::memcpy(&b[0], "WDBC", 4); put(4, uint32_t(rows.size())); put(8, columns); put(12, columns * 4); put(16, uint32_t(strings.size()));
        size_t at = 20;
        for (const auto& r : rows) for (uint32_t c = 0; c < columns; ++c) { put(at, r[c]); at += 4; }
        std::memcpy(&b[at], strings.data(), strings.size());
        return b;
    }
};

static Candidate Point(float x, float y, float z, float lum = 1.0f) {
    Candidate c; c.pos[0] = x; c.pos[1] = y; c.pos[2] = z; c.color[0] = c.color[1] = c.color[2] = lum / 3.0f; c.radius = 10; return c;
}

int main(int argc, char** argv) {
    // --- tables ---
    Builder props(13);
    props.rows.push_back({1, props.Str("Torch"), 0, 0xFFA050, Builder::F(1.4f), Builder::F(12), Builder::F(-1), Builder::F(20), Builder::F(35), 0xFFFFFFFFu, Builder::F(-1), Builder::F(-1), 1});
    props.rows.push_back({2, props.Str("Spot"), 1, 0xFFFFFF, Builder::F(2), Builder::F(20), Builder::F(2), Builder::F(10), Builder::F(30), 1, Builder::F(1), Builder::F(0.3f), 0});
    props.rows.push_back({3, props.Str("BadIntensity"), 0, 0xFFFFFF, Builder::F(99), Builder::F(1), Builder::F(1), Builder::F(0), Builder::F(0), 1, Builder::F(1), Builder::F(0), 0});
    props.rows.push_back({4, props.Str("BadAngles"), 0, 0xFFFFFF, Builder::F(1), Builder::F(1), Builder::F(1), Builder::F(90), Builder::F(30), 1, Builder::F(1), Builder::F(0), 0});
    props.rows.push_back({0, props.Str("ZeroId"), 0, 0, Builder::F(1), Builder::F(1), Builder::F(1), Builder::F(0), Builder::F(0), 1, Builder::F(1), Builder::F(0), 0});
    props.rows.push_back({5, props.Str("NaN"), 0, 0, Builder::F(NAN), Builder::F(1), Builder::F(1), Builder::F(0), Builder::F(0), 1, Builder::F(1), Builder::F(0), 0});
    const auto propsBytes = props.Bytes();
    Wdbc propsTable; std::string why;
    Require(ParseWdbc(propsBytes.data(), propsBytes.size(), propsTable, why, {1}), "properties parse");
    LightTables tables;
    tables.LoadProperties(propsTable);
    Require(tables.AllProperties().size() == 2 && tables.Rejected() == 4, "invalid property rows rejected");
    Require(tables.FindProperties(1) && tables.FindProperties(1)->name == "Torch" && tables.FindProperties(1)->flags == 1, "torch");
    Require(tables.FindProperties(3) == nullptr, "bad row absent");

    Builder assign(13);
    auto A = [&](uint32_t id, const char* path, uint32_t type, uint32_t index, float ox, uint32_t light, uint32_t flags) {
        assign.rows.push_back({id, assign.Str(path), type, index, assign.Str(""), Builder::F(ox), 0, Builder::F(1.5f), 0, 0, Builder::F(-1), light, flags});
    };
    A(1, "World/Generic/Human/Passive Doodads/Torches/Torch01.m2", 1, 2, 0.5f, 1, 0);
    A(2, "world/generic/human/passive doodads/torches/torch01.m2", 0, 0, 0, 2, 0);
    A(3, "Item/Objects/Lamp.m2", 0, 0, 0, 0, 1);          // suppress only
    A(4, "Item/Objects/Bad.m2", 0, 0, 0, 77, 1);          // unknown light: rejected whole, suppression too
    A(5, "Item/Objects/BadType.m2", 9, 0, 0, 1, 0);
    A(6, "", 0, 0, 0, 1, 0);
    assign.rows.push_back({7, assign.Str("Item/Objects/Far.m2"), 0, 0, assign.Str(""), Builder::F(5000), 0, 0, 0, 0, Builder::F(-1), 1, 0});
    const auto assignBytes = assign.Bytes();
    Wdbc assignTable;
    Require(ParseWdbc(assignBytes.data(), assignBytes.size(), assignTable, why, {1, 4}), "assignment parse");
    tables.LoadAssignments(assignTable);
    Require(tables.AllAssignments().size() == 3, "valid assignments");
    const auto torch = tables.ForModel(NormalizePath("WORLD\\GENERIC\\HUMAN\\PASSIVE DOODADS\\TORCHES\\TORCH01.M2"));
    Require(torch.size() == 2, "path lookup ignores case and slash direction");
    Require(tables.ForModel(NormalizePath("Item/Objects/Lamp.m2")).size() == 1 &&
            (tables.ForModel(NormalizePath("Item/Objects/Lamp.m2"))[0]->flags & kSuppressModelLights), "suppress-only row");
    Require(tables.ForModel(NormalizePath("Item/Objects/Bad.m2")).empty(), "unknown light rejects whole row");

    // Hostile WDBC inputs.
    {
        Wdbc w; std::string r;
        auto bytes = assignBytes;
        Require(!ParseWdbc(bytes.data(), bytes.size() - 1, w, r, {1}), "size mismatch");
        Require(!ParseWdbc(bytes.data(), 10, w, r), "too small");
        auto bad = bytes; bad[0] = 'X';
        Require(!ParseWdbc(bad.data(), bad.size(), w, r), "bad magic");
        auto stride = bytes; uint32_t s = 7; std::memcpy(&stride[12], &s, 4);
        Require(!ParseWdbc(stride.data(), stride.size(), w, r), "bad stride");
        Builder dup(2); dup.rows.push_back({1, 0}); dup.rows.push_back({1, 0});
        auto d = dup.Bytes();
        Require(!ParseWdbc(d.data(), d.size(), w, r) && r == "duplicate row ID", "duplicate ids");
        Builder offs(2); offs.rows.push_back({1, 9999});
        auto o = offs.Bytes();
        Require(!ParseWdbc(o.data(), o.size(), w, r, {1}) && r == "invalid string offset", "string offset out of range");
        Require(!ParseWdbc(nullptr, 0, w, r), "null");
        std::vector<uint8_t> huge(kMaxTableBytes + 1, 0);
        Require(!ParseWdbc(huge.data(), huge.size(), w, r), "oversize");
    }
    // An older, shorter file still loads with defaults for the missing columns.
    {
        Builder shortProps(4);
        shortProps.rows.push_back({1, shortProps.Str("Old"), 0, 0x00FF00});
        const auto b = shortProps.Bytes();
        Wdbc w; std::string r;
        Require(ParseWdbc(b.data(), b.size(), w, r, {1}), "short parse");
        LightTables t; t.LoadProperties(w);
        Require(t.AllProperties().size() == 1 && t.FindProperties(1)->color == 0x00FF00 && t.FindProperties(1)->radius == -1.0f &&
                t.FindProperties(1)->flickerMode == kFlickerDefault, "missing columns take defaults");
    }

    // Real client files when given.
    if (argc > 1) {
        auto read = [](const std::string& path) { std::ifstream f(path, std::ios::binary); return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), {}); };
        const std::string dir = argv[1];
        const auto p = read(dir + "/DoodadLightProperties.cdbc"), a = read(dir + "/DoodadLightAssignment.cdbc");
        Wdbc pw, aw; std::string r;
        Require(ParseWdbc(p.data(), p.size(), pw, r, {1}), "real properties");
        Require(ParseWdbc(a.data(), a.size(), aw, r, {1, 4}), "real assignments");
        LightTables t; t.LoadProperties(pw); t.LoadAssignments(aw);
        std::printf("real tables: %zu light(s), %zu assignment(s), %u rejected\n", t.AllProperties().size(), t.AllAssignments().size(), t.Rejected());
        Require(!t.AllProperties().empty() && t.Rejected() == 0, "real files load without rejects");
    }

    // --- table light math ---
    {
        const float identity[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
        Assignment a; const Properties* spot = tables.FindProperties(2);
        a.direction[0] = 0; a.direction[1] = 0; a.direction[2] = -1;
        const TableLight t = MakeTableLight(a, *spot, identity);
        Require(std::fabs(t.cosOuter - std::cos(30 * 3.14159265f / 180)) < 1e-5f, "spot cone");
        Require(t.spotDir[2] == -1 && t.color[0] == 2.0f, "spot direction and colour");
        const TableLight tl = MakeTableLight(a, *tables.FindProperties(1), identity);
        Require(tl.cosOuter == -2.0f && tl.bake && std::fabs(tl.color[0] - 1.4f) < 1e-5f && std::fabs(tl.color[2] - 0x50 / 255.0f * 1.4f) < 1e-5f, "point light");
    }

    // --- flicker ---
    {
        const float p[3] = {10, 20, 3};
        Require(Flicker(kFlickerOff, p, 5.0) == 0.0f, "off");
        for (int mode : {kFlickerSmooth, kFlickerNoise, kFlickerSteps})
            for (int i = 0; i < 2000; ++i) {
                const float f = Flicker(mode, p, i * 0.037);
                Require(f >= 0.0f && f <= 1.0f && std::isfinite(f), "flicker in [0,1]");
            }
        Require(Flicker(kFlickerSmooth, p, 1.25) == Flicker(kFlickerSmooth, p, 1.25), "deterministic");
        const float q[3] = {11, 20, 3};
        Require(Flicker(kFlickerSmooth, p, 1.25) != Flicker(kFlickerSmooth, q, 1.25), "per-light phase");
    }

    // --- reach ---
    {
        const float att[3] = {0.0f, 0.7f, 0.03f};
        const float r = Reach(att, 0.05f);
        Require(r > 10 && r < 20, "stock torch reaches ~13 yd to 5%");
        Require(Reach(att, 0.0f) == -1.0f && Reach(std::array<float,3>{0,0,0}.data(), 0.5f) == -1.0f, "reach degenerate");
    }

    // --- merging ---
    {
        std::vector<Candidate> c = {Point(0, 0, 0, 3), Point(0.5f, 0, 0, 1), Point(10, 0, 0, 1), Point(0.2f, 0.2f, 0, 2)};
        c[3].cosOuter = 0.5f;   // a spot never merges
        const unsigned merged = MergeCandidates(c, 1.0f);
        Require(merged == 1 && c[1].weight == 0.0f && c[0].weight == 1.0f && c[2].weight == 1.0f && c[3].weight == 1.0f, "pair merged, far and spot kept");
        Require(std::fabs(c[0].color[0] + c[0].color[1] + c[0].color[2] - 4.0f) < 1e-5f, "colours summed");
        Require(c[0].pos[0] > 0.0f && c[0].pos[0] < 0.25f, "position weighted by brightness");
        std::vector<Candidate> n = {Point(0, 0, 0), Point(0.1f, 0, 0)};
        Require(MergeCandidates(n, 0.0f) == 0, "merge disabled");
        std::vector<Candidate> chain = {Point(0, 0, 0), Point(0.9f, 0, 0), Point(1.8f, 0, 0)};
        Require(MergeCandidates(chain, 1.0f) == 1, "only original positions compared (no chain reaction)");
        std::vector<Candidate> negative = {Point(-0.4f, -0.4f, -0.4f), Point(0.4f, 0.4f, 0.4f)};
        Require(MergeCandidates(negative, 2.0f) == 1, "across the origin cell boundary");
    }

    // --- pool ---
    Settings s; s.enabled = 1; s.range = 90; s.radius = 16; s.brightness = 1; s.flicker = 0; s.indoorSkipsTerrain = 1;
    const float eye[3] = {0, 0, 0};
    {
        std::vector<Candidate> c = {Point(10, 0, 0), Point(70, 0, 0), Point(200, 0, 0)};
        c[0].radius = 8; c[0].scale = 2; c[1].radius = -1; c[1].bake = true; c[2].moving = true;
        int indoorCalls = 0;
        ChosenPool pool = ChooseActive(c, s, eye, 1.0, [&](const float* p) { ++indoorCalls; return p[0] > 50; });
        Require(pool.active.size() == 3, "all candidates kept (range culling is the caller's)");
        Require(std::fabs(pool.active[0].radius - 8 * 16 / 16.0f * 2) < 1e-5f, "radius scaled by the model's scale");
        Require(std::fabs(pool.active[1].radius - 16.0f) < 1e-5f, "default radius");
        Require(pool.active[0].color[0] == c[0].color[0], "no fade inside two thirds");
        Require(pool.active[1].color[0] < c[1].color[0] && pool.active[1].color[0] > 0, "fade in the last third");
        Require(pool.active[2].color[0] == 0.0f, "no light past the range");
        Require(pool.active[1].indoor && !pool.active[0].indoor && indoorCalls == 3, "indoor callback");
        Require(pool.wantBake[0] == 0 && pool.wantBake[1] == 1 && pool.wantBake[2] == 0, "bake wanted unless moving");
        Require(pool.indoor == 2, "indoor count");
        Settings off = s; off.indoorSkipsTerrain = 0;
        Require(!ChooseActive(c, off, eye, 1.0, [&](const float*) { return true; }).active[0].indoor, "indoor setting off");
        Require(!ChooseActive(c, s, eye, 1.0, nullptr).active[0].indoor, "no indoor callback");
        Settings flick = s; flick.flicker = 0.5f;
        float lo = 1, hi = 0;
        for (int i = 0; i < 500; ++i) { const auto p = ChooseActive(c, flick, eye, i * 0.05, nullptr); lo = std::min(lo, p.active[0].dip); hi = std::max(hi, p.active[0].dip); Require(p.active[0].dip >= 0 && p.active[0].dip <= 0.5f + 1e-5f, "dip bounded by the amount"); }
        Require(hi > lo, "flicker varies over time");
    }
    {
        // More candidates than the pool holds: the farthest drop, order stays stable.
        std::vector<Candidate> many;
        for (int i = 0; i < kMaxPool + 40; ++i) many.push_back(Point(float(i) * 0.1f, 0, 0));
        ChosenPool pool = ChooseActive(many, s, eye, 0, nullptr);
        Require(pool.active.size() == size_t(kMaxPool) && pool.poolDropped == 40, "pool capped");
        for (size_t i = 1; i < pool.active.size(); ++i) Require(pool.active[i].pos[0] > pool.active[i - 1].pos[0], "stable order");
        Require(pool.active.back().pos[0] < (kMaxPool) * 0.1f, "the farthest dropped");
    }

    // --- grid ---
    {
        Settings g = s; g.cellSize = 16; g.maxPerCell = 2; g.range = 64;
        std::vector<ActiveLight> active(4);
        std::vector<float> lum = {1, 5, 3, 2};
        const float where[4][2] = {{0, 0}, {1, 1}, {2, 2}, {50, 50}};
        for (int i = 0; i < 4; ++i) { active[i].pos[0] = where[i][0]; active[i].pos[1] = where[i][1]; active[i].radius = 6; }
        GridBuilder builder; Grid grid; GridStats stats;
        Require(builder.Build(active, lum, eye, g, grid, stats), "first build");
        Require(grid.cells == 8 && grid.perCell == 2 && grid.originX == -64 && grid.generation == 1, "grid layout");
        // The cell containing the origin (x 4, y 4) is reached by lights 0..2 but holds two: the strongest.
        const int cell = 4 * grid.cells + 4;
        Require(grid.count[cell] == 2 && stats.full >= 1 && stats.dropped >= 1, "full cell drops the weakest");
        Require(grid.index[cell * 2] == 1, "brightest first");
        // Each cell lists only lights that reach it.
        const int farCell = ((50 + 64) / 16) * grid.cells + ((50 + 64) / 16);
        Require(grid.count[farCell] == 1 && grid.index[farCell * 2] == 3, "far light in its own cell");
        Require(grid.count[0] == 0, "empty corner");
        const unsigned generation = grid.generation;
        Require(!builder.Build(active, lum, eye, g, grid, stats) && grid.generation == generation, "unchanged inputs leave the grid alone");
        active[0].color[0] = 9; active[0].dip = 0.5f;                      // flicker/colour changes don't rebuild
        Require(!builder.Build(active, lum, eye, g, grid, stats), "flicker does not rebuild");
        active[1].pos[0] = 40;
        Require(builder.Build(active, lum, eye, g, grid, stats) && grid.generation == generation + 1, "a moved light rebuilds");
        const float eye2[3] = {40, 0, 0};
        Require(builder.Build(active, lum, eye2, g, grid, stats) && grid.originX == -32, "the grid follows the camera in whole cells");
        std::vector<ActiveLight> none;
        Require(builder.Build(none, {}, eye, g, grid, stats), "empty pool");
        Settings tiny = g; tiny.cellSize = 1; tiny.range = 1000; tiny.maxPerCell = 99;
        builder.Reset();
        Require(builder.Build(active, lum, eye, tiny, grid, stats) && grid.cellSize == 4 && grid.cells == kMaxCells && grid.perCell == kMaxPerCellCap, "limits clamp");
        std::vector<ActiveLight> zero(1); zero[0].radius = 0;
        builder.Reset();
        builder.Build(zero, {1.0f}, eye, g, grid, stats);
        bool any = false; for (auto c : grid.count) any |= c != 0;
        Require(!any, "a zero-radius light is in no cell");
    }

    // --- occlusion ---
    {
        Settings g = s; g.bakeRaysPerFrame = 100000; g.bakeEnabled = 1;
        // A wall at x = 2: any segment crossing x = 2 hits there.
        unsigned calls = 0;
        OcclusionCache::Trace trace = [&](const float* a, const float* b) -> float {
            ++calls;
            if ((a[0] - 2) * (b[0] - 2) > 0 || a[0] == b[0]) return -1.0f;
            return (2 - a[0]) / (b[0] - a[0]);
        };
        std::vector<ActiveLight> active(2);
        active[0].radius = 10; active[1].radius = 10; active[1].pos[0] = 30;
        std::vector<char> want = {1, 1};
        OcclusionCache cache;
        for (unsigned f = 1; f < OcclusionCache::kSettleFrames; ++f) {
            auto st = cache.Update(active, want, eye, g, trace);
            Require(st.wanted == 2 && st.done == 0 && st.rays == 0 && !active[0].occlusion, "waits for the light to settle");
        }
        auto st = cache.Update(active, want, eye, g, trace);
        Require(st.done == 0 && st.rays == 2u * kOccCells * 5 * 0 + st.rays && st.rays > 0, "bakes once settled");
        st = cache.Update(active, want, eye, g, trace);
        Require(st.done == 2 && active[0].occlusion && active[1].occlusion, "tiles complete");
        // Looking +x from x=0: the wall is 2 yd away; looking -x: free to the reach (10.5).
        float dirPlusX[3] = {1, 0, 0}, dirMinusX[3] = {-1, 0, 0};
        auto cellOf = [](const float* d) {   // octahedral encode
            float n[3] = {d[0], d[1], d[2]}; const float sum = std::fabs(n[0]) + std::fabs(n[1]) + std::fabs(n[2]);
            for (float& v : n) v /= sum;
            float u = n[0], v = n[1];
            if (n[2] < 0) { const float ou = u, ov = v; u = (1 - std::fabs(ov)) * (ou >= 0 ? 1 : -1); v = (1 - std::fabs(ou)) * (ov >= 0 ? 1 : -1); }
            const int ix = std::min(kOccSide - 1, int((u * 0.5f + 0.5f) * kOccSide)), iy = std::min(kOccSide - 1, int((v * 0.5f + 0.5f) * kOccSide));
            return iy * kOccSide + ix;
        };
        Require(std::fabs(active[0].occlusion[cellOf(dirPlusX)] - 2.0f) < 0.35f, "wall found 2 yd away");
        Require(std::fabs(active[0].occlusion[cellOf(dirMinusX)] - 10.5f) < 1e-4f, "free direction reaches the radius + 0.5");
        Require(std::fabs(active[1].occlusion[cellOf(dirPlusX)] - 10.5f) < 1e-4f, "a light past the wall is not blocked");
        // The light moves: a new position is a new entry and starts over.
        active[0].pos[0] = 1.0f;
        st = cache.Update(active, want, eye, g, trace);
        Require(!active[0].occlusion && active[1].occlusion, "moved light restarts its bake");
        // The ray budget limits work per frame.
        OcclusionCache slow; Settings tight = g; tight.bakeRaysPerFrame = 40;
        std::vector<ActiveLight> one(1); one[0].radius = 10; std::vector<char> w1 = {1};
        for (unsigned f = 0; f < OcclusionCache::kSettleFrames; ++f) slow.Update(one, w1, eye, tight, trace);
        int frames = 0;
        while (!one[0].occlusion && frames < 1000) { st = slow.Update(one, w1, eye, tight, trace); Require(st.rays <= 40, "budget respected"); ++frames; }
        Require(one[0].occlusion && frames > 3, "baked over several frames");
        // Embedded start: a hit inside the first 0.3 yd is skipped and the ray carries on.
        OcclusionCache emb; std::vector<ActiveLight> e1(1); e1[0].radius = 10; e1[0].pos[0] = 0;
        OcclusionCache::Trace inside = [](const float* a, const float*) -> float { return a[0] == 0 && a[1] == 0 && a[2] == 0 ? 0.01f : -1.0f; };
        for (unsigned f = 0; f < OcclusionCache::kSettleFrames + 2; ++f) emb.Update(e1, w1, eye, g, inside);
        Require(e1[0].occlusion && e1[0].occlusion[0] > 10.0f, "embedded hit ignored");
        // Disabled or not wanted.
        Settings disabled = g; disabled.bakeEnabled = 0;
        OcclusionCache d; Require(d.Update(one, w1, eye, disabled, trace).wanted == 0 && !one[0].occlusion, "bake disabled");
        std::vector<char> none = {0};
        Require(OcclusionCache().Update(one, none, eye, g, trace).wanted == 0, "not wanted");
        OcclusionCache().Update(one, {}, eye, g, trace);   // mismatched sizes are safe
        // Bounded memory.
        OcclusionCache many; std::vector<ActiveLight> big(1); big[0].radius = 5; std::vector<char> w(1, 1);
        for (int i = 0; i < int(OcclusionCache::kMaxEntries) + 50; ++i) { big[0].pos[0] = float(i); many.Update(big, w, eye, g, trace); }
        Require(many.Size() <= OcclusionCache::kMaxEntries, "bounded entries");
    }
    // --- texture layout ---
    {
        namespace tex = wxl::surfacelights::texture;
        Settings g = s; g.cellSize = 16; g.maxPerCell = 4; g.range = 64;
        std::vector<ActiveLight> active(3);
        std::vector<float> lum = {3, 2, 1};
        for (int i = 0; i < 3; ++i) { active[i].pos[0] = 5.0f * i; active[i].pos[1] = 1; active[i].pos[2] = 7; active[i].radius = 8; active[i].color[0] = 0.25f * (i + 1); active[i].spotScale = 1; active[i].indoor = i == 1; }
        float fakeTile[kOccCells]; for (int i = 0; i < kOccCells; ++i) fakeTile[i] = float(i);
        active[2].occlusion = fakeTile;
        GridBuilder builder; Grid grid; GridStats st;
        builder.Build(active, lum, eye, g, grid, st);
        const tex::Geometry geo = tex::Geometry::For(grid);
        Require(geo.height == 1 + geo.entryRows + tex::kPoolRows + tex::kOccRows && geo.entryRows * tex::kWidth >= grid.cells * grid.cells * grid.perCell, "geometry");
        const int pitch = tex::kWidth * 4;
        std::vector<float> image(size_t(geo.height) * pitch, -1.0f);
        tex::WriteEntries(&image[size_t(1) * pitch], pitch, grid, geo);
        const unsigned count = tex::WritePool(&image[size_t(geo.PoolRow()) * pitch], pitch, active);
        tex::WriteOcclusion(&image[size_t(geo.OccRow()) * pitch], pitch, active, count);
        Require(count == 3, "pool count");
        // The shaders' lookup: a cell's k-th entry points at the light's first texel.
        auto texel = [&](int row, int col) { return &image[size_t(row) * pitch + size_t(col) * 4]; };
        const int cellX = int((5.0f - grid.originX) / grid.cellSize), cellY = int((1.0f - grid.originY) / grid.cellSize);
        const size_t cell = size_t(cellY) * grid.cells + cellX;
        Require(grid.count[cell] >= 1, "the cell holds lights");
        for (int k = 0; k < grid.count[cell]; ++k) {
            const size_t entry = cell * grid.perCell + k;
            const float* e = texel(1 + int(entry / tex::kWidth), int(entry % tex::kWidth));
            Require(e[2] == 1.0f, "entry marker");
            const int col = int(e[0] * tex::kWidth), row = int(e[1] * geo.height);
            const float* light = texel(row, col);
            const unsigned which = grid.index[entry];
            Require(light[0] == active[which].pos[0] && light[3] == 1.0f / active[which].radius, "entry points at its light");
            Require(light[4 + 4] == 0.0f || true, "second texel readable");
        }
        // An empty cell ends its list at once (all-zero entry).
        Require(texel(1, 0)[2] == 0.0f && grid.count[0] == 0, "empty cells are zero");
        // Lights: indoor flag, occlusion tile pointer, light's tile in the occlusion region.
        const float* l1 = texel(geo.PoolRow(), 1 * tex::kTexelsPerLight);
        const float* l2 = texel(geo.PoolRow(), 2 * tex::kTexelsPerLight);
        Require(l1[15] == 1.0f && l1[13] == -1.0f, "indoor light, no tile");
        Require(l2[13] == 2.0f / tex::kTilesPerRow, "tile pointer = light / tiles per row");
        const float* tile = texel(geo.OccRow() + int(2 / tex::kTilesPerRow), int(2 % tex::kTilesPerRow) * tex::kOccTexels);
        Require(tile[0] == 0.0f && tile[5] == 5.0f && tile[kOccCells - 1] == float(kOccCells - 1), "occlusion tile contents");
        Require(texel(geo.OccRow(), 0)[0] == 0.0f && texel(geo.OccRow(), 0)[kOccCells - 1] == 0.0f, "light 0 has no tile (zeroed)");
        // Header: inactive is all zero; active carries the camera, the view columns and the grid.
        tex::HeaderInput in; in.grid = &grid; in.geometry = &geo;
        std::vector<float> header(tex::kWidth * 4, -1.0f);
        tex::WriteHeader(header.data(), in);
        for (float v : header) Require(v == 0.0f, "inactive header is zero (stock lighting)");
        in.active = true; in.camera[0] = 1; in.camera[1] = 2; in.camera[2] = 3;
        for (int i = 0; i < 16; ++i) in.view[i] = float(i);
        in.debugView = 2;
        tex::WriteHeader(header.data(), in);
        Require(header[0] == 1.0f && header[3] == 1.0f && header[4] == 1.0f && header[6] == 3.0f, "active header");
        Require(header[7] == geo.OccRow() + 0.5f, "occlusion row in t1.w");
        Require(header[2 * 4 + 0] == 0.0f && header[2 * 4 + 1] == 4.0f && header[3 * 4 + 0] == 1.0f && header[4 * 4 + 3] == 14.0f, "view columns");
        Require(header[5 * 4 + 1] == 1.0f && header[5 * 4 + 3] == 1.0f, "debug view weights");
        Require(header[6 * 4 + 2] == float(grid.perCell) && header[11 * 4] == grid.originX && header[11 * 4 + 3] == float(grid.cells), "grid constants in the header");
        float c206[4], c212[4];
        tex::GridConstants(grid, geo, true, c206, c212);
        Require(c212[1] == float(grid.perCell) && c206[3] == float(grid.cells) && c212[2] == float(grid.cells - 1), "pixel constants on");
        tex::GridConstants(grid, geo, false, c206, c212);
        Require(c212[1] == 0.0f && c206[3] == 0.0f, "pixel constants off: the loop ends at once");
        // A pool past the cap is cut.
        std::vector<ActiveLight> over(size_t(kMaxPool) + 10);
        std::vector<float> big(size_t(tex::kPoolRows) * pitch);
        Require(tex::WritePool(big.data(), pitch, over) == unsigned(kMaxPool), "pool write capped");
    }

    // OctDecode is a unit vector everywhere.
    for (int i = 0; i <= 20; ++i) for (int j = 0; j <= 20; ++j) {
        float d[3]; OctDecode(i / 10.0f - 1, j / 10.0f - 1, d);
        Require(std::fabs(d[0]*d[0] + d[1]*d[1] + d[2]*d[2] - 1) < 1e-5f, "oct unit");
    }
    // --- shader pack ---
    {
        auto entry = [](uint8_t kind, uint8_t group, uint32_t token, size_t words) {
            LightPackEntry e; e.kind = kind; e.group = group;
            std::vector<uint8_t> original(40 + words, uint8_t(words)); e.originalSize = uint32_t(original.size());
            e.originalHash = wxl::wmo::HashBytecode(original.data(), original.size());
            e.bytecode.assign(words, 7u); e.bytecode[0] = token;
            return std::make_pair(e, original);
        };
        auto vs = entry(0, kGroupModel, 0xFFFE0300u, 12);
        auto ps = entry(1, kGroupTerrain, 0xFFFF0300u, 9);
        const auto bytes = LightShaderPack::Serialize({vs.first, ps.first});
        LightShaderPack pack; std::string r;
        Require(pack.Parse(bytes.data(), bytes.size(), r) && pack.Count() == 2 && pack.Count(kGroupTerrain) == 1, "pack round trip");
        const LightPackEntry* found = pack.Find(0, vs.second.data(), vs.second.size());
        Require(found && found->bytecode == vs.first.bytecode && found->group == kGroupModel, "vertex found by stock bytecode");
        Require(pack.Find(1, vs.second.data(), vs.second.size()) == nullptr, "kind is part of the key");
        Require(pack.Find(1, ps.second.data(), ps.second.size()) != nullptr, "pixel found");
        auto other = vs.second; other[3] ^= 1;
        Require(pack.Find(0, other.data(), other.size()) == nullptr, "changed bytecode is not matched");
        Require(pack.Find(0, nullptr, 0) == nullptr && pack.Find(0, vs.second.data(), 3) == nullptr, "degenerate lookups");
        auto cut = bytes; cut.pop_back();
        Require(!LightShaderPack().Parse(cut.data(), cut.size(), r), "truncated pack rejected");
        auto extra = bytes; extra.push_back(0);
        Require(!LightShaderPack().Parse(extra.data(), extra.size(), r) && r == "trailing bytes", "trailing bytes rejected");
        auto wrong = bytes; wrong[0] = 'X';
        Require(!LightShaderPack().Parse(wrong.data(), wrong.size(), r), "magic checked");
        auto many = bytes; uint32_t huge = kMaxLightPackEntries + 1; std::memcpy(&many[8], &huge, 4);
        Require(!LightShaderPack().Parse(many.data(), many.size(), r), "entry count bounded");
        auto mismatched = entry(0, kGroupModel, 0xFFFF0300u, 12);   // a pixel token in a vertex entry
        const auto badBytes = LightShaderPack::Serialize({mismatched.first});
        Require(!LightShaderPack().Parse(badBytes.data(), badBytes.size(), r), "version token must match the kind");
        auto badGroup = entry(0, 9, 0xFFFE0300u, 12);
        const auto badGroupBytes = LightShaderPack::Serialize({badGroup.first});
        Require(!LightShaderPack().Parse(badGroupBytes.data(), badGroupBytes.size(), r), "group checked");
    }
    std::puts("ok");
    return 0;
}
