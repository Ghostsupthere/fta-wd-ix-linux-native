// Offscreen D3D9/4x MSAA fixture for the shipped soft-particle pack.
#include <d3d9.h>
#include <SDL3/SDL.h>
#include <dlfcn.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <vector>
#include "Engine/Source/ModernAssets/WxlSurfaceLights/LightShaderPack.hpp"
#include "Engine/Source/ModernAssets/WxlVisuals/msaa_depth.h"

void Require(bool value, const char* what) {
    if (!value) { std::fprintf(stderr, "%s\n", what); std::exit(1); }
}
void Check(HRESULT result, const char* what) {
    if (FAILED(result)) { std::fprintf(stderr, "%s (%08x)\n", what, unsigned(result)); std::exit(1); }
}
std::vector<uint32_t> Read(const char* path) {
    std::ifstream file(path, std::ios::binary);
    file.seekg(0, std::ios::end);
    const auto size = file.tellg();
    Require(size > 0 && size <= 0x20000 && size % 4 == 0, "invalid stock shader");
    file.seekg(0);
    std::vector<uint32_t> data(static_cast<size_t>(size) / 4);
    Require(bool(file.read(reinterpret_cast<char*>(data.data()), size)), "read stock shader");
    return data;
}
template<class F>
double Measure(IDirect3DDevice9* dev, F action) {
    constexpr unsigned iterations = 40;
    IDirect3DQuery9 *first, *last, *frequency, *disjoint;
    Check(dev->CreateQuery(D3DQUERYTYPE_TIMESTAMP, &first), "first timestamp");
    Check(dev->CreateQuery(D3DQUERYTYPE_TIMESTAMP, &last), "last timestamp");
    Check(dev->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ, &frequency), "timestamp frequency");
    Check(dev->CreateQuery(D3DQUERYTYPE_TIMESTAMPDISJOINT, &disjoint), "timestamp disjoint");
    Check(disjoint->Issue(D3DISSUE_BEGIN), "disjoint begin");
    Check(first->Issue(D3DISSUE_END), "timestamp begin");
    for (unsigned i = 0; i < iterations; ++i) action();
    Check(last->Issue(D3DISSUE_END), "timestamp end");
    Check(frequency->Issue(D3DISSUE_END), "frequency end");
    Check(disjoint->Issue(D3DISSUE_END), "disjoint end");
    const auto read = [](IDirect3DQuery9* query, void* value, DWORD bytes) {
        const auto start = SDL_GetTicks();
        HRESULT result;
        while ((result = query->GetData(value, bytes, D3DGETDATA_FLUSH)) == S_FALSE) {
            Require(SDL_GetTicks() - start < 5000, "GPU timestamp timeout");
            SDL_Delay(1);
        }
        Check(result, "read timestamp");
    };
    UINT64 begin{}, end{}, cycles{};
    BOOL invalid{};
    read(first, &begin, sizeof(begin)); read(last, &end, sizeof(end));
    read(frequency, &cycles, sizeof(cycles)); read(disjoint, &invalid, sizeof(invalid));
    first->Release(); last->Release(); frequency->Release(); disjoint->Release();
    Require(!invalid && cycles && end >= begin, "invalid GPU timestamp");
    return 1000.0 * (end - begin) / cycles / iterations;
}

int main(int argc, char** argv) {
    const bool noAlphaAdd = argc == 5 && std::strcmp(argv[4], "--one-plus-one") == 0;
    Require(argc == 4 || noAlphaAdd, "usage: probe soft_particles.pack original.vs original.ps [--one-plus-one]");
    Require(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());
    auto* window = SDL_CreateWindow("soft particle fixture", 64, 64, SDL_WINDOW_HIDDEN | SDL_WINDOW_VULKAN);
    Require(window, SDL_GetError());
    auto* library = dlopen("libdxvk_d3d9.so", RTLD_NOW | RTLD_LOCAL);
    Require(library, dlerror());
    const auto create = reinterpret_cast<IDirect3D9*(*)(UINT)>(dlsym(library, "Direct3DCreate9"));
    Require(create, "Direct3DCreate9");
    auto* d3d = create(D3D_SDK_VERSION);
    Require(d3d, "D3D9 factory");
    D3DPRESENT_PARAMETERS pp{};
    pp.BackBufferWidth = pp.BackBufferHeight = 64;
    pp.BackBufferCount = 1;
    pp.BackBufferFormat = D3DFMT_A8R8G8B8;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.Windowed = TRUE;
    pp.hDeviceWindow = reinterpret_cast<HWND>(window);
    IDirect3DDevice9* dev{};
    Check(d3d->CreateDevice(0, D3DDEVTYPE_HAL, pp.hDeviceWindow, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &dev), "device");
    constexpr UINT width = 3440, height = 1440;
    IDirect3DSurface9 *target{}, *depth{}, *resolved{}, *pixels{};
    IDirect3DTexture9 *snapshot{}, *white{};
    Check(dev->CreateRenderTarget(width, height, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_4_SAMPLES, 0, FALSE, &target, nullptr), "4x color");
    Check(dev->CreateDepthStencilSurface(width, height, D3DFMT_D24S8, D3DMULTISAMPLE_4_SAMPLES, 0, FALSE, &depth, nullptr), "4x depth");
    const auto intz = static_cast<D3DFORMAT>(MAKEFOURCC('I','N','T','Z'));
    Check(dev->CreateTexture(width, height, 1, D3DUSAGE_DEPTHSTENCIL, intz, D3DPOOL_DEFAULT, &snapshot, nullptr), "INTZ");
    Check(dev->CreateRenderTarget(width, height, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, 0, FALSE, &resolved, nullptr), "resolved color");
    Check(dev->CreateOffscreenPlainSurface(width, height, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &pixels, nullptr), "CPU pixels");
    Check(dev->CreateTexture(1, 1, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &white, nullptr), "white texture");
    D3DLOCKED_RECT lock{};
    Check(white->LockRect(0, &lock, nullptr, 0), "lock white");
    *static_cast<DWORD*>(lock.pBits) = 0xffffffff;
    white->UnlockRect(0);

    {
        DepthCopy copy;
        Require(copy.Attach(DepthCopyMethod::Resz, depth, snapshot), "attach resolve");
        wxl::surfacelights::LightShaderPack pack;
        std::string error;
        Require(pack.Load(argv[1], error), error.c_str());
        const auto vs = Read(argv[2]), ps = Read(argv[3]);
        auto* newV = pack.Find(0, vs.data(), vs.size() * 4);
        auto* newP = pack.Find(1, ps.data(), ps.size() * 4);
        Require(newV && newP, "archived shader pair absent");
        IDirect3DVertexShader9 *baseVs{}, *softVs{};
        IDirect3DPixelShader9 *basePs{}, *softPs{};
        Check(dev->CreateVertexShader(vs.data(), &baseVs), "base VS");
        Check(dev->CreatePixelShader(ps.data(), &basePs), "base PS");
        Check(dev->CreateVertexShader(newV->bytecode.data(), &softVs), "soft VS");
        Check(dev->CreatePixelShader(newP->bytecode.data(), &softPs), "soft PS");
        Check(dev->SetRenderTarget(0, target), "bind color");
        Check(dev->SetDepthStencilSurface(depth), "bind depth");
        Check(dev->BeginScene(), "begin");
        Check(dev->SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1), "particle layout");
        dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
        dev->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
        dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
        dev->SetRenderState(D3DRS_SRCBLEND, noAlphaAdd ? D3DBLEND_ONE : D3DBLEND_SRCALPHA);
        dev->SetRenderState(D3DRS_DESTBLEND, noAlphaAdd ? D3DBLEND_ONE : D3DBLEND_INVSRCALPHA);
        constexpr float projection[] = {1,0,0,0, 0,1,0,0, 0,0,1.001f,-.1f, 0,0,1,0};
        constexpr float uv[] = {1,0,0,0, 0,1,0,0};
        constexpr float transform[] = {0,1,1,0, 1,0,0,0, 0,1,0,0, 0,0,1,0};
        constexpr float fog[] = {0,0,0,0};
        const float profile[] = {1.f/width, 1.f/height,0,0, 1.001f,-.1f,1,noAlphaAdd ? 1.f : 0.f};
        dev->SetVertexShaderConstantF(2, projection, 4);
        dev->SetVertexShaderConstantF(6, uv, 2);
        dev->SetVertexShaderConstantF(30, transform, 4);
        dev->SetPixelShaderConstantF(2, fog, 1);
        dev->SetPixelShaderConstantF(219, profile, 2);
        dev->SetTexture(0, white);
        dev->SetSamplerState(15, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
        dev->SetSamplerState(15, D3DSAMP_MINFILTER, D3DTEXF_POINT);
        dev->SetSamplerState(15, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
        dev->SetSamplerState(15, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
        const auto clearDepth = [&] {
            Check(dev->Clear(0, nullptr, D3DCLEAR_ZBUFFER, 0, 1.001f - .1f/5.f, 0), "wall depth");
        };
        clearDepth();
        Require(copy.Copy(dev), "warm resolve");
        const double justClear = Measure(dev, clearDepth);
        const double clearResolve = Measure(dev, [&] { clearDepth(); Require(copy.Copy(dev), "timed resolve"); });
        dev->SetTexture(15, snapshot);
        struct Vertex { float x,y,z; DWORD rgba; float u,v; };
        constexpr float z = 4.75f, edge = z * .5f;
        const Vertex quad[] = {{-edge,edge,z,0xffff0000,0,0}, {edge,edge,z,0xffff0000,1,0},
                               {-edge,-edge,z,0xffff0000,0,1}, {edge,-edge,z,0xffff0000,1,1}};
        const auto draw = [&] { Check(dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(Vertex)), "billboard draw"); };
        dev->SetVertexShader(baseVs); dev->SetPixelShader(basePs); draw();
        const double stock = Measure(dev, draw);
        dev->SetVertexShader(softVs); dev->SetPixelShader(softPs); draw();
        const double soft = Measure(dev, draw);
        dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0xff000000, 1, 0);
        draw();
        Check(dev->EndScene(), "end");
        Check(dev->StretchRect(target, nullptr, resolved, nullptr, D3DTEXF_NONE), "resolve color");
        Check(dev->GetRenderTargetData(resolved, pixels), "pixel readback");
        Check(pixels->LockRect(&lock, nullptr, D3DLOCK_READONLY), "readback lock");
        const auto* row = reinterpret_cast<const DWORD*>(static_cast<const char*>(lock.pBits) + (height/2) * lock.Pitch);
        const int red = (row[width/2] >> 16) & 255;
        pixels->UnlockRect();
        Require(red >= 61 && red <= 67, "intersection should retain one quarter of source contribution");
        std::printf("3440x1440 4x MSAA (%s): pixel red=%d, resolve incremental %.3f ms, stock quad %.3f ms, soft quad %.3f ms\n",
                    noAlphaAdd ? "one-plus-one" : "alpha blend", red, clearResolve - justClear, stock, soft);
        baseVs->Release(); basePs->Release(); softVs->Release(); softPs->Release();
    }
    snapshot->Release(); white->Release(); depth->Release(); target->Release(); resolved->Release(); pixels->Release();
    dev->Release(); d3d->Release(); SDL_DestroyWindow(window); SDL_Quit();
}
