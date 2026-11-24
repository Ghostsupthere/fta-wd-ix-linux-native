#include "WoW/Source/WorldClient/RelinkCandidates.h"
#include "Engine/Source/Tempest/C3Ray.h"
#include "Engine/Source/Tempest/C2Vector.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <limits>
#include <random>
#include <vector>

namespace Profiler {
struct State { uint32_t rayTriangleTests = 0, linkBspFaces = 0, linkTriangleTests = 0; } counters;
struct MovementWorkScope { static State* Active() { return &counters; } };
}
namespace NTempest {
bool Intersect(const C3Ray&, const C3Vector*, const uint16_t*, float*, C2Vector*, float);
}
struct BspQuery {
    static inline uint16_t testFaces[8192];
    static inline uint32_t testFaceSub;
};
struct Face { uint8_t m_flags, material; };
struct BspQuery_SegmentLink {
    Face* m_faces;
    const C3Vector* m_vertexList;
    const uint16_t* m_indices;
    C3Ray m_ray;
    uint32_t m_faceIgnoreFlags = 0x82;
    float m_interiorT = 1050, m_groundT = 1050;
    int32_t m_interiorFace = -1, m_groundFace = -1;
    RelinkCandidates::Mode m_candidateMode = RelinkCandidates::Mode::Filter;
    void operator()(uint16_t face);
    void VisitFace(uint16_t face, bool candidate);
    bool GetHits(float*, int32_t*, float*, int32_t*);
    float m_oosegMag = 0.001f, m_interiorMaxT = 1.05f, m_groundMaxT = 1.05f;
};
#include "query.inc"

struct Node { uint16_t flags, neg, pos, count; uint32_t start; float plane; };
struct Geometry {
    std::vector<C3Vector> vertices;
    std::vector<uint16_t> indices, faces;
    std::vector<Face> flags;
    std::vector<Node> nodes;
    RelinkCandidates::GeometryView View() const {
        return {indices.data(), uint32_t(indices.size()), vertices.data(), uint32_t(vertices.size())};
    }
};

Geometry Load(const char* file) {
    std::ifstream input(file, std::ios::binary);
    std::array<uint32_t, 5> lengths{};
    input.read(reinterpret_cast<char*>(lengths.data()), sizeof(lengths));
    auto read = [&](auto& values, uint32_t size) {
        const size_t width = sizeof(values[0]);
        assert(size % width == 0);
        values.resize(size / width);
        input.read(reinterpret_cast<char*>(values.data()), size);
    };
    Geometry g;
    read(g.vertices, lengths[0]);
    read(g.indices, lengths[1]);
    read(g.flags, lengths[2]);
    read(g.nodes, lengths[3]);
    read(g.faces, lengths[4]);
    assert(input);
    return g;
}

struct Result {
    int32_t interior, ground;
    float interiorT, groundT;
    uint64_t admitted;
    uint32_t count;
};
Result Query(Geometry& g, const uint16_t* faces, uint32_t count, C3Ray ray, const uint8_t* mask) {
    BspQuery_SegmentLink query{g.flags.data(), g.vertices.data(), g.indices.data(), ray};
    for (uint32_t i = 0; i < count; ++i) {
        if (mask) query.VisitFace(faces[i], mask[i]);
        else query(faces[i]);
    }
    uint64_t fingerprint = 0;
    const auto admitted = BspQuery::testFaceSub;
    for (uint32_t i = 0; i < admitted; ++i) {
        const auto face = BspQuery::testFaces[i];
        g.flags[face].m_flags &= ~0x80;
        fingerprint = fingerprint * 1099511628211ull ^ face;
    }
    BspQuery::testFaceSub = 0;
    return {query.m_interiorFace, query.m_groundFace, query.m_interiorT, query.m_groundT, fingerprint, admitted};
}

void Compare(Geometry& g, const uint16_t* faces, uint32_t count, C3Ray ray, const uint8_t* mask) {
    const auto stock = Query(g, faces, count, ray, nullptr);
    const auto filtered = Query(g, faces, count, ray, mask);
    assert(stock.interior == filtered.interior && stock.interiorT == filtered.interiorT);
    assert(stock.ground == filtered.ground && stock.groundT == filtered.groundT);
    assert(stock.admitted == filtered.admitted && stock.count == filtered.count);
    for (uint32_t i = 0; i < count; ++i) {
        if (!mask[i]) assert(!NTempest::Intersect(ray, g.vertices.data(), &g.indices[3 * faces[i]], nullptr, nullptr, .002f));
    }
}

void CheckGeometry(const char* name, Geometry& g, std::mt19937& random) {
    unsigned checked = 0;
    uint64_t before = 0, after = 0;
    double cold = 0;
    std::uniform_real_distribution<float> fraction(0.0f, 0.999f);
    for (const auto& node : g.nodes) {
        if (!(node.flags & 4) || node.count <= 300) continue;
        assert(node.start + node.count <= g.faces.size());
        const auto* faces = g.faces.data() + node.start;
        std::vector<uint8_t> mask(node.count);
        for (unsigned sample = 0; sample < 8; ++sample) {
            const auto face = faces[random() % node.count];
            const auto& at = g.vertices[g.indices[3 * face]];
            RelinkCandidates::Cell cell{int(std::floor(at.x)), int(std::floor(at.y)), (sample & 1) != 0};
            const auto start = std::chrono::steady_clock::now();
            RelinkCandidates::BuildMask(cell, faces, node.count, g.View(), mask.data());
            cold += std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count();
            const auto initial = Profiler::counters.linkTriangleTests;
            for (unsigned p = 0; p < 10; ++p) {
                const float d = p % 2 ? 0.999999f : 1.000001f;
                const float x = p == 0 ? at.x : float(cell.x) + fraction(random);
                const float y = p == 0 ? at.y : float(cell.y) + fraction(random);
                const C3Ray ray{{x, y, cell.down ? 300.0f : -300.0f}, {0, 0, cell.down ? -d : d}};
                Compare(g, faces, node.count, ray, mask.data());
            }
            const auto tests = Profiler::counters.linkTriangleTests - initial;
            const uint32_t all = Query(g, faces, node.count, {{float(cell.x), float(cell.y), 300}, {0, 0, -1}}, nullptr).count;
            before += uint64_t(all) * 10;
            after += tests - uint64_t(all) * 10;
            ++checked;
        }
    }
    assert(checked > 0 && after < before);
    const auto node = std::max_element(g.nodes.begin(), g.nodes.end(), [](const auto& a, const auto& b) {
        return ((a.flags & 4) ? a.count : 0) < ((b.flags & 4) ? b.count : 0);
    });
    const auto* faces = g.faces.data() + node->start;
    const auto& anchor = g.vertices[g.indices[3 * faces[0]]];
    const RelinkCandidates::Cell cell{int(std::floor(anchor.x)), int(std::floor(anchor.y)), true};
    std::vector<uint8_t> mask(node->count);
    RelinkCandidates::BuildMask(cell, faces, node->count, g.View(), mask.data());
    const C3Ray ray{{anchor.x, anchor.y, 300}, {0, 0, -1}};
    auto benchmark = [&](const uint8_t* candidates) {
        const uint32_t prior = Profiler::counters.linkTriangleTests;
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < 1500; ++i) Query(g, faces, node->count, ray, candidates);
        const double elapsed = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count();
        return std::make_pair(elapsed / 1500, (Profiler::counters.linkTriangleTests - prior) / 1500);
    };
    const auto stock = benchmark(nullptr);
    const auto filtered = benchmark(mask.data());
    std::printf("%s: %u cells, %llu -> %llu triangle tests, %.1f us mean cold mask build; "
                "warm leaf %.2f -> %.2f us (%u -> %u tests)\n", name, checked,
                static_cast<unsigned long long>(before), static_cast<unsigned long long>(after), cold / checked,
                stock.first, filtered.first, stock.second, filtered.second);
}

int main(int argc, char** argv) {
    std::mt19937 random(101);
    Geometry synthetic;
    std::uniform_real_distribution<float> coordinate(-25, 25);
    for (unsigned f = 0; f < 2200; ++f) {
        const float x = coordinate(random), y = coordinate(random), z = coordinate(random);
        const auto base = uint16_t(synthetic.vertices.size());
        synthetic.vertices.emplace_back(x, y, z);
        synthetic.vertices.emplace_back(x + .3f, y - .3f, z + .8f);
        synthetic.vertices.emplace_back(x + .5f, y + .3f, z + .3f);
        synthetic.indices.insert(synthetic.indices.end(), {base, uint16_t(base + 1), uint16_t(base + 2)});
        synthetic.faces.push_back(f);
        synthetic.flags.push_back({f % 7 == 0 ? uint8_t(2) : uint8_t(0x20), 0});
    }
    synthetic.faces.push_back(1); // duplicate suppression and stock tie order
    synthetic.nodes.push_back({4, 0xffff, 0xffff, uint16_t(synthetic.faces.size()), 0, 0});
    CheckGeometry("synthetic", synthetic, random);
    for (int i = 1; i < argc; ++i) {
        auto g = Load(argv[i]);
        CheckGeometry(argv[i], g, random);
    }
    using namespace RelinkCandidates;
    Geometry ties{{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}}, {0, 1, 2, 0, 1, 2}, {0, 1, 0},
                  {{0x20, 0}, {0x20, 0}}, {}};
    const C3Ray vertical{{0.25f, 0.25f, 2}, {0, 0, -1}};
    std::array<uint8_t, 3> tieMask{};
    BuildMask({0, 0, true}, ties.faces.data(), ties.faces.size(), ties.View(), tieMask.data());
    assert(Query(ties, ties.faces.data(), ties.faces.size(), vertical, tieMask.data()).interior == 1);
    ties.flags[1].m_flags |= 2;
    assert(Query(ties, ties.faces.data(), ties.faces.size(), vertical, tieMask.data()).interior == 0);
    Compare(ties, ties.faces.data(), ties.faces.size(), vertical, tieMask.data());
    Cell cell{};
    assert(CellForRay({{-0.01f, -1, 0}, {0, 0, -1}}, cell));
    assert((cell == Cell{-1, -1, true}));
    assert(!CellForRay({{0, 0, 0}, {.0001f, 0, 1}}, cell));
    assert(!CellForRay({{std::numeric_limits<float>::infinity(), 0, 0}, {0, 0, 1}}, cell));
    Lookup lookup;
    const auto* warm = GetMask(1, 0, {-1, -1, true}, synthetic.faces.data(), synthetic.faces.size(),
        synthetic.View(), &lookup);
    assert(warm && lookup == Lookup::Built);
    assert(warm == GetMask(1, 0, {-1, -1, true}, synthetic.faces.data(), synthetic.faces.size(),
        synthetic.View(), &lookup) && lookup == Lookup::Hit);
    assert(!GetMask(0, 0, {-1, -1, true}, synthetic.faces.data(), synthetic.faces.size(),
        synthetic.View(), &lookup) && lookup == Lookup::Unavailable);
    // A burst may exhaust the time budget sooner, but cannot exceed the build cap.
    unsigned builds = 0, deferrals = 0;
    for (uint64_t id = 100; id < 110; ++id) {
        GetMask(id, 0, {-1, -1, true}, synthetic.faces.data(), synthetic.faces.size(),
            synthetic.View(), &lookup, 42);
        builds += lookup == Lookup::Built;
        deferrals += lookup == Lookup::Deferred;
    }
    assert(builds > 0 && builds <= 4 && builds + deferrals == 10);
    GetMask(200, 0, {-1, -1, true}, synthetic.faces.data(), synthetic.faces.size(),
        synthetic.View(), &lookup, 43);
    assert(lookup == Lookup::Built);
    GetMask(200, 0, {-1, -1, true}, synthetic.faces.data(), synthetic.faces.size(),
        synthetic.View(), &lookup, 43);
    assert(lookup == Lookup::Hit);
}
