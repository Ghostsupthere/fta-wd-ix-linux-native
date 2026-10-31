// Selected GameObject preview lifecycle: the portable half of the supplied wxl-gameobject-editor
// `GameObjectPreview.hpp`. The native backend lives in WoW/Source/Ui/Linux/WxlGameObjectEditor.cpp.
// Copyright (C) 2026 WarcraftXL; GPL-3.0-or-later.
#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace wxl::scripts::gameobjecteditor
{
    namespace previewdetail
    {
        using Matrix = std::array<float, 16>;

        enum class ModelKind : uint8_t { M2, Wmo, SceneM2 };

        // Addresses are identity tokens only. The native backend resolves and validates them afresh before use.
        struct Binding
        {
            uint64_t guid = 0;
            uintptr_t owner = 0;
            uintptr_t model = 0;
            uintptr_t handler = 0;
            uintptr_t resource = 0;
            uintptr_t payload = 0;
            uintptr_t entity = 0;
            ModelKind kind = ModelKind::M2;

            bool operator==(Binding const& other) const
            {
                return guid == other.guid && owner == other.owner && model == other.model &&
                    handler == other.handler && resource == other.resource && payload == other.payload &&
                    entity == other.entity && kind == other.kind;
            }
        };

        struct LiveTransform
        {
            Binding binding;
            Matrix objectMatrix;
            Matrix renderMatrix;
        };

        inline bool ValidMatrix(Matrix const& matrix)
        {
            for (float value : matrix)
                if (!std::isfinite(value))
                    return false;

            constexpr float MapCoordinateLimit = 17066.166f;
            return std::abs(matrix[12]) <= MapCoordinateLimit &&
                std::abs(matrix[13]) <= MapCoordinateLimit && std::abs(matrix[14]) <= MapCoordinateLimit &&
                std::abs(matrix[3]) < 0.00001f && std::abs(matrix[7]) < 0.00001f &&
                std::abs(matrix[11]) < 0.00001f && std::abs(matrix[15] - 1.0f) < 0.00001f;
        }

        inline float Determinant(Matrix const& matrix)
        {
            return matrix[0] * (matrix[5] * matrix[10] - matrix[6] * matrix[9]) -
                matrix[1] * (matrix[4] * matrix[10] - matrix[6] * matrix[8]) +
                matrix[2] * (matrix[4] * matrix[9] - matrix[5] * matrix[8]);
        }

        inline bool PreserveScale(Matrix const& original, Matrix const& desired, Matrix& result)
        {
            if (!ValidMatrix(original) || !ValidMatrix(desired))
                return false;

            result = desired;
            for (size_t row = 0; row < 3; ++row)
            {
                size_t const offset = row * 4;
                float originalLength = 0.0f;
                float desiredLength = 0.0f;
                for (size_t column = 0; column < 3; ++column)
                {
                    originalLength += original[offset + column] * original[offset + column];
                    desiredLength += desired[offset + column] * desired[offset + column];
                }
                if (!std::isfinite(originalLength) || !std::isfinite(desiredLength) ||
                    originalLength < 0.00000001f || desiredLength < 0.00000001f)
                    return false;

                float const scale = std::sqrt(originalLength / desiredLength);
                for (size_t column = 0; column < 3; ++column)
                    result[offset + column] *= scale;
            }

            float const originalDeterminant = Determinant(original);
            float const resultDeterminant = Determinant(result);
            if (!std::isfinite(originalDeterminant) || !std::isfinite(resultDeterminant) ||
                originalDeterminant == 0.0f || resultDeterminant == 0.0f)
                return false;

            // The editor supplies rigid transforms. Reject nearly singular/sheared bases before
            // native WMO inverse-matrix and spatial updates, even if every coefficient is finite.
            float basisLengths = 1.0f;
            for (size_t row = 0; row < 3; ++row)
            {
                size_t const offset = row * 4;
                basisLengths *= std::sqrt(result[offset] * result[offset] +
                    result[offset + 1] * result[offset + 1] + result[offset + 2] * result[offset + 2]);
            }
            if (!std::isfinite(basisLengths) || std::abs(resultDeterminant) < basisLengths * 0.99f)
                return false;

            if (std::signbit(originalDeterminant) != std::signbit(resultDeterminant))
                for (size_t column = 0; column < 3; ++column)
                    result[column] = -result[column];

            return ValidMatrix(result);
        }

        inline bool SameMatrix(Matrix const& left, Matrix const& right)
        {
            for (size_t index = 0; index < left.size(); ++index)
                if (!std::isfinite(left[index]) || !std::isfinite(right[index]) ||
                    std::abs(left[index] - right[index]) > 0.00001f)
                    return false;
            return true;
        }

        // The same lifecycle is exercised by the native backend and the pointer-reuse fixture.
        class State
        {
        public:
            template<class Backend>
            bool Apply(uint64_t guid, Matrix const& desired, Backend& backend)
            {
                if (!guid || !ValidMatrix(desired))
                    return false;

                if (original_ && original_->binding.guid != guid)
                    Restore(backend);

                auto live = backend.Resolve(guid);
                if (!live)
                    return false;

                // An unloaded/replaced model must get a fresh snapshot, never a stale restore.
                if (!original_ || !(original_->binding == live->binding))
                {
                    LiveTransform replacement = *live;
                    // A model can reload while its owning GO survives. Keep the authoritative owner
                    // placement, but take scale and identity from the newly resolved model.
                    if (original_ && original_->binding.guid == live->binding.guid &&
                        original_->binding.owner == live->binding.owner &&
                        original_->binding.handler == live->binding.handler)
                    {
                        replacement.objectMatrix = original_->objectMatrix;
                        if (!PreserveScale(live->renderMatrix, original_->objectMatrix, replacement.renderMatrix))
                            return false;
                    }
                    original_ = replacement;
                }

                Matrix objectMatrix;
                Matrix renderMatrix;
                if (!PreserveScale(original_->objectMatrix, desired, objectMatrix) ||
                    !PreserveScale(original_->renderMatrix, desired, renderMatrix))
                    return false;

                if (SameMatrix(live->objectMatrix, objectMatrix) && SameMatrix(live->renderMatrix, renderMatrix))
                    return true;

                return backend.Write(*live, objectMatrix, renderMatrix);
            }

            template<class Backend>
            void Restore(Backend& backend)
            {
                if (original_)
                {
                    auto live = backend.Resolve(original_->binding.guid);
                    if (live && live->binding == original_->binding &&
                        (!SameMatrix(live->objectMatrix, original_->objectMatrix) ||
                         !SameMatrix(live->renderMatrix, original_->renderMatrix)))
                        backend.Write(*live, original_->objectMatrix, original_->renderMatrix);
                }
                Forget();
            }

            void Forget() { original_.reset(); }

        private:
            std::optional<LiveTransform> original_;
        };
    }
}
