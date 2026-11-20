#include <cassert>
#include <cstdint>

#ifdef NDEBUG
#error Assertions must be enabled
#endif

struct C3Vector {
    float x, y, z;
};

struct Bounds {
    C3Vector value;
    C3Vector Center() const { return value; }
};

struct ModelData {
    struct {
        Bounds m_extent;
    } m_collisionBounds;
};

struct SharedModel {
    ModelData* m_data;
};

struct CGUnit_C;

struct CM2Model {
    CM2Model* m_attachList = nullptr;
    CM2Model* m_attachNext = nullptr;
    int m_attachmentId = 0;
    SharedModel* m_shared = nullptr;
    bool instanceReady = false;
    bool sharedReady = false;
    bool waitSucceeds = false;
    int waits = 0;
    int releases = 0;
    int sequences = 0;
    CGUnit_C* owner = nullptr;
    CM2Model* replacement = nullptr;

    bool IsLoaded() const { return instanceReady; }
    bool IsLoaded(int, int) const { return sharedReady; }
    bool WaitForLoad(const char*);
    void Release() { ++releases; }
    void SetBoneSequence(unsigned, unsigned, unsigned, unsigned, float, unsigned, unsigned) {
        ++sequences;
    }
};

struct CGUnit_C {
    CM2Model* model = nullptr;
    CM2Model* GetObjectModel() const { return model; }
    float GetBaseAlpha() const { return 1.0f; }
};

bool CM2Model::WaitForLoad(const char*) {
    ++waits;
    if (owner) {
        owner->model = replacement;
    }
    instanceReady = waitSucceeds;
    return waitSucceeds;
}

static bool ReadyModelForUI(CGUnit_C*, int) {
    return true;
}

constexpr int COMP_HAND_LEFT = 0;
constexpr int COMP_HAND_RIGHT = 1;

struct CCharacterComponent {
    static inline int visualRemovals = 0;
    static void RemoveVisuals(CM2Model*) { ++visualRemovals; }
    static void ComponentCloseFingers(CM2Model*, int) {}
};

struct CGCharacterModelBase {
    CM2Model* duplicate = nullptr;
    CM2Model* displayed = nullptr;
    CM2Model* GetDuplicateUnitCharacterModel(CGUnit_C*) const { return duplicate; }
    void SetModel(CM2Model* model) { displayed = model; }
    void SetFrameAlpha(uint8_t) {}
    void SetUpModel(CGUnit_C*);
};

#include "portrait_setup.inc"

static C3Vector GetTrajectoryCenter(CGUnit_C* root) {
#include "trajectory_center.inc"
    return center;
}

int main() {
    CM2Model previous;
    CM2Model duplicate;
    CGUnit_C unit;
    CGCharacterModelBase portrait{&duplicate, &previous};

    portrait.SetUpModel(&unit);
    assert(duplicate.waits == 1 && duplicate.releases == 1);
    assert(portrait.displayed == &previous);
    assert(CCharacterComponent::visualRemovals == 0 && duplicate.sequences == 0);

    duplicate.waitSucceeds = true;
    portrait.SetUpModel(&unit);
    assert(duplicate.waits == 2 && duplicate.releases == 2);
    assert(portrait.displayed == &duplicate);
    assert(CCharacterComponent::visualRemovals == 1 && duplicate.sequences == 1);

    CM2Model ready;
    ready.instanceReady = true;
    portrait.duplicate = &ready;
    portrait.SetUpModel(&unit);
    assert(portrait.displayed == &ready && ready.waits == 0 && ready.releases == 1);

    assert(GetTrajectoryCenter(&unit).z == 1.0f);
    CM2Model rootModel;
    unit.model = &rootModel;
    assert(GetTrajectoryCenter(&unit).z == 1.0f && rootModel.waits == 0);
    rootModel.sharedReady = true;
    assert(GetTrajectoryCenter(&unit).z == 1.0f && rootModel.waits == 1);

    ModelData data{};
    data.m_collisionBounds.m_extent.value = {0.0f, 0.0f, 12.0f};
    SharedModel shared{&data};
    rootModel.m_shared = &shared;
    rootModel.waitSucceeds = true;
    assert(GetTrajectoryCenter(&unit).z == 12.0f && rootModel.waits == 2);

    rootModel.owner = &unit;
    rootModel.replacement = &ready;
    assert(GetTrajectoryCenter(&unit).z == 1.0f && unit.model == &ready);
}
