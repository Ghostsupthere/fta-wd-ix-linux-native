// Offline generator for the native client's patched WMO shader pack. Runs under Wine
// with the checksum-pinned Microsoft d3dcompiler_47.dll (see
// tools/build-wmo-shader-pack.py). The native client never loads this tool or Wine.
//
//   wxl_wmo_shader_pack <input-dir> <output-pack>
//
// <input-dir> holds raw D3D9 bytecode: *.vs and *.ps files extracted from the client's
// MapObj shader permutations.
#include <windows.h>
#include <d3dcompiler.h>
#include "Engine/Source/ModernAssets/WxlWmo/WmoShaderPack.h"
#include "Engine/Source/ModernAssets/WxlWmo/models/wmo/WmoShaderText.hpp"
#include "Engine/Source/ModernAssets/WxlWmo/WmoShaderCompact.hpp"
#include "Engine/Source/ModernAssets/WxlGrass/GrassShaderText.hpp"
#include "Engine/Source/ModernAssets/WxlGrass/GrassInstancing.hpp"
#include "Engine/Source/ModernAssets/WxlSurfaceLights/LightShaderPack.hpp"
#include "Engine/Source/ModernAssets/WxlSurfaceLights/ModelLightShaderText.hpp"
#include "Engine/Source/ModernAssets/WxlSurfaceLights/TerrainLightShaderText.hpp"
#include <map>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <vector>

namespace text = wxl::modern::assets::wmo::shadertext;
using wxl::wmo::PackEntry;

typedef HRESULT(WINAPI* PFN_Assemble)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*, ID3DInclude*, UINT, ID3DBlob**, ID3DBlob**);
typedef HRESULT(WINAPI* PFN_Disassemble)(LPCVOID, SIZE_T, UINT, LPCSTR, ID3DBlob**);
static PFN_Assemble g_assemble; static PFN_Disassemble g_disassemble;
static bool g_quiet = false;   // the first attempt of a fallback chain reports only its final failure

static bool ReadFile(const std::string& path, std::vector<uint8_t>& out) {
    FILE* f = std::fopen(path.c_str(), "rb"); if (!f) return false;
    uint8_t buffer[65536]; size_t n; out.clear();
    while ((n = std::fread(buffer, 1, sizeof buffer, f)) > 0) out.insert(out.end(), buffer, buffer + n);
    std::fclose(f); return true;
}
static std::vector<std::string> List(const std::string& dir, const char* extension) {
    std::vector<std::string> names; WIN32_FIND_DATAA data;
    HANDLE h = FindFirstFileA((dir + "\\*." + extension).c_str(), &data);
    if (h == INVALID_HANDLE_VALUE) return names;
    do names.push_back(data.cFileName); while (FindNextFileA(h, &data));
    FindClose(h); std::sort(names.begin(), names.end()); return names;
}
static std::string Disassemble(const std::vector<uint8_t>& code) {
    ID3DBlob* blob = nullptr;
    if (FAILED(g_disassemble(code.data(), code.size(), 0, nullptr, &blob)) || !blob) return {};
    std::string out(static_cast<const char*>(blob->GetBufferPointer())); blob->Release(); return out;
}
static bool Assemble(const std::string& source, const char* label, std::vector<uint32_t>& out) {
    ID3DBlob *blob = nullptr, *errors = nullptr;
    const HRESULT hr = g_assemble(source.c_str(), source.size(), label, nullptr, nullptr, 0, &blob, &errors);
    if (FAILED(hr) || !blob) {
        if (!g_quiet) std::fprintf(stderr, "%s: assemble failed: %s\n", label, errors ? static_cast<const char*>(errors->GetBufferPointer()) : "?");
        if (errors) errors->Release(); if (blob) blob->Release(); return false;
    }
    if (errors) errors->Release();
    if (blob->GetBufferSize() % 4) { blob->Release(); return false; }
    const auto* p = static_cast<const uint32_t*>(blob->GetBufferPointer());
    out.assign(p, p + blob->GetBufferSize() / 4); blob->Release(); return true;
}

int main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "usage: wxl_wmo_shader_pack <input-dir> <output-pack>\n"); return 2; }
    HMODULE compiler = LoadLibraryA("d3dcompiler_47.dll");
    g_assemble = reinterpret_cast<PFN_Assemble>(compiler ? GetProcAddress(compiler, "D3DAssemble") : nullptr);
    g_disassemble = reinterpret_cast<PFN_Disassemble>(compiler ? GetProcAddress(compiler, "D3DDisassemble") : nullptr);
    if (!g_assemble || !g_disassemble) { std::fprintf(stderr, "d3dcompiler_47.dll missing\n"); return 2; }
    if (std::strcmp(argv[1], "--disasm") == 0) {   // debugging aid: print one shader's disassembly
        std::vector<uint8_t> code;
        if (!ReadFile(argv[2], code)) return 1;
        std::fputs(Disassemble(code).c_str(), stdout);
        return 0;
    }
    if (std::strcmp(argv[1], "--lights") == 0 && argc >= 4) {
        // Surface lights (wxl-living-azeroth): the client's terrain, M2 and WMO shaders with the supplied
        // text patches applied. Inputs are named <group>__<family>__<index>.vs|ps with group tv/tp
        // (terrain vertex/pixel), m2 or wmo (vertex). Output: argv[3].
        namespace sl = wxl::surfacelights;
        const std::string lightDir = argv[2];
        struct Result { std::string patched; sl::LightPackEntry entry; bool conflict = false; };
        std::map<std::pair<int, uint64_t>, Result> results;
        unsigned seenCount = 0, skipped = 0, conflicts = 0;
        unsigned terrainVertex = 0, terrainVertexSkipped = 0, terrainPixel = 0, terrainPixelSkipped = 0;
        std::map<std::string, unsigned> skipReasons;
        for (const char* extension : {"vs", "ps"}) for (const auto& name : List(lightDir, extension)) {
            const size_t a = name.find("__"), b = a == std::string::npos ? a : name.find("__", a + 2);
            if (b == std::string::npos) continue;
            const std::string group = name.substr(0, a), family = name.substr(a + 2, b - a - 2);
            std::vector<uint8_t> code;
            if (!ReadFile(lightDir + "\\" + name, code) || code.size() < 8 || code.size() > wxl::wmo::kMaxShaderBytes) continue;
            ++seenCount;
            const bool pixel = std::strcmp(extension, "ps") == 0;
            std::string source = Disassemble(code), why;
            bool ok = false;
            uint8_t groupId = sl::kGroupModel;
            if (group == "tv") { groupId = sl::kGroupTerrain; ++terrainVertex; ok = sl::terrainshader::EditTerrainVertex(source, why); if (!ok) ++terrainVertexSkipped; }
            else if (group == "tp") { groupId = sl::kGroupTerrain; ++terrainPixel; ok = sl::terrainshader::EditTerrainPixel(source, why); if (!ok) ++terrainPixelSkipped; }
            else if (group == "m2" || group == "wmo") {
                groupId = group == "wmo" ? sl::kGroupWmo : sl::kGroupModel;
                sl::modelshader::g_family = family;
                ok = sl::modelshader::EditVertex(source, why);
            } else continue;
            if (!ok) { ++skipped; ++skipReasons[group + ": " + why]; continue; }
            sl::LightPackEntry e; e.kind = pixel ? 1 : 0; e.group = groupId; e.originalSize = uint32_t(code.size());
            e.originalHash = wxl::wmo::HashBytecode(code.data(), code.size());
            const auto key = std::make_pair(int(e.kind), e.originalHash);
            const auto it = results.find(key);
            if (it != results.end()) {   // the same stock bytecode in two families: only if both edits agree
                if (it->second.patched != source && !it->second.conflict) { it->second.conflict = true; ++conflicts; std::printf("conflict: %s edits differently from an earlier family\n", name.c_str()); }
                continue;
            }
            g_quiet = false;
            if (!Assemble(source, name.c_str(), e.bytecode)) { ++skipped; ++skipReasons[group + ": assemble"]; continue; }
            Result r; r.patched = std::move(source); r.entry = std::move(e);
            results.emplace(key, std::move(r));
        }
        // The terrain patch is a pair: a patched pixel shader reads a vertex output only a patched
        // vertex shader writes, so any terrain shader left unpatched drops the whole terrain set (the
        // supplied module marks it "not active" the same way).
        const bool terrainComplete = terrainVertex && terrainPixel && !terrainVertexSkipped && !terrainPixelSkipped;
        std::vector<sl::LightPackEntry> entries;
        for (auto& [key, r] : results) {
            if (r.conflict) continue;
            if (r.entry.group == sl::kGroupTerrain && !terrainComplete) continue;
            entries.push_back(std::move(r.entry));
        }
        for (const auto& [reason, n] : skipReasons) std::printf("  %u x %s\n", n, reason.c_str());
        const auto bytes = sl::LightShaderPack::Serialize(entries);
        FILE* file = std::fopen(argv[3], "wb");
        if (!file || std::fwrite(bytes.data(), 1, bytes.size(), file) != bytes.size()) { std::fprintf(stderr, "cannot write light pack\n"); return 1; }
        std::fclose(file);
        size_t perGroup[4] = {};
        for (const auto& e : entries) ++perGroup[e.group];
        std::printf("surface lights: %u shaders read, %u skipped, %u conflicts; terrain %s (vertex %u/%u patched, pixel %u/%u); pack: terrain %zu, M2 %zu, WMO %zu, %zu bytes\n",
                    seenCount, skipped, conflicts, terrainComplete ? "complete" : "INCOMPLETE (terrain dropped)",
                    terrainVertex - terrainVertexSkipped, terrainVertex, terrainPixel - terrainPixelSkipped, terrainPixel,
                    perGroup[1], perGroup[2], perGroup[3], bytes.size());
        return 0;
    }
    if (std::strcmp(argv[1], "--grass") == 0 && argc >= 4) {   // grass-motion vertex shaders (*.vs)
        const std::string grassDir = argv[2];
        std::vector<PackEntry> grass, instanced; unsigned seenCount = 0, skipped = 0;
        for (const auto& name : List(grassDir, "vs")) {
            std::vector<uint8_t> code;
            if (!ReadFile(grassDir + "\\" + name, code) || code.size() < 8 || code.size() > wxl::wmo::kMaxShaderBytes) continue;
            ++seenCount;
            // Hardware-instanced versions, keyed by the bytecode the engine will hold (its own, or the
            // motion-patched one that replaces it): the runtime looks up what the device is using.
            const auto addInstanced = [&](const std::string& text, const uint8_t* bytes, size_t size, const char* label) {
                std::string derived, why; PackEntry e; e.kind = 0; e.originalHash = wxl::wmo::HashBytecode(bytes, size); e.originalSize = uint32_t(size);
                if (!wxl::grass::instancing::MakeInstancedGrassText(text, derived, why) || !Assemble(derived, name.c_str(), e.bytecode)) {
                    std::printf("skip instanced grass %s (%s): %s\n", name.c_str(), label, why.c_str()); return; }
                instanced.push_back(std::move(e));
            };
            addInstanced(Disassemble(code), code.data(), code.size(), "stock");
            PackEntry e; e.kind = 0; e.originalHash = wxl::wmo::HashBytecode(code.data(), code.size()); e.originalSize = uint32_t(code.size());
            const std::string patched = wxl::grass::text::BuildGrassMotionText(Disassemble(code));
            if (patched.empty() || !Assemble(patched, name.c_str(), e.bytecode)) { ++skipped; std::printf("skip grass %s\n", name.c_str()); continue; }
            addInstanced(patched, reinterpret_cast<const uint8_t*>(e.bytecode.data()), e.bytecode.size() * 4, "motion");
            grass.push_back(std::move(e));
        }
        if (argc >= 5) {
            const auto packedInstanced = wxl::wmo::ShaderPack::Serialize(instanced);
            FILE* file = std::fopen(argv[4], "wb");
            if (!file || std::fwrite(packedInstanced.data(), 1, packedInstanced.size(), file) != packedInstanced.size()) { std::fprintf(stderr, "cannot write instanced pack\n"); return 1; }
            std::fclose(file);
            std::printf("instanced grass vertex shaders %zu, %zu bytes\n", instanced.size(), packedInstanced.size());
        }
        const auto packed = wxl::wmo::ShaderPack::Serialize(grass);
        FILE* file = std::fopen(argv[3], "wb");
        if (!file || std::fwrite(packed.data(), 1, packed.size(), file) != packed.size()) { std::fprintf(stderr, "cannot write pack\n"); return 1; }
        std::fclose(file);
        std::printf("grass vertex shaders %u (skipped %u), %zu bytes\n", seenCount, skipped, packed.size());
        return 0;
    }
    const std::string dir = argv[1];
    std::vector<PackEntry> entries;
    std::set<std::pair<int, int>> mappings;
    std::vector<text::CompactLayout> compactLayouts;
    unsigned compactVs = 0, compactPs = 0;
    unsigned skippedVs = 0, skippedPs = 0, vsCount = 0, psCount = 0;
    std::set<uint64_t> seen;

    // Optional: the surface-light pack (argv[3]). WMO vertex shaders it replaces change the engine's own
    // bytecode, and the layered-material module looks its shaders up by that current bytecode, so the
    // layered variant of each light-patched shader is added under the light-patched bytecode's hash.
    wxl::surfacelights::LightShaderPack lightPack;
    bool haveLightPack = false;
    if (argc >= 4) {
        std::string lightWhy;
        haveLightPack = lightPack.Load(argv[3], lightWhy);
        std::printf("surface-light pack %s: %s\n", argv[3], haveLightPack ? "loaded" : lightWhy.c_str());
    }
    unsigned lightVariants = 0, lightVariantsSkipped = 0;
    const auto addVertex = [&](const std::vector<uint8_t>& code, const std::string& name, bool lightVariant) {
        const uint64_t hash = wxl::wmo::HashBytecode(code.data(), code.size());
        if (!seen.insert(hash).second) return;
        ++vsCount;
        text::VsMapping map{};
        const std::string patched = text::BuildLayeredVsText(Disassemble(code), map);
        PackEntry e; e.kind = 0; e.originalHash = hash; e.originalSize = uint32_t(code.size());
        g_quiet = true;
        const bool suppliedFits = !patched.empty() && Assemble(patched, name.c_str(), e.bytecode);
        g_quiet = false;
        if (!suppliedFits) {
            // Too few output registers for the supplied layout: carry the extra floats in unused
            // components of the stock outputs instead (a compact layout, keyed 0x80 + id).
            text::CompactLayout layout;
            const std::string compactText = text::BuildCompactVsText(Disassemble(code), layout);
            e.bytecode.clear();
            g_quiet = lightVariant;
            const bool compactOk = !compactText.empty() && Assemble(compactText, name.c_str(), e.bytecode);
            g_quiet = false;
            if (!compactOk) {
                if (lightVariant) ++lightVariantsSkipped; else ++skippedVs;
                std::printf("skip vs %s%s: %s\n", name.c_str(), lightVariant ? " (light-patched)" : "", compactText.empty() ? "shape" : "assemble");
                return;
            }
            const std::string key = layout.Key();
            size_t id = 0;
            while (id < compactLayouts.size() && compactLayouts[id].Key() != key) ++id;
            if (id == compactLayouts.size()) compactLayouts.push_back(layout);
            e.uvUsage = e.weightUsage = uint8_t(0x80 + id);
            ++compactVs;
            if (lightVariant) ++lightVariants;
            entries.push_back(std::move(e));
            return;
        }
        e.uvUsage = uint8_t(map.uv[1]); e.weightUsage = uint8_t(map.weights);
        mappings.insert({map.uv[1], map.weights});
        if (lightVariant) ++lightVariants;
        entries.push_back(std::move(e));
    };
    for (const auto& name : List(dir, "vs")) {
        std::vector<uint8_t> code;
        if (!ReadFile(dir + "\\" + name, code) || code.size() < 8 || code.size() > wxl::wmo::kMaxShaderBytes) continue;
        addVertex(code, name, false);
        if (haveLightPack)
            if (const auto* lit = lightPack.Find(0, code.data(), code.size()); lit && lit->group == wxl::surfacelights::kGroupWmo) {
                std::vector<uint8_t> patchedCode(lit->bytecode.size() * 4);
                std::memcpy(patchedCode.data(), lit->bytecode.data(), patchedCode.size());
                addVertex(patchedCode, name, true);
            }
    }
    seen.clear();
    for (const auto& name : List(dir, "ps")) {
        std::vector<uint8_t> code;
        if (!ReadFile(dir + "\\" + name, code) || code.size() < 8 || code.size() > wxl::wmo::kMaxShaderBytes) continue;
        const uint64_t hash = wxl::wmo::HashBytecode(code.data(), code.size());
        if (!seen.insert(hash).second) continue;
        ++psCount;
        const std::string source = Disassemble(code);
        {   // two layers: independent of the vertex mapping
            PackEntry e; e.kind = 1; e.layers = 2; e.originalHash = hash; e.originalSize = uint32_t(code.size());
            std::array<unsigned, 8> slots{};
            const std::string patched = text::BuildTwoLayerPsText(source, text::VsMapping{}, &slots);
            if (!patched.empty() && Assemble(patched, name.c_str(), e.bytecode)) {
                for (int i = 0; i < 8; ++i) e.samplerSlots[i] = uint8_t(slots[i]);
                entries.push_back(std::move(e));
            } else { ++skippedPs; std::printf("skip ps %s two-layer: %s\n", name.c_str(), patched.empty() ? "shape" : "assemble"); }
        }
        for (const auto& [uv, weights] : mappings) {   // four layers: one per distinct vertex mapping
            PackEntry e; e.kind = 1; e.layers = 4; e.uvUsage = uint8_t(uv); e.weightUsage = uint8_t(weights);
            e.originalHash = hash; e.originalSize = uint32_t(code.size());
            text::VsMapping map{}; map.uv[0] = 0; map.uv[1] = uv; map.uv[2] = uv; map.weights = weights;
            std::array<unsigned, 8> slots{}; int tint = -1;
            const std::string patched = text::BuildLayeredPsText(source, map, tint, &slots);
            if (!patched.empty() && Assemble(patched, name.c_str(), e.bytecode)) {
                e.tintRegister = tint;
                for (int i = 0; i < 8; ++i) e.samplerSlots[i] = uint8_t(slots[i]);
                entries.push_back(std::move(e));
            } else { ++skippedPs; std::printf("skip ps %s four-layer map %d/%d: %s\n", name.c_str(), uv, weights, patched.empty() ? "shape" : "assemble"); }
        }
        for (size_t id = 0; id < compactLayouts.size(); ++id) {   // four layers on the compact vertex layouts
            PackEntry e; e.kind = 1; e.layers = 4; e.uvUsage = e.weightUsage = uint8_t(0x80 + id);
            e.originalHash = hash; e.originalSize = uint32_t(code.size());
            std::array<unsigned, 8> slots{}; int tint = -1;
            const std::string patched = text::BuildCompactLayeredPsText(source, compactLayouts[id], tint, &slots);
            if (!patched.empty() && Assemble(patched, name.c_str(), e.bytecode)) {
                e.tintRegister = tint;
                for (int i = 0; i < 8; ++i) e.samplerSlots[i] = uint8_t(slots[i]);
                entries.push_back(std::move(e)); ++compactPs;
            }
        }
    }
    const auto bytes = wxl::wmo::ShaderPack::Serialize(entries);
    FILE* out = std::fopen(argv[2], "wb");
    if (!out || std::fwrite(bytes.data(), 1, bytes.size(), out) != bytes.size()) { std::fprintf(stderr, "cannot write pack\n"); return 1; }
    std::fclose(out);
    if (haveLightPack) std::printf("light-patched WMO vertex shaders with a layered variant: %u (skipped %u)\n", lightVariants, lightVariantsSkipped);
    std::printf("vertex shaders %u (skipped %u, compact layouts %u in %zu mappings), pixel shaders %u (variants skipped %u, compact variants %u), mappings %zu, entries %zu, %zu bytes\n",
                vsCount, skippedVs, compactVs, compactLayouts.size(), psCount, skippedPs, compactPs, mappings.size(), entries.size(), bytes.size());
    return 0;
}
