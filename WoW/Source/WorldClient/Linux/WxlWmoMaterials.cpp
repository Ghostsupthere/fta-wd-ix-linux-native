#if defined(WOWDECOMP_LINUX)
#define WOWDECOMP_GX_DXVK
#include <d3d9.h>
#include "WxlWmoMaterials.h"
#include "WxlVisualEnvironment.h"
#include "Engine/Source/Gx/CGxDevice/CGxShader.h"
#include "Engine/Source/Gx/CGxDeviceD3d9Ex/CGxDeviceD3d9Ex.h"
#include "Engine/Source/Gx/Gx.h"
#include "Engine/Source/Base/Handle.h"
#include "Engine/Source/ModernAssets/NativeVisualHooks.h"
#include "Engine/Source/ModernAssets/WxlWmo/WmoShaderPack.h"
#include "Engine/Source/Services/Texture.h"
#include "WoW/Source/WorldClient/Map.h"
#include "WoW/Source/WorldClient/MapObj.h"
#include "WoW/Source/WorldClient/MapObjGroup.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <unordered_map>
#include <utility>

namespace {
using wxl::wmo::PackEntry;
using wxl::wmo::ShaderPack;

constexpr uint64_t kExpireFrames = 300;
constexpr UINT kMeshStride = 36;   // ModernWmoMeshView stride: 4 UV sets + packed weights
constexpr size_t kMaxPairs = 256, kMaxMeshes = 256, kMaxTextureSets = 256;

template<class T> struct Release { void operator()(T* p) const { if (p) p->Release(); } };
template<class T> using Com = std::unique_ptr<T, Release<T>>;

ShaderPack pack;
bool initialised = false, enabled = false;
WxlWmoContext context;
IDirect3DDevice9* device = nullptr;
Com<IDirect3DVertexDeclaration9> declaration;
uint64_t frame = 0, draws = 0, fallbacks = 0, shaderMisses = 0, textureWaits = 0;
// Reach counters: how far WMO batches get toward a layered draw.
uint64_t leaves = 0, modernLeaves = 0, latchedBatches = 0, bindsWithBatch = 0, notLayered = 0;
FILE* logFile = nullptr;

void Log(const char* format, ...) __attribute__((format(printf, 1, 2)));
void Log(const char* format, ...) {
    if (!logFile) return;
    va_list args; va_start(args, format);
    std::vfprintf(logFile, format, args); va_end(args);
    std::fputc('\n', logFile); std::fflush(logFile);
}

struct Pair {
    Com<IDirect3DVertexShader9> vs;
    Com<IDirect3DPixelShader9> ps;
    const PackEntry* pixel = nullptr;
};
std::map<std::pair<const PackEntry*, const PackEntry*>, Pair> pairs;
struct EntryLookup { const CGxShader* shader; const uint8_t* code; size_t size; const PackEntry* entry; bool pixel; uint32_t layers; };
struct Mesh { std::weak_ptr<ModernWmoGroupVisuals> source; Com<IDirect3DVertexBuffer9> vb; uint64_t lastUse = 0; };
std::unordered_map<const ModernWmoGroupVisuals*, Mesh> meshes;
struct Textures {
    std::weak_ptr<ModernWmoRootVisuals> source;
    std::array<CTexture*, 8> handles{};
    uint64_t lastUse = 0;
    bool tried = false;
    ~Textures() { for (CTexture* texture : handles) if (texture) HandleClose(reinterpret_cast<HOBJECT>(texture)); }
};
std::map<std::pair<const ModernWmoRootVisuals*, uint32_t>, std::unique_ptr<Textures>> textureSets;

struct Pending {
    const Pair* pair = nullptr;
    IDirect3DVertexBuffer9* vb = nullptr;
    uint32_t materialFlags = 0, materialTint = 0;
    uint32_t vertices = 0, stages = 0;
    std::array<void*, 8> previous{};
    std::array<unsigned, 8> slots{};
    int tint = -1;
    bool texturesBound = false;
} pending;

CGxDeviceD3d9Ex* Backend() {
    auto* gx = g_theGxDevicePtr;
    return gx && gx->m_api == GxApi_D3d9Ex ? static_cast<CGxDeviceD3d9Ex*>(gx) : nullptr;
}
EGxRenderState TextureState(unsigned slot) { return static_cast<EGxRenderState>(GxRs_Texture0 + slot); }

void RestoreTextures() {
    if (pending.texturesBound && g_theGxDevicePtr)
        for (uint32_t s = 0; s < pending.stages; ++s)
            g_theGxDevicePtr->RsSet(TextureState(pending.slots[s]), pending.previous[s]);
    pending = {};
}

void Initialise() {
    if (initialised) return;
    initialised = true;
    enabled = WxlProfileEnabled("WXL_MODERN_ASSETS", true) && WxlProfileEnabled("WXL_MODERN_WMO_MATERIALS", true);
    std::error_code error;
    const auto executable = std::filesystem::canonical("/proc/self/exe", error);
    const auto directory = error ? std::filesystem::current_path() : executable.parent_path();
    std::filesystem::create_directories("Logs", error);
    logFile = std::fopen("Logs/WmoVisuals.log", "w");
    if (!enabled) { Log("wmo materials: disabled by profile"); return; }
    std::string why;
    const auto path = (directory / "Utils/WarcraftXL/visuals/wmo_shaders.pack").string();
    if (!pack.Load(path, why)) {
        enabled = false;
        Log("wmo materials: no usable shader pack (%s: %s); modern WMO layers draw stock. Run tools/build-wmo-shader-pack.py", path.c_str(), why.c_str());
        return;
    }
    Log("wmo materials: shader pack loaded, %zu patched shaders", pack.Count());
}

void ClearGpu() {
    RestoreTextures();
    pairs.clear(); meshes.clear(); textureSets.clear(); declaration.reset();
    device = nullptr;
}

bool EnsureDevice(IDirect3DDevice9* dev) {
    if (dev != device) { ClearGpu(); device = dev; }
    if (!dev) return false;
    if (declaration) return true;
    const D3DVERTEXELEMENT9 elements[] = {
        {0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
        {0, 12, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_NORMAL, 0},
        {0, 24, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR, 0},
        {1, 0, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
        {1, 8, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 1},
        {1, 16, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 2},
        {1, 24, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 3},
        {1, 32, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR, 1}, D3DDECL_END()};
    IDirect3DVertexDeclaration9* created = nullptr;
    if (FAILED(dev->CreateVertexDeclaration(elements, &created))) return false;
    declaration.reset(created);
    return true;
}

bool Code(const CGxShader* shader, const uint8_t*& bytes, size_t& size) {
    if (!shader || !shader->m_valid) return false;
    bytes = shader->m_code.m_data; size = shader->m_code.Count();
    return bytes && size >= 8 && size <= wxl::wmo::kMaxShaderBytes;
}

const Pair* ShaderPair(IDirect3DDevice9* dev, CGxShader* vertex, CGxShader* pixel, uint32_t layers) {
    const uint8_t *vcode, *pcode; size_t vsize, psize;
    if (!Code(vertex, vcode, vsize) || !Code(pixel, pcode, psize)) return nullptr;
    const PackEntry* vsEntry = pack.FindVertex(vcode, vsize);
    if (!vsEntry) { ++shaderMisses; return nullptr; }
    const PackEntry* psEntry = pack.FindPixel(pcode, psize, layers, *vsEntry);
    if (!psEntry) { ++shaderMisses; return nullptr; }
    const auto key = std::make_pair(vsEntry, psEntry);
    auto found = pairs.find(key);
    if (found != pairs.end()) return &found->second;
    if (pairs.size() >= kMaxPairs) return nullptr;
    IDirect3DVertexShader9* v = nullptr; IDirect3DPixelShader9* p = nullptr;
    if (FAILED(dev->CreateVertexShader(vsEntry->bytecode.data(), &v))) return nullptr;
    Com<IDirect3DVertexShader9> vOwner(v);
    if (FAILED(dev->CreatePixelShader(psEntry->bytecode.data(), &p))) return nullptr;
    Pair& pair = pairs[key];
    pair.vs = std::move(vOwner); pair.ps.reset(p); pair.pixel = psEntry;
    return &pair;
}

IDirect3DVertexBuffer9* MeshBuffer(IDirect3DDevice9* dev) {
    const auto& source = context.groupData;
    if (meshes.find(source.get()) == meshes.end() && meshes.size() >= kMaxMeshes) return nullptr;
    auto& mesh = meshes[source.get()];
    mesh.lastUse = frame;
    if (mesh.source.lock() == source && mesh.vb) return mesh.vb.get();
    mesh.vb.reset(); mesh.source = source;
    const ModernWmoMeshView view = ModernWmoMesh(*source);
    const UINT size = static_cast<UINT>(view.vertexCount * view.vertexStride);
    IDirect3DVertexBuffer9* vb = nullptr;
    if (!size || FAILED(dev->CreateVertexBuffer(size, D3DUSAGE_WRITEONLY, 0, D3DPOOL_MANAGED, &vb, nullptr))) return nullptr;
    Com<IDirect3DVertexBuffer9> owner(vb);
    void* dest = nullptr;
    if (FAILED(vb->Lock(0, size, &dest, 0))) return nullptr;
    std::memcpy(dest, view.vertices, size);
    if (FAILED(vb->Unlock())) return nullptr;
    mesh.vb = std::move(owner);
    return mesh.vb.get();
}

bool ResolveTextures(const ModernWmoMaterialView& material, std::array<CGxTex*, 8>& out) {
    const auto key = std::make_pair(context.rootData.get(), material.index);
    if (textureSets.find(key) == textureSets.end() && textureSets.size() >= kMaxTextureSets) return false;
    auto& slot = textureSets[key];
    if (slot && slot->source.lock() != context.rootData) slot.reset();
    if (!slot) { slot = std::make_unique<Textures>(); slot->source = context.rootData; }
    Textures& set = *slot;
    set.lastUse = frame;
    const uint32_t count = material.layers == 4 ? 8 : 2;
    if (!set.tried) {
        set.tried = true;
        for (uint32_t s = 0; s < count; ++s)
            if (material.textures[s] && *material.textures[s]) set.handles[s] = CMap::LoadTexture(material.textures[s]);
    }
    for (uint32_t s = 0; s < count; ++s) {
        if (!set.handles[s]) return false;
        out[s] = TextureGetGxTex(set.handles[s], 0, nullptr);
        if (!out[s]) { ++textureWaits; return false; }   // still streaming: stock this batch, retry next
    }
    return true;
}

void Bind(CGxShader* vertex, CGxShader* pixelShader) {
    RestoreTextures();
    const uint32_t materialIndex = std::exchange(context.material, UINT32_MAX);
    if (!enabled || !context.rootData || !context.groupData || materialIndex == UINT32_MAX) return;
    ++bindsWithBatch;
    ModernWmoMaterialView material;
    const ModernWmoMeshView mesh = ModernWmoMesh(*context.groupData);
    if (!ModernWmoFindMaterial(*context.rootData, materialIndex, material) || mesh.vertexCount == 0 || mesh.vertexStride != kMeshStride || mesh.uvSets < 2 ||
        (material.layers != 2 && material.layers != 4) || (material.layers == 4 && (mesh.uvSets != 4 || !mesh.hasWeights))) { ++notLayered; return; }
    auto* gx = Backend();
    if (!gx || !EnsureDevice(gx->m_d3dDevice)) return;
    const Pair* pair = ShaderPair(device, vertex, pixelShader, material.layers);
    if (!pair) { ++fallbacks; return; }
    IDirect3DVertexBuffer9* vb = MeshBuffer(device);
    std::array<CGxTex*, 8> textures{};
    if (!vb || !ResolveTextures(material, textures)) { ++fallbacks; return; }
    pending.pair = pair; pending.vb = vb;
    pending.materialFlags = material.flags; pending.materialTint = material.tint;
    pending.vertices = static_cast<uint32_t>(mesh.vertexCount);
    pending.stages = material.layers == 4 ? 8 : 2;
    pending.tint = pair->pixel->tintRegister;
    for (uint32_t s = 0; s < pending.stages; ++s) pending.slots[s] = pair->pixel->samplerSlots[s];
    // Texture uploads pass through Gx's deferred flush. Shaders, constants and streams
    // change only inside the draw callback, after that flush.
    for (uint32_t s = 0; s < pending.stages; ++s) {
        pending.previous[s] = gx->m_appRenderStates[TextureState(pending.slots[s])].m_value.m_data.p;
        g_theGxDevicePtr->RsSet(TextureState(pending.slots[s]), static_cast<void*>(textures[s]));
    }
    pending.texturesBound = true;
}

bool Draw(void* raw, int type, int base, unsigned minimum, unsigned count, unsigned start, unsigned primitives) {
    if (!pending.pair) return false;
    auto* dev = static_cast<IDirect3DDevice9*>(raw);
    const Pending now = pending;
    IDirect3DVertexBuffer9 *vb0 = nullptr, *vb1 = nullptr;
    IDirect3DVertexDeclaration9* decl = nullptr;
    IDirect3DVertexShader9* vs = nullptr; IDirect3DPixelShader9* ps = nullptr;
    UINT off0 = 0, off1 = 0, stride0 = 0, stride1 = 0, frequency0 = 0, frequency1 = 0;
    DWORD fvf = 0, samplers[8][2] = {};
    float tintOld[4] = {};
    bool valid = dev == device && base >= 0 && minimum <= now.vertices && count <= now.vertices - minimum;
    valid = SUCCEEDED(dev->GetStreamSource(0, &vb0, &off0, &stride0)) && valid;
    valid = SUCCEEDED(dev->GetStreamSource(1, &vb1, &off1, &stride1)) && valid;
    valid = SUCCEEDED(dev->GetStreamSourceFreq(0, &frequency0)) && valid;
    valid = SUCCEEDED(dev->GetStreamSourceFreq(1, &frequency1)) && valid;
    valid = SUCCEEDED(dev->GetVertexDeclaration(&decl)) && valid;
    valid = SUCCEEDED(dev->GetFVF(&fvf)) && valid;
    valid = SUCCEEDED(dev->GetVertexShader(&vs)) && valid;
    valid = SUCCEEDED(dev->GetPixelShader(&ps)) && valid;
    if (now.tint >= 0) valid = SUCCEEDED(dev->GetPixelShaderConstantF(now.tint, tintOld, 1)) && valid;
    for (uint32_t s = 0; s < now.stages; ++s) {
        valid = SUCCEEDED(dev->GetSamplerState(now.slots[s], D3DSAMP_ADDRESSU, &samplers[s][0])) && valid;
        valid = SUCCEEDED(dev->GetSamplerState(now.slots[s], D3DSAMP_ADDRESSV, &samplers[s][1])) && valid;
    }
    D3DVERTEXBUFFER_DESC desc = {};
    D3DVERTEXELEMENT9 elements[MAXD3DDECLLENGTH + 1] = {};
    UINT elementCount = MAXD3DDECLLENGTH + 1;
    bool position = false, normal = false, color = false;
    if (decl && SUCCEEDED(decl->GetDeclaration(elements, &elementCount)))
        for (UINT i = 0; i < elementCount && i < MAXD3DDECLLENGTH; ++i) {
            const auto& e = elements[i];
            if (e.Stream != 0 || e.Method != D3DDECLMETHOD_DEFAULT || e.UsageIndex != 0) continue;
            if (e.Usage == D3DDECLUSAGE_POSITION && e.Offset == 0 && e.Type == D3DDECLTYPE_FLOAT3) position = true;
            if (e.Usage == D3DDECLUSAGE_NORMAL && e.Offset == 12 && e.Type == D3DDECLTYPE_FLOAT3) normal = true;
            if (e.Usage == D3DDECLUSAGE_COLOR && e.Offset == 24 && e.Type == D3DDECLTYPE_D3DCOLOR) color = true;
        }
    const uint64_t rebase = uint64_t(off0) + uint64_t(base) * stride0;
    valid = position && normal && color && vb0 && SUCCEEDED(vb0->GetDesc(&desc)) && stride0 >= 28 &&
            frequency0 == 1 && frequency1 == 1 && rebase + uint64_t(minimum + count) * stride0 <= desc.Size && valid;
    bool applied = false;
    if (valid) {
        applied = SUCCEEDED(dev->SetStreamSource(0, vb0, static_cast<UINT>(rebase), stride0));
        applied = SUCCEEDED(dev->SetStreamSource(1, now.vb, 0, kMeshStride)) && applied;
        applied = SUCCEEDED(dev->SetVertexDeclaration(declaration.get())) && applied;
        applied = SUCCEEDED(dev->SetVertexShader(now.pair->vs.get())) && applied;
        applied = SUCCEEDED(dev->SetPixelShader(now.pair->ps.get())) && applied;
        for (uint32_t s = 0; s < now.stages; ++s) {
            applied = SUCCEEDED(dev->SetSamplerState(now.slots[s], D3DSAMP_ADDRESSU, (now.materialFlags & 0x40) ? D3DTADDRESS_CLAMP : D3DTADDRESS_WRAP)) && applied;
            applied = SUCCEEDED(dev->SetSamplerState(now.slots[s], D3DSAMP_ADDRESSV, (now.materialFlags & 0x80) ? D3DTADDRESS_CLAMP : D3DTADDRESS_WRAP)) && applied;
        }
        if (now.tint >= 0) {
            const uint32_t t = now.materialTint;
            const float tint[4] = {float((t >> 16) & 255) / 255, float((t >> 8) & 255) / 255, float(t & 255) / 255, (t & 0xFFFFFF) ? 1.0f : 0.0f};
            applied = SUCCEEDED(dev->SetPixelShaderConstantF(now.tint, tint, 1)) && applied;
        }
    }
    const auto restore = [&] {
        if (!valid) return;   // a failed snapshot never mutates device state
        dev->SetStreamSource(0, vb0, off0, stride0); dev->SetStreamSource(1, vb1, off1, stride1);
        if (fvf) dev->SetFVF(fvf); else dev->SetVertexDeclaration(decl);
        dev->SetVertexShader(vs); dev->SetPixelShader(ps);
        for (uint32_t s = 0; s < now.stages; ++s) {
            dev->SetSamplerState(now.slots[s], D3DSAMP_ADDRESSU, samplers[s][0]);
            dev->SetSamplerState(now.slots[s], D3DSAMP_ADDRESSV, samplers[s][1]);
        }
        if (now.tint >= 0) dev->SetPixelShaderConstantF(now.tint, tintOld, 1);
    };
    if (!applied) restore();
    // The batch is consumed before the real draw so nested draws cannot inherit it.
    pending.pair = nullptr;
    dev->DrawIndexedPrimitive(static_cast<D3DPRIMITIVETYPE>(type), applied ? 0 : base, minimum, count, start, primitives);
    if (applied) {
        restore(); ++draws;
        if (draws == 1) Log("wmo materials: first layered draw (%u layers)", now.stages == 8 ? 4u : 2u);
    } else {
        if (++fallbacks == 1) Log("wmo materials: first fallback to the stock draw (device state or stream mismatch)");
    }
    if (vb0) vb0->Release(); if (vb1) vb1->Release(); if (decl) decl->Release();
    if (vs) vs->Release(); if (ps) ps->Release();
    RestoreTextures();
    return true;
}
} // namespace

WxlWmoLeafScope::WxlWmoLeafScope(CMapObj* root, CMapObjGroup* group) : m_previous(std::move(context)) {
    Initialise();
    NativeVisualHooks::wmoBind = enabled ? &Bind : nullptr;
    NativeVisualHooks::wmoDraw = enabled ? &Draw : nullptr;
    RestoreTextures();
    context = {};
    context.root = root; context.group = group;
    ++leaves;
    if (enabled && root && group && root->m_wxlModernWmo) {
        context.rootData = root->m_wxlVisuals;
        context.groupData = context.rootData ? group->m_wxlVisuals : nullptr;
        if (context.rootData && context.groupData) ++modernLeaves;
    }
}

WxlWmoLeafScope::~WxlWmoLeafScope() {
    RestoreTextures();
    context = std::move(m_previous);
}

void WxlWmoNoteBatch(const SMOBatch* batch) {
    RestoreTextures();
    context.material = UINT32_MAX;
    if (!batch || !context.rootData || !context.groupData || !context.group) return;
    const auto* begin = context.group->m_batchList;
    const int32_t count = context.group->m_batchListCount;
    if (count < 0 || count > 65536 || !begin || batch < begin || batch >= begin + count) return;
    context.material = batch->m_texture;
    ++latchedBatches;
}

void WxlWmoFrame() {
    RestoreTextures();
    ++frame;
    for (auto it = meshes.begin(); it != meshes.end();)
        if (it->second.source.expired() || frame - it->second.lastUse > kExpireFrames) it = meshes.erase(it); else ++it;
    for (auto it = textureSets.begin(); it != textureSets.end();)
        if (it->second->source.expired() || frame - it->second->lastUse > kExpireFrames) it = textureSets.erase(it); else ++it;
    if (frame % 3600 == 0 && enabled)
        Log("wmo materials: leaves=%llu modern-leaves=%llu latched=%llu binds=%llu not-layered=%llu | draws=%llu fallbacks=%llu shader-misses=%llu texture-waits=%llu",
            (unsigned long long)leaves, (unsigned long long)modernLeaves, (unsigned long long)latchedBatches, (unsigned long long)bindsWithBatch, (unsigned long long)notLayered,
            (unsigned long long)draws, (unsigned long long)fallbacks, (unsigned long long)shaderMisses, (unsigned long long)textureWaits);
}

void WxlWmoRelease() {
    if (enabled && leaves)
        Log("wmo materials (release): leaves=%llu modern-leaves=%llu latched=%llu binds=%llu not-layered=%llu | draws=%llu fallbacks=%llu shader-misses=%llu texture-waits=%llu",
            (unsigned long long)leaves, (unsigned long long)modernLeaves, (unsigned long long)latchedBatches, (unsigned long long)bindsWithBatch, (unsigned long long)notLayered,
            (unsigned long long)draws, (unsigned long long)fallbacks, (unsigned long long)shaderMisses, (unsigned long long)textureWaits);
    ClearGpu();
    context = {};
    NativeVisualHooks::wmoBind = nullptr;
    NativeVisualHooks::wmoDraw = nullptr;
}
#endif
