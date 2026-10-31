#if defined(WOWDECOMP_LINUX)
#define WOWDECOMP_GX_DXVK
#include <d3d9.h>
#include "WxlGrassInstancing.h"
#include "WxlVisualEnvironment.h"
#include "Engine/Source/Gx/CGxDevice/CGxShader.h"
#include "Engine/Source/Gx/CGxDeviceD3d9Ex/CGxDeviceD3d9Ex.h"
#include "Engine/Source/Gx/Gx.h"
#include "Engine/Source/ModernAssets/NativeVisualHooks.h"
#include "Engine/Source/ModernAssets/WxlGrass/GrassInstancing.hpp"
#include "Engine/Source/ModernAssets/WxlWmo/WmoShaderPack.h"
#include "WoW/Source/WorldClient/DetailDoodad.h"
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <unordered_map>
#include <vector>

namespace {
using namespace wxl::grass::instancing;
using wxl::wmo::PackEntry;
using wxl::wmo::ShaderPack;

constexpr uint64_t kExpireFrames = 900, kSweepEvery = 128;
constexpr size_t kMaxGeometry = 2048;

template<class T> struct Release { void operator()(T* p) const { if (p) p->Release(); } };
template<class T> using Com = std::unique_ptr<T, Release<T>>;

ShaderPack pack;
bool initialised = false, enabled = false;
IDirect3DDevice9* device = nullptr;
Com<IDirect3DVertexDeclaration9> declaration;
std::unordered_map<const PackEntry*, Com<IDirect3DVertexShader9>> shaders;
CGxShader* noted = nullptr;
uint64_t frame = 0;
FILE* logFile = nullptr;
bool problem = false;

// Counters since the last report.
uint64_t planned = 0, drawn = 0, instancesDrawn = 0, builds = 0, fallbacks = 0, failures = 0;

void Log(const char* format, ...) __attribute__((format(printf, 1, 2)));
void Log(const char* format, ...) {
    if (!logFile) return;
    va_list args; va_start(args, format);
    std::vfprintf(logFile, format, args); va_end(args);
    std::fputc('\n', logFile); std::fflush(logFile);
}

struct Geometry {
    Com<IDirect3DVertexBuffer9> vb;
    Com<IDirect3DIndexBuffer9> ib;
    uint32_t vertices = 0, indices = 0;
    bool bad = false;
};
std::unordered_map<uint32_t, Geometry> geometry;

struct Range { uint32_t doodad; uint32_t first, count; };
struct Entry {
    Com<IDirect3DVertexBuffer9> vb;
    std::vector<Range> ranges;
    uint32_t adds = 0;
    uint64_t lastUse = 0;
    IDirect3DVertexShader9* shader = nullptr;
};
// Never destroyed: batches are freed during shutdown, after static destruction would have run.
using EntryMap = std::unordered_map<const CDetailDoodadInstBatch*, std::unique_ptr<Entry>>;
EntryMap& entries = *new EntryMap;
// Batches that could not be instanced, with the plant count they were refused at (so a batch that
// keeps failing is not rebuilt every frame).
using RefusedMap = std::unordered_map<const CDetailDoodadInstBatch*, uint32_t>;
RefusedMap& refused = *new RefusedMap;

const Entry* armed = nullptr;

CGxDeviceD3d9Ex* Backend() {
    auto* gx = g_theGxDevicePtr;
    return gx && gx->m_api == GxApi_D3d9Ex ? static_cast<CGxDeviceD3d9Ex*>(gx) : nullptr;
}

void DropAll() {
    armed = nullptr;
    NativeVisualHooks::instancedDraw = nullptr;
    entries.clear();
    refused.clear();
    geometry.clear();
    shaders.clear();
    declaration.reset();
}

bool EnsureDevice(IDirect3DDevice9* current) {
    if (device != current) {
        DropAll();
        device = current;
        problem = false;
    }
    if (declaration) return true;
    // Stream 0: model vertex (position, uv). Stream 1: the plant (three transform rows, normal,
    // colour). Normal and colour keep the engine's own element types, so the shader sees the same values.
    const D3DVERTEXELEMENT9 elements[] = {
        {0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
        {0, 12, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
        {1, 0, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 1},
        {1, 16, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 2},
        {1, 32, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 3},
        {1, 48, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_NORMAL, 0},
        {1, 60, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR, 0},
        D3DDECL_END()};
    IDirect3DVertexDeclaration9* created = nullptr;
    if (FAILED(current->CreateVertexDeclaration(elements, &created))) return false;
    declaration.reset(created);
    return true;
}

bool Code(const CGxShader* shader, const uint8_t*& bytes, size_t& size) {
    if (!shader || !shader->m_valid) return false;
    bytes = shader->m_code.m_data; size = shader->m_code.Count();
    return bytes && size >= 8 && size <= wxl::wmo::kMaxShaderBytes;
}

IDirect3DVertexShader9* InstancedShader(IDirect3DDevice9* dev, CGxShader* vertex) {
    const uint8_t* code; size_t size;
    if (!Code(vertex, code, size)) return nullptr;
    const PackEntry* entry = pack.FindVertex(code, size);
    if (!entry) return nullptr;
    auto found = shaders.find(entry);
    if (found != shaders.end()) return found->second.get();
    IDirect3DVertexShader9* created = nullptr;
    if (FAILED(dev->CreateVertexShader(entry->bytecode.data(), &created))) {
        created = nullptr;
        Log("instanced grass: the backend rejected a derived shader (%zu words)", entry->bytecode.size());
    }
    shaders[entry].reset(created);
    return created;
}

Geometry* EnsureGeometry(IDirect3DDevice9* dev, uint32_t id) {
    auto found = geometry.find(id);
    if (found != geometry.end()) return found->second.bad ? nullptr : &found->second;
    if (geometry.size() >= kMaxGeometry) return nullptr;
    Geometry& g = geometry[id];
    std::vector<GpuVertex> vertices;
    std::vector<uint16_t> indices;
    if (!WxlGrassModelGeometry(id, vertices, indices)) { g.bad = true; return nullptr; }
    IDirect3DVertexBuffer9* vb = nullptr; IDirect3DIndexBuffer9* ib = nullptr;
    const UINT vbBytes = UINT(vertices.size() * sizeof(GpuVertex)), ibBytes = UINT(indices.size() * sizeof(uint16_t));
    void* mapped = nullptr;
    if (FAILED(dev->CreateVertexBuffer(vbBytes, D3DUSAGE_WRITEONLY, 0, D3DPOOL_DEFAULT, &vb, nullptr)) ||
        FAILED(dev->CreateIndexBuffer(ibBytes, D3DUSAGE_WRITEONLY, D3DFMT_INDEX16, D3DPOOL_DEFAULT, &ib, nullptr))) {
        if (vb) vb->Release();
        if (ib) ib->Release();
        g.bad = true;
        return nullptr;
    }
    g.vb.reset(vb); g.ib.reset(ib);
    if (FAILED(vb->Lock(0, vbBytes, &mapped, 0)) || !mapped) { g.bad = true; return nullptr; }
    std::memcpy(mapped, vertices.data(), vbBytes); vb->Unlock();
    if (FAILED(ib->Lock(0, ibBytes, &mapped, 0)) || !mapped) { g.bad = true; return nullptr; }
    std::memcpy(mapped, indices.data(), ibBytes); ib->Unlock();
    g.vertices = uint32_t(vertices.size()); g.indices = uint32_t(indices.size());
    return &g;
}

Entry* Build(IDirect3DDevice9* dev, const CDetailDoodadInstBatch& batch, IDirect3DVertexShader9* shader) {
    std::vector<GpuInstance> plants;
    std::vector<uint32_t> doodads;
    if (!WxlGrassBakeInstances(batch, plants, doodads) || plants.empty()) return nullptr;

    // Group plants by model, keeping first-seen model order; one instanced draw per model.
    std::vector<uint32_t> order;
    std::unordered_map<uint32_t, uint32_t> slot;
    for (uint32_t id : doodads)
        if (slot.emplace(id, uint32_t(order.size())).second) order.push_back(id);
    for (uint32_t id : order)
        if (!EnsureGeometry(dev, id)) return nullptr;
    std::vector<uint32_t> counts(order.size(), 0);
    for (uint32_t id : doodads) ++counts[slot[id]];
    std::vector<uint32_t> start(order.size(), 0);
    for (size_t g = 1; g < order.size(); ++g) start[g] = start[g - 1] + counts[g - 1];
    std::vector<GpuInstance> sorted(plants.size());
    std::vector<uint32_t> fill = start;
    for (size_t i = 0; i < plants.size(); ++i) sorted[fill[slot[doodads[i]]]++] = plants[i];

    const UINT bytes = UINT(sorted.size() * sizeof(GpuInstance));
    IDirect3DVertexBuffer9* vb = nullptr;
    void* mapped = nullptr;
    if (FAILED(dev->CreateVertexBuffer(bytes, D3DUSAGE_WRITEONLY, 0, D3DPOOL_DEFAULT, &vb, nullptr)) || !vb) return nullptr;
    Com<IDirect3DVertexBuffer9> guard(vb);
    if (FAILED(vb->Lock(0, bytes, &mapped, 0)) || !mapped) return nullptr;
    std::memcpy(mapped, sorted.data(), bytes);
    vb->Unlock();

    auto entry = std::make_unique<Entry>();
    entry->vb = std::move(guard);
    entry->adds = batch.m_adds.Count();
    for (size_t g = 0; g < order.size(); ++g) entry->ranges.push_back({order[g], start[g], counts[g]});
    ++builds;
    Entry* raw = entry.get();
    entries[&batch] = std::move(entry);
    return raw;
}

bool InstancedDraw(void* devicePointer, int, int, unsigned, unsigned, unsigned, unsigned) {
    const Entry* entry = armed;
    if (!entry) return false;
    auto* dev = static_cast<IDirect3DDevice9*>(devicePointer);

    IDirect3DVertexDeclaration9* savedDeclaration = nullptr;
    IDirect3DVertexShader9* savedShader = nullptr;
    IDirect3DVertexBuffer9* savedVb[2] = {};
    UINT savedOffset[2] = {}, savedStride[2] = {}, savedFrequency[2] = {1, 1};
    IDirect3DIndexBuffer9* savedIndices = nullptr;
    bool saved = SUCCEEDED(dev->GetVertexDeclaration(&savedDeclaration)) && SUCCEEDED(dev->GetVertexShader(&savedShader));
    for (UINT i = 0; i < 2; ++i) {
        saved = saved && SUCCEEDED(dev->GetStreamSource(i, &savedVb[i], &savedOffset[i], &savedStride[i]));
        saved = saved && SUCCEEDED(dev->GetStreamSourceFreq(i, &savedFrequency[i]));
    }
    saved = saved && SUCCEEDED(dev->GetIndices(&savedIndices));

    const auto releaseSaved = [&] {
        for (auto* vb : savedVb) if (vb) vb->Release();
        if (savedIndices) savedIndices->Release();
        if (savedDeclaration) savedDeclaration->Release();
        if (savedShader) savedShader->Release();
    };
    if (!saved) { releaseSaved(); ++failures; return true; }   // nothing drawn this call; the state is untouched

    bool ok = SUCCEEDED(dev->SetVertexShader(entry->shader)) && SUCCEEDED(dev->SetVertexDeclaration(declaration.get()));
    for (const Range& range : entry->ranges) {
        if (!ok) break;
        if (!range.count) continue;
        const Geometry& g = geometry[range.doodad];
        ok = SUCCEEDED(dev->SetStreamSource(0, g.vb.get(), 0, sizeof(GpuVertex))) && SUCCEEDED(dev->SetIndices(g.ib.get())) &&
             SUCCEEDED(dev->SetStreamSourceFreq(0, D3DSTREAMSOURCE_INDEXEDDATA | range.count)) &&
             SUCCEEDED(dev->SetStreamSource(1, entry->vb.get(), range.first * sizeof(GpuInstance), sizeof(GpuInstance))) &&
             SUCCEEDED(dev->SetStreamSourceFreq(1, D3DSTREAMSOURCE_INSTANCEDATA | 1u)) &&
             SUCCEEDED(dev->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, g.vertices, 0, g.indices / 3));
        if (ok) instancesDrawn += range.count;
    }

    bool restored = true;
    for (UINT i = 0; i < 2; ++i) {
        restored = SUCCEEDED(dev->SetStreamSourceFreq(i, savedFrequency[i])) && restored;
        restored = SUCCEEDED(dev->SetStreamSource(i, savedVb[i], savedOffset[i], savedStride[i])) && restored;
    }
    restored = SUCCEEDED(dev->SetIndices(savedIndices)) && restored;
    restored = SUCCEEDED(dev->SetVertexDeclaration(savedDeclaration)) && restored;
    restored = SUCCEEDED(dev->SetVertexShader(savedShader)) && restored;
    releaseSaved();
    if (!restored) {
        problem = true;
        Log("instanced grass: device state restore failed; the stock path runs until graphics reset");
    }
    if (!ok) { ++failures; Log("instanced grass: the backend rejected an instanced draw"); problem = true; }
    if (drawn++ == 0) std::fprintf(stderr, "WarcraftXL: instanced grass active (%zu derived shader(s))\n", pack.Count());
    return true;
}
}  // namespace

void WxlGrassInstancingInitialize() {
    if (initialised) return;
    initialised = true;
    enabled = WxlProfileEnabled("WXL_GRASS_INSTANCING", true);
    if (!enabled) return;
    std::error_code error;
    const auto executable = std::filesystem::canonical("/proc/self/exe", error);
    const auto directory = error ? std::filesystem::current_path() : executable.parent_path();
    std::string why;
    if (!pack.Load((directory / "Utils/WarcraftXL/visuals/grass_instanced.pack").string(), why)) {
        std::fprintf(stderr, "WarcraftXL: instanced grass pack unavailable (%s); stock grass drawing. Run tools/build-wmo-shader-pack.py\n", why.c_str());
        enabled = false;
        return;
    }
    logFile = std::fopen("Logs/GrassInstancing.log", "w");
    Log("instanced grass: %zu derived shaders loaded", pack.Count());
}

void WxlGrassInstancingNoteShader(CGxShader* vertex) { noted = vertex; }

const void* WxlGrassInstancingPlan(const CDetailDoodadInstBatch* batch) {
    if (!enabled || problem || !batch || !noted) return nullptr;
    auto* gx = Backend();
    if (!gx || !gx->m_d3dDevice || !EnsureDevice(gx->m_d3dDevice)) return nullptr;
    IDirect3DVertexShader9* shader = InstancedShader(gx->m_d3dDevice, noted);
    if (!shader) { ++fallbacks; return nullptr; }
    ++planned;
    auto found = entries.find(batch);
    Entry* entry = found != entries.end() ? found->second.get() : nullptr;
    if (entry && entry->adds != batch->m_adds.Count()) { entries.erase(found); entry = nullptr; }
    if (!entry) {
        const auto no = refused.find(batch);
        if (no != refused.end() && no->second == batch->m_adds.Count()) { ++fallbacks; return nullptr; }
        entry = Build(gx->m_d3dDevice, *batch, shader);
        if (!entry) refused[batch] = batch->m_adds.Count();
    }
    if (!entry) { ++fallbacks; return nullptr; }
    entry->shader = shader;
    entry->lastUse = frame;
    return entry;
}

void WxlGrassInstancingArm(const void* plan) {
    armed = static_cast<const Entry*>(plan);
    NativeVisualHooks::instancedDraw = &InstancedDraw;
}

void WxlGrassInstancingDisarm() {
    armed = nullptr;
    NativeVisualHooks::instancedDraw = nullptr;
}

void WxlGrassInstancingForget(const CDetailDoodadInstBatch* batch) {
    if (!entries.empty()) entries.erase(batch);
    if (!refused.empty()) refused.erase(batch);
}

void WxlGrassInstancingFrame() {
    if (!enabled) return;
    ++frame;
    if (frame % kSweepEvery) return;
    for (auto it = entries.begin(); it != entries.end();)
        it = frame - it->second->lastUse > kExpireFrames ? entries.erase(it) : std::next(it);
    if (frame % 600 == 0)
        Log("instanced grass: %zu batches cached, %zu models; last interval: planned %llu drawn %llu instances %llu builds %llu fallbacks %llu failures %llu",
            entries.size(), geometry.size(), (unsigned long long)planned, (unsigned long long)drawn, (unsigned long long)instancesDrawn,
            (unsigned long long)builds, (unsigned long long)fallbacks, (unsigned long long)failures);
    if (frame % 600 == 0) planned = drawn = instancesDrawn = builds = fallbacks = failures = 0;
}

void WxlGrassInstancingRelease() {
    DropAll();
    device = nullptr;
    problem = false;
}
#endif
