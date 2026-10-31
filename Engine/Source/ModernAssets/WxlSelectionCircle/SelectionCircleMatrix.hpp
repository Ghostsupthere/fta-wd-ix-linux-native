#pragma once
// Orientation maths for WarcraftXL's wxl-selection-circle (GPL-3.0-or-later, Copyright (C) 2026
// WarcraftXL): the stock BuildSelectionRotMatrix turns the circle toward the camera; this module
// writes the unit's own forward direction instead. Pure, so it is covered by
// tests/wxl_selection_circle_test.cpp.
#include <cmath>
#include <cstddef>

namespace wxl::selcircle {

inline constexpr float kPi = 3.14159265358979323846f;

// Rendered facing from the first row of a model world matrix (transformed local +X, the forward
// direction). False when the row is degenerate or not finite.
inline bool FacingFromWorldRow(float forwardX, float forwardY, float& facing) {
    const float lengthSquared = forwardX * forwardX + forwardY * forwardY;
    if (!std::isfinite(lengthSquared) || lengthSquared < 1.0e-8f) return false;
    facing = std::atan2(forwardY, forwardX);
    return std::isfinite(facing);
}

// Stock BuildSelectionRotMatrix uses -atan2(object - camera), so UnitSelectTexture's lower front
// marker points back toward the camera. Keep that texture convention, replacing the camera
// direction with the unit's forward direction. `matrix` is a row-major 4x4.
inline void WriteWorldFacingMatrix(float* matrix, float facing) {
    const float angle = kPi - facing;
    const float cosine = std::cos(angle);
    const float sine = std::sin(angle);
    for (std::size_t i = 0; i < 16; ++i) matrix[i] = 0.0f;
    matrix[0] = cosine;
    matrix[1] = sine;
    matrix[4] = -sine;
    matrix[5] = cosine;
    matrix[10] = 1.0f;
    matrix[15] = 1.0f;
}

}  // namespace wxl::selcircle
