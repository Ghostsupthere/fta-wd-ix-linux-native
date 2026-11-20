#include <cassert>
#include <cmath>
#include <cstdint>
#include <functional>

#ifdef NDEBUG
#error Assertions must be enabled
#endif

struct C3Vector {
    float z;
};

struct CAaBox {
    C3Vector b, t;

    void operator*=(float scale) {
        b.z *= scale;
        t.z *= scale;
    }
};

struct CAaSphere {
    float r;
};

struct M2SequenceInfo {
    CAaBox m_bounds{};
    void Clear() { m_bounds = {}; }
};

struct ModelData {
    struct {
        CAaBox m_extent;
    } m_collisionBounds = {{{0.0f}, {3.0f}}};
};

struct CM2Shared {
    ModelData data;
    ModelData* m_data = &data;
};

struct CM2Model {
    CM2Shared shared;
    CM2Shared* m_shared = &shared;
    bool instanceReady = false;
    bool sharedReady = false;
    bool failed = false;
    bool waitFails = false;
    bool hasAttachment = false;
    int waits = 0;
    std::function<void()> completed;

    bool HasLoadFailed() const { return failed; }
    bool IsLoaded() const { return instanceReady; }
    bool IsLoaded(int, int) const { return sharedReady; }
    bool WaitForLoad(const char*) {
        assert(sharedReady);
        ++waits;
        failed = waitFails;
        instanceReady = !failed;
        if (completed) {
            completed();
        }
        return instanceReady;
    }

    void GetSequenceInfo(uint32_t, uint32_t, M2SequenceInfo&) {
        assert(instanceReady && !failed);
    }
    bool HasAttachment(uint32_t) const {
        assert(instanceReady && !failed);
        return hasAttachment;
    }
    C3Vector GetAttachmentPivot(uint32_t) const {
        assert(instanceReady && !failed);
        return {1.0f};
    }
    bool HasSequence(uint32_t id) const {
        assert(instanceReady && !failed);
        return id == 0;
    }
    CAaSphere GetBoundingSphere() const {
        assert(instanceReady && !failed);
        return {4.0f};
    }
};

constexpr uint32_t TYPE_UNIT = 1;
constexpr uint32_t TYPE_DYNAMICOBJECT = 2;

struct CGObject_C {
    struct ObjectFields { uint32_t m_type = 0; } fields;
    ObjectFields* m_obj = &fields;
    float m_scaleModifier = 1.0f;
    CM2Model* objectModel = nullptr;

    virtual CM2Model* GetObjectModel() const { return objectModel; }
    float GetScale() const { return 1.0f; }
};

struct CGUnit_C : CGObject_C {
    struct UnitFields { uint32_t m_unitFieldFlags = 0; } unitFields;
    UnitFields* m_unit = &unitFields;
    CM2Model* m_worldModel = nullptr;
    CM2Model* m_mountModel = nullptr;
    float m_mountScale = 1.0f;

    CGUnit_C() { fields.m_type = TYPE_UNIT; }
    CM2Model* GetObjectModel() const override { return m_mountModel ? m_mountModel : m_worldModel; }
    int GetClientStandState() const { return 0; }
    bool IsActivePlayer() const { return true; }
    float GetMaxCameraHeight() const { return 15.0f; }
};

struct CGBarberShop {
    static constexpr bool m_barberShopEnabled = false;
};

static struct { int m_intValue = 0; } cameraOptions;
static auto* s_cvCameraHeightIgnoreStandState = &cameraOptions;
static uint32_t OsGetAsyncTimeMs() { return 0; }

struct CGCamera {
    uint32_t m_state = 0;
    float m_targetHeightNear = 5.0f;
    float m_targetHeightFar = 5.0f;
    float m_targetHeightSwim = 5.0f;
    int heightUpdates = 0;
    int liquidUpdates = 0;
    int32_t FinishLoadingTarget(CGObject_C*);
    void UpdateTargetHeight(CGObject_C*, uint32_t) { ++heightUpdates; }
    void UpdateLiquidSurfaceStatus(CGObject_C*) { ++liquidUpdates; }
};

#include "camera_target.inc"

int main() {
    CGUnit_C target;
    CM2Model rider;
    target.m_worldModel = &rider;
    CGCamera camera;

    assert(camera.FinishLoadingTarget(&target) == 0 && rider.waits == 0);
    assert(camera.m_state == 0 && camera.m_targetHeightNear == 5.0f);
    assert(camera.heightUpdates == 0 && camera.liquidUpdates == 0);

    rider.sharedReady = true;
    rider.waitFails = true;
    assert(camera.FinishLoadingTarget(&target) == 0 && rider.waits == 1);
    rider.m_shared = nullptr;
    assert(camera.FinishLoadingTarget(&target) == 1 && rider.waits == 1);
    assert(camera.m_state == 4 && camera.m_targetHeightNear == 2.0f);
    assert(camera.m_targetHeightFar == 2.0f && camera.m_targetHeightSwim == 2.0f);
    assert(camera.heightUpdates == 1 && camera.liquidUpdates == 1);

    CM2Model mount;
    mount.instanceReady = true;
    mount.hasAttachment = true;
    CM2Model mountedRider;
    target.m_mountModel = &mount;
    target.m_worldModel = &mountedRider;
    CGCamera mountedCamera;
    assert(mountedCamera.FinishLoadingTarget(&target) == 0 && mountedCamera.m_state == 0);

    mountedRider.sharedReady = true;
    assert(mountedCamera.FinishLoadingTarget(&target) == 1 && mountedRider.waits == 1);
    assert(std::fabs(mountedCamera.m_targetHeightNear - 3.7f) < 0.001f);

    mount.failed = true;
    mount.m_shared = nullptr;
    assert(mountedCamera.FinishLoadingTarget(&target) == 1);
    assert(std::fabs(mountedCamera.m_targetHeightNear - 2.7f) < 0.001f);

    CM2Model replacement;
    replacement.instanceReady = true;
    CM2Model loadingMount;
    loadingMount.sharedReady = true;
    loadingMount.completed = [&] { target.m_mountModel = &replacement; };
    target.m_mountModel = &loadingMount;
    CGCamera changedCamera;
    assert(changedCamera.FinishLoadingTarget(&target) == 0 && loadingMount.waits == 1);
    assert(changedCamera.m_state == 0 && changedCamera.heightUpdates == 0);

    CM2Model loadingRider;
    loadingRider.sharedReady = true;
    loadingRider.completed = [&] { target.m_worldModel = &replacement; };
    target.m_worldModel = &loadingRider;
    assert(changedCamera.FinishLoadingTarget(&target) == 0 && loadingRider.waits == 1);
    assert(changedCamera.FinishLoadingTarget(&target) == 1);

    CGObject_C dynamicTarget;
    dynamicTarget.fields.m_type = TYPE_DYNAMICOBJECT;
    dynamicTarget.objectModel = &replacement;
    CGCamera dynamicCamera;
    assert(dynamicCamera.FinishLoadingTarget(&dynamicTarget) == 1);
    assert(std::fabs(dynamicCamera.m_targetHeightNear - 3.96f) < 0.001f);
    dynamicTarget.objectModel = &rider;
    assert(dynamicCamera.FinishLoadingTarget(&dynamicTarget) == 1);
    assert(dynamicCamera.m_targetHeightNear == 2.0f);
}
