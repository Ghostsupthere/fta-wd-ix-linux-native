// Adapted from wxl-modern-wmo e56fa7c9, Copyright (C) 2026 WarcraftXL, GPLv3.
#pragma once
#include <cctype>
#include <cstdio>
#include <string>
#include <array>

namespace wxl::modern::assets::wmo::shadertext
{
    struct DclInfo
    {
        std::string usage;   // "position", "normal", "color", "texcoord", "2d", ...
        int         index = 0;
        char        regClass = 0;  // 'v', 'o', 's'
        int         reg = -1;
    };

    /// Parses one "dcl_<usage><idx> <reg>[.mask]" line; false when the line is not a declaration.
    inline bool ParseDcl(const std::string& line, DclInfo& out)
    {
        size_t p = line.find_first_not_of(" \t");
        if (p == std::string::npos || line.compare(p, 4, "dcl_") != 0) return false;
        size_t u = p + 4;
        size_t ue = u;
        // Sampler declarations have a digit in their usage token (dcl_2d).
        if (line.compare(u, 2, "2d") == 0) ue += 2;
        while (ue < line.size() && (std::isalpha(static_cast<unsigned char>(line[ue])) )) ++ue;
        out.usage.assign(line, u, ue - u);
        out.index = 0;
        while (ue < line.size() && std::isdigit(static_cast<unsigned char>(line[ue])))
            out.index = out.index * 10 + (line[ue++] - '0');
        size_t r = line.find_first_not_of(" \t", ue);
        if (r == std::string::npos) return false;
        out.regClass = line[r];
        if (out.regClass != 'v' && out.regClass != 'o' && out.regClass != 's') return false;
        size_t re = r + 1;
        out.reg = 0;
        bool any = false;
        while (re < line.size() && std::isdigit(static_cast<unsigned char>(line[re])))
        {
            out.reg = out.reg * 10 + (line[re++] - '0');
            any = true;
        }
        return any;
    }

    /// Highest register number of one class ('r', 'v', 'c', 'o') used anywhere in the text.
    inline int MaxRegister(const std::string& text, char cls)
    {
        int maxReg = -1;
        for (size_t p = 0; (p = text.find(cls, p)) != std::string::npos; ++p)
        {
            if (p && (std::isalnum(static_cast<unsigned char>(text[p - 1])) || text[p - 1] == '_'))
                continue;
            size_t q = p + 1;
            int v = 0;
            bool any = false;
            while (q < text.size() && std::isdigit(static_cast<unsigned char>(text[q])))
            {
                v = v * 10 + (text[q++] - '0');
                any = true;
            }
            if (any && v > maxReg) maxReg = v;
        }
        return maxReg;
    }

    /// Byte offset just past the LAST declaration line (the canonical insertion point for new dcls).
    inline size_t AfterLastDcl(const std::string& text)
    {
        size_t last = std::string::npos;
        size_t pos = 0;
        while (pos < text.size())
        {
            size_t eol = text.find('\n', pos);
            if (eol == std::string::npos) eol = text.size();
            DclInfo d;
            if (ParseDcl(text.substr(pos, eol - pos), d))
                last = eol + 1;
            pos = eol + 1;
        }
        return last;
    }

    /// Finds the first `texld <dest>, <coord>, s<sampler>` line; all operands returned as written.
    struct TexldLine { std::string dest, coord; size_t at = std::string::npos, len = 0; };
    inline TexldLine FindTexld(const std::string& text, unsigned sampler)
    {
        TexldLine out;
        char sTok[8];
        std::snprintf(sTok, sizeof sTok, "s%u", sampler);
        auto trim = [](std::string s) {
            size_t a = s.find_first_not_of(" \t\r"), b = s.find_last_not_of(" \t\r");
            return a == std::string::npos ? std::string{} : s.substr(a, b - a + 1);
        };
        size_t pos = 0;
        while (pos < text.size())
        {
            size_t eol = text.find('\n', pos);
            if (eol == std::string::npos) eol = text.size();
            const std::string line = text.substr(pos, eol - pos);
            const size_t t = line.find("texld ");
            if (t != std::string::npos)
            {
                const size_t c1 = line.find(',', t);
                const size_t c2 = c1 == std::string::npos ? std::string::npos : line.find(',', c1 + 1);
                if (c1 != std::string::npos && c2 != std::string::npos && trim(line.substr(c2 + 1)) == sTok)
                {
                    out.dest  = trim(line.substr(t + 6, c1 - (t + 6)));
                    out.coord = trim(line.substr(c1 + 1, c2 - (c1 + 1)));
                    out.at    = pos;
                    out.len   = eol - pos;
                    return out;
                }
            }
            pos = eol + 1;
        }
        return out;
    }

    // ------------------------------------------------------------------ the vertex-shader patch
    /// Output usage indices chosen for the extra interpolants (shared VS -> PS contract per pair).
    struct VsMapping { int uv[3]; int weights; };

    // Pack UV B into the unused zw components of native TEXCOORD0. UV C/D share one
    // additional float4, weights use one more. This fits the high-shadow SM3 register budget.
    inline bool ExtendBaseUv(std::string& text, char regClass, int& reg)
    {
        size_t pos = 0;
        while (pos < text.size())
        {
            size_t end = text.find('\n', pos);
            if (end == std::string::npos) end = text.size();
            const std::string line = text.substr(pos, end - pos);
            DclInfo d;
            if (ParseDcl(line, d) && d.regClass == regClass && d.usage == "texcoord" && d.index == 0)
            {
                const std::string token = std::string(1, regClass) + std::to_string(d.reg);
                const size_t at = line.find(token, line.find_first_of(" \t", line.find("dcl_")));
                if (at == std::string::npos) return false;
                size_t tail = line.find_first_of(" \t\r", at);
                if (tail == std::string::npos) tail = line.size();
                const auto mask = line.substr(at + token.size(), tail - at - token.size());
                if (mask == ".xy") text.replace(pos + at + token.size(), 3, ".xyzw");
                else if (regClass != 'o' || !mask.empty()) return false;
                reg = d.reg;
                return true;
            }
            pos = end + 1;
        }
        return false;
    }

    inline bool UsageFree(const std::string& text, int usage)
    {
        size_t pos = 0;
        while (pos < text.size())
        {
            size_t end = text.find('\n', pos);
            if (end == std::string::npos) end = text.size();
            DclInfo d;
            if (ParseDcl(text.substr(pos, end - pos), d) && d.regClass == 'v' &&
                d.usage == "texcoord" && d.index == usage) return false;
            pos = end + 1;
        }
        return true;
    }
    inline bool SamplerSlots(const std::string& text, unsigned count, std::array<unsigned, 8>& slots)
    {
        bool used[16]{};
        size_t pos = 0;
        while (pos < text.size())
        {
            size_t end = text.find('\n', pos);
            if (end == std::string::npos) end = text.size();
            DclInfo d;
            if (ParseDcl(text.substr(pos, end - pos), d) && d.regClass == 's' && d.reg >= 0 && d.reg < 16)
                used[d.reg] = true;
            pos = end + 1;
        }
        slots[0] = 0;
        unsigned next = 1;
        for (unsigned slot = 1; slot < 16 && next < count; ++slot)
            if (!used[slot]) slots[next++] = slot;
        return next == count;
    }

    /**
     * @brief Patches a stock single-layer vs_3_0: adds inputs for UV sets B..D (stream-1 TEXCOORD1..3)
     *        and the weights (stream-1 COLOR1), plus pass-through outputs on free texcoord usages.
     *
     * The stock body -- position transform, lighting into the colour output, fog -- is untouched; the
     * texcoord0 output keeps feeding layer A's UV (identical values now sourced from stream 1). "" when
     * the shape does not match (unknown extra inputs, no free output registers, wrong profile).
     */
    inline std::string BuildLayeredVsText(const std::string& source, VsMapping& map)
    {
        std::string text = source;
        int baseOutput = -1;
        if (!ExtendBaseUv(text, 'o', baseOutput)) return {};
        if (text.find("vs_3_0") == std::string::npos) return {};

        bool usedTexUsage[16] = {};
        int maxIn = -1, maxOut = -1;
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
                    // Only the four inputs our vertex declaration feeds may appear; anything else means
                    // this is not the plain single-layer VS and the batch draws stock.
                    const bool ok = (d.usage == "position" && d.index == 0) ||
                                    (d.usage == "normal" && d.index == 0) ||
                                    (d.usage == "color" && d.index == 0) ||
                                    (d.usage == "texcoord" && d.index == 0);
                    if (!ok) return {};
                    if (d.reg > maxIn) maxIn = d.reg;
                }
                else if (d.regClass == 'o')
                {
                    if (d.usage == "texcoord" && d.index < 16) usedTexUsage[d.index] = true;
                    if (d.reg > maxOut) maxOut = d.reg;
                }
            }
            pos = eol + 1;
        }
        if (maxIn < 0 || maxIn + 4 > 15 || maxOut < 0 || maxOut + 2 > 11) return {};

        int chosen[2], n = 0;
        for (int i = 1; i < 15 && n < 2; ++i)
            if (!usedTexUsage[i]) chosen[n++] = i;
        if (n < 2) return {};
        map.uv[0] = 0; map.uv[1] = chosen[0]; map.uv[2] = chosen[0]; map.weights = chosen[1];

        const int vB = maxIn + 1, vC = maxIn + 2, vD = maxIn + 3, vW = maxIn + 4;
        const int oCD = maxOut + 1, oW = maxOut + 2;

        char buf[512];
        std::snprintf(buf, sizeof buf,
                      "dcl_texcoord1 v%d\n"
                      "dcl_texcoord2 v%d\n"
                      "dcl_texcoord3 v%d\n"
                      "dcl_color1 v%d\n"
                      "dcl_texcoord%d o%d\n"
                      "dcl_texcoord%d o%d\n",
                      vB, vC, vD, vW,
                      map.uv[1], oCD, map.weights, oW);

        const size_t ins = AfterLastDcl(text);
        if (ins == std::string::npos) return {};
        std::string out = text;
        out.insert(ins, buf);

        std::snprintf(buf, sizeof buf,
                      "\nmov o%d.zw, v%d.xyxy\n"
                      "mov o%d.xy, v%d\n"
                      "mov o%d.zw, v%d.xyxy\n"
                      "mov o%d, v%d\n",
                      baseOutput, vB, oCD, vC, oCD, vD, oW, vW);
        out += buf;
        return out;
    }

    // ------------------------------------------------------------------ the pixel-shader patch
    /**
     * @brief Patches a stock single-layer ps_3_0: the single diffuse fetch becomes the four-layer
     *        height blend + tint, writing the SAME destination register, so the stock combine
     *        (vertex colour, lighting, fog, alpha) runs unchanged on the blended texel.
     *
     * Sampler contract: s0..s3 = diffuse A..D, s4..s7 = height A..D (height in alpha). `tintReg`
     * receives the constant register the bind must fill with {tint.rgb, enable}. "" on shape mismatch.
     */
    inline std::string BuildLayeredPsText(const std::string& source, const VsMapping& map, int& tintReg,
        std::array<unsigned, 8>* samplerMap = nullptr)
    {
        std::string text = source;
        int baseInput = -1;
        if (!ExtendBaseUv(text, 'v', baseInput)) return {};
        if (text.find("ps_3_0") == std::string::npos) return {};
        for (int usage : {map.uv[1], map.weights})
            if (!UsageFree(text, usage)) return {};
        std::array<unsigned, 8> slots{};
        if (!SamplerSlots(text, 8, slots)) return {};

        const TexldLine diffuse = FindTexld(text, 0);
        if (diffuse.at == std::string::npos) return {};
        if (diffuse.coord != "v" + std::to_string(baseInput)) return {};
        if (diffuse.dest.find('.') != std::string::npos) return {}; // masked dest: unexpected shape

        const int maxIn = MaxRegister(text, 'v');
        const int maxTmp = MaxRegister(text, 'r');
        const int maxConst = MaxRegister(text, 'c');
        if (maxIn < 0 || maxIn + 2 > 9) return {};
        if (maxTmp + 8 > 31) return {};
        if (maxConst + 2 > 223) return {};

        const int vB = baseInput, vC = maxIn + 1, vD = maxIn + 1, vW = maxIn + 2;
        const int cL = maxConst + 1;   // literals: x = height floor, y = 1
        tintReg      = maxConst + 2;   // uploaded per batch: rgb = tint colour, w = enable
        const int b0 = maxTmp + 1, b1 = maxTmp + 2, b2 = maxTmp + 3, b3 = maxTmp + 4;
        const int bH = maxTmp + 5, bW = maxTmp + 6, bK = maxTmp + 7, bS = maxTmp + 8;

        std::string out = text;

        // The blend block, replacing the one stock diffuse fetch. Ordered exactly as the modern branch
        // computes it: heights -> weights -> peak-relative blend -> renormalise -> diffuse sum -> tint.
        char blk[2048];
        std::snprintf(blk, sizeof blk,
            "texld r%d, %s, s%u\n"
            "texld r%d, v%d.zwzw, s%u\n"
            "texld r%d, v%d, s%u\n"
            "texld r%d, v%d.zwzw, s%u\n"
            "mov r%d.x, r%d.w\n"
            "mov r%d.y, r%d.w\n"
            "mov r%d.z, r%d.w\n"
            "mov r%d.w, r%d.w\n"
            "max r%d, r%d, c%d.x\n"
            "mov r%d.xyz, v%d\n"
            "dp3_sat r%d.x, v%d, c%d.y\n"
            "add r%d.w, -r%d.x, c%d.y\n"
            "mul r%d, r%d, r%d\n"
            "max r%d.x, r%d.x, r%d.y\n"
            "max r%d.x, r%d.x, r%d.z\n"
            "max r%d.x, r%d.x, r%d.w\n"
            "mad_sat r%d, -r%d, r%d, r%d.x\n"
            "add r%d, -r%d, c%d.y\n"
            "mul r%d, r%d, r%d\n"
            "dp4 r%d.x, r%d, c%d.y\n"
            "rcp r%d.x, r%d.x\n"
            "mul r%d, r%d, r%d.x\n"
            "texld r%d, %s, s0\n"
            "texld r%d, v%d.zwzw, s%u\n"
            "texld r%d, v%d, s%u\n"
            "texld r%d, v%d.zwzw, s%u\n"
            "mul %s, r%d, r%d.x\n"
            "mad %s, r%d, r%d.y, %s\n"
            "mad %s, r%d, r%d.z, %s\n"
            "mad %s, r%d, r%d.w, %s\n"
            "mul r%d.x, v%d.w, c%d.w\n"
            "lrp r%d.xyz, r%d.x, c%d, %s\n"
            "mov %s.xyz, r%d",
            b0, diffuse.coord.c_str(), slots[4],
            b1, vB, slots[5],
            b2, vC, slots[6],
            b3, vD, slots[7],
            bH, b0,
            bH, b1,
            bH, b2,
            bH, b3,
            bH, bH, cL,
            bW, vW,
            bS, vW, cL,
            bW, bS, cL,
            bK, bW, bH,
            bS, bK, bK,
            bS, bS, bK,
            bS, bS, bK,
            bW, bW, bH, bS,
            bW, bW, cL,
            bW, bW, bK,
            bS, bW, cL,
            bS, bS,
            bW, bW, bS,
            b0, diffuse.coord.c_str(),
            b1, vB, slots[1],
            b2, vC, slots[2],
            b3, vD, slots[3],
            diffuse.dest.c_str(), b0, bW,
            diffuse.dest.c_str(), b1, bW, diffuse.dest.c_str(),
            diffuse.dest.c_str(), b2, bW, diffuse.dest.c_str(),
            diffuse.dest.c_str(), b3, bW, diffuse.dest.c_str(),
            bS, vW, tintReg,
            b0, bS, tintReg, diffuse.dest.c_str(),
            diffuse.dest.c_str(), b0);
        out.replace(diffuse.at, diffuse.len, blk);

        // New inputs (the VS pair's added outputs) and the seven extra samplers, after the last dcl;
        // the literal register in canonical position, right after the version line.
        char dcls[512];
        std::snprintf(dcls, sizeof dcls,
                      "dcl_texcoord%d v%d\n"
                      "dcl_texcoord%d v%d\n"
                      "dcl_2d s%u\ndcl_2d s%u\ndcl_2d s%u\ndcl_2d s%u\ndcl_2d s%u\ndcl_2d s%u\ndcl_2d s%u\n",
                      map.uv[1], vC, map.weights, vW,
                      slots[1], slots[2], slots[3], slots[4], slots[5], slots[6], slots[7]);
        const size_t ins = AfterLastDcl(out);
        if (ins == std::string::npos) return {};
        out.insert(ins, dcls);

        const size_t ver = out.find("ps_3_0");
        size_t verEol = out.find('\n', ver);
        if (verEol == std::string::npos) return {};
        char defLine[64];
        std::snprintf(defLine, sizeof defLine, "def c%d, 0.004, 1, 0, 0\n", cL);
        out.insert(verEol + 1, defLine);
        if (samplerMap) *samplerMap = slots;
        return out;
    }

    // Two diffuse layers use the overlay texture's alpha, following upstream's Composite fix.
    // Reuses the same VS side-stream layout while preserving the native PS lighting/fog body.
    inline std::string BuildTwoLayerPsText(const std::string& source, const VsMapping&,
        std::array<unsigned, 8>* samplerMap = nullptr)
    {
        std::string text = source;
        int v = -1;
        if (!ExtendBaseUv(text, 'v', v)) return {};
        std::array<unsigned, 8> slots{};
        if (text.find("ps_3_0") == std::string::npos || !SamplerSlots(text, 2, slots))
            return {};
        const auto fetch = FindTexld(text, 0);
        const int r = MaxRegister(text, 'r') + 1;
        if (fetch.coord != "v" + std::to_string(v)) return {};
        if (fetch.at == std::string::npos || fetch.dest.find('.') != std::string::npos || v > 9 || r + 1 > 31)
            return {};
        char block[512];
        std::snprintf(block, sizeof block, "texld %s, %s, s0\ntexld r%d, v%d.zwzw, s%u\nlrp r%d, r%d.w, r%d, %s\nmov %s, r%d",
            fetch.dest.c_str(), fetch.coord.c_str(), r, v, slots[1], r + 1, r, r, fetch.dest.c_str(), fetch.dest.c_str(), r + 1);
        std::string result = text;
        result.replace(fetch.at, fetch.len, block);
        const size_t at = AfterLastDcl(result);
        if (at == std::string::npos) return {};
        std::snprintf(block, sizeof block, "dcl_2d s%u\n", slots[1]);
        result.insert(at, block);
        if (samplerMap) *samplerMap = slots;
        return result;
    }
}
