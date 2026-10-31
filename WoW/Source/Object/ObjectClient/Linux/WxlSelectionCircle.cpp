// Native port of the supplied wxl-selection-circle (GPL-3.0-or-later, Copyright (C) 2026
// WarcraftXL); see WxlSelectionCircle.h. The per-frame ProjectTex2d refresh the Windows module
// delegates to the quest-ring module is not needed here: the matrix is built immediately before
// the draw in the same function.
#include "WoW/Source/Object/ObjectClient/Linux/WxlSelectionCircle.h"

#if defined(WOWDECOMP_LINUX)

#include <cmath>

#include "Engine/Source/Model2/M2Model.h"
#include "Engine/Source/ModernAssets/NativeDiagnostic.h"
#include "Engine/Source/ModernAssets/WxlSelectionCircle/SelectionCircleMatrix.hpp"
#include "Engine/Source/Tempest/C44Matrix.h"
#include "WoW/Source/Object/ObjectClient/Unit_C.h"
#include "WoW/Source/WorldClient/Linux/WxlVisualEnvironment.h"

void WxlSelectionCircleApply(const CGUnit_C* unit, C44Matrix* matrix) {
    static const bool enabled = WxlProfileEnabled("WXL_SELECTION_CIRCLE", true);
    if (!enabled || !unit || !matrix) return;

    float facing = unit->GetRawFacing();
    // The rendered orientation (including stationary turns) is the model's world-matrix forward row.
    if (const CM2Model* model = unit->m_worldModel) {
        float visual = 0.0f;
        if (wxl::selcircle::FacingFromWorldRow(model->m_worldTransform.a0, model->m_worldTransform.a1, visual)) facing = visual;
    }
    if (!std::isfinite(facing)) return;

    wxl::selcircle::WriteWorldFacingMatrix(&matrix->a0, facing);
    static bool logged = false;
    if (!logged) {
        logged = true;
        WxlNativeDiagnostic("selection-circle: first unit-facing matrix applied (facing=%.3f)", facing);
    }
}

#endif  // WOWDECOMP_LINUX
