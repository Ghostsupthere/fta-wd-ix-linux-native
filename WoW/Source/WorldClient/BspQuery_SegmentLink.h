#ifndef WOW_SOURCE_WORLDCLIENT_BSPQUERY_SEGMENTLINK_H
#define WOW_SOURCE_WORLDCLIENT_BSPQUERY_SEGMENTLINK_H

#include <cstddef>
#include <cstdint>
#include "Engine/Source/Tempest/C3Ray.h"
#include "Engine/Source/Tempest/C3Segment.h"
#include "WoW/Source/WorldClient/MapObjGroup.h"
#include "WoW/Source/WorldClient/RelinkCandidates.h"

class CAaBsp;
class CAaBspNode;

struct BspQuery_SegmentLink : public BspQuery {
    /* 0010 */ C3Ray m_ray;
    /* 0028 */ C3Segment m_seg;
    /* 0040 */ float m_oosegMag;
    /* 0044 */ float m_interiorMaxT;
    /* 0048 */ float m_groundMaxT;
    /* 004C */ float m_interiorT;
    /* 0050 */ float m_groundT;
    /* 0054 */ int32_t m_interiorFace;
    /* 0058 */ int32_t m_groundFace;
    /* 005C */ uint32_t m_faceIgnoreFlags;

    RelinkProbe::Faces* m_sample = nullptr;
    RelinkCandidates::Mode m_candidateMode = RelinkCandidates::Mode::Off;
    uint32_t m_indexCount = 0, m_vertexCount = 0;
    BspQuery_SegmentLink(SMOPoly* faces, C3Vector* vertexList, uint16_t* indices, const C3Segment& seg, float interiorMaxT, float groundMaxT);
    void operator()(uint16_t face);
    bool GetFaceIndicesUsingCache(const CAaBsp& aaBsp, const CAaBspNode* node);
    bool GetHits(float* interiorT, int32_t* interiorFace, float* groundT, int32_t* groundFace);

private:
    void VisitFace(uint16_t face, bool candidate);
    bool VisitFilteredLeaf(const CAaBsp& bsp, const CAaBspNode& node);
};

#endif
