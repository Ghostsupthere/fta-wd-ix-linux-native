// Synthetic, hidden-window probe. Does not start the game or alter client data.
#include <d3d9.h>
#include <SDL3/SDL.h>
#include <dlfcn.h>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>
#include "Engine/Source/ModernAssets/WxlVisuals/water_renderer.h"
#include "Engine/Source/ModernAssets/WxlVisuals/renderer.h"
#include "Engine/Source/ModernAssets/WxlVisuals/selection_policy.h"
#include "Engine/Source/ModernAssets/WxlVisuals/log.h"
static void Check(HRESULT result,const char* action) {
    if(FAILED(result)){std::fprintf(stderr,"%s: %08x\n",action,unsigned(result));std::exit(1);}
}
static void Require(bool result,const char* action) {
    if(!result){std::fprintf(stderr,"FAILED: %s\n",action);std::exit(1);}
}
struct Vertex {float x,y,z,w;DWORD colour;};
static std::vector<DWORD> Pixels(IDirect3DDevice9* dev,unsigned width,unsigned height) {
    IDirect3DSurface9 *target=nullptr,*resolved=nullptr,*cpu=nullptr;
    Check(dev->GetRenderTarget(0,&target),"get target");
    Check(dev->CreateRenderTarget(width,height,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&resolved,nullptr),"resolve target");
    Check(dev->StretchRect(target,nullptr,resolved,nullptr,D3DTEXF_NONE),"resolve colour");
    Check(dev->CreateOffscreenPlainSurface(width,height,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&cpu,nullptr),"CPU surface");
    Check(dev->GetRenderTargetData(resolved,cpu),"read colour");
    D3DLOCKED_RECT locked={};Check(cpu->LockRect(&locked,nullptr,D3DLOCK_READONLY),"lock");
    std::vector<DWORD> pixels(width*height);
    for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x)
        pixels[y*width+x]=reinterpret_cast<const DWORD*>(static_cast<const unsigned char*>(locked.pBits)+y*locked.Pitch)[x]&0xffffff;
    cpu->UnlockRect();target->Release();resolved->Release();cpu->Release();return pixels;
}
int main(int argc,char** argv) {
    Require(argc>=2,"waterdata.bin argument");
    const unsigned samples=argc>2?unsigned(std::atoi(argv[2])):0;
    Require(SDL_Init(SDL_INIT_VIDEO),SDL_GetError());
    auto* window=SDL_CreateWindow("FTA modern visuals probe",128,128,SDL_WINDOW_VULKAN|SDL_WINDOW_HIDDEN);
    Require(window!=nullptr,SDL_GetError());
    auto* library=dlopen("libdxvk_d3d9.so",RTLD_NOW|RTLD_LOCAL);Require(library!=nullptr,dlerror());
    auto create=reinterpret_cast<IDirect3D9*(*)(UINT)>(dlsym(library,"Direct3DCreate9"));Require(create!=nullptr,"factory");
    auto* d3d=create(D3D_SDK_VERSION);Require(d3d!=nullptr,"D3D9");
    D3DPRESENT_PARAMETERS pp={};pp.BackBufferWidth=pp.BackBufferHeight=128;pp.BackBufferFormat=D3DFMT_A8R8G8B8;
    pp.BackBufferCount=1;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.Windowed=TRUE;pp.hDeviceWindow=reinterpret_cast<HWND>(window);
    pp.EnableAutoDepthStencil=TRUE;pp.AutoDepthStencilFormat=D3DFMT_D24S8;pp.MultiSampleType=static_cast<D3DMULTISAMPLE_TYPE>(samples);
    pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9* dev=nullptr;Check(d3d->CreateDevice(0,D3DDEVTYPE_HAL,pp.hDeviceWindow,D3DCREATE_HARDWARE_VERTEXPROCESSING,&pp,&dev),"device");
    Require(ProbeDepthCopy(d3d,0,D3DDEVTYPE_HAL).method==DepthCopyMethod::Resz,"backend advertises native depth resolve");
    LogOpen("/tmp/wxl-modern-visuals-probe.log");LogSetLevel(2);
    Require(GlobalWaterData().Load(argv[1]),"water data");
    for(unsigned resize=0;resize<2;++resize) {
        const unsigned width=resize?192:128,height=resize?96:128;
        if(resize){pp.BackBufferWidth=width;pp.BackBufferHeight=height;Check(dev->Reset(&pp),"resize reset");}
        IDirect3DSurface9* bound=nullptr;IDirect3DTexture9* texture=nullptr;
        Check(dev->GetDepthStencilSurface(&bound),"depth");
        Check(dev->CreateTexture(width,height,1,D3DUSAGE_DEPTHSTENCIL,static_cast<D3DFORMAT>(MAKEFOURCC('I','N','T','Z')),D3DPOOL_DEFAULT,&texture,nullptr),"INTZ");
        {
            DepthCopy copy;Require(copy.Attach(DepthCopyMethod::Resz,bound,texture),"attach copy");
            Check(dev->BeginScene(),"probe scene");
            const auto test=TestDepthCopy(dev,copy,bound,texture);
            std::printf("depth samples=%u size=%ux%u passed=%d read=%g,%g\n",samples,width,height,test.passed,test.read[0],test.read[1]);
            Require(test.passed,"RESZ actual GPU depths");
            IDirect3DSurface9* privateSurface=nullptr;IDirect3DTexture9* privateTexture=nullptr;
            Check(dev->CreateDepthStencilSurface(32,16,D3DFMT_D24S8,pp.MultiSampleType,0,FALSE,&privateSurface,nullptr),"private test depth");
            Check(dev->CreateTexture(32,16,1,D3DUSAGE_DEPTHSTENCIL,static_cast<D3DFORMAT>(MAKEFOURCC('I','N','T','Z')),D3DPOOL_DEFAULT,&privateTexture,nullptr),"private INTZ");
            {
                DepthCopy privateCopy;Require(privateCopy.Attach(DepthCopyMethod::Resz,privateSurface,privateTexture),"private copy");
                Require(TestDepthCopy(dev,privateCopy,privateSurface,privateTexture).passed,"private readiness surfaces with actual framebuffer");
            }
            privateSurface->Release();privateTexture->Release();
            Check(dev->EndScene(),"end probe");
            SceneDepth depth={texture,bound,&copy};
            FrameInputs frame={};frame.cameraRelativeView[0]=frame.cameraRelativeView[15]=1;frame.cameraRelativeView[5]=frame.cameraRelativeView[10]=-1;
            frame.camPos[0]=resize?-8944:0;frame.camPos[1]=resize?-134:0;frame.camPos[2]=resize?88:10;
            frame.camTarget[0]=frame.camPos[0];frame.camTarget[1]=frame.camPos[1];frame.camTarget[2]=frame.camPos[2]-10;frame.dayFraction=0.5f;frame.toLight[0]=0.2f;frame.toLight[1]=0.4f;frame.toLight[2]=0.894427f;
            frame.glProjection[0]=frame.glProjection[5]=1;frame.glProjection[10]=1001.0f/999.0f;
            frame.glProjection[11]=1;frame.glProjection[14]=-2000.0f/999.0f;
            frame.viewport={0,0,width,height,0,1};frame.fogColor=0xff8899aa;frame.sunColor=frame.directColor=0xffffe8bd;
            frame.ambientColor=0xff808080;frame.fogStart=5;frame.fogEnd=100;frame.zoneFogDistance=100;frame.farClip=1000;frame.mapId=0;
            WaterInputs inputs={};for(auto& c:inputs.skyColors)c=0xff8899aa;
            inputs.riverColors[0]=inputs.oceanColors[0]=0x80557777;inputs.riverColors[1]=inputs.oceanColors[1]=0xff224466;
            inputs.stockFogApplies=false;
            Config config;VisualSelection selection;selection.localLights=true;ApplyVisualSelection(config,selection);
            LocalPointLight light;light.position[0]=frame.camPos[0];light.position[1]=frame.camPos[1];light.position[2]=frame.camPos[2]-2;
            light.color[0]=6;light.color[1]=2;light.color[2]=0.5f;light.attenuation[0]=1;light.attenuation[1]=0.1f;light.attenuation[2]=0.05f;
            Require(engine::SelectLocalPointLight(frame.localLights,light,frame.camPos,{true,1.0f}),"point-light selection");
            WaterRenderer water;Renderer atmosphere;
            for(int quality=1;quality<=3;++quality) {
                config.quality=config.waterQuality=quality;config.temporal=0;
                Check(dev->BeginScene(),"begin");
                Check(dev->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER|D3DCLEAR_STENCIL,0xff556644,0.995995996f,0),"scene clear");
                dev->SetRenderState(D3DRS_ZENABLE,TRUE);dev->SetRenderState(D3DRS_ZWRITEENABLE,TRUE);
                dev->SetRenderState(D3DRS_STENCILENABLE,FALSE);dev->SetViewport(&frame.viewport);
                OverrideWaterSeconds(1.0+quality);
                Require(water.Begin(dev,depth,frame,inputs,config),water.LastSkipReason());
                water.Tag(dev,WaterClass::Lake);
                dev->SetVertexShader(nullptr);dev->SetPixelShader(nullptr);dev->SetTexture(0,nullptr);
                dev->SetFVF(D3DFVF_XYZRHW|D3DFVF_DIFFUSE);dev->SetRenderState(D3DRS_LIGHTING,FALSE);
                dev->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);
                dev->SetRenderState(D3DRS_ZWRITEENABLE,TRUE);
                dev->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1);dev->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_DIFFUSE);
                const float z=0.8008008f;
                const Vertex quad[]={{-.5f,-.5f,z,1,0xff335577},{float(width)-.5f,-.5f,z,1,0xff335577},{-.5f,float(height)-.5f,z,1,0xff335577},{float(width)-.5f,float(height)-.5f,z,1,0xff335577}};
                Check(dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(Vertex)),"stock water plane");
                Require(water.End(dev,depth),water.LastSkipReason());
                DWORD stencil=~0u;dev->GetRenderState(D3DRS_STENCILENABLE,&stencil);Require(stencil==FALSE,"stencil restored");
                Check(dev->EndScene(),"end water");
                const auto waterImage=Pixels(dev,width,height);
                unsigned waterPixels=0;for(auto pixel:waterImage)if(pixel!=0x335577)++waterPixels;
                Require(waterPixels>width*height/2,"water shade changed native plane");
                Check(dev->BeginScene(),"atmosphere scene");
                Require(atmosphere.ReadyToRender(dev,depth,frame.viewport),atmosphere.LastSkipReason());
                Require(atmosphere.Render(dev,depth,frame,config),atmosphere.LastSkipReason());
                Check(dev->EndScene(),"end atmosphere");
                const auto fogImage=Pixels(dev,width,height);
                unsigned fogPixels=0;for(size_t i=0;i<fogImage.size();++i)if(fogImage[i]!=waterImage[i])++fogPixels;
                std::printf("quality=%d waterPixels=%u fogPixels=%u waves=%d classes=%u\n",quality,waterPixels,fogPixels,water.WavesSimulated(),water.ShadedClasses());
                Require(fogPixels>0,"atmosphere changes water scene");
            }
            {
                // A unit wading through the water for a few seconds must drive the ripple
                // simulation, and the ripple map must change the shaded water.
                config.quality=config.waterQuality=2;config.temporal=0;
                VisualSelection ripples;ripples.waterRipples=true;ApplyVisualSelection(config,ripples);
                config.quality=config.waterQuality=2;config.temporal=0;
                Require(config.waterRipples>0,"ripples enabled by profile");
                WaterContact wader;wader.guid=7;wader.surface=0;wader.position[2]=-0.5f;wader.radius=0.5f;wader.height=2;
                std::vector<DWORD> restImage,rippleImage;
                for(int step=0;step<150;++step) {
                    wader.position[0]=frame.camTarget[0]+std::sin(step*0.1f)*3;wader.position[1]=frame.camTarget[1]+step*0.04f;
                    WaterInputs wading=inputs;wading.contacts.count=0;
                    Require(engine::SelectWaterContact(wading.contacts,wader,frame.camTarget),"wader selected as contact");
                    Check(dev->BeginScene(),"ripple scene");
                    Check(dev->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER|D3DCLEAR_STENCIL,0xff556644,0.995995996f,0),"ripple clear");
                    dev->SetRenderState(D3DRS_ZENABLE,TRUE);dev->SetRenderState(D3DRS_ZWRITEENABLE,TRUE);dev->SetViewport(&frame.viewport);
                    OverrideWaterSeconds(20.0+step/30.0);
                    Require(water.Begin(dev,depth,frame,wading,config),water.LastSkipReason());
                    water.Tag(dev,WaterClass::Lake);
                    dev->SetVertexShader(nullptr);dev->SetPixelShader(nullptr);dev->SetTexture(0,nullptr);
                    dev->SetFVF(D3DFVF_XYZRHW|D3DFVF_DIFFUSE);dev->SetRenderState(D3DRS_LIGHTING,FALSE);dev->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);
                    dev->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1);dev->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_DIFFUSE);
                    const float z=0.8008008f;
                    const Vertex quad[]={{-.5f,-.5f,z,1,0xff335577},{float(width)-.5f,-.5f,z,1,0xff335577},{-.5f,float(height)-.5f,z,1,0xff335577},{float(width)-.5f,float(height)-.5f,z,1,0xff335577}};
                    Check(dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(Vertex)),"ripple water plane");
                    Require(water.End(dev,depth),water.LastSkipReason());
                    Check(dev->EndScene(),"end ripple");
                    if(step==149)rippleImage=Pixels(dev,width,height);
                }
                const auto stats=water.RippleStats();
                std::printf("ripples: texels=%d running=%d shaded=%d contacts=%u tracks=%u steps=%llu dropped=%u\n",stats.texels,stats.running,stats.shaded,stats.contacts,stats.tracks,(unsigned long long)stats.steps,stats.droppedSteps);
                Require(stats.contacts>0&&stats.tracks>0,"ripple contact tracked");
                Require(stats.steps>0&&stats.running,"ripple simulation stepped");
                Require(water.RipplesAvailable(),"ripples available");
                // Same frame without contacts: shading must differ where the wake is.
                config.waterRipples=0;
                Check(dev->BeginScene(),"rest scene");
                Check(dev->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER|D3DCLEAR_STENCIL,0xff556644,0.995995996f,0),"rest clear");
                dev->SetViewport(&frame.viewport);OverrideWaterSeconds(20.0+149/30.0);
                Require(water.Begin(dev,depth,frame,inputs,config),water.LastSkipReason());water.Tag(dev,WaterClass::Lake);
                const float z=0.8008008f;
                const Vertex quad[]={{-.5f,-.5f,z,1,0xff335577},{float(width)-.5f,-.5f,z,1,0xff335577},{-.5f,float(height)-.5f,z,1,0xff335577},{float(width)-.5f,float(height)-.5f,z,1,0xff335577}};
                Check(dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(Vertex)),"rest water plane");
                Require(water.End(dev,depth),water.LastSkipReason());Check(dev->EndScene(),"end rest");
                restImage=Pixels(dev,width,height);
                unsigned differing=0;for(size_t i=0;i<restImage.size();++i)if(restImage[i]!=rippleImage[i])++differing;
                std::printf("ripplePixels=%u\n",differing);
                Require(differing>0,"ripples change the shaded water");
                ApplyVisualSelection(config,selection);config.quality=config.waterQuality=3;config.temporal=0;
            }
            Check(dev->BeginScene(),"abort scene");
            Require(water.Begin(dev,depth,frame,inputs,config),"begin before abort");water.Abort(dev);
            frame.inLiquid=true;Require(!water.Begin(dev,depth,frame,inputs,config),"underwater keeps native liquid");
            frame.inLiquid=false;config.water=false;Require(!water.Begin(dev,depth,frame,inputs,config),"water profile off");
            Check(dev->EndScene(),"end abort");
            VisualSelection raysOnly;raysOnly.distanceFog=false;raysOnly.distanceHaze=false;raysOnly.godRays=true;
            ApplyVisualSelection(config,raysOnly);config.temporal=0;
            frame.cameraRelativeView[5]=frame.cameraRelativeView[10]=1;
            Check(dev->BeginScene(),"god rays scene");
            Check(dev->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0xff304050,1.0f,0),"god rays clear");
            const D3DRECT occluder={0,0,LONG(width/2),LONG(height)};
            Check(dev->Clear(1,&occluder,D3DCLEAR_ZBUFFER,0,0.95f,0),"god rays occluder");
            Require(atmosphere.Render(dev,depth,frame,config),"rays-only atmosphere");
            Require(atmosphere.RenderGodRaysAfterWorld(dev,depth),"late god rays");
            Check(dev->EndScene(),"end god rays");
            unsigned rayPixels=0;for(auto pixel:Pixels(dev,width,height))if(pixel!=0x304050)++rayPixels;
            Require(rayPixels>0,"god rays produce pixels");std::printf("godRayPixels=%u\n",rayPixels);
            water.ReleaseAll();atmosphere.ReleaseAll();
        }
        texture->Release();bound->Release();
    }
    dev->Release();d3d->Release();dlclose(library);SDL_DestroyWindow(window);SDL_Quit();
    std::puts("PASS: native water/environment, depth, stencil, profile/underwater, abort, resize");
}
