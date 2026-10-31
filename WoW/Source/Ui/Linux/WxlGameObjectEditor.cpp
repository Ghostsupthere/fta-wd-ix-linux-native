// Native port of the supplied wxl-gameobject-editor (GPL-3.0-or-later, Copyright (C) 2026
// WarcraftXL). The Lua contract, the gesture state machine and the preview lifecycle follow
// scripts/wxl-gameobject-editor; the Win32 window-message feed, the EndScene detour and the
// fixed-address object writes are replaced as described in WxlGameObjectEditor.h.
#include "WoW/Source/Ui/Linux/WxlGameObjectEditor.h"

#if defined(WOWDECOMP_LINUX)

// Match the existing native D3D9 translation unit's Windows type boundary.
#define WOWDECOMP_GX_DXVK
#include <d3d9.h>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <optional>
#include <string>
#include <strings.h>

#include "Engine/Source/FrameScript/FrameScript.h"
#include "Engine/Source/Model2/M2Data.h"
#include "Engine/Source/Model2/M2Model.h"
#include "Engine/Source/Model2/M2Shared.h"
#include "Engine/Source/ModernAssets/NativeDiagnostic.h"
#include "Engine/Source/ModernAssets/NativeVisualHooks.h"
#include "Engine/Source/ModernAssets/WxlGameObject/GameObjectPreviewState.hpp"
#include "Engine/Source/Tempest/CAaSphere.h"
#include "Engine/Source/Tempest/CMath.h"
#include "Engine/Source/lua-5.1/src/lauxlib.h"
#include "Engine/Source/lua-5.1/src/lua.h"
#include "WoW/Source/Object/ObjectClient/GameObject_C.h"
#include "WoW/Source/ObjectMgrClient/ObjectMgrClient.h"
#include "WoW/Source/WorldClient/Linux/WxlVisualEnvironment.h"
#include "WoW/Source/WorldClient/CWorldMath.h"
#include "WoW/Source/WorldClient/Map.h"
#include "WoW/Source/WorldClient/MapObjDef.h"
#include "WoW/Source/WorldClient/MapDoodadDef.h"
#include "WoW/Source/WorldClient/World.h"
#include "WoW/Source/WorldClient/WorldScene.h"

#include "imgui.h"
#include "imgui_impl_dx9.h"
#include "ImGuizmo.h"

namespace preview = wxl::scripts::gameobjecteditor::previewdetail;

namespace {

constexpr const char* kVersion = "1.4.0";
constexpr size_t kMaxTokenLength = 128;
constexpr float kGizmoReferenceHeight = 1080.0f;
constexpr float kMaxGizmoPixelScale = 2.0f;

// --- the native object backend --------------------------------------------------------------

// The resolved parts of one live GameObject. Pointers are identity tokens for the preview state;
// every use resolves the object afresh.
struct LiveObject {
    CGGameObject_C* object = nullptr;
    CM2Model* model = nullptr;            // M2 objects
    CMapEntity* entity = nullptr;         // world-object scene (WMO or doodad) the type owns
    preview::LiveTransform transform{};
};

preview::Matrix ToMatrix(const C44Matrix& matrix) {
    preview::Matrix result;
    std::memcpy(result.data(), &matrix, sizeof(result));
    return result;
}

C44Matrix ToEngine(const preview::Matrix& matrix) {
    C44Matrix result;
    std::memcpy(&result, matrix.data(), sizeof(result));
    return result;
}

CGGameObject_C* ResolveObject(uint64_t guid) {
    if (!guid) return nullptr;
    auto* object = static_cast<CGGameObject_C*>(ClntObjMgrObjectPtr(WOWGUID{guid}, TYPE_GAMEOBJECT, __FILE__, __LINE__));
    return object && object->m_obj && object->m_obj->m_guid.m_guid == guid ? object : nullptr;
}

std::optional<LiveObject> ResolveLive(uint64_t guid) {
    CGGameObject_C* object = ResolveObject(guid);
    if (!object || !object->m_typeData) return std::nullopt;

    LiveObject live;
    live.object = object;
    preview::Binding& binding = live.transform.binding;
    binding.guid = guid;
    binding.owner = reinterpret_cast<uintptr_t>(object);
    binding.handler = reinterpret_cast<uintptr_t>(object->m_typeData);
    live.transform.objectMatrix = ToMatrix(object->m_matrix);

    if (CM2Model* model = object->m_worldModel) {
        // A model that is still loading has no bounds to update; wait for it like the Windows
        // module waits for the resource's loaded flag.
        if (!model->m_shared || !(model->m_shared->m_flags & 1) || !model->m_shared->m_data) return std::nullopt;
        live.model = model;
        live.entity = object->m_worldObject;
        binding.kind = preview::ModelKind::M2;
        binding.model = reinterpret_cast<uintptr_t>(model);
        binding.resource = reinterpret_cast<uintptr_t>(model->m_shared);
        binding.payload = reinterpret_cast<uintptr_t>(model->m_shared->m_data);
        binding.entity = reinterpret_cast<uintptr_t>(live.entity);
        live.transform.renderMatrix = ToMatrix(model->m_worldTransform);
    } else {
        // Types that own a map-object scene (WMO or scene doodad) are moved through the world.
        auto* owner = dynamic_cast<CGGameObject_C_Type_WorldObj*>(object->m_typeData);
        CMapEntity* entity = owner ? owner->m_worldObject : nullptr;
        if (!entity) return std::nullopt;
        live.entity = entity;
        binding.model = reinterpret_cast<uintptr_t>(entity);
        auto* base = reinterpret_cast<CMapBaseObj*>(entity);
        if (base->m_type & CMapBaseObj::Type_MapObjDef) {
            auto* def = static_cast<CMapObjDef*>(base);
            binding.kind = preview::ModelKind::Wmo;
            binding.resource = reinterpret_cast<uintptr_t>(def->m_owner);
            live.transform.renderMatrix = ToMatrix(def->m_mat);
        } else if (base->m_type & CMapBaseObj::Type_DoodadDef) {
            auto* def = static_cast<CMapDoodadDef*>(base);
            binding.kind = preview::ModelKind::SceneM2;
            binding.resource = reinterpret_cast<uintptr_t>(def->m_model);
            live.transform.renderMatrix = ToMatrix(def->m_mat);
        } else {
            return std::nullopt;
        }
        if (!binding.resource) return std::nullopt;
    }

    if (!preview::ValidMatrix(live.transform.objectMatrix) || !preview::ValidMatrix(live.transform.renderMatrix)) {
        return std::nullopt;
    }
    return live;
}

class NativeBackend {
public:
    std::optional<preview::LiveTransform> Resolve(uint64_t guid) const {
        auto live = ResolveLive(guid);
        if (!live) return std::nullopt;
        return live->transform;
    }

    bool Write(const preview::LiveTransform& saved, const preview::Matrix& objectMatrix, const preview::Matrix& renderMatrix) const {
        // Resolve again before the first write; saved pointers alone never authorize a mutation.
        auto live = ResolveLive(saved.binding.guid);
        if (!live || !(live->transform.binding == saved.binding)) return false;

        C44Matrix object = ToEngine(objectMatrix);
        C44Matrix render = ToEngine(renderMatrix);
        live->object->m_matrix = object;

        if (live->transform.binding.kind == preview::ModelKind::M2) {
            CM2Model* model = live->model;
            // The same inputs CGGameObject_C::UpdateWorldObject feeds the map, without recomputing
            // the matrix from the authoritative position fields.
            CAaBox bounds = model->m_shared->m_data->m_collisionBounds.m_extent;
            C44Matrix transform = object;
            CWorldMath::TransformAABox(transform, bounds, live->object->m_worldBounds);
            CAaBox box = model->GetBoundingBox();
            CAaSphere sphere = model->GetBoundingSphere();
            C3Vector center((bounds.t.x + bounds.b.x) * 0.5f, (bounds.t.y + bounds.b.y) * 0.5f, 0.5f * (bounds.t.z + bounds.b.z));
            model->m_fFlags |= 0x8000;
            model->m_worldTransform = render;
            if (live->entity) CMap::ObjectUpdate(live->entity, object, box, sphere, center, 1, 0xFFFFFFFF);
        } else {
            // Refreshes the WMO render and inverse matrices, attached doodads, collision and
            // spatial bounds (or the scene doodad's transform and bounds).
            World::ObjectUpdate(live->entity, render);
        }
        return true;
    }
};

// --- Lua argument helpers -------------------------------------------------------------------

bool ReadString(lua_State* L, int index, std::string& output) {
    if (lua_type(L, index) != LUA_TSTRING) return false;
    size_t length = 0;
    const char* value = lua_tolstring(L, index, &length);
    if (!value || length == 0 || length > kMaxTokenLength || std::memchr(value, '\0', length)) return false;
    output.assign(value, length);
    return true;
}

bool ReadNumber(lua_State* L, int index, float& output) {
    if (lua_type(L, index) != LUA_TNUMBER) return false;
    const double value = lua_tonumber(L, index);
    constexpr double kMax = std::numeric_limits<float>::max();
    if (!std::isfinite(value) || value < -kMax || value > kMax) return false;
    output = static_cast<float>(value);
    return true;
}

bool ReadRuntimeGuid(lua_State* L, int index, uint64_t& output) {
    std::string value;
    if (!ReadString(L, index, value) || value.size() != 16) return false;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), output, 16);
    return parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size() && output != 0;
}

bool IsFinite(float value) { return std::isfinite(value) != 0; }

bool IsValidPosition(float x, float y, float z) {
    // The server's map coordinate extent.
    constexpr float kMapCoordinateLimit = 533.33333f * 32.0f - 0.5f;
    return IsFinite(x) && IsFinite(y) && IsFinite(z) && std::abs(x) <= kMapCoordinateLimit &&
           std::abs(y) <= kMapCoordinateLimit && std::abs(z) <= kMapCoordinateLimit;
}

float NormalizeDegrees(float value) {
    if (!IsFinite(value)) return 0.0f;
    value = std::remainder(value, 360.0f);
    return value == -0.0f ? 0.0f : value;
}

class ScopedImGuiContext {
public:
    explicit ScopedImGuiContext(ImGuiContext* context) : previous_(ImGui::GetCurrentContext()) { ImGui::SetCurrentContext(context); }
    ~ScopedImGuiContext() { ImGui::SetCurrentContext(previous_); }

private:
    ImGuiContext* previous_;
};

// --- the editor -----------------------------------------------------------------------------

enum class Tool { Translate, Rotate };
enum class Space { World, Local };
enum class Axis { All, X, Y, Z };

struct Transform { float x = 0, y = 0, z = 0, yaw = 0, pitch = 0, roll = 0; };

struct Selection {
    std::string identity;
    std::string nonce;
    Transform transform;
    uint64_t runtimeGuid = 0;
};

struct PendingPoll {
    bool valid = false;
    std::string phase;
    uint64_t sequence = 0;
    Selection selection;
};

class Editor {
public:
    bool Enabled() const { return enabled_; }
    void Register();
    void OnWorldLeave() { ClearSelection(); }

    int LuaSync(lua_State* L);
    int LuaClear(lua_State* L);
    int LuaSetTool(lua_State* L);
    int LuaSetSpace(lua_State* L);
    int LuaSetAxis(lua_State* L);
    int LuaSetSnap(lua_State* L);
    int LuaTransform(lua_State* L);
    int LuaPoll(lua_State* L);

    void Draw(void* device);
    void DeviceLost();
    bool MouseInput(int kind, int button, int x, int y);

private:
    bool EnsureInit(IDirect3DDevice9* device);
    void DrawGizmo(const ImGuiIO& io);
    void RebuildMatrix();
    void ReadManipulatedMatrix();
    void ApplySelection(const Selection& selection, bool preservePending);
    void ApplyDeferredSync();
    void ResetCapture();
    void ReleaseMouseButtons();
    void ClearSelection();
    void Publish(const char* phase);
    bool ApplyPreview() { return state_.Apply(selection_.runtimeGuid, ToMatrixArray(), backend_); }
    preview::Matrix ToMatrixArray() const {
        preview::Matrix matrix;
        std::copy_n(matrix_, matrix.size(), matrix.begin());
        return matrix;
    }
    void RestorePreview() { state_.Restore(backend_); }

    bool enabled_ = true;
    ImGuiContext* context_ = nullptr;
    IDirect3DDevice9* backendDevice_ = nullptr;
    bool initialized_ = false;
    bool warnedInit_ = false;
    std::chrono::steady_clock::time_point lastFrame_{};

    bool selected_ = false;
    Selection selection_;
    std::optional<Selection> deferredSync_;
    PendingPoll pending_;
    uint64_t nextSequence_ = 0;
    float matrix_[16] = {};

    Tool tool_ = Tool::Translate;
    Space space_ = Space::World;
    Axis axis_ = Axis::All;
    float positionSnap_ = 0.0f;
    float rotationSnap_ = 0.0f;

    bool gizmoHovered_ = false;
    bool gizmoUsing_ = false;
    bool dragging_ = false;
    bool clickCaptured_ = false;
    bool mouseUpPending_ = false;

    preview::State state_;
    NativeBackend backend_;
};

Editor g_editor;

// ImGui button index for an SDL button number (1 left, 2 middle, 3 right, then the extras).
int ImGuiButton(int sdlButton) {
    switch (sdlButton) {
        case 1: return 0;
        case 2: return 2;
        case 3: return 1;
        case 4: return 3;
        case 5: return 4;
        default: return -1;
    }
}

bool BackBufferSize(IDirect3DDevice9* device, float& width, float& height) {
    IDirect3DSwapChain9* chain = nullptr;
    if (FAILED(device->GetSwapChain(0, &chain)) || !chain) return false;
    D3DPRESENT_PARAMETERS parameters{};
    const bool ok = SUCCEEDED(chain->GetPresentParameters(&parameters));
    chain->Release();
    if (!ok || !parameters.BackBufferWidth || !parameters.BackBufferHeight) return false;
    width = static_cast<float>(parameters.BackBufferWidth);
    height = static_cast<float>(parameters.BackBufferHeight);
    return true;
}

void DrawHook(void* device) { g_editor.Draw(device); }
void DeviceLostHook() { g_editor.DeviceLost(); }
bool MouseHook(int kind, int button, int x, int y) { return g_editor.MouseInput(kind, button, x, y); }

bool Editor::EnsureInit(IDirect3DDevice9* device) {
    if (initialized_ && backendDevice_ == device) return true;
    if (!device) return false;

    if (context_) {
        // A full device replacement is distinct from a reset on the same pointer: release the old
        // backend reference and bind the new device before the next frame.
        ScopedImGuiContext scope(context_);
        if (backendDevice_) ImGui_ImplDX9_Shutdown();
        backendDevice_ = nullptr;
        initialized_ = false;
        if (!ImGui_ImplDX9_Init(device)) {
            if (!warnedInit_) { warnedInit_ = true; WxlNativeDiagnostic("gameobject-editor: DX9 backend rebind deferred"); }
            return false;
        }
        backendDevice_ = device;
        initialized_ = true;
        warnedInit_ = false;
        return true;
    }

    ImGuiContext* previous = ImGui::GetCurrentContext();
    context_ = ImGui::CreateContext();
    ImGui::SetCurrentContext(context_);
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    ImGui::StyleColorsDark();
    if (!ImGui_ImplDX9_Init(device)) {
        ImGui::DestroyContext(context_);
        context_ = nullptr;
        ImGui::SetCurrentContext(previous);
        std::fprintf(stderr, "WarcraftXL: game-object editor: ImGui backend initialization failed\n");
        return false;
    }
    ImGuizmo::SetImGuiContext(context_);
    backendDevice_ = device;
    initialized_ = true;
    warnedInit_ = false;
    lastFrame_ = std::chrono::steady_clock::now();
    ImGui::SetCurrentContext(previous);
    std::fprintf(stderr, "WarcraftXL: game-object editor: mouse gizmo renderer ready\n");
    return true;
}

void Editor::Register() {
    enabled_ = WxlProfileEnabled("WXL_GAMEOBJECT_EDITOR", true);
    if (!enabled_) return;
    FrameScript_RegisterFunction("WarcraftXL_GOEditorVersion", [](lua_State* L) -> int { lua_pushstring(L, kVersion); return 1; });
    FrameScript_RegisterFunction("WarcraftXL_GOEditorSync", [](lua_State* L) -> int { return g_editor.LuaSync(L); });
    FrameScript_RegisterFunction("WarcraftXL_GOEditorClear", [](lua_State* L) -> int { return g_editor.LuaClear(L); });
    FrameScript_RegisterFunction("WarcraftXL_GOEditorSetTool", [](lua_State* L) -> int { return g_editor.LuaSetTool(L); });
    FrameScript_RegisterFunction("WarcraftXL_GOEditorSetSpace", [](lua_State* L) -> int { return g_editor.LuaSetSpace(L); });
    FrameScript_RegisterFunction("WarcraftXL_GOEditorSetAxis", [](lua_State* L) -> int { return g_editor.LuaSetAxis(L); });
    FrameScript_RegisterFunction("WarcraftXL_GOEditorSetSnap", [](lua_State* L) -> int { return g_editor.LuaSetSnap(L); });
    FrameScript_RegisterFunction("WarcraftXL_GOEditorTransform", [](lua_State* L) -> int { return g_editor.LuaTransform(L); });
    FrameScript_RegisterFunction("WarcraftXL_GOEditorPoll", [](lua_State* L) -> int { return g_editor.LuaPoll(L); });
}

void Editor::Draw(void* rawDevice) {
    // ImGui's DX9 backend captures a full state block for every frame it renders; with nothing
    // selected the editor does no work at all.
    if (!selected_) return;
    auto* device = static_cast<IDirect3DDevice9*>(rawDevice);
    if (!EnsureInit(device) || !context_) return;

    float width = 0, height = 0;
    if (!BackBufferSize(device, width, height)) return;

    ScopedImGuiContext scope(context_);
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(width, height);
    const auto now = std::chrono::steady_clock::now();
    io.DeltaTime = std::max(std::chrono::duration<float>(now - lastFrame_).count(), 1.0f / 1000.0f);
    lastFrame_ = now;
    io.MouseDrawCursor = false;

    ImGui_ImplDX9_NewFrame();
    ImGui::NewFrame();
    ImGuizmo::BeginFrame();

    DrawGizmo(io);

    ImGui::Render();
    ImDrawData* drawData = ImGui::GetDrawData();
    if (drawData && drawData->CmdListsCount > 0 && drawData->TotalVtxCount > 0) {
        ImGui_ImplDX9_RenderDrawData(drawData);
    }
}

void Editor::DeviceLost() {
    if (!initialized_ || !context_) return;
    ScopedImGuiContext scope(context_);
    ImGui_ImplDX9_InvalidateDeviceObjects();
}

bool Editor::MouseInput(int kind, int button, int x, int y) {
    // An idle editor never touches its ImGui context or claims WoW's mouse state.
    if (!selected_ || !initialized_ || !context_) return false;

    ScopedImGuiContext scope(context_);
    ImGuiIO& io = ImGui::GetIO();
    if (kind == 0) {
        io.AddMousePosEvent(static_cast<float>(x), static_cast<float>(y));
        return false;  // WoW keeps owning the one visible cursor
    }
    if (kind == 3) {
        // Focus loss: queue the releases too, otherwise a lost mouse-up leaves the gizmo held.
        ReleaseMouseButtons();
        clickCaptured_ = false;
        mouseUpPending_ = true;
        return false;
    }

    const int imguiButton = ImGuiButton(button);
    if (imguiButton < 0) return false;
    io.AddMousePosEvent(static_cast<float>(x), static_cast<float>(y));
    io.AddMouseButtonEvent(imguiButton, kind == 1);

    const bool captureActive = clickCaptured_ || dragging_ || gizmoUsing_;
    if (imguiButton == 0 && kind == 1 && gizmoHovered_) {
        clickCaptured_ = true;
        mouseUpPending_ = false;
        return true;
    }
    if (imguiButton == 0 && kind == 2 && captureActive) {
        clickCaptured_ = false;
        mouseUpPending_ = true;
        return true;
    }
    return captureActive || gizmoHovered_;
}

void Editor::DrawGizmo(const ImGuiIO& io) {
    gizmoHovered_ = false;
    gizmoUsing_ = false;
    if (!selected_) return;

    // Only the selected editor object is resolved. An asynchronously loading or removed model is
    // never replaced with a nearby object.
    if (!ApplyPreview()) {
        if (dragging_) Publish("end");
        ResetCapture();
        return;
    }

    const float* view = reinterpret_cast<const float*>(&CWorldScene::viewMatrix);
    const float* projection = reinterpret_cast<const float*>(&CWorldScene::projMatrix);

    ImGuizmo::SetOrthographic(false);
    ImGuizmo::SetDrawlist(ImGui::GetBackgroundDrawList());
    ImGuizmo::SetRect(0.0f, 0.0f, io.DisplaySize.x, io.DisplaySize.y);
    ImGuizmo::SetID(0x57474F45);  // "WGOE"

    // Keep the visible stroke modest and the mouse target generous; an isolated rotation axis gets
    // its whole ring so it can be grabbed without hunting for a segment.
    ImGuizmo::Style& style = ImGuizmo::GetStyle();
    const ImGuizmo::Style savedStyle = style;
    const float pixelScale = std::clamp(io.DisplaySize.y / kGizmoReferenceHeight, 1.0f, kMaxGizmoPixelScale);
    const bool isolatedRotation = tool_ == Tool::Rotate && axis_ != Axis::All;
    style.RotationLineThickness = 3.0f * pixelScale;
    style.RotationOuterLineThickness = 3.0f * pixelScale;
    style.RotationLineHitRadius = (isolatedRotation ? 18.0f : 12.0f) * pixelScale;
    style.RotationOuterLineHitRadius = 8.0f * pixelScale;
    style.RotationFullAxisRings = isolatedRotation;
    ImGuizmo::SetGizmoSizeClipSpace(0.13f);
    ImGuizmo::SetAxisMask(isolatedRotation && axis_ == Axis::X, isolatedRotation && axis_ == Axis::Y, isolatedRotation && axis_ == Axis::Z);

    const ImGuizmo::OPERATION operation = tool_ == Tool::Translate ? ImGuizmo::TRANSLATE : ImGuizmo::ROTATE;
    const ImGuizmo::MODE mode = space_ == Space::World ? ImGuizmo::WORLD : ImGuizmo::LOCAL;
    const float step = tool_ == Tool::Translate ? positionSnap_ : rotationSnap_;
    const float snap[3] = {step, step, step};
    const bool modified = ImGuizmo::Manipulate(view, projection, operation, mode, matrix_, nullptr, step > 0.0f ? snap : nullptr);

    gizmoHovered_ = ImGuizmo::IsOver();
    gizmoUsing_ = ImGuizmo::IsUsing();
    style = savedStyle;

    if (gizmoUsing_) {
        if (modified) {
            ReadManipulatedMatrix();
            ApplyPreview();
        }
        if (!dragging_) {
            deferredSync_.reset();  // a new gesture supersedes any older server echo
            dragging_ = true;
            Publish("begin");
        } else if (modified) {
            Publish("update");
        }
        return;
    }

    if (dragging_) {
        // ImGuizmo observes the button-up in this frame. Capture the final matrix before applying
        // any server echo that Sync queued during the drag.
        ReadManipulatedMatrix();
        ApplyPreview();
        Publish("end");
        dragging_ = false;
        clickCaptured_ = false;
        mouseUpPending_ = false;
        ApplyDeferredSync();
    } else if (mouseUpPending_) {
        // A down/up entirely between rendered frames did not manipulate anything.
        clickCaptured_ = false;
        mouseUpPending_ = false;
        ApplyDeferredSync();
    }
}

void Editor::RebuildMatrix() {
    const float translation[3] = {selection_.transform.x, selection_.transform.y, selection_.transform.z};
    // ImGuizmo's native Euler component order is X/Y/Z: roll, pitch, yaw.
    const float rotation[3] = {selection_.transform.roll, selection_.transform.pitch, selection_.transform.yaw};
    const float scale[3] = {1.0f, 1.0f, 1.0f};
    ImGuizmo::RecomposeMatrixFromComponents(translation, rotation, scale, matrix_);
}

void Editor::ReadManipulatedMatrix() {
    float translation[3] = {}, rotation[3] = {}, scale[3] = {};
    ImGuizmo::DecomposeMatrixToComponents(matrix_, translation, rotation, scale);

    if (IsValidPosition(translation[0], translation[1], translation[2])) {
        selection_.transform.x = translation[0];
        selection_.transform.y = translation[1];
        selection_.transform.z = translation[2];
    }
    if (tool_ == Tool::Rotate && IsFinite(rotation[0]) && IsFinite(rotation[1]) && IsFinite(rotation[2])) {
        selection_.transform.roll = NormalizeDegrees(rotation[0]);
        selection_.transform.pitch = NormalizeDegrees(rotation[1]);
        selection_.transform.yaw = NormalizeDegrees(rotation[2]);
    }
    // Recompose with a unit scale: scaling is intentionally not part of this editor contract.
    RebuildMatrix();
}

void Editor::ApplySelection(const Selection& selection, bool preservePending) {
    const bool identityChanged = !selected_ || selection_.identity != selection.identity ||
                                 selection_.nonce != selection.nonce || selection_.runtimeGuid != selection.runtimeGuid;
    if (identityChanged) RestorePreview();
    if (identityChanged && !preservePending) pending_.valid = false;

    selection_ = selection;
    selection_.transform.yaw = NormalizeDegrees(selection_.transform.yaw);
    selection_.transform.pitch = NormalizeDegrees(selection_.transform.pitch);
    selection_.transform.roll = NormalizeDegrees(selection_.transform.roll);
    selected_ = true;
    RebuildMatrix();
    ApplyPreview();
}

void Editor::ApplyDeferredSync() {
    if (!deferredSync_) return;
    Selection selection = std::move(*deferredSync_);
    deferredSync_.reset();
    ApplySelection(selection, true);
}

void Editor::ResetCapture() {
    // Cancel ImGuizmo's internal gesture too: clearing only MouseDown lets its next Manipulate call
    // write one final stale matrix after Cancel or reselection.
    if (context_) {
        ScopedImGuiContext scope(context_);
        ImGuizmo::Enable(false);
        ImGuizmo::Enable(true);
    }
    gizmoHovered_ = false;
    gizmoUsing_ = false;
    clickCaptured_ = false;
    dragging_ = false;
    mouseUpPending_ = false;
    ReleaseMouseButtons();
}

void Editor::ReleaseMouseButtons() {
    if (!context_) return;
    ScopedImGuiContext scope(context_);
    ImGuiIO& io = ImGui::GetIO();
    for (int button = 0; button < IM_ARRAYSIZE(io.MouseDown); ++button) {
        io.AddMouseButtonEvent(button, false);
        io.MouseDown[button] = false;
    }
}

void Editor::ClearSelection() {
    RestorePreview();
    ResetCapture();
    selected_ = false;
    selection_ = {};
    deferredSync_.reset();
    pending_ = {};
    std::fill(std::begin(matrix_), std::end(matrix_), 0.0f);
}

void Editor::Publish(const char* phase) {
    if (!selected_ || !phase) return;
    ++nextSequence_;
    if (nextSequence_ == 0) ++nextSequence_;
    pending_.valid = true;
    pending_.phase = phase;
    pending_.sequence = nextSequence_;
    pending_.selection = selection_;
}

int Editor::LuaSync(lua_State* L) {
    Selection sync;
    const bool valid = lua_gettop(L) >= 9 && ReadString(L, 1, sync.identity) && ReadString(L, 2, sync.nonce) &&
                       ReadNumber(L, 3, sync.transform.x) && ReadNumber(L, 4, sync.transform.y) && ReadNumber(L, 5, sync.transform.z) &&
                       ReadNumber(L, 6, sync.transform.yaw) && ReadNumber(L, 7, sync.transform.pitch) && ReadNumber(L, 8, sync.transform.roll) &&
                       ReadRuntimeGuid(L, 9, sync.runtimeGuid) && IsValidPosition(sync.transform.x, sync.transform.y, sync.transform.z);

    if (valid) {
        // The overlay is registered with the device on the first selection; an idle editor installs
        // no hook at all.
        NativeVisualHooks::overlayDraw = &DrawHook;
        NativeVisualHooks::overlayDeviceLost = &DeviceLostHook;
        NativeVisualHooks::mouseInput = &MouseHook;

        if (dragging_ || clickCaptured_ || gizmoUsing_) {
            // An acknowledgement can arrive between native input and Lua's next poll. Do not let the
            // preceding gesture's echo snap this live gesture back when it finishes.
            if (sync.identity != selection_.identity || sync.nonce != selection_.nonce) {
                deferredSync_ = std::move(sync);
            } else if (sync.runtimeGuid != selection_.runtimeGuid) {
                // Apply can finish as a new gesture begins: rebind its replacement without replacing
                // the newer local gesture with the preceding server coordinates.
                sync.transform = selection_.transform;
                ApplySelection(sync, true);
            }
        } else {
            ApplySelection(sync, false);
        }
    }

    lua_pushboolean(L, valid ? 1 : 0);
    return 1;
}

int Editor::LuaClear(lua_State* L) {
    ClearSelection();
    lua_pushboolean(L, 1);
    return 1;
}

int Editor::LuaSetTool(lua_State* L) {
    std::string value;
    bool valid = ReadString(L, 1, value) && !dragging_ && !clickCaptured_ && !gizmoUsing_;
    if (valid) {
        if (!strcasecmp(value.c_str(), "translate") || !strcasecmp(value.c_str(), "move") || !strcasecmp(value.c_str(), "position")) {
            tool_ = Tool::Translate;
        } else if (!strcasecmp(value.c_str(), "rotate") || !strcasecmp(value.c_str(), "rotation") || !strcasecmp(value.c_str(), "orientation")) {
            tool_ = Tool::Rotate;
        } else {
            valid = false;
        }
    }
    lua_pushboolean(L, valid ? 1 : 0);
    return 1;
}

int Editor::LuaSetSpace(lua_State* L) {
    std::string value;
    bool valid = ReadString(L, 1, value) && !dragging_ && !clickCaptured_ && !gizmoUsing_;
    if (valid) {
        if (!strcasecmp(value.c_str(), "world")) space_ = Space::World;
        else if (!strcasecmp(value.c_str(), "local")) space_ = Space::Local;
        else valid = false;
    }
    lua_pushboolean(L, valid ? 1 : 0);
    return 1;
}

int Editor::LuaSetSnap(lua_State* L) {
    float position = 0, rotation = 0;
    const bool valid = !dragging_ && !clickCaptured_ && ReadNumber(L, 1, position) && ReadNumber(L, 2, rotation) &&
                       position >= 0 && position <= 100 && rotation >= 0 && rotation <= 180;
    if (valid) {
        positionSnap_ = position;
        rotationSnap_ = rotation;
    }
    lua_pushboolean(L, valid ? 1 : 0);
    return 1;
}

int Editor::LuaTransform(lua_State* L) {
    Transform value;
    const bool valid = selected_ && !dragging_ && !clickCaptured_ && !gizmoUsing_ && ReadNumber(L, 1, value.x) &&
                       ReadNumber(L, 2, value.y) && ReadNumber(L, 3, value.z) && ReadNumber(L, 4, value.yaw) &&
                       ReadNumber(L, 5, value.pitch) && ReadNumber(L, 6, value.roll) && IsValidPosition(value.x, value.y, value.z);
    if (valid) {
        selection_.transform = value;
        selection_.transform.yaw = NormalizeDegrees(value.yaw);
        selection_.transform.pitch = NormalizeDegrees(value.pitch);
        selection_.transform.roll = NormalizeDegrees(value.roll);
        deferredSync_.reset();
        RebuildMatrix();
        Publish("end");
    }
    lua_pushboolean(L, valid ? 1 : 0);
    return 1;
}

int Editor::LuaSetAxis(lua_State* L) {
    std::string value;
    bool valid = ReadString(L, 1, value) && !dragging_ && !clickCaptured_ && !gizmoUsing_;
    if (valid) {
        if (!strcasecmp(value.c_str(), "all")) axis_ = Axis::All;
        else if (!strcasecmp(value.c_str(), "x")) axis_ = Axis::X;
        else if (!strcasecmp(value.c_str(), "y")) axis_ = Axis::Y;
        else if (!strcasecmp(value.c_str(), "z")) axis_ = Axis::Z;
        else valid = false;
    }
    lua_pushboolean(L, valid ? 1 : 0);
    return 1;
}

int Editor::LuaPoll(lua_State* L) {
    if (!pending_.valid) return 0;
    PendingPoll poll = std::move(pending_);
    pending_ = {};
    lua_pushstring(L, poll.phase.c_str());
    lua_pushnumber(L, static_cast<double>(poll.sequence));
    lua_pushnumber(L, poll.selection.transform.x);
    lua_pushnumber(L, poll.selection.transform.y);
    lua_pushnumber(L, poll.selection.transform.z);
    lua_pushnumber(L, poll.selection.transform.yaw);
    lua_pushnumber(L, poll.selection.transform.pitch);
    lua_pushnumber(L, poll.selection.transform.roll);
    lua_pushstring(L, poll.selection.identity.c_str());
    lua_pushstring(L, poll.selection.nonce.c_str());
    return 10;
}

}  // namespace

void WxlRegisterGameObjectEditorLua() { g_editor.Register(); }

void WxlGameObjectEditorWorldLeave() { g_editor.OnWorldLeave(); }

#endif  // WOWDECOMP_LINUX
