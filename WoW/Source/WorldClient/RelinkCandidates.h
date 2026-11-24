#ifndef WOW_SOURCE_WORLDCLIENT_RELINKCANDIDATES_H
#define WOW_SOURCE_WORLDCLIENT_RELINKCANDIDATES_H

#include <cstdint>


class C3Ray;
class C3Vector;

namespace RelinkCandidates {

enum class Mode { Off, Filter, Verify };
Mode Setting();

struct Cell {
    int32_t x, y;
    bool down;
    bool operator==(const Cell& other) const { return x == other.x && y == other.y && down == other.down; }
};

bool CellForRay(const C3Ray& ray, Cell& cell);

struct GeometryView {
    const uint16_t* indices;
    uint32_t indexCount;
    const C3Vector* vertices;
    uint32_t vertexCount;
};

// A mask of the original leaf order. Zero certifies a horizontal rejection for every ray in the cell.
void BuildMask(Cell cell, const uint16_t* faces, uint32_t count, GeometryView mesh, uint8_t* mask);

// A miss may defer filling to limit the cost of cold geometry or entering a new cell.
const uint8_t* GetMask(uint64_t geometry, uint32_t faceStart, Cell cell,
                       const uint16_t* faces, uint32_t count, GeometryView mesh);

} // namespace RelinkCandidates
#endif
