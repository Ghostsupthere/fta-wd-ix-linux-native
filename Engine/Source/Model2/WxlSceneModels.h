#ifndef ENGINE_SOURCE_MODEL2_WXLSCENEMODELS_H
#define ENGINE_SOURCE_MODEL2_WXLSCENEMODELS_H
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <unordered_set>
#include <vector>

// Native owning links are traversed afresh; no model address survives a frame.
// Promote child seeds to their root so parent palettes are handled first.
template<class Scene, class Operation> void WxlVisitSceneModels(Scene* scene, Operation operation) {
    using Model = std::remove_pointer_t<decltype(scene->m_animateList)>;
    constexpr size_t limit = 16384;
    std::vector<Model*> pending;
    std::unordered_set<Model*> seen;
    auto seed = [&](Model* model) {
        uint32_t depth = 0;
        while (model->m_attachParent && model->m_attachParent->m_scene == scene) {
            if (++depth >= 256) return;
            model = model->m_attachParent;
        }
        if (model->m_scene == scene) pending.push_back(model);
    };
    size_t count = 0;
    for (Model* model = scene->m_animateList; model && count++ < limit; model = model->m_animateNext) seed(model);
    count = 0;
    for (Model* model = scene->m_drawList; model && count++ < limit; model = model->m_drawNext) seed(model);
    while (!pending.empty() && seen.size() < limit) {
        Model* model = pending.back(); pending.pop_back();
        if (!seen.insert(model).second || model->m_scene != scene) continue;
        operation(model);
        count = 0;
        for (Model* child = model->m_attachList; child && count++ < limit; child = child->m_attachNext)
            pending.push_back(child);
    }
}
#endif
