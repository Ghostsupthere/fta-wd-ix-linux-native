// Native extension of the supplied WMO shader text transforms (models/wmo/WmoShaderText.hpp,
// which stays byte-identical to the import).
//
// The supplied vertex patch adds two output registers (UV C/D and the weights) after the
// stock outputs. The highest shadow tiers of the reflective and specular vertex shaders already
// use ten of the eleven output registers the assembler allows without a point-size output, so
// the supplied patch cannot be assembled for them and those batches draw stock. This builder
// reuses the unused components of the stock texcoord outputs (for example the zw of a ".xy"
// output or the w of a ".xyz" output) and adds one register for what is left, which fits.
// Pixel shaders reading the layout assemble the same eight floats back into the registers the
// supplied blend code expects, so the blend itself is the supplied code unchanged.
//
// Offline only: used by tools/wxl_wmo_shader_pack.cpp under the pinned d3dcompiler.
#pragma once
#include "models/wmo/WmoShaderText.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace wxl::modern::assets::wmo::shadertext
{
    /// Where each of the eight extra floats travels from the vertex to the pixel shader:
    /// UV C (x, y), UV D (x, y), weights (x, y, z, w), as {texcoord usage, component 0..3}.
    struct CompactLayout
    {
        struct Slot { int usage = -1; int comp = 0; };
        std::array<Slot, 8> slots;

        std::string Key() const
        {
            std::string key;
            for (const Slot& s : slots) key += std::to_string(s.usage) + ":" + std::to_string(s.comp) + ",";
            return key;
        }
    };

    namespace compact_detail
    {
        struct OutputDcl
        {
            int usage = 0;        // texcoord index (only texcoord outputs are recorded)
            int reg = 0;
            int width = 0;        // 1..4 components declared (prefix masks only)
            size_t maskAt = 0;    // offset in the text of the mask (or where one would go)
            size_t maskLength = 0;
            bool usable = false;  // plain prefix mask
        };

        inline int PrefixWidth(const std::string& mask)
        {
            if (mask.empty() || mask == ".xyzw") return 4;
            if (mask == ".x") return 1;
            if (mask == ".xy") return 2;
            if (mask == ".xyz") return 3;
            return 0;
        }

        inline const char* MaskText(int width)
        {
            static const char* const kMasks[] = {"", ".x", ".xy", ".xyz", ""};
            return kMasks[width];
        }

        /// The declaration lines of one register class, in text order, with mask positions.
        inline std::vector<OutputDcl> TexcoordDcls(const std::string& text, char regClass)
        {
            std::vector<OutputDcl> out;
            size_t pos = 0;
            while (pos < text.size())
            {
                size_t end = text.find('\n', pos);
                if (end == std::string::npos) end = text.size();
                const std::string line = text.substr(pos, end - pos);
                DclInfo d;
                if (ParseDcl(line, d) && d.regClass == regClass && d.usage == "texcoord")
                {
                    OutputDcl o;
                    o.usage = d.index;
                    o.reg = d.reg;
                    const std::string token = std::string(1, regClass) + std::to_string(d.reg);
                    const size_t at = line.find(token, line.find_first_of(" \t", line.find("dcl_")));
                    if (at != std::string::npos)
                    {
                        size_t tail = line.find_first_of(" \t\r", at);
                        if (tail == std::string::npos) tail = line.size();
                        o.maskAt = pos + at + token.size();
                        o.maskLength = tail - at - token.size();
                        o.width = PrefixWidth(line.substr(at + token.size(), o.maskLength));
                        o.usable = o.width > 0;
                    }
                    out.push_back(o);
                }
                pos = end + 1;
            }
            return out;
        }

        inline int Highest(const std::vector<int>& values, int fallback)
        {
            int highest = fallback;
            for (int v : values) highest = std::max(highest, v);
            return highest;
        }

        /// The COLOR1 output (the specular colour) is only consumed by pixel shaders that declare COLOR1, and
        /// BuildCompactLayeredPsText refuses those, so a vertex layout that takes the register over can
        /// only ever be paired with pixel shaders that never read it; any other pairing has no patched pixel
        /// shader and the batch draws stock. Rewrites the declaration to a free texcoord usage and returns
        /// {register, usage}, or {-1, -1}.
        inline std::pair<int, int> ReclaimColour1(std::string& text, const std::vector<int>& usedUsage)
        {
            size_t pos = 0;
            while (pos < text.size())
            {
                size_t eol = text.find('\n', pos);
                if (eol == std::string::npos) eol = text.size();
                DclInfo d;
                const std::string line = text.substr(pos, eol - pos);
                if (ParseDcl(line, d) && d.regClass == 'o' && d.usage == "color" && d.index == 1)
                {
                    int usage = -1;
                    for (int u = 1; u < 15 && usage < 0; ++u)
                        if (std::find(usedUsage.begin(), usedUsage.end(), u) == usedUsage.end()) usage = u;
                    if (usage < 0) return {-1, -1};
                    text.replace(pos + line.find("color1"), 6, "texcoord" + std::to_string(usage));
                    return {d.reg, usage};
                }
                pos = eol + 1;
            }
            return {-1, -1};
        }

        /// Rewrites the mask of the declaration of `usage` in `text` to a prefix of `width`.
        /// Returns false when no such declaration exists.
        inline bool WidenDcl(std::string& text, char regClass, int usage, int width)
        {
            for (const OutputDcl& o : TexcoordDcls(text, regClass))
                if (o.usage == usage)
                {
                    if (!o.usable) return false;
                    if (width > o.width) text.replace(o.maskAt, o.maskLength, MaskText(width));
                    return true;
                }
            return false;
        }
    }

    /// Vertex patch for a stock vs_3_0 whose outputs leave too few registers for BuildLayeredVsText.
    /// Writes the layout the matching pixel shaders need. "" when the shape does not match.
    inline std::string BuildCompactVsText(const std::string& source, CompactLayout& layout)
    {
        using namespace compact_detail;
        std::string text = source;
        int baseOutput = -1;
        if (!ExtendBaseUv(text, 'o', baseOutput)) return {};
        if (text.find("vs_3_0") == std::string::npos) return {};

        int maxIn = -1, maxOut = -1;
        std::vector<int> usedUsage;
        size_t pos = 0;
        while (pos < text.size())
        {
            size_t eol = text.find('\n', pos);
            if (eol == std::string::npos) eol = text.size();
            DclInfo d;
            if (ParseDcl(text.substr(pos, eol - pos), d))
            {
                if (d.regClass == 'v')
                {
                    const bool ok = (d.usage == "position" && d.index == 0) || (d.usage == "normal" && d.index == 0) ||
                                    (d.usage == "color" && d.index == 0) || (d.usage == "texcoord" && d.index == 0);
                    if (!ok) return {};
                    maxIn = std::max(maxIn, d.reg);
                }
                else if (d.regClass == 'o')
                {
                    if (d.usage == "texcoord") usedUsage.push_back(d.index);
                    maxOut = std::max(maxOut, d.reg);
                }
            }
            pos = eol + 1;
        }
        if (maxIn < 0 || maxIn + 4 > 15 || maxOut < 0) return {};

        // Registers worth reclaiming when the free components are not enough.
        std::string out;
        std::pair<int, int> reclaimed{-1, -1};
        int reclaimedUsage = -1, reclaimedReg = -1;
        // Unused components of the stock texcoord outputs, in register order.
        struct Free { int usage; int reg; int comp; };
        std::vector<Free> free;
        auto dcls = TexcoordDcls(text, 'o');
        std::sort(dcls.begin(), dcls.end(), [](const OutputDcl& a, const OutputDcl& b) { return a.reg < b.reg; });
        for (const OutputDcl& o : dcls)
            if (o.usable)
                for (int c = o.width; c < 4; ++c) free.push_back({o.usage, o.reg, c});

        if (free.size() < 8)
        {
            // Too few spare components: take over the COLOR1 output.
            reclaimed = ReclaimColour1(text, usedUsage);
            if (reclaimed.first >= 0)
            {
                reclaimedReg = reclaimed.first;
                reclaimedUsage = reclaimed.second;
                usedUsage.push_back(reclaimedUsage);
                for (int c = 0; c < 4; ++c) free.push_back({reclaimedUsage, reclaimedReg, c});
            }
        }

        struct Placement { int usage; int reg; int comp; };
        std::array<Placement, 8> placed;
        size_t taken = 0;
        for (; taken < 8 && taken < free.size(); ++taken) placed[taken] = {free[taken].usage, free[taken].reg, free[taken].comp};
        int newUsage = -1, newReg = -1;
        const int extra = int(8 - taken);
        if (extra > 0)
        {
            for (int u = 1; u < 15 && newUsage < 0; ++u)
                if (std::find(usedUsage.begin(), usedUsage.end(), u) == usedUsage.end()) newUsage = u;
            newReg = maxOut + 1;
            if (newUsage < 0 || extra > 4 || newReg > 10) return {};
            for (int i = 0; i < extra; ++i) placed[taken + i] = {newUsage, newReg, i};
        }

        // Widen the stock declarations that gained components; declare the new register.
        out = text;
        std::vector<std::pair<int, int>> widths;   // usage -> new width
        for (const Placement& p : placed)
        {
            if (p.usage == newUsage || p.usage == reclaimedUsage) continue;
            bool seen = false;
            for (auto& w : widths)
                if (w.first == p.usage) { w.second = std::max(w.second, p.comp + 1); seen = true; }
            if (!seen) widths.push_back({p.usage, p.comp + 1});
        }
        for (const auto& w : widths)
            if (!WidenDcl(out, 'o', w.first, w.second)) return {};

        const int vB = maxIn + 1, vC = maxIn + 2, vD = maxIn + 3, vW = maxIn + 4;
        char buf[256];
        std::snprintf(buf, sizeof buf, "dcl_texcoord1 v%d\ndcl_texcoord2 v%d\ndcl_texcoord3 v%d\ndcl_color1 v%d\n", vB, vC, vD, vW);
        std::string declarations = buf;
        if (newUsage >= 0)
        {
            std::snprintf(buf, sizeof buf, "dcl_texcoord%d o%d%s\n", newUsage, newReg, MaskText(extra));
            declarations += buf;
        }
        const size_t ins = AfterLastDcl(out);
        if (ins == std::string::npos) return {};
        out.insert(ins, declarations);

        static const char kComp[] = "xyzw";
        std::string body;
        std::snprintf(buf, sizeof buf, "\nmov o%d.zw, v%d.xyxy\n", baseOutput, vB);
        body += buf;
        const int sources[8] = {vC, vC, vD, vD, vW, vW, vW, vW};
        const int sourceComp[8] = {0, 1, 0, 1, 0, 1, 2, 3};
        for (int i = 0; i < 8; ++i)
        {
            std::snprintf(buf, sizeof buf, "mov o%d.%c, v%d.%c\n", placed[i].reg, kComp[placed[i].comp], sources[i], kComp[sourceComp[i]]);
            body += buf;
        }
        out += body;

        for (int i = 0; i < 8; ++i) layout.slots[i] = {placed[i].usage, placed[i].comp};
        return out;
    }

    /// Four-layer pixel patch for a vertex shader built by BuildCompactVsText. The supplied blend
    /// block runs unchanged; two temporaries stand in for the (C,D) and weight interpolants.
    inline std::string BuildCompactLayeredPsText(const std::string& source, const CompactLayout& layout, int& tintReg,
                                                 std::array<unsigned, 8>* samplerMap = nullptr)
    {
        using namespace compact_detail;
        // The supplied builder requires two free texcoord usages; use ones no vertex shader writes.
        if (source.find("dcl_color1") != std::string::npos) return {};   // the vertex layout may have reclaimed it
        VsMapping fake{};
        fake.uv[0] = 0; fake.uv[1] = 14; fake.uv[2] = 14; fake.weights = 15;
        std::string text = BuildLayeredPsText(source, fake, tintReg, samplerMap);
        if (text.empty()) return {};

        const int stockMaxIn = MaxRegister(source, 'v');
        const int vC = stockMaxIn + 1, vW = stockMaxIn + 2;
        const int maxTmp = MaxRegister(text, 'r');
        const int tCD = maxTmp + 1, tW = maxTmp + 2;
        if (maxTmp + 2 > 31) return {};

        // Drop the supplied builder's two dedicated input declarations.
        for (int usage : {14, 15})
        {
            const size_t at = text.find("dcl_texcoord" + std::to_string(usage) + " v");
            if (at == std::string::npos) return {};
            const size_t lineStart = text.rfind('\n', at);
            const size_t begin = lineStart == std::string::npos ? 0 : lineStart + 1;
            const size_t lineEnd = text.find('\n', at);
            text.erase(begin, (lineEnd == std::string::npos ? text.size() : lineEnd + 1) - begin);
        }

        // The blend block reads v<vC> (xy = C, zw = D) and v<vW> (weights); point it at temporaries.
        const auto rename = [&](int from, int to) {
            const std::string token = "v" + std::to_string(from);
            for (size_t at = 0; (at = text.find(token, at)) != std::string::npos;)
            {
                const size_t after = at + token.size();
                const bool before = at > 0 && (std::isalnum(static_cast<unsigned char>(text[at - 1])) || text[at - 1] == '_');
                const bool digit = after < text.size() && std::isdigit(static_cast<unsigned char>(text[after]));
                if (before || digit) { at = after; continue; }
                text.replace(at, token.size(), "r" + std::to_string(to));
                at += 1;
            }
        };
        rename(vC, tCD);
        rename(vW, tW);

        // Make sure the pixel shader receives the components the layout carries.
        std::vector<std::pair<int, int>> need;   // usage -> width
        for (const CompactLayout::Slot& s : layout.slots)
        {
            if (s.usage < 0) return {};
            bool seen = false;
            for (auto& n : need)
                if (n.first == s.usage) { n.second = std::max(n.second, s.comp + 1); seen = true; }
            if (!seen) need.push_back({s.usage, s.comp + 1});
        }
        int nextIn = std::max(MaxRegister(text, 'v'), stockMaxIn) + 1;
        std::map<int, int> inputReg;   // usage -> pixel input register
        std::string added;
        char buf[128];
        for (const auto& n : need)
        {
            bool declared = false;
            for (const OutputDcl& d : TexcoordDcls(text, 'v'))
                if (d.usage == n.first) { inputReg[n.first] = d.reg; declared = true; }
            if (declared)
            {
                if (!WidenDcl(text, 'v', n.first, n.second)) return {};
                continue;
            }
            if (nextIn > 9) return {};
            inputReg[n.first] = nextIn;
            std::snprintf(buf, sizeof buf, "dcl_texcoord%d v%d%s\n", n.first, nextIn, MaskText(n.second));
            added += buf;
            ++nextIn;
        }
        static const char kComp[] = "xyzw";
        const int dest[8] = {tCD, tCD, tCD, tCD, tW, tW, tW, tW};
        const char destComp[8] = {'x', 'y', 'z', 'w', 'x', 'y', 'z', 'w'};
        std::string prologue;
        for (int i = 0; i < 8; ++i)
        {
            std::snprintf(buf, sizeof buf, "mov r%d.%c, v%d.%c\n", dest[i], destComp[i], inputReg[layout.slots[i].usage], kComp[layout.slots[i].comp]);
            prologue += buf;
        }
        const size_t ins = AfterLastDcl(text);
        if (ins == std::string::npos) return {};
        text.insert(ins, added + prologue);
        return text;
    }
}
