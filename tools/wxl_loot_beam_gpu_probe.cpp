// Hidden-window native DXVK probe; synthetic geometry only, no game/login/assets.
#include <d3d9.h>
#include <SDL3/SDL.h>
#include <dlfcn.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "Engine/Source/ModernAssets/WxlLootBeam/BeamRenderer.hpp"
#include "Engine/Source/ModernAssets/WxlLootBeam/LootBeamScene.hpp"
using namespace wxl::scripts::lootbeam;
static void Check(HRESULT result,const char* operation) {
    if (FAILED(result)) {std::fprintf(stderr,"%s failed: 0x%x\n",operation,unsigned(result));std::exit(1);}
}
int main(int argc,char** argv) {
    const bool unrebased=argc>1 && !std::strcmp(argv[1],"--unrebased");
    if (!SDL_Init(SDL_INIT_VIDEO)) {std::fprintf(stderr,"SDL: %s\n",SDL_GetError());return 1;}
    auto* window=SDL_CreateWindow("FTA loot-beam renderer probe",128,128,SDL_WINDOW_VULKAN|SDL_WINDOW_HIDDEN);
    if (!window) {std::fprintf(stderr,"window: %s\n",SDL_GetError());return 1;}
    void* library=dlopen("libdxvk_d3d9.so",RTLD_NOW|RTLD_LOCAL);
    if (!library) {std::fprintf(stderr,"DXVK: %s\n",dlerror());return 1;}
    auto create=reinterpret_cast<IDirect3D9*(*)(UINT)>(dlsym(library,"Direct3DCreate9"));
    if (!create) return 1;
    auto* d3d=create(D3D_SDK_VERSION);if (!d3d) return 1;
    D3DPRESENT_PARAMETERS present{};
    present.BackBufferWidth=present.BackBufferHeight=128;present.BackBufferFormat=D3DFMT_A8R8G8B8;
    present.BackBufferCount=1;present.SwapEffect=D3DSWAPEFFECT_DISCARD;present.Windowed=TRUE;
    present.hDeviceWindow=reinterpret_cast<HWND>(window);
    present.EnableAutoDepthStencil=TRUE;present.AutoDepthStencilFormat=D3DFMT_D24S8;
    present.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9* device=nullptr;
    Check(d3d->CreateDevice(0,D3DDEVTYPE_HAL,present.hDeviceWindow,D3DCREATE_HARDWARE_VERTEXPROCESSING,&present,&device),"CreateDevice");
    Scene scene;scene.camera[0]=-8944;scene.camera[1]=-134;scene.camera[2]=88;
    const float view[]{1,0,0,0,0,0,1,0,0,1,0,0,0,0,0,1};
    const float projection[]{1,0,0,0,0,1,0,0,0,0,100.f/99.9f,1,0,0,-10.f/99.9f,0};
    std::memcpy(scene.view,view,sizeof(view));std::memcpy(scene.projection,projection,sizeof(projection));
    Corpse corpse;corpse.guid=1;corpse.hint=5;
    corpse.position[0]=-8944;corpse.position[1]=-124;corpse.position[2]=83;Consider(scene,corpse);
    if (!unrebased) RebaseWorldView(scene);
    State state;std::vector<Vertex> vertices;
    for (uint64_t now=0;now<=400;now+=100) state.Update(scene,true,true,now,vertices);
    IDirect3DSurface9 *target=nullptr,*readback=nullptr;
    Check(device->GetRenderTarget(0,&target),"GetRenderTarget");
    Check(device->CreateOffscreenPlainSurface(128,128,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&readback,nullptr),"readback surface");
    auto run=[&](float depth) {
        Check(device->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0xFF000000,depth,0),"Clear");
        Check(device->BeginScene(),"BeginScene");
        DrawResult result;
        const bool drawn=Draw(device,vertices.data(),unsigned(vertices.size()),scene.view,scene.projection,nullptr,&result);
        Check(device->EndScene(),"EndScene");
        if (!drawn) {std::fprintf(stderr,"beam rejected: %s\n",DrawResultName(result));std::exit(1);}
        Check(device->GetRenderTargetData(target,readback),"GetRenderTargetData");
        D3DLOCKED_RECT lock{};Check(readback->LockRect(&lock,nullptr,D3DLOCK_READONLY),"LockRect");
        unsigned lit=0;
        for (unsigned y=0;y<128;++y) for (unsigned x=0;x<128;++x) {
            const auto* row=reinterpret_cast<const uint32_t*>(static_cast<const unsigned char*>(lock.pBits)+y*lock.Pitch);
            if (row[x]&0xFFFFFF) ++lit;
        }
        Check(readback->UnlockRect(),"UnlockRect");return lit;
    };
    const unsigned visible=run(1),occluded=run(0);
    std::printf("mode=%s vertices=%zu visible_pixels=%u occluded_pixels=%u\n",unrebased?"unrebased":"camera-relative",vertices.size(),visible,occluded);
    readback->Release();target->Release();device->Release();d3d->Release();
    SDL_DestroyWindow(window);SDL_Quit();dlclose(library);
    return (unrebased ? !visible && !occluded : visible && !occluded) ? 0 : 2;
}
