#ifndef WOW_OBJECT_BODY_SHAPE_NAMES_H
#define WOW_OBJECT_BODY_SHAPE_NAMES_H

#include "Engine/Source/Retail/BodyShape.h"

// Body shapes other players shared with us arrive by character name (an addon message names its sender), but the
// renderer looks them up by guid. This keeps the name table and binds names to the players currently in view.
namespace BodyShapeNames {

// Remember `values` for this character name (case-insensitive) and bind it as soon as the player is in view.
void Set(const char* name, const BodyShape::Values& values);

// Forget one name, or every name when `name` is null.
void Clear(const char* name);

// Bind remembered names to the visible players. Cheap to call every frame: it only does work once a second.
void Resolve();

} // namespace BodyShapeNames

#endif
