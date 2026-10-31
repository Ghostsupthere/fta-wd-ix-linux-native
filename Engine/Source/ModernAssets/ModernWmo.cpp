#include "Engine/Source/ModernAssets/ModernWmo.h"

#include "Engine/Source/ModernAssets/FileDataIdResolver.h"
#include "WxlWmo/models/wmo/WmoTranslate.hpp"
#include "WxlWmo/models/wmo/WmoVisualData.hpp"
#include "Engine/Source/ModernAssets/NativeDiagnostic.h"

#include <atomic>
#include <span>

namespace {
std::atomic<size_t> s_visualBytes{0};
constexpr size_t kVisualBudget = 128u * 1024u * 1024u;

template<class T> std::shared_ptr<T> OwnVisuals(std::unique_ptr<T> value, size_t bytes) {
    size_t current = s_visualBytes.load(std::memory_order_relaxed);
    do {
        if (bytes > kVisualBudget - current) {
            WxlNativeDiagnostic("modern-wmo: visual side-data CPU budget reached");
            return {};
        }
    } while (!s_visualBytes.compare_exchange_weak(current, current + bytes, std::memory_order_relaxed));
    return std::shared_ptr<T>(value.release(), [bytes](T* object) {
        delete object;
        s_visualBytes.fetch_sub(bytes, std::memory_order_relaxed);
    });
}
bool ResolveWmoPath(void*, uint32_t id, std::string& path) {
    return ResolveModernAssetFileDataId(id, path);
}
} // namespace

bool ModernWmoFindMaterial(const ModernWmoRootVisuals& root, uint32_t index, ModernWmoMaterialView& out) {
    for (const auto& material : root.materials) {
        if (material.index != index) continue;
        out = {};
        out.index = material.index; out.layers = material.layers; out.flags = material.flags; out.tint = material.tint;
        for (size_t i = 0; i < 8; ++i) out.textures[i] = material.textures[i].c_str();
        return true;
    }
    return false;
}

ModernWmoMeshView ModernWmoMesh(const ModernWmoGroupVisuals& group) {
    ModernWmoMeshView view;
    view.uvSets = group.uvSets; view.hasWeights = group.hasWeights;
    view.vertexCount = group.vertices.size(); view.vertexStride = sizeof(wxl::modern::assets::wmo::visual::Vertex);
    view.vertices = group.vertices.data();
    return view;
}

size_t ModernWmoVisualBytes() { return s_visualBytes.load(std::memory_order_relaxed); }

ModernWmoTransformResult TransformModernWmoRoot(const void* source, uint32_t sourceSize,
    std::vector<uint8_t>& output, std::shared_ptr<ModernWmoRootVisuals>* visuals) {
    output.clear();
    if (visuals) visuals->reset();
    if (!source) {
        return ModernWmoTransformResult::Rejected;
    }
    const std::span<const uint8_t> raw(static_cast<const uint8_t*>(source), sourceSize);
    const wxl::modern::assets::wmo::ResolveCtx resolver{&ResolveWmoPath, nullptr};
    if (!wxl::modern::assets::wmo::TranslateWmoRoot(raw, resolver, output))
        return ModernWmoTransformResult::NotApplicable;
    if (visuals) {
        namespace data = wxl::modern::assets::wmo::visual;
        // Private transport tails stay in this temporary buffer; the native
        // chunk parser receives only the canonical converter output.
        auto envelope = output;
        auto snapshot = std::make_unique<data::Root>();
        uint32_t bodySize = 0;
        if (data::AppendRoot(raw, resolver, envelope) && data::ReadRoot(envelope, *snapshot, bodySize) &&
            bodySize == output.size()) {
            size_t bytes = sizeof(data::Root) + snapshot->materials.capacity() * sizeof(data::Material);
            for (const auto& material : snapshot->materials)
                for (const auto& path : material.textures) bytes += path.capacity() + 1;
            *visuals = OwnVisuals(std::move(snapshot), bytes);
        }
    }
    return ModernWmoTransformResult::Transformed;
}

ModernWmoTransformResult TransformModernWmoGroup(const void* source, uint32_t sourceSize,
    std::vector<uint8_t>& output, std::shared_ptr<ModernWmoGroupVisuals>* visuals) {
    output.clear();
    if (visuals) visuals->reset();
    if (!source) {
        return ModernWmoTransformResult::Rejected;
    }
    const std::span<const uint8_t> raw(static_cast<const uint8_t*>(source), sourceSize);
    const bool changed = wxl::modern::assets::wmo::TranslateWmoGroup(raw, output);
    if (visuals) {
        namespace data = wxl::modern::assets::wmo::visual;
        // A group beneath a converted root may already have native-shaped
        // chunks while retaining extra UV sets and source blend weights.
        auto envelope = changed ? output : std::vector<uint8_t>(raw.begin(), raw.end());
        const size_t canonicalSize = envelope.size();
        auto snapshot = std::make_unique<data::Group>();
        uint32_t bodySize = 0;
        if (data::AppendGroup(raw, envelope) && data::ReadGroup(envelope, *snapshot, bodySize) &&
            bodySize == canonicalSize) {
            const size_t bytes = sizeof(data::Group) + snapshot->vertices.capacity() * sizeof(data::Vertex);
            *visuals = OwnVisuals(std::move(snapshot), bytes);
        }
    }
    return changed ? ModernWmoTransformResult::Transformed : ModernWmoTransformResult::NotApplicable;
}
