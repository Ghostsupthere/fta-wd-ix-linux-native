// Native scene/lifecycle adapter; also compiled by the native API fixture.
namespace domino = wow::ec::physics;
namespace phys = wxl::scripts::itemphysics;
struct WxlItemPhysicsState {
    const M2Data* data = nullptr;
    C44Matrix* palette = nullptr;
    CM2Scene* scene = nullptr;
    CM2Model* parent = nullptr;
    uint32_t attachment = 0, clock = 0, realClock = 0, clockChangedReal = 0, retryAt = 0;
    uint64_t generation = 0;
    ModernPhysicsLoadResult loaded = ModernPhysicsLoadResult::Missing;
    bool checked = false, applied = false, initialized = false, idleAdvanced = false;
    domino::Model definition;
    std::unique_ptr<domino::Instance> solver;
    std::vector<domino::V3> pivots;
    std::vector<int16_t> parents;
    std::vector<float> nativePose;
};

namespace {
bool SamePalette(const CM2Model* model, const WxlItemPhysicsState& state) {
    return model->m_shared && model->m_shared->m_data == state.data && model->m_boneMatrices == state.palette &&
        state.data && state.data->m_bones.m_count * 16 == state.nativePose.size();
}
void Restore(CM2Model* model) {
    auto* state = model->m_wxlItemPhysics;
    if (!state || !state->applied) return;
    if (SamePalette(model, *state))
        std::memcpy(model->m_boneMatrices, state->nativePose.data(), state->nativePose.size() * sizeof(float));
    state->applied = false;
}
bool ReadOriginal(const char* path, std::vector<uint8_t>& bytes) {
    SFileImpl* file = nullptr;
    if (!SFile::OpenEx(nullptr, path, 0, &file) || !file) return false;
    struct Close { SFileImpl* file; ~Close() { SFileCloseFile(file); } } close{file};
    uint32_t high = 0;
    const uint32_t size = SFileGetFileSize(file, &high);
    if (high || !size || size > 64u * 1024u * 1024u) return false;
    bytes.resize(size);
    uint32_t got = 0;
    return SFileReadFile(file, bytes.data(), size, &got, nullptr, 0) && got == size;
}
bool ValidMatrix(const float* matrix) {
    for (uint32_t i = 0; i < 16; ++i) if (!std::isfinite(matrix[i])) return false;
    const double determinant = double(matrix[0]) * (double(matrix[5])*matrix[10]-double(matrix[6])*matrix[9]) -
        double(matrix[1]) * (double(matrix[4])*matrix[10]-double(matrix[6])*matrix[8]) +
        double(matrix[2]) * (double(matrix[4])*matrix[9]-double(matrix[5])*matrix[8]);
    return std::abs(determinant) >= 1.e-10;
}
// Equipment policy from the supplied ItemPhysics.cpp; decoder fields stay raw.
void Tune(domino::Model& model, const std::string& path) {
    const bool weapon = path.starts_with("item\\objectcomponents\\weapon\\") ||
        path.starts_with("item\\objectcomponents\\shield\\");
    const bool belt = path.starts_with("item\\objectcomponents\\waist\\");
    const bool shoulder = path.starts_with("item\\objectcomponents\\shoulder\\");
    model.weightedWeapon = weapon;
    model.stableArmor = !weapon;
    model.slidingShoulder = shoulder;
    for (auto& shape : model.shapes) {
        shape.restitution *= .8f;
        if (!weapon) shape.friction *= shoulder ? .4f : .8f;
    }
    for (auto& body : model.bodies) {
        if (!weapon && body.type == domino::kBodyKinematic)
            body.carry = 1.f - (1.f - std::clamp(body.carry, 0.f, 1.f)) * .8f;
        if (body.type != domino::kBodyDynamic) continue;
        if (weapon) body.gravity *= 1.104f;
        body.linDamp = weapon ? (body.linDamp / .8f + 1.f) * .95f : body.linDamp / .8f + .2f;
        body.angDamp = weapon ? (body.angDamp / .8f + 1.f) * .95f : body.angDamp / .8f + .2f;
    }
    for (auto& joint : model.joints) {
        if (joint.type == domino::kJointWeld) {
            joint.f[2] = 0;
            if (weapon) joint.f[1] = std::max(joint.f[1] / .8f * .95f, 1.f);
            else {
                if (joint.f[0] > 0) joint.f[0] *= belt ? 1.15f : 1.1f;
                joint.f[1] = std::max(joint.f[1] / .8f, belt ? 1.f : .8f);
                joint.f[3] = std::max(joint.f[3] / .8f, .8f);
            }
        } else if (!weapon && joint.type == domino::kJointShoulder)
            joint.f[5] = std::max(joint.f[5] / .8f, .2f);
        else if (!weapon && joint.type == domino::kJointRevolute)
            joint.f[4] = std::max(joint.f[4] / .8f, .2f);
    }
}

// The test NormPath would make, without copying the path: most world doodads are not items.
bool HasItemPrefix(const char* path) {
    static constexpr char prefix[] = "item\\";
    if (!path) return false;
    for (size_t i = 0; i + 1 < sizeof(prefix); ++i) {
        char c = path[i];
        if (!c) return false;
        if (c == '/') c = '\\';
        if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
        if (c != prefix[i]) return false;
    }
    return true;
}

void Apply(CM2Model* model, CM2Scene* scene) {
    if (!(model->m_fFlags & 1) || !model->m_shared || !model->m_shared->m_data || !model->m_boneMatrices) return;
    const M2Data* data = model->m_shared->m_data;
    const uint32_t count = data->m_bones.m_count;
    if (!HasItemPrefix(model->m_shared->m_fileName)) return;
    char name[1024];
    if (!count || count > 256 || !domino::NormPath(model->m_shared->m_fileName, name, sizeof(name)) ||
        !std::string_view(name).starts_with("item\\")) return;
    const bool enabled = WxlProfileEnabled("WXL_ITEM_PHYSICS", true) &&
        (!std::string_view(name).starts_with("item\\objectcomponents\\waist\\") ||
         WxlProfileEnabled("WXL_ITEM_PHYSICS_BELTS", true));
    auto* state = model->m_wxlItemPhysics;
    if (!enabled) { RetireNativeItemPhysics(model); return; }
    if (state && (state->data != data || state->palette != model->m_boneMatrices || state->scene != scene ||
        state->parent != model->m_attachParent || state->attachment != model->m_attachmentId ||
        state->generation != WxlEquipmentGeneration())) {
        RetireNativeItemPhysics(model);
        state = nullptr;
    }
    const uint32_t now = OsGetAsyncTimeMs();
    if (!state) {
        auto created = std::make_unique<WxlItemPhysicsState>();
        created->data = data; created->palette = model->m_boneMatrices; created->scene = scene;
        created->parent = model->m_attachParent; created->attachment = model->m_attachmentId;
        created->generation = WxlEquipmentGeneration();
        created->clock = scene->m_time; created->realClock = created->clockChangedReal = now;
        state = model->m_wxlItemPhysics = created.release();
    }
    if (!state->checked || (state->loaded == ModernPhysicsLoadResult::PendingFileDataId &&
        static_cast<int32_t>(now - state->retryAt) >= 0)) {
        std::string error;
        uint32_t pending = 0;
        state->loaded = LoadModernPhysicsAsset(name, ReadOriginal, ResolveModernAssetFileDataId,
                                               state->definition, error, pending);
        state->checked = true;
        state->retryAt = now + 1000;
        if (state->loaded == ModernPhysicsLoadResult::Loaded) {
            state->pivots.clear(); state->parents.clear();
            for (uint32_t i = 0; i < count; ++i) {
                const auto& bone = data->m_bones[i];
                state->pivots.push_back({bone.m_pivot.x, bone.m_pivot.y, bone.m_pivot.z});
                state->parents.push_back(static_cast<int16_t>(bone.m_parentIndex));
            }
            bool valid = true;
            for (uint32_t i = 0; i < count && valid; ++i) {
                const auto& pivot = state->pivots[i];
                valid = std::isfinite(pivot.x) && std::isfinite(pivot.y) && std::isfinite(pivot.z);
                uint32_t hops = 0;
                for (int parent = state->parents[i]; valid && parent >= 0; parent = state->parents[parent]) {
                    if (static_cast<uint32_t>(parent) >= count || ++hops >= count) { valid = false; break; }
                }
            }
            if (!valid || !phys::BindSkeleton(state->definition, state->pivots, state->parents, error)) {
                state->loaded = ModernPhysicsLoadResult::Rejected;
                if (!valid) error = "invalid native skeleton hierarchy/pivots";
            } else {
                Tune(state->definition, name);
                state->solver = std::make_unique<domino::Instance>(&state->definition);
                WxlNativeDiagnostic("item-physics: loaded %s bodies=%zu joints=%zu", name,
                                    state->definition.bodies.size(), state->definition.joints.size());
                // PHYS needs every native bone, including ordinarily static rigs.
                if (model->m_fFlags & 0x1000) { model->m_fFlags &= ~0x1000u; return; }
            }
        }
        if (state->loaded == ModernPhysicsLoadResult::Rejected)
            WxlNativeDiagnostic("item-physics: rejected %s: %s", name, error.c_str());
    }
    if (!state->solver || state->loaded != ModernPhysicsLoadResult::Loaded) return;
    static_assert(sizeof(C44Matrix) == 16 * sizeof(float));
    const float* native = reinterpret_cast<const float*>(model->m_boneMatrices);
    const float* placement = reinterpret_cast<const float*>(&model->m_matrixF4);
    const float* sceneToWorld = reinterpret_cast<const float*>(&scene->m_viewInv);
    if (!ValidMatrix(placement) || !ValidMatrix(sceneToWorld)) return;
    for (uint32_t i = 0; i < count * 16; ++i) if (!std::isfinite(native[i])) return;
    state->nativePose.assign(native, native + count * 16);
    std::vector<std::array<double, 16>> world(count);
    for (uint32_t i = 0; i < count; ++i) domino::World(native + i * 16, sceneToWorld, world[i].data());
    domino::Frame frame;
    frame.boneWorld = reinterpret_cast<const double (*)[16]>(world.data());
    frame.pivots = state->pivots.data(); frame.boneCount = count;
    domino::World(placement, sceneToWorld, frame.placement);
    const uint32_t delta = scene->m_time - state->clock;
    if (delta > 0x7FFFFFFF || now - state->realClock > 2000) {
        state->solver = std::make_unique<domino::Instance>(&state->definition);
        state->clockChangedReal = now;
        state->realClock = now;
        state->idleAdvanced = false;
    } else if (state->initialized) {
        if (delta) {
            frame.dt = float(delta) / 1000.f;
            state->clockChangedReal = now;
            // The resumed native clock includes already-integrated idle time.
            if (state->idleAdvanced) frame.dt = std::min(frame.dt, float(now - state->realClock) / 1000.f);
            state->idleAdvanced = false;
        } else if (now - state->clockChangedReal >= 100) {
            frame.dt = float(now - state->realClock) / 1000.f;
            if (frame.dt > 0) state->idleAdvanced = true;
        }
    }
    state->clock = scene->m_time;
    if (frame.dt > 0 || !state->initialized) state->realClock = now;
    state->initialized = true;
    state->solver->Step(frame);
    auto output = state->nativePose;
    std::array<bool, 256> moved{};
    for (size_t i = 0; i < state->definition.bodies.size(); ++i) {
        const uint32_t bone = state->definition.bodies[i].bone;
        double simulated[16];
        if (bone < count && state->solver->Output(i, simulated)) {
            domino::FromModel(simulated, placement, output.data() + bone * 16);
            moved[bone] = true;
        }
    }
    domino::Carry(state->parents.data(), count, state->nativePose.data(), output.data(), moved.data());
    for (float value : output) if (!std::isfinite(value)) {
        state->solver.reset(); state->loaded = ModernPhysicsLoadResult::Rejected;
        WxlNativeDiagnostic("item-physics: non-finite simulation rejected for %s", name);
        return;
    }
    std::memcpy(model->m_boneMatrices, output.data(), output.size() * sizeof(float));
    state->applied = true;
}

} // namespace

void RetireNativeItemPhysics(CM2Model* model) {
    Restore(model);
    delete model->m_wxlItemPhysics;
    model->m_wxlItemPhysics = nullptr;
}
void RestoreNativeItemPhysics(CM2Scene* scene) { WxlVisitSceneModels(scene, [](CM2Model* model) { Restore(model); }); }
void ApplyNativeItemPhysics(CM2Scene* scene) { WxlVisitSceneModels(scene, [scene](CM2Model* model) { Apply(model, scene); }); }
