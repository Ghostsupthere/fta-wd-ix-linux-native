#include "Engine/Source/ModernAssets/WxlGameObject/GameObjectPreviewState.hpp"

#include <cstdlib>
#include <iostream>
#include <limits>
#include <map>
#include <vector>

using namespace wxl::scripts::gameobjecteditor::previewdetail;

static void Check(bool condition)
{
    if (!condition)
        std::abort();
}

static Matrix Placement(float scale, float x = 0.0f)
{
    return {scale, 0, 0, 0, 0, scale, 0, 0, 0, 0, scale, 0, x, 2, 3, 1};
}

static LiveTransform Object(uint64_t guid, uintptr_t owner, uintptr_t model, float scale = 2.0f)
{
    LiveTransform live{};
    live.binding.guid = guid;
    live.binding.owner = owner;
    live.binding.model = model;
    live.binding.handler = owner + 1;
    live.binding.resource = model + 1;
    live.objectMatrix = Placement(scale);
    live.renderMatrix = Placement(scale);
    return live;
}

struct Backend
{
    std::map<uint64_t, LiveTransform> objects;
    std::vector<Binding> writes;
    bool unavailable = false;

    std::optional<LiveTransform> Resolve(uint64_t guid)
    {
        auto found = objects.find(guid);
        if (unavailable || found == objects.end())
            return std::nullopt;
        return found->second;
    }

    bool Write(LiveTransform const& live, Matrix const& objectMatrix, Matrix const& renderMatrix)
    {
        auto current = Resolve(live.binding.guid);
        Check(current && current->binding == live.binding);
        writes.push_back(live.binding);
        objects.at(live.binding.guid).objectMatrix = objectMatrix;
        objects.at(live.binding.guid).renderMatrix = renderMatrix;
        return true;
    }
};

int main()
{
    Matrix output;
    Matrix desired = Placement(1.0f, 10.0f);
    // 90-degree yaw; nonuniform original scale must survive all three basis rows.
    desired[0] = 0; desired[1] = 1; desired[4] = -1; desired[5] = 0;
    Matrix original = Placement(2.0f);
    original[5] = 3; original[10] = 4;
    Check(PreserveScale(original, desired, output));
    Check(output[0] == 0 && output[1] == 2 && output[4] == -3 && output[10] == 4);
    Check(output[12] == 10 && output[13] == 2 && output[14] == 3);
    original[0] = -2;
    Check(PreserveScale(original, desired, output) && Determinant(output) < 0);

    Matrix invalid = desired;
    invalid[12] = 17067;
    Check(!PreserveScale(original, invalid, output));
    invalid = desired; invalid[0] = std::numeric_limits<float>::infinity();
    Check(!PreserveScale(original, invalid, output));
    invalid = desired; invalid[0] = std::numeric_limits<float>::quiet_NaN();
    Check(!PreserveScale(original, invalid, output));
    invalid = desired; invalid[0] = 1; invalid[1] = 0; invalid[4] = 1; invalid[5] = 0.000001f;
    Check(!PreserveScale(original, invalid, output));
    invalid = desired; invalid[8] = invalid[9] = invalid[10] = 0;
    Check(!PreserveScale(original, invalid, output));

    Backend backend;
    backend.objects.emplace(1, Object(1, 100, 200));
    LiveTransform const baseline = backend.objects.at(1);
    State state;
    Check(!state.Apply(1, invalid, backend) && backend.writes.empty());
    Check(state.Apply(1, desired, backend));
    Check(backend.writes.size() == 1 && backend.objects.at(1).renderMatrix[1] == 2);
    Check(state.Apply(1, desired, backend) && backend.writes.size() == 1); // stationary means no dirty write

    // A native refresh overwriting the preview is corrected on the next selected-editor callback.
    backend.objects.at(1).renderMatrix = baseline.renderMatrix;
    Check(state.Apply(1, desired, backend) && backend.writes.size() == 2);
    state.Restore(backend);
    Check(backend.writes.size() == 3);
    Check(SameMatrix(backend.objects.at(1).objectMatrix, baseline.objectMatrix));
    Check(SameMatrix(backend.objects.at(1).renderMatrix, baseline.renderMatrix));

    Check(state.Apply(1, desired, backend));
    backend.unavailable = true;
    Check(!state.Apply(1, desired, backend));
    backend.unavailable = false;
    state.Restore(backend); // a transient load failure must retain the original snapshot
    Check(SameMatrix(backend.objects.at(1).renderMatrix, baseline.renderMatrix));

    Check(state.Apply(1, desired, backend));
    size_t count = backend.writes.size();
    backend.objects.erase(1);
    backend.objects.emplace(2, Object(2, 100, 200)); // addresses reused for another GUID
    state.Restore(backend);
    Check(backend.writes.size() == count);
    Check(SameMatrix(backend.objects.at(2).renderMatrix, Placement(2.0f)));

    Check(state.Apply(2, desired, backend));
    backend.objects.at(2) = Object(2, 300, 400); // same GUID, replaced owner
    count = backend.writes.size();
    state.Restore(backend);
    Check(backend.writes.size() == count);
    Check(SameMatrix(backend.objects.at(2).renderMatrix, Placement(2.0f)));

    Check(state.Apply(2, desired, backend));
    backend.objects.at(2).binding.model = 500;
    backend.objects.at(2).binding.resource = 501;
    backend.objects.at(2).renderMatrix = Placement(3.0f, 10.0f);
    Check(state.Apply(2, desired, backend)); // surviving owner, reloaded model with new scale
    state.Restore(backend);
    Check(SameMatrix(backend.objects.at(2).objectMatrix, Placement(2.0f)));
    Check(SameMatrix(backend.objects.at(2).renderMatrix, Placement(3.0f)));

    Check(state.Apply(2, desired, backend));
    backend.objects.emplace(3, Object(3, 600, 700));
    Check(state.Apply(3, desired, backend)); // selection switch restores the prior selected GO
    Check(SameMatrix(backend.objects.at(2).renderMatrix, Placement(3.0f)));
    count = backend.writes.size();
    state.Forget();
    state.Restore(backend); // world-leave path must perform no stale writes
    Check(backend.writes.size() == count);

    std::cout << "GameObject preview: scale, bounds, idempotence, restore, model reload and GUID/pointer reuse passed\n";
}
