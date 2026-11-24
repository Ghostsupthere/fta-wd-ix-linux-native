#include "BspQuery_SegmentLink.h"
#include "WoW/Source/WorldClient/RelinkProbe.h"
#include "Engine/Source/Base/Profiler.h"
#include <cmath>
#include <cstdio>
#include "Engine/Source/Tempest/NTempest.h"
#include "WoW/Source/WorldClient/Types.h"
#include "WoW/Common/AaBsp.h"

// VERIFY: d9cfa43d auto
// OFFSET: 0x007C77D0
BspQuery_SegmentLink::BspQuery_SegmentLink(SMOPoly* faces, C3Vector* vertexList, uint16_t* indices, const C3Segment& seg, float interiorMaxT, float groundMaxT) {
    this->m_faces = faces;
    this->m_overflowFlags = nullptr;
    this->m_vertexList = vertexList;
    this->m_indices = indices;
    this->m_ray.origin = C3Vector(0.0f, 0.0f, 0.0f);
    this->m_ray.dir = C3Vector(0.0f, 0.0f, 0.0f);
    this->m_seg.b = C3Vector(0.0f, 0.0f, 0.0f);
    this->m_seg.t = C3Vector(0.0f, 0.0f, 0.0f);
    this->m_ray.origin = seg.b;
    this->m_ray.dir = C3Vector(seg.t.x - seg.b.x, seg.t.y - seg.b.y, seg.t.z - seg.b.z);
    this->m_seg = seg;

    float length = std::sqrt(this->m_ray.dir.z * this->m_ray.dir.z + this->m_ray.dir.y * this->m_ray.dir.y + this->m_ray.dir.x * this->m_ray.dir.x);
    this->m_oosegMag = 1.0f / length;
    this->m_ray.dir.x = 1.0f / length * this->m_ray.dir.x;
    this->m_ray.dir.y = this->m_ray.dir.y * (1.0f / length);
    this->m_ray.dir.z = 1.0f / length * this->m_ray.dir.z;
    this->m_interiorMaxT = interiorMaxT;
    this->m_groundMaxT = groundMaxT;
    this->m_interiorFace = -1;
    this->m_groundFace = -1;
    this->m_faceIgnoreFlags = 0x82;
    this->m_interiorT = interiorMaxT * length;
    this->m_groundT = length * groundMaxT;
}

// OFFSET: 0x007C6600
void BspQuery_SegmentLink::operator()(uint16_t face) {
    VisitFace(face, true);
}

void BspQuery_SegmentLink::VisitFace(uint16_t face, bool candidate) {
    auto* work = Profiler::MovementWorkScope::Active();
    if (work) ++work->linkBspFaces;
    uint8_t flags = this->m_faces[face].m_flags;

    if (flags & this->m_faceIgnoreFlags) {
        return;
    }

    if (BspQuery::testFaceSub >= 0x2000) {
        return;
    }

    BspQuery::testFaces[BspQuery::testFaceSub++] = face;
    this->m_faces[face].m_flags |= 0x80;

    if (!candidate && m_candidateMode != RelinkCandidates::Mode::Verify) return;

    float t = 0.0f;

    if (work) ++work->linkTriangleTests;
    if (!NTempest::Intersect(this->m_ray, this->m_vertexList, &this->m_indices[3 * face], &t, nullptr, 0.0020000001f)) {
        return;
    }

    if (!candidate) {
        static bool reported = false;
        if (!reported) {
            std::fprintf(stderr, "WarcraftXL relink candidates: verification found a rejected face hit\n");
            reported = true;
        }
    }
    if (flags & 0x20) {
        if (t < 0.0f) {
            return;
        }

        if (t <= this->m_interiorT) {
            this->m_interiorT = t;
            this->m_interiorFace = face;
        }

        if (t <= this->m_groundT) {
            this->m_groundFace = face;
            this->m_groundT = t;
        }
    } else if (flags & 0x8) {
        if (t >= 0.0f && t <= this->m_interiorT) {
            this->m_interiorFace = face;
            this->m_interiorT = t;
        }
    } else if (flags & 0x4) {
        if (t >= 0.0f && t <= this->m_groundT) {
            this->m_groundFace = face;
            this->m_groundT = t;
        }
    }
}

// VERIFY: bd10af2f manual
// OFFSET: 0x007C6710
bool BspQuery_SegmentLink::GetHits(float* interiorT, int32_t* interiorFace, float* groundT, int32_t* groundFace) {
    *interiorFace = -1;
    *groundFace = -1;

    if (this->m_interiorFace == -1 && this->m_groundFace == -1) {
        return false;
    }

    if (this->m_groundFace != -1) {
        float t = this->m_groundT * this->m_oosegMag;
        *groundT = t;

        if (t >= this->m_groundMaxT) {
            t = this->m_groundMaxT;
        }

        *groundT = t;
        *groundFace = this->m_groundFace;
    }

    if (this->m_interiorFace != -1) {
        float t = this->m_interiorT * this->m_oosegMag;
        *interiorT = t;

        if (t >= this->m_interiorMaxT) {
            t = this->m_interiorMaxT;
        }

        *interiorT = t;
        *interiorFace = this->m_interiorFace;
    }

    return true;
}

bool BspQuery_SegmentLink::VisitFilteredLeaf(const CAaBsp& bsp, const CAaBspNode& node) {
    if (m_candidateMode == RelinkCandidates::Mode::Off || node.m_nFaces <= 300 || !m_indices || !m_vertexList ||
        !bsp.m_nodeFaceIndices || node.m_faceStart > bsp.m_nNodeFaceIndices ||
        node.m_nFaces > bsp.m_nNodeFaceIndices - node.m_faceStart)
        return false;

    RelinkCandidates::Cell cell;
    if (!RelinkCandidates::CellForRay(m_ray, cell)) return false;
    const auto* faces = bsp.m_nodeFaceIndices + node.m_faceStart;
    const RelinkCandidates::GeometryView mesh{m_indices, m_indexCount, m_vertexList, m_vertexCount};
    const auto* mask = RelinkCandidates::GetMask(bsp.m_relinkGeometryId, node.m_faceStart, cell, faces, node.m_nFaces, mesh);
    if (!mask) return false;
    // Preserve visit order, dynamic flags, duplicate suppression and the query-wide face cap.
    for (uint32_t i = 0; i < node.m_nFaces; ++i) VisitFace(faces[i], mask[i] != 0);
    return true;
}

// VERIFY: 62c67e6e manual
// OFFSET: 0x007C6790
bool BspQuery_SegmentLink::GetFaceIndicesUsingCache(const CAaBsp& aaBsp, const CAaBspNode* node) {
    if (m_sample) ++m_sample->leaves;
    if (!g_BspDigestCache) return VisitFilteredLeaf(aaBsp, *node);

    CAaBspNodeDigest* digest = g_BspDigestCache->GetDigest(aaBsp, node, this->m_faces, this->m_vertexList, this->m_indices);

    if (!digest) return VisitFilteredLeaf(aaBsp, *node);

    if (m_sample) ++m_sample->digests;
    float maxX = this->m_seg.t.x;
    float maxY = this->m_seg.t.y;
    float minX = this->m_seg.b.x;
    float maxZ = this->m_seg.t.z;

    if (minX > maxX) {
        minX = this->m_seg.t.x;
        maxX = this->m_seg.b.x;
    }

    float minY = this->m_seg.b.y;

    if (minY > maxY) {
        minY = this->m_seg.t.y;
        maxY = this->m_seg.b.y;
    }

    float minZ = this->m_seg.b.z;

    if (minZ > maxZ) {
        minZ = this->m_seg.t.z;
        maxZ = this->m_seg.b.z;
    }

    minX -= 0.0099999998f;
    maxX += 0.0099999998f;
    minY -= 0.0099999998f;
    maxY += 0.0099999998f;
    minZ -= 0.0099999998f;
    maxZ += 0.0099999998f;

    uint8_t outcodes[450];

    for (uint32_t i = 0; i < digest->m_nVertices; i++) {
        const C3Vector& vertex = digest->m_vertices[i];
        uint8_t code = 0;

        if (minX > vertex.x) {
            code = 0x20;
        }

        if (maxX < vertex.x) {
            code |= 0x10;
        }

        if (minY > vertex.y) {
            code |= 0x8;
        }

        if (maxY < vertex.y) {
            code |= 0x4;
        }

        if (minZ > vertex.z) {
            code |= 0x2;
        }

        if (maxZ < vertex.z) {
            code |= 0x1;
        }

        outcodes[i] = code;
    }

    for (uint32_t i = 0; i < digest->m_nFaces; i++) {
        if (auto* work = Profiler::MovementWorkScope::Active()) ++work->linkBspFaces;
        uint16_t face = digest->m_faceIndices[i];
        uint32_t flags = this->m_faces[face].m_flags;

        if ((this->m_faceIgnoreFlags & digest->m_faceFlags[i]) || (flags & this->m_faceIgnoreFlags)) {
            continue;
        }

        if (BspQuery::testFaceSub >= 0x2000) {
            continue;
        }

        BspQuery::testFaces[BspQuery::testFaceSub++] = face;
        this->m_faces[face].m_flags |= 0x80;

        const uint16_t* verts = &digest->m_faceVertexIndices[3 * i];

        if (outcodes[verts[2]] & outcodes[verts[1]] & outcodes[verts[0]] & 0x3F) {
            continue;
        }

        float t = 0.0f;

        if (auto* work = Profiler::MovementWorkScope::Active()) ++work->linkTriangleTests;
        if (!NTempest::Intersect(this->m_ray, digest->m_vertices, verts, &t, nullptr, 0.0020000001f)) {
            continue;
        }

        if (flags & 0x20) {
            if (t < 0.0f) {
                continue;
            }

            if (t <= this->m_interiorT) {
                this->m_interiorT = t;
                this->m_interiorFace = face;
            }

            if (t <= this->m_groundT) {
                this->m_groundT = t;
                this->m_groundFace = face;
            }
        } else if (flags & 0x8) {
            if (t >= 0.0f && t <= this->m_interiorT) {
                this->m_interiorT = t;
                this->m_interiorFace = face;
            }
        } else if (flags & 0x4) {
            if (t >= 0.0f && t <= this->m_groundT) {
                this->m_groundT = t;
                this->m_groundFace = face;
            }
        }
    }

    return true;
}
