#ifndef WOW_SOURCE_WORLDCLIENT_TERRAINAREAREPLAY_H
#define WOW_SOURCE_WORLDCLIENT_TERRAINAREAREPLAY_H

class CMapChunk;

namespace TerrainAreaReplay {

// Recording is scoped to a single ready terrain area on the render thread.
extern bool recording;
void RecordTerrain(CMapChunk* chunk, int bucket);
void RecordWetChunk(CMapChunk* chunk);
void Invalidate();

}

#endif
