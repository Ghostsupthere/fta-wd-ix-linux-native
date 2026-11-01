#ifndef ENGINE_SOURCE_MODERNASSETS_MODERNM3_H
#define ENGINE_SOURCE_MODERNASSETS_MODERNM3_H

#include <cstdint>
#include <vector>

// Drop-in M3 (HotS/SC2 MD34 family) models and DDS textures, ported from the Windows asset host.
// Put "<dir>/<base>.m3" (optionally with "<base>.m3a" and "<base>.m3cfg") in a loose folder named
// patch-ZB.mpq or patch-ZA.mpq under the client's Data directory; opening "<dir>/<base>.m2" then
// serves a model converted on the spot, and "<base>00.skin" its skin. A requested ".blp" is served
// from the ".dds" of the same name in that folder. Nothing happens unless such a folder exists.
//
// The drop-in wins over an archive file of the same name, as the Windows provider did. Both
// functions accept any client file name and return false for anything that is not a drop-in.
// Set WXL_MODERN_ASSETS=0 to disable.
bool ModernM3Provide(const char* name, std::vector<uint8_t>& output);
bool ModernM3Exists(const char* name);

#endif
