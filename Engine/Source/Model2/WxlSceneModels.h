#ifndef ENGINE_SOURCE_MODEL2_WXLSCENEMODELS_H
#define ENGINE_SOURCE_MODEL2_WXLSCENEMODELS_H
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <vector>

// Pointer set that is emptied by bumping a stamp, so a pass that visits thousands of models
// per frame stops allocating once the table has reached its working size.
class WxlPointerSet {
public:
    void Clear() {
        count_ = 0;
        if (++stamp_ == 0) { for (Slot& slot : slots_) slot.stamp = 0; stamp_ = 1; }
    }
    size_t Size() const { return count_; }
    // True when the pointer was not already present.
    bool Insert(const void* key) {
        if ((count_ + 1) * 2 > slots_.size()) Grow();
        return Place(key);
    }
private:
    struct Slot { const void* key = nullptr; uint32_t stamp = 0; };
    bool Place(const void* key) {
        const size_t mask = slots_.size() - 1;
        size_t index = (((reinterpret_cast<uintptr_t>(key) >> 4) * 0x9E3779B97F4A7C15ull) >> 20) & mask;
        for (;; index = (index + 1) & mask) {
            Slot& slot = slots_[index];
            if (slot.stamp != stamp_) { slot.key = key; slot.stamp = stamp_; ++count_; return true; }
            if (slot.key == key) return false;
        }
    }
    void Grow() {
        std::vector<Slot> previous(slots_.empty() ? 256 : slots_.size() * 2);
        previous.swap(slots_);
        count_ = 0;
        for (const Slot& slot : previous) if (slot.stamp == stamp_) Place(slot.key);
    }
    std::vector<Slot> slots_;
    size_t count_ = 0;
    uint32_t stamp_ = 1;
};

// Native owning links are traversed afresh; no model address survives a frame.
// Promote child seeds to their root so parent palettes are handled first.
template<class Scene, class Operation> void WxlVisitSceneModels(Scene* scene, Operation operation) {
    using Model = std::remove_pointer_t<decltype(scene->m_animateList)>;
    constexpr size_t limit = 16384;
    // The scratch lives with the thread; a visit started from inside another one gets its own.
    struct Scratch { WxlPointerSet seen; std::vector<Model*> pending; bool busy = false; };
    thread_local Scratch shared;
    Scratch nested;
    Scratch& scratch = shared.busy ? nested : shared;
    struct Release { Scratch& s; ~Release() { s.busy = false; s.pending.clear(); } } release{scratch};
    scratch.busy = true;
    scratch.seen.Clear(); scratch.pending.clear();
    auto& pending = scratch.pending;
    auto& seen = scratch.seen;
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
    while (!pending.empty() && seen.Size() < limit) {
        Model* model = pending.back(); pending.pop_back();
        if (!seen.Insert(model) || model->m_scene != scene) continue;
        operation(model);
        count = 0;
        for (Model* child = model->m_attachList; child && count++ < limit; child = child->m_attachNext)
            pending.push_back(child);
    }
}
#endif
