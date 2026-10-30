// Native integration of WarcraftXL's GPL-3.0-or-later equipment physics core.
#include "Engine/Source/ModernAssets/NativeItemPhysics.h"
#if defined(WOWDECOMP_LINUX)
#include "Engine/Source/ModernAssets/ModernPhysics.h"
#include "Engine/Source/ModernAssets/FileDataIdResolver.h"
#include "Engine/Source/ModernAssets/NativeDiagnostic.h"
#include "Engine/Source/Model2/M2Model.h"
#include "Engine/Source/Model2/M2Scene.h"
#include "Engine/Source/Model2/WxlSceneModels.h"
#include "Engine/Source/BlizzardCore/SFile2-Core.h"
#include "Engine/Source/Os/TimeManager.h"
#include "WoW/Common/ComponentCore/WxlEquipmentOwner.h"
#include "WoW/Source/WorldClient/Linux/WxlVisualEnvironment.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <unordered_set>

#include "Engine/Source/ModernAssets/NativeItemPhysics.inl"
#endif
