// Hardware-instanced ground-effect (grass) drawing: the plant transform and the vertex-shader
// derivation. Adapted from the supplied wxl-living-azeroth `features/GrassInstanced.cpp`
// (Morfium's Living Azeroth, GPL-3.0-or-later; see UPSTREAM.json in the supplied tree). The
// Windows device detours, the fixed-address memory reads and the runtime D3DAssemble are not
// imported: the native client reads the engine's own types and the derived shaders are
// assembled offline (tools/wxl_wmo_shader_pack.cpp) into Utils/WarcraftXL/visuals/
// grass_instanced.pack.
//
// One plant is drawn from the model's static vertices (position, uv; stream 0) and a 64-byte
// instance record (stream 1): the model-to-world transform as three rows with the scale folded
// in and the translation in w, then the normal and colour the engine's own bake writes into every
// vertex of the plant. The derived vertex shader computes the baked position from them and then
// runs the engine's code unchanged.
#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace wxl::grass::instancing
{
    struct GpuVertex { float pos[3]; float uv[2]; };
    static_assert(sizeof(GpuVertex) == 20, "GpuVertex");

    // Rows of p' = (p * M) * scale + translation, written so that p'.i = dot(row_i, (p, 1)).
    struct GpuInstance { float x[4], y[4], z[4]; float normal[3]; uint32_t color; };
    static_assert(sizeof(GpuInstance) == 64, "GpuInstance");

    // The images of the model's x, y and z axes under the plant's rotation: p * M = p.x*image[0] +
    // p.y*image[1] + p.z*image[2], the same row-vector convention as the engine's C3Vector * C33Matrix.
    struct AxisImages { float image[3][3]; };

    /// Rotation about +Z by `rotation` radians: the engine's non-tilting plant.
    inline AxisImages YawImages(float rotation)
    {
        const float c = std::cos(rotation), s = std::sin(rotation);
        return {{{c, s, 0.0f}, {-s, c, 0.0f}, {0.0f, 0.0f, 1.0f}}};
    }

    inline void SetPlantTransform(const AxisImages& images, float scale, const float position[3], GpuInstance& out)
    {
        for (int row = 0; row < 3; ++row)
        {
            float* target = row == 0 ? out.x : row == 1 ? out.y : out.z;
            for (int axis = 0; axis < 3; ++axis) target[axis] = images.image[axis][row] * scale;
            target[3] = position[row];
        }
    }

    /// Reference evaluation of an instance record on a model vertex (the vertex shader's job).
    inline void Transform(const GpuInstance& g, const float p[3], float out[3])
    {
        out[0] = g.x[0] * p[0] + g.x[1] * p[1] + g.x[2] * p[2] + g.x[3];
        out[1] = g.y[0] * p[0] + g.y[1] * p[1] + g.y[2] * p[2] + g.y[3];
        out[2] = g.z[0] * p[0] + g.z[1] * p[1] + g.z[2] * p[2] + g.z[3];
    }

    // --- the instanced vertex shader -----------------------------------------------------------
    // The grass shader reads its position only as v0 (the baked world-space position). The derived
    // shader computes that from the model vertex (stream 0, still v0) and the plant's rows (v4..v6),
    // then runs the engine's code unchanged on r31. Normal (v1) and colour (v2) come from stream 1,
    // the uv (v3) from stream 0.
    inline constexpr const char* kInstancedInputs =
        "    dcl_texcoord1 v4\n"
        "    dcl_texcoord2 v5\n"
        "    dcl_texcoord3 v6\n"
        "    mov r30, v0\n"   // shader model 3 permits only one input register per instruction
        "    dp4 r31.x, r30, v4\n"
        "    dp4 r31.y, r30, v5\n"
        "    dp4 r31.z, r30, v6\n"
        "    mov r31.w, v0.w\n";

    namespace detail
    {
        inline bool IsTokenChar(char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }

        inline std::string ReplaceV0(const std::string& line)
        {
            std::string out;
            for (size_t i = 0; i < line.size();)
            {
                const bool match = line.compare(i, 2, "v0") == 0 && (i == 0 || !IsTokenChar(line[i - 1])) &&
                                   (i + 2 >= line.size() || !(line[i + 2] >= '0' && line[i + 2] <= '9'));
                if (match) { out += "r31"; i += 2; }
                else out += line[i++];
            }
            return out;
        }

        inline std::string Trim(const std::string& s)
        {
            const size_t a = s.find_first_not_of(" \t\r");
            if (a == std::string::npos) return std::string();
            return s.substr(a, s.find_last_not_of(" \t\r") - a + 1);
        }
    }

    /// Derives the instanced version of a grass vertex shader from its disassembly (stock or with the
    /// motion patch). False, with the reason in `error`, when it is not the plain grass vertex layout.
    inline bool MakeInstancedGrassText(const std::string& source, std::string& out, std::string& error)
    {
        if (source.find("r30") != std::string::npos || source.find("r31") != std::string::npos)
        { error = "the shader already uses r30 or r31"; return false; }

        std::vector<std::string> lines;
        for (size_t at = 0; at <= source.size();)
        {
            size_t end = source.find('\n', at);
            if (end == std::string::npos) end = source.size();
            lines.push_back(source.substr(at, end - at));
            at = end + 1;
        }

        size_t lastDcl = std::string::npos;
        bool position = false, normal = false, colour = false, texcoord = false;
        for (size_t i = 0; i < lines.size(); ++i)
        {
            const std::string t = detail::Trim(lines[i]);
            if (t.compare(0, 4, "dcl_") != 0) continue;
            lastDcl = i;
            if (t.find(" v") == std::string::npos) continue;   // an output or a sampler
            if (t == "dcl_position v0") position = true;
            else if (t == "dcl_normal v1") normal = true;
            else if (t == "dcl_color v2") colour = true;
            else if (t == "dcl_texcoord v3") texcoord = true;
            else { error = "unexpected input: " + t; return false; }
        }
        if (!(position && normal && colour && texcoord) || lastDcl == std::string::npos)
        { error = "not the grass vertex layout"; return false; }

        out.clear();
        for (size_t i = 0; i < lines.size(); ++i)
        {
            out += i > lastDcl ? detail::ReplaceV0(lines[i]) : lines[i];
            out += '\n';
            if (i == lastDcl) out += kInstancedInputs;
        }
        return true;
    }
}
