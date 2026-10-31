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
#include "Engine/Source/ModernAssets/WxlGrass/GrassShaderText.hpp"
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
        std::fprintf(stderr, "%s: assemble failed: %s\n", label, errors ? static_cast<const char*>(errors->GetBufferPointer()) : "?");
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
    if (std::strcmp(argv[1], "--grass") == 0 && argc >= 4) {   // grass-motion vertex shaders (*.vs)
        const std::string grassDir = argv[2];
        std::vector<PackEntry> grass; unsigned seenCount = 0, skipped = 0;
        for (const auto& name : List(grassDir, "vs")) {
            std::vector<uint8_t> code;
            if (!ReadFile(grassDir + "\\" + name, code) || code.size() < 8 || code.size() > wxl::wmo::kMaxShaderBytes) continue;
            ++seenCount;
            PackEntry e; e.kind = 0; e.originalHash = wxl::wmo::HashBytecode(code.data(), code.size()); e.originalSize = uint32_t(code.size());
            const std::string patched = wxl::grass::text::BuildGrassMotionText(Disassemble(code));
            if (patched.empty() || !Assemble(patched, name.c_str(), e.bytecode)) { ++skipped; std::printf("skip grass %s\n", name.c_str()); continue; }
            grass.push_back(std::move(e));
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
    unsigned skippedVs = 0, skippedPs = 0, vsCount = 0, psCount = 0;
    std::set<uint64_t> seen;

    for (const auto& name : List(dir, "vs")) {
        std::vector<uint8_t> code;
        if (!ReadFile(dir + "\\" + name, code) || code.size() < 8 || code.size() > wxl::wmo::kMaxShaderBytes) continue;
        const uint64_t hash = wxl::wmo::HashBytecode(code.data(), code.size());
        if (!seen.insert(hash).second) continue;
        ++vsCount;
        text::VsMapping map{};
        const std::string patched = text::BuildLayeredVsText(Disassemble(code), map);
        PackEntry e; e.kind = 0; e.originalHash = hash; e.originalSize = uint32_t(code.size());
        if (patched.empty() || !Assemble(patched, name.c_str(), e.bytecode)) { ++skippedVs; std::printf("skip vs %s: %s\n", name.c_str(), patched.empty() ? "shape" : "assemble"); continue; }
        e.uvUsage = uint8_t(map.uv[1]); e.weightUsage = uint8_t(map.weights);
        mappings.insert({map.uv[1], map.weights});
        entries.push_back(std::move(e));
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
    }
    const auto bytes = wxl::wmo::ShaderPack::Serialize(entries);
    FILE* out = std::fopen(argv[2], "wb");
    if (!out || std::fwrite(bytes.data(), 1, bytes.size(), out) != bytes.size()) { std::fprintf(stderr, "cannot write pack\n"); return 1; }
    std::fclose(out);
    std::printf("vertex shaders %u (skipped %u), pixel shaders %u (variants skipped %u), mappings %zu, entries %zu, %zu bytes\n",
                vsCount, skippedVs, psCount, skippedPs, mappings.size(), entries.size(), bytes.size());
    return 0;
}
