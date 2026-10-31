// Hidden-window native DXVK probe for the game-object editor overlay: the vendored ImGui DX9
// backend and ImGuizmo on a real dxvk-native D3D9 device, with a D3D row-vector camera. Synthetic
// scene only; no game, login or assets. Checks that the translate gizmo draws, that hovering and a
// mouse drag along its X handle move the matrix along +X only, that the rotate gizmo with an isolated
// axis turns the matrix about that axis, and that the overlay leaves the device state it found.
#include <d3d9.h>
#include <SDL3/SDL.h>
#include <dlfcn.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "imgui.h"
#include "imgui_impl_dx9.h"
#include "ImGuizmo.h"

static void Check(HRESULT result, const char* operation) {
    if (FAILED(result)) { std::fprintf(stderr, "%s failed: 0x%x\n", operation, unsigned(result)); std::exit(1); }
}
static int failures = 0;
static void Expect(bool condition, const char* what) {
    std::printf("%s: %s\n", condition ? "ok  " : "FAIL", what);
    if (!condition) ++failures;
}

constexpr unsigned kSize = 256;

// Left-handed look-at and perspective, row-vector (the engine's convention): v' = v * M.
static void LookAt(const float eye[3], const float at[3], float out[16]) {
    float z[3] = {at[0] - eye[0], at[1] - eye[1], at[2] - eye[2]};
    const float zl = std::sqrt(z[0] * z[0] + z[1] * z[1] + z[2] * z[2]);
    for (float& v : z) v /= zl;
    float x[3] = {z[2] * 0 - z[1] * 0 + 0, 0, 0};   // up = +Z: x = up x z
    x[0] = 0 * z[2] - 1 * z[1]; x[1] = 1 * z[0] - 0 * z[2]; x[2] = 0;
    const float xl = std::sqrt(x[0] * x[0] + x[1] * x[1] + x[2] * x[2]);
    for (float& v : x) v /= xl;
    const float y[3] = {z[1] * x[2] - z[2] * x[1], z[2] * x[0] - z[0] * x[2], z[0] * x[1] - z[1] * x[0]};
    const float m[16] = {x[0], y[0], z[0], 0, x[1], y[1], z[1], 0, x[2], y[2], z[2], 0,
                         -(x[0] * eye[0] + x[1] * eye[1] + x[2] * eye[2]),
                         -(y[0] * eye[0] + y[1] * eye[1] + y[2] * eye[2]),
                         -(z[0] * eye[0] + z[1] * eye[1] + z[2] * eye[2]), 1};
    std::memcpy(out, m, sizeof(m));
}
static void Perspective(float fovY, float aspect, float zn, float zf, float out[16]) {
    const float yScale = 1.0f / std::tan(fovY * 0.5f), xScale = yScale / aspect;
    const float m[16] = {xScale, 0, 0, 0, 0, yScale, 0, 0, 0, 0, zf / (zf - zn), 1, 0, 0, -zn * zf / (zf - zn), 0};
    std::memcpy(out, m, sizeof(m));
}
static void Project(const float view[16], const float projection[16], const float p[3], float& sx, float& sy) {
    float v[4] = {p[0], p[1], p[2], 1}, c[4] = {};
    float a[4];
    for (int i = 0; i < 4; ++i) a[i] = v[0] * view[i] + v[1] * view[4 + i] + v[2] * view[8 + i] + v[3] * view[12 + i];
    for (int i = 0; i < 4; ++i) c[i] = a[0] * projection[i] + a[1] * projection[4 + i] + a[2] * projection[8 + i] + a[3] * projection[12 + i];
    sx = (c[0] / c[3] * 0.5f + 0.5f) * kSize;
    sy = (1.0f - (c[1] / c[3] * 0.5f + 0.5f)) * kSize;
}

int main() {
    if (!SDL_Init(SDL_INIT_VIDEO)) { std::fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 1; }
    auto* window = SDL_CreateWindow("FTA game-object gizmo probe", kSize, kSize, SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN);
    if (!window) { std::fprintf(stderr, "window: %s\n", SDL_GetError()); return 1; }
    void* library = dlopen("libdxvk_d3d9.so", RTLD_NOW | RTLD_LOCAL);
    if (!library) { std::fprintf(stderr, "DXVK: %s\n", dlerror()); return 1; }
    auto create = reinterpret_cast<IDirect3D9* (*)(UINT)>(dlsym(library, "Direct3DCreate9"));
    auto* d3d = create ? create(D3D_SDK_VERSION) : nullptr;
    if (!d3d) return 1;
    D3DPRESENT_PARAMETERS present{};
    present.BackBufferWidth = present.BackBufferHeight = kSize;
    present.BackBufferFormat = D3DFMT_A8R8G8B8;
    present.BackBufferCount = 1;
    present.SwapEffect = D3DSWAPEFFECT_DISCARD;
    present.Windowed = TRUE;
    present.hDeviceWindow = reinterpret_cast<HWND>(window);
    present.EnableAutoDepthStencil = TRUE;
    present.AutoDepthStencilFormat = D3DFMT_D24S8;
    present.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9* device = nullptr;
    Check(d3d->CreateDevice(0, D3DDEVTYPE_HAL, present.hDeviceWindow, D3DCREATE_HARDWARE_VERTEXPROCESSING, &present, &device), "CreateDevice");

    // WoW-style world: +Z up, the camera 10 yards behind the object looking along +Y.
    float view[16], projection[16];
    const float eye[3] = {0, -10, 2}, at[3] = {0, 0, 0};
    LookAt(eye, at, view);
    Perspective(1.0f, 1.0f, 0.5f, 200.0f, projection);

    ImGuiContext* context = ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    Expect(ImGui_ImplDX9_Init(device), "ImGui DX9 backend initializes on dxvk-native");
    ImGuizmo::SetImGuiContext(context);

    IDirect3DSurface9 *target = nullptr, *readback = nullptr;
    Check(device->GetRenderTarget(0, &target), "GetRenderTarget");
    Check(device->CreateOffscreenPlainSurface(kSize, kSize, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &readback, nullptr), "readback");

    float matrix[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    bool translate = true, over = false, using_ = false;
    auto frame = [&](bool render) -> unsigned {
        io.DisplaySize = ImVec2(kSize, kSize);
        io.DeltaTime = 1.0f / 60.0f;
        ImGui_ImplDX9_NewFrame();
        ImGui::NewFrame();
        ImGuizmo::BeginFrame();
        ImGuizmo::SetOrthographic(false);
        ImGuizmo::SetDrawlist(ImGui::GetBackgroundDrawList());
        ImGuizmo::SetRect(0, 0, kSize, kSize);
        ImGuizmo::SetID(0x57474F45);
        ImGuizmo::SetGizmoSizeClipSpace(0.13f);
        ImGuizmo::Style& style = ImGuizmo::GetStyle();
        style.RotationFullAxisRings = !translate;
        ImGuizmo::SetAxisMask(false, false, !translate);  // rotate: only the Z (yaw) ring
        ImGuizmo::Manipulate(view, projection, translate ? ImGuizmo::TRANSLATE : ImGuizmo::ROTATE, ImGuizmo::WORLD, matrix, nullptr, nullptr);
        over = ImGuizmo::IsOver();
        using_ = ImGuizmo::IsUsing();
        ImGui::Render();
        unsigned lit = 0;
        if (render) {
            Check(device->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0xFF000000, 1.0f, 0), "Clear");
            Check(device->BeginScene(), "BeginScene");
            ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
            Check(device->EndScene(), "EndScene");
            Check(device->GetRenderTargetData(target, readback), "GetRenderTargetData");
            D3DLOCKED_RECT lock{};
            Check(readback->LockRect(&lock, nullptr, D3DLOCK_READONLY), "LockRect");
            for (unsigned y = 0; y < kSize; ++y) {
                const auto* row = reinterpret_cast<const uint32_t*>(static_cast<const unsigned char*>(lock.pBits) + y * lock.Pitch);
                for (unsigned x = 0; x < kSize; ++x) if (row[x] & 0xFFFFFF) ++lit;
            }
            Check(readback->UnlockRect(), "UnlockRect");
        }
        return lit;
    };

    // Device state the overlay must leave as it found it.
    Check(device->SetRenderState(D3DRS_ZENABLE, TRUE), "SetRenderState");
    Check(device->SetRenderState(D3DRS_CULLMODE, D3DCULL_CW), "SetRenderState");
    DWORD zEnable = 0, cull = 0;

    io.AddMousePosEvent(5, 5);
    const unsigned lit = frame(true);
    Expect(lit > 200, "translate gizmo draws pixels");
    device->GetRenderState(D3DRS_ZENABLE, &zEnable);
    device->GetRenderState(D3DRS_CULLMODE, &cull);
    Expect(zEnable == TRUE && cull == D3DCULL_CW, "overlay restores depth and cull state");
    Expect(!over && !using_, "gizmo is not hovered with the mouse in a corner");

    // Hover the +X handle: scan along the projected axis for a position ImGuizmo reports as over.
    float ox, oy, xx, xy;
    const float origin[3] = {0, 0, 0}, xAxis[3] = {1, 0, 0};
    Project(view, projection, origin, ox, oy);
    Project(view, projection, xAxis, xx, xy);
    float hoverX = 0, hoverY = 0;
    bool found = false;
    for (float t = 0.25f; t <= 1.6f && !found; t += 0.05f) {
        const float px = ox + (xx - ox) * t, py = oy + (xy - oy) * t;
        io.AddMousePosEvent(px, py);
        frame(false);
        frame(false);
        if (over) { found = true; hoverX = px; hoverY = py; }
    }
    Expect(found, "mouse over the X handle reports hover");

    if (found) {
        io.AddMousePosEvent(hoverX, hoverY);
        io.AddMouseButtonEvent(0, true);
        frame(false);
        Expect(using_, "pressing on the handle starts a manipulation");
        const float dx = (xx - ox) * 0.4f, dy = (xy - oy) * 0.4f;
        for (int step = 1; step <= 6; ++step) {
            io.AddMousePosEvent(hoverX + dx * step / 6, hoverY + dy * step / 6);
            frame(false);
        }
        io.AddMouseButtonEvent(0, false);
        frame(false);
        Expect(!using_, "releasing ends the manipulation");
        std::printf("matrix translation after drag: %.3f %.3f %.3f\n", matrix[12], matrix[13], matrix[14]);
        Expect(matrix[12] > 0.05f, "dragging along the projected X axis moves the matrix toward +X");
        Expect(std::abs(matrix[13]) < 0.01f && std::abs(matrix[14]) < 0.01f, "the drag stays on the X axis");
    }

    // Rotate with an isolated Z ring: grab the ring and turn it; the basis must rotate about Z.
    translate = false;
    float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    std::memcpy(matrix, identity, sizeof(matrix));
    io.AddMouseButtonEvent(0, false);
    io.AddMousePosEvent(5, 5);
    frame(true);
    bool ring = false;
    float ringX = 0, ringY = 0;
    for (float radius = 4; radius <= 110 && !ring; radius += 2)
        for (int a = 0; a < 360 && !ring; a += 6) {
            const float px = ox + radius * std::cos(a * 0.0174533f), py = oy + radius * std::sin(a * 0.0174533f);
            io.AddMousePosEvent(px, py);
            frame(false);
            frame(false);
            if (over) { ring = true; ringX = px; ringY = py; }
        }
    Expect(ring, "an isolated rotation ring can be grabbed");
    if (ring) std::printf("ring grabbed at %.1f,%.1f (object at %.1f,%.1f)\n", ringX, ringY, ox, oy);
    if (ring) {
        io.AddMousePosEvent(ringX, ringY);
        io.AddMouseButtonEvent(0, true);
        frame(false);
        for (int step = 1; step <= 8; ++step) {
            const float angle = 0.12f * step;
            const float rx = ringX - ox, ry = ringY - oy;
            io.AddMousePosEvent(ox + rx * std::cos(angle) - ry * std::sin(angle), oy + rx * std::sin(angle) + ry * std::cos(angle));
            frame(false);
        }
        io.AddMouseButtonEvent(0, false);
        frame(false);
        const float angleZ = std::atan2(matrix[1], matrix[0]) * 57.29578f;
        std::printf("Z-ring turn: %.1f degrees; row2 %.3f %.3f %.3f\n", angleZ, matrix[8], matrix[9], matrix[10]);
        Expect(std::abs(angleZ) > 5.0f, "turning the Z ring rotates the matrix");
        Expect(std::abs(matrix[10] - 1.0f) < 0.01f && std::abs(matrix[8]) < 0.01f && std::abs(matrix[9]) < 0.01f, "the rotation is about the Z axis only");
    }

    ImGui_ImplDX9_Shutdown();
    ImGui::DestroyContext(context);
    readback->Release();
    target->Release();
    device->Release();
    d3d->Release();
    SDL_DestroyWindow(window);
    SDL_Quit();
    dlclose(library);
    std::printf(failures ? "FAILED (%d)\n" : "all checks passed\n", failures);
    return failures ? 2 : 0;
}
