// Formats: wow-retail-backport/references/rules.md 6a; physread.py; wowdev.wiki/PHYS.
// Older layout defaults follow the upgrader contract; authored fields are never re-tuned.
#include "PhysicsData.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <map>
#include <stdexcept>
#include <tuple>

namespace wxl::scripts::itemphysics
{
    namespace
    {
        using Bytes = std::span<uint8_t const>;
        using Chunks = std::map<std::string, Bytes>;

        template<class T> T Read(Bytes bytes, size_t offset)
        {
            if (offset > bytes.size() || sizeof(T) > bytes.size() - offset)
                throw std::runtime_error("truncated PHYS record");
            T value;
            std::memcpy(&value, bytes.data() + offset, sizeof(value));
            return value;
        }

        float Float(Bytes bytes, size_t offset)
        {
            float const value = Read<float>(bytes, offset);
            if (!std::isfinite(value))
                throw std::runtime_error("non-finite PHYS field");
            return value;
        }

        domino::V3 Vec(Bytes bytes, size_t offset)
        {
            return {Float(bytes, offset), Float(bytes, offset + 4), Float(bytes, offset + 8)};
        }

        Bytes Records(Chunks const& chunks, std::string const& name, size_t stride)
        {
            auto const found = chunks.find(name);
            if (found == chunks.end())
                return {};
            if (!stride || found->second.size() % stride)
                throw std::runtime_error("invalid " + name + " stride");
            return found->second;
        }

        void Require(bool value, char const* error)
        {
            if (!value)
                throw std::runtime_error(error);
        }

        Chunks Split(Bytes bytes)
        {
            Chunks chunks;
            Require(bytes.size() <= 8 * 1024 * 1024, "PHYS payload exceeds 8 MiB");
            for (size_t at = 0; at < bytes.size();)
            {
                Require(bytes.size() - at >= 8, "truncated PHYS chunk header");
                std::string tag(reinterpret_cast<char const*>(bytes.data() + at), 4);
                std::reverse(tag.begin(), tag.end());
                auto const size = Read<uint32_t>(bytes, at + 4);
                Require(size <= bytes.size() - at - 8, "PHYS chunk exceeds payload");
                Require(chunks.emplace(tag, bytes.subspan(at + 8, size)).second, "duplicate PHYS chunk");
                at += 8 + size;
            }
            return chunks;
        }

        void Hulls(Chunks const& chunks, domino::Model& model)
        {
            auto const found = chunks.find("PLYT");
            if (found == chunks.end())
                return;
            Bytes const data = found->second;
            uint32_t const count = Read<uint32_t>(data, 0);
            Require(count <= 255 && 4 + size_t(count) * 0x50 <= data.size(), "invalid PLYT header");
            size_t at = 4 + size_t(count) * 0x50;
            for (size_t i = 0; i < count; ++i)
            {
                size_t const head = 4 + i * 0x50;
                uint32_t const nv = Read<uint32_t>(data, head);
                uint32_t const nf = Read<uint32_t>(data, head + 0x10);
                uint32_t const ne = Read<uint32_t>(data, head + 0x28);
                Require(nv >= 4 && nv <= 255 && nf >= 4 && nf <= 255 && ne <= 255,
                    "invalid PLYT topology count");
                domino::HullDef hull;
                hull.centroid = Vec(data, head + 0x38);
                hull.volume = Float(data, head + 0x44);
                hull.area = Float(data, head + 0x48);
                Require(hull.volume > 0 && hull.area > 0, "invalid PLYT mass properties");
                for (size_t v = 0; v < nv; ++v, at += 12)
                    hull.v.push_back(Vec(data, at));
                for (size_t f = 0; f < nf; ++f, at += 16)
                {
                    hull.n.push_back(Vec(data, at));
                    hull.d.push_back(Float(data, at + 12));
                }
                Require(at <= data.size() && nf + ne * 4 <= data.size() - at, "truncated PLYT edges");
                hull.face.assign(data.begin() + at, data.begin() + at + nf);
                at += nf;
                hull.edge.assign(data.begin() + at, data.begin() + at + ne * 4);
                at += ne * 4;
                for (uint8_t edge : hull.face)
                    Require(edge < ne, "invalid PLYT face edge");
                for (int e = 0; e < int(ne); ++e)
                {
                    int const twin = e + int8_t(hull.edge[e * 4]);
                    Require(twin >= 0 && twin < int(ne) && hull.edge[e * 4 + 1] < nv &&
                        hull.edge[e * 4 + 2] < nf && hull.edge[e * 4 + 3] < ne, "invalid PLYT half edge");
                }
                model.hulls.push_back(std::move(hull));
            }
            Require(at == data.size(), "unexpected PLYT trailing bytes");
        }

        domino::V3 BoxTransform(Bytes record, domino::V3 point, bool translation)
        {
            return {
                Float(record, 0) * point.x + Float(record, 12) * point.y + Float(record, 24) * point.z +
                    (translation ? Float(record, 36) : 0),
                Float(record, 4) * point.x + Float(record, 16) * point.y + Float(record, 28) * point.z +
                    (translation ? Float(record, 40) : 0),
                Float(record, 8) * point.x + Float(record, 20) * point.y + Float(record, 32) * point.z +
                    (translation ? Float(record, 44) : 0)};
        }

        domino::HullDef Box(Bytes record)
        {
            domino::V3 const half = Vec(record, 48);
            Require(half.x > 0 && half.y > 0 && half.z > 0, "invalid BOXS half extents");
            domino::HullDef hull;
            hull.centroid = Vec(record, 36);
            hull.volume = 8 * half.x * half.y * half.z;
            hull.area = 8 * (half.x * half.y + half.x * half.z + half.y * half.z);
            for (int i = 0; i < 8; ++i)
                hull.v.push_back(BoxTransform(record,
                    {(i & 1) ? half.x : -half.x, (i & 2) ? half.y : -half.y, (i & 4) ? half.z : -half.z}, true));
            constexpr uint8_t faces[6][4] = {{1,3,7,5}, {0,4,6,2}, {2,6,7,3},
                                           {0,1,5,4}, {4,5,7,6}, {0,2,3,1}};
            constexpr domino::V3 normals[6] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
            for (uint8_t f = 0; f < 6; ++f)
            {
                auto const n = BoxTransform(record, normals[f], false);
                auto const p = hull.v[faces[f][0]];
                hull.n.push_back(n);
                hull.d.push_back(n.x * p.x + n.y * p.y + n.z * p.z);
                hull.face.push_back(f * 4);
                for (uint8_t e = 0; e < 4; ++e)
                {
                    int twin = -1;
                    for (int other = 0; other < 24; ++other)
                        if (faces[other / 4][other % 4] == faces[f][(e + 1) % 4] &&
                            faces[other / 4][(other + 1) % 4] == faces[f][e])
                            twin = other;
                    Require(twin >= 0, "BOXS topology error");
                    hull.edge.insert(hull.edge.end(), {uint8_t(int8_t(twin - (f * 4 + e))), faces[f][e],
                        f, uint8_t(f * 4 + (e + 1) % 4)});
                }
            }
            return hull;
        }
    }

    std::vector<std::string> CompanionPaths(std::string const& model)
    {
        char normalized[1024];
        if (!domino::NormPath(model.c_str(), normalized, sizeof(normalized)))
            return {};
        std::string name(normalized);
        if (!name.ends_with(".m2"))
            return {};
        name.resize(name.size() - 3);
        std::vector<std::string> result{name + ".phys"};
        // Measured in the installed archives: retail _hu_f.phys -> native _huf.m2, etc.
        if (name.size() >= 4 && (name.back() == 'm' || name.back() == 'f') && name[name.size() - 4] == '_')
        {
            name.insert(name.size() - 1, "_");
            result.push_back(name + ".phys");
        }
        return result;
    }

    bool ValidateBones(domino::Model const& model, size_t count, std::string& error)
    {
        for (auto const& body : model.bodies)
            if (body.bone != 0xFFFF && body.bone >= count)
            {
                error = "PHYS bone " + std::to_string(body.bone) + " exceeds M2 bone count " + std::to_string(count);
                return false;
            }
        return true;
    }

    bool BindSkeleton(domino::Model& model, std::span<domino::V3 const> pivots,
        std::span<int16_t const> parents, std::string& error)
    {
        if (pivots.empty() || pivots.size() != parents.size() || !ValidateBones(model, pivots.size(), error))
            return false;
        auto sub = [](domino::V3 a, domino::V3 b) { return domino::V3{a.x-b.x, a.y-b.y, a.z-b.z}; };
        auto add = [](domino::V3 a, domino::V3 b) { return domino::V3{a.x+b.x, a.y+b.y, a.z+b.z}; };
        auto norm2 = [](domino::V3 v) { return v.x*v.x + v.y*v.y + v.z*v.z; };
        std::vector<domino::V3> origins;
        std::vector<bool> known;
        for (auto const& body : model.bodies)
        {
            origins.push_back(body.pos);
            known.push_back(!model.legacyOrigins || body.type != domino::kBodyDynamic || norm2(body.pos) > 0);
        }
        // Legacy zero origins are placeholders. At rest the two joint frame translations coincide:
        // originB = originA + frameA.translation - frameB.translation (identity body rotations).
        for (size_t pass = 0; pass < origins.size(); ++pass)
            for (auto const& joint : model.joints)
            {
                if (joint.type == domino::kJointDistance || joint.type == domino::kJointPrismatic)
                    continue;
                auto const delta = sub(joint.pa, joint.pb);
                if (known[joint.a] && !known[joint.b])
                {
                    origins[joint.b] = add(origins[joint.a], delta);
                    known[joint.b] = true;
                }
                else if (known[joint.b] && !known[joint.a])
                {
                    origins[joint.a] = sub(origins[joint.b], delta);
                    known[joint.a] = true;
                }
            }
        std::vector<bool> used(pivots.size());
        std::vector<uint16_t> mapped;
        for (size_t i = 0; i < model.bodies.size(); ++i)
        {
            auto const& body = model.bodies[i];
            if (body.bone == 0xFFFF)
            {
                mapped.push_back(0xFFFF);
                continue;
            }
            // A free sliding/distance origin has no unique reconstruction. Its explicit bone is authoritative.
            if (!known[i])
            {
                mapped.push_back(body.bone);
                if (body.bone < used.size())
                    used[body.bone] = true;
                continue;
            }
            float const tolerance2 = 1e-8f; // 0.1 mm; measured native/source pivot roundoff is below this
            // Some backports remove the unweighted origin bone. Drive that body from model placement.
            if (body.type != domino::kBodyDynamic && norm2(origins[i]) <= tolerance2 &&
                (body.bone == 0xFFFF || norm2(sub(pivots[body.bone], origins[i])) > tolerance2))
            {
                mapped.push_back(0xFFFF);
                continue;
            }
            int best = -1;
            int bestScore = 1000000;
            for (size_t b = 0; b < pivots.size(); ++b)
            {
                if (used[b] || norm2(sub(pivots[b], origins[i])) > tolerance2)
                    continue;
                int depth = 0;
                for (int p = parents[b]; p >= 0; p = parents[p])
                {
                    if (size_t(p) >= parents.size() || ++depth >= int(parents.size()))
                    {
                        error = "invalid native skeleton hierarchy";
                        return false;
                    }
                }
                // Duplicated pivots are common at the head of a chain: map shallow bones first.
                int const score = depth * 2048 + (b == body.bone ? 0 : 1024) + int(b);
                if (score < bestScore)
                {
                    best = int(b);
                    bestScore = score;
                }
            }
            if (best < 0)
            {
                error = "PHYS body " + std::to_string(i) + " has no matching native rest pivot";
                return false;
            }
            mapped.push_back(uint16_t(best));
            used[best] = true;
        }
        for (size_t i = 0; i < model.bodies.size(); ++i)
        {
            model.bodies[i].bone = mapped[i];
            if (known[i])
                model.bodies[i].pos = origins[i];
        }
        return true;
    }

    bool Decode(Bytes bytes, domino::Model& out, std::string& error)
    {
        error.clear();
        try
        {
            auto const chunks = Split(bytes);
            auto const version = Read<uint16_t>(chunks.at("PHYS"), 0);
            Require(version <= 6, "unknown PHYS version");
            domino::Model model;
            Hulls(chunks, model);
            std::vector<std::pair<uint32_t, uint32_t>> ranges;
            for (auto const& [tag, stride] : std::array<std::pair<char const*, size_t>, 4>{{
                {"BDY4",48},{"BDY3",44},{"BDY2",32},{"BODY",28}}})
            {
                auto const data = Records(chunks, tag, stride);
                if (data.empty())
                    continue;
                Require(data.size() / stride <= 255, "too many PHYS bodies");
                model.legacyOrigins = stride <= 32;
                for (size_t at = 0; at < data.size(); at += stride)
                {
                    uint16_t const type = Read<uint16_t>(data, at);
                    Require(type <= 2, "unknown body type");
                    domino::BodyDef body;
                    body.type = type == 0 ? domino::kBodyKinematic : type == 1 ? domino::kBodyDynamic : domino::kBodyStatic;
                    body.pos = Vec(data, at + 4);
                    bool const modern = stride >= 44;
                    body.bone = Read<uint16_t>(data, at + (modern ? 2 : 16));
                    int32_t const first = modern ? Read<uint16_t>(data, at + 16) : Read<int32_t>(data, at + 20);
                    int32_t const count = Read<int32_t>(data, at + (modern ? 20 : 24));
                    Require(first >= 0 && count >= 0 && first <= 65535 && count <= 65535, "invalid body shape range");
                    if (stride >= 32)
                        body.gravity = Float(data, at + (modern ? 24 : 28));
                    if (modern)
                    {
                        body.inertia = Float(data, at + 28);
                        body.linDamp = Float(data, at + 32);
                        body.angDamp = Float(data, at + 36);
                        body.carry = Float(data, at + 40);
                    }
                    if (stride >= 48)
                    {
                        body.carry = Float(data, at + 40);
                        body.group = Read<uint16_t>(data, at + 44);
                    }
                    model.bodies.push_back(body);
                    ranges.emplace_back(uint32_t(first), uint32_t(count));
                }
                break;
            }
            Require(!model.bodies.empty(), "no PHYS bodies");
            size_t const shapeStride = chunks.contains("SHP2") ? 32 : 20;
            auto const shapes = Records(chunks, shapeStride == 32 ? "SHP2" : "SHAP", shapeStride);
            Require(shapes.size() / shapeStride <= 255, "too many PHYS shapes");
            std::vector<int> owner(shapes.size() / shapeStride, -1);
            for (size_t i = 0; i < ranges.size(); ++i)
            {
                auto const [first, count] = ranges[i];
                Require(first <= owner.size() && count <= owner.size() - first, "body shape range exceeds table");
                for (size_t s = first; s < first + count; ++s)
                {
                    Require(owner[s] == -1, "shape belongs to multiple bodies");
                    owner[s] = int(i);
                }
            }
            for (size_t i = 0; i < owner.size(); ++i)
            {
                Require(owner[i] >= 0, "unowned PHYS shape");
                size_t const at = i * shapeStride;
                auto const kind = Read<uint16_t>(shapes, at);
                auto const index = Read<uint16_t>(shapes, at + 2);
                domino::ShapeDef shape;
                shape.body = uint8_t(owner[i]);
                shape.friction = Float(shapes, at + 8);
                shape.restitution = Float(shapes, at + 12);
                shape.density = Float(shapes, at + 16);
                Require(shape.friction >= 0 && shape.restitution >= 0, "invalid shape material");
                if (kind == 1)
                {
                    auto const table = Records(chunks, "CAPS", 28);
                    Require(size_t(index) * 28 + 28 <= table.size(), "invalid capsule index");
                    auto const record = table.subspan(size_t(index) * 28, 28);
                    shape.type = domino::kShapeCapsule;
                    shape.a = Vec(record, 0);
                    shape.b = Vec(record, 12);
                    shape.r = Float(record, 24);
                }
                else if (kind == 2)
                {
                    auto const table = Records(chunks, "SPHS", 16);
                    Require(size_t(index) * 16 + 16 <= table.size(), "invalid sphere index");
                    shape.type = domino::kShapeSphere;
                    shape.a = Vec(table, size_t(index) * 16);
                    shape.r = Float(table, size_t(index) * 16 + 12);
                }
                else if (kind == 0)
                {
                    auto const table = Records(chunks, "BOXS", 60);
                    Require(size_t(index) * 60 + 60 <= table.size(), "invalid box index");
                    shape.type = domino::kShapeHull;
                    shape.hull = uint16_t(model.hulls.size());
                    model.hulls.push_back(Box(table.subspan(size_t(index) * 60, 60)));
                }
                else
                {
                    Require(kind == 3 && index < model.hulls.size(), "invalid shape type or hull index");
                    shape.type = domino::kShapeHull;
                    shape.hull = index;
                }
                Require(shape.r >= 0, "negative shape radius");
                model.shapes.push_back(shape);
            }
            auto const joins = Records(chunks, "JOIN", 16);
            Require(joins.size() / 16 <= 255, "too many PHYS joints");
            std::array<std::pair<std::string, size_t>, 6> formats{};
            for (auto const& [type, tag, stride] : std::array<std::tuple<size_t, char const*, size_t>, 11>{{
                {0,"SPHJ",28}, {1,"SHJ2",124}, {1,"SHOJ",version >= 2 ? 116u : 108u},
                {2,"WLJ3",116}, {2,"WLJ2",112}, {2,"WELJ",104}, {3,"REV2",120}, {3,"REVJ",112},
                {4,"PRS2",128}, {4,"PRSJ",120}, {5,"DSTJ",28}}})
                if (formats[type].first.empty() && chunks.contains(tag))
                    formats[type] = {tag, stride};
            constexpr uint8_t types[] = {domino::kJointSpherical, domino::kJointShoulder, domino::kJointWeld,
                domino::kJointRevolute, domino::kJointPrismatic, domino::kJointDistance};
            for (size_t at = 0; at < joins.size(); at += 16)
            {
                auto const a = Read<uint32_t>(joins, at);
                auto const b = Read<uint32_t>(joins, at + 4);
                auto const type = Read<uint16_t>(joins, at + 12);
                auto const index = Read<uint16_t>(joins, at + 14);
                Require(a < model.bodies.size() && b < model.bodies.size() && a != b && type < 6,
                    "invalid joint bodies/type");
                auto const& [tag, stride] = formats[type];
                Require(!tag.empty(), "missing joint table");
                auto const table = Records(chunks, tag, stride);
                Require(size_t(index) * stride + stride <= table.size(), "invalid joint table index");
                auto const record = table.subspan(size_t(index) * stride, stride);
                domino::JointDef joint;
                joint.a = uint8_t(a);
                joint.b = uint8_t(b);
                joint.type = types[type];
                if (type == 0 || type == 5)
                {
                    joint.pa = Vec(record, 0);
                    joint.pb = Vec(record, 12);
                    joint.f[0] = Float(record, 24);
                }
                else
                {
                    for (size_t i = 0; i < 9; ++i)
                    {
                        joint.fa[i] = Float(record, i * 4);
                        joint.fb[i] = Float(record, 48 + i * 4);
                    }
                    joint.pa = Vec(record, 36);
                    joint.pb = Vec(record, 84);
                    if (type == 1)
                    {
                        for (size_t i = 0; i < 3; ++i)
                            joint.f[i] = Float(record, 96 + i * 4);
                        joint.f[3] = stride >= 116 ? Float(record, 108) : 0;
                        joint.mode = stride >= 116 ? Read<int8_t>(record, 112) : 0;
                        joint.f[4] = stride >= 124 ? Float(record, 116) : 1;
                        joint.f[5] = stride >= 124 ? Float(record, 120) : .7f;
                    }
                    else if (type == 2)
                    {
                        joint.f[0] = Float(record, 96);
                        joint.f[1] = Float(record, stride == 104 ? 100 : 104);
                        joint.f[2] = stride == 104 ? 0 : Float(record, 100);
                        joint.f[3] = stride == 104 ? 0 : Float(record, 108);
                    }
                    else if (type == 3)
                    {
                        joint.f[0] = Float(record, 96);
                        joint.f[1] = Float(record, 100);
                        joint.f[2] = Float(record, 104);
                        joint.mode = Read<int8_t>(record, 108);
                        joint.f[3] = stride >= 120 ? Float(record, 112) : 1;
                        joint.f[4] = stride >= 120 ? Float(record, 116) : .7f;
                    }
                    else
                    {
                        for (size_t i = 0; i < 5; ++i)
                            joint.f[i] = Float(record, 96 + i * 4);
                        joint.mode = Read<int8_t>(record, 116);
                        Require(joint.mode == 0, "prismatic motor requires a measured runtime implementation");
                    }
                }
                model.joints.push_back(joint);
            }
            out = std::move(model);
            return true;
        }
        catch (std::exception const& exception)
        {
            error = exception.what();
            return false;
        }
    }
}
