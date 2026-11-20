#include <cassert>
#include <cstdint>
#ifdef NDEBUG
#error Assertions must be enabled
#endif
struct Async { uint32_t m_size = 12; };
static int destroyedReads = 0;
void AsyncFileReadDestroyObject(Async* read) { assert(read); ++destroyedReads; }
namespace native_crash {
enum class Event { ModelFailed };
void Record(Event, const char*) {}
}
struct CM2Shared {
    Async* m_asyncObject = nullptr;
    bool m_loadFailed = false;
    const char* m_fileName = "model";
    bool finishSkin = true;
    int skinBuilds = 0;
    static void LoadFailedCallback(void*);
    static void SkinProfileLoadedCallback(void*);
    int FinishLoadingSkinProfile(uint32_t) { ++skinBuilds; return finishSkin; }
};
struct CM2Model {
    CM2Shared* m_shared = nullptr;
    CM2Model** m_callbackPrev = nullptr;
    CM2Model* m_attachParent = nullptr;
    bool ready = false;
    bool m_instanceLoadFailed = false;

    bool HasLoadFailed() const;
    bool IsLoaded(int, int) { return ready; }
    void DetachFromParent() { m_attachParent = nullptr; }
};
struct WOWGUID {
    int value = 0;
    explicit operator bool() const { return value != 0; }
};
struct CGObject_C { CM2Model* m_worldModel = nullptr; };
static CGObject_C target;
static CGObject_C source;
constexpr int TYPE_OBJECT = 1;
CGObject_C* ClntObjMgrObjectPtr(const WOWGUID& guid, int, const char*, int) {
    return guid.value == 1 ? &target : guid.value == 2 ? &source : nullptr;
}
struct CEffect {
    WOWGUID m_targetGuid{1}, m_sourceGuid{2};
    CM2Model* m_model = nullptr;
    int m_attachmentId = 3;
    uint32_t m_flags = 0;
    int retired = 0;
    int attached = 0;

    void MoveToDestroyList() { ++retired; m_flags |= 0x900; }
    void DetachFromParent() { if (m_model && m_model->m_attachParent) m_model->DetachFromParent(); }
    int32_t UpdateAttachment();
    void PostInitialize();
};

#include "model_has_failed.inc"
#include "effect_callbacks.inc"
#include "effect_attachment.inc"
    ++attached;
    return 1;
}

#include "effect_initialize.inc"
    UpdateAttachment();
}

int main() {
    Async read;
    CM2Shared parentShared, effectShared;
    CM2Model parent{&parentShared}, model{&effectShared};
    target.m_worldModel = &parent;
    source.m_worldModel = &model;
    model.ready = true;
    parentShared.m_asyncObject = &read;
    CEffect effect;
    effect.m_model = &model;
    assert(EffectAttachmentOwner(effect) == &target);
    effect.m_targetGuid = {};
    assert(EffectAttachmentOwner(effect) == &source);
    effect.m_targetGuid = {1};

    effect.PostInitialize();
    assert(effect.m_flags == 0 && effect.retired == 0);
    assert(effect.UpdateAttachment() == 0 && effect.attached == 0 && effect.retired == 0);
    parentShared.m_asyncObject = nullptr;
    CM2Model* callback = &parent;
    parent.m_callbackPrev = &callback;
    assert(GetEffectModelLoadState(&parent) == EffectModelLoadState::Pending);

    parent.ready = true;
    parent.m_callbackPrev = nullptr;
    effect.PostInitialize();
    assert(effect.m_flags == 0x800 && effect.attached == 1);
    parent.ready = false;
    parentShared.m_asyncObject = &read;
    assert(effect.UpdateAttachment() == 0 && effect.retired == 0);
    CM2Shared::LoadFailedCallback(&parentShared);
    assert(parentShared.m_loadFailed && !parentShared.m_asyncObject && destroyedReads == 1);
    model.m_attachParent = &parent;
    assert(effect.UpdateAttachment() == 0 && effect.retired == 1 && !model.m_attachParent);
    CEffect failedParent;
    failedParent.m_model = &model;
    failedParent.PostInitialize();
    assert(failedParent.attached == 0 && failedParent.retired == 1);

    parentShared.m_loadFailed = false;
    parent.ready = true;
    effectShared.m_asyncObject = &read;
    model.ready = false;
    CEffect pendingChild;
    pendingChild.m_model = &model;
    pendingChild.PostInitialize();
    assert(pendingChild.m_flags == 0 && pendingChild.retired == 0);
    assert(pendingChild.UpdateAttachment() == 0 && pendingChild.retired == 0);
    effectShared.finishSkin = false;
    CM2Shared::SkinProfileLoadedCallback(&effectShared);
    assert(effectShared.m_loadFailed && !effectShared.m_asyncObject && destroyedReads == 2 && effectShared.skinBuilds == 1);
    pendingChild.PostInitialize();
    assert(pendingChild.retired == 1 && pendingChild.attached == 0);

    effectShared.m_asyncObject = &read;
    CM2Shared::SkinProfileLoadedCallback(&effectShared);
    assert(effectShared.skinBuilds == 1 && destroyedReads == 3);
    effectShared.m_loadFailed = false;
    effectShared.finishSkin = true;
    effectShared.m_asyncObject = &read;
    CM2Shared::SkinProfileLoadedCallback(&effectShared);
    assert(!effectShared.m_loadFailed && !effectShared.m_asyncObject && effectShared.skinBuilds == 2);
    assert(GetEffectModelLoadState(&model) == EffectModelLoadState::Failed);

    parent.ready = true;
    model.ready = false;
    model.m_instanceLoadFailed = true;
    effectShared.m_asyncObject = &read;
    CEffect failedInstance;
    failedInstance.m_model = &model;
    failedInstance.PostInitialize();
    assert(failedInstance.retired == 1 && failedInstance.attached == 0);

    CEffect soundOnly;
    soundOnly.PostInitialize();
    assert(soundOnly.attached == 1 && soundOnly.retired == 0);
}
