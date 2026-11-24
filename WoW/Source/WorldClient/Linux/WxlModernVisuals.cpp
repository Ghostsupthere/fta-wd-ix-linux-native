#if defined(WOWDECOMP_LINUX)
#define WOWDECOMP_GX_DXVK
#include "Engine/Source/Base/Profiler.h"
#include <d3d9.h>
#endif
#include "WxlModernVisuals.h"
#if defined(WOWDECOMP_LINUX)
#include "WxlVisualEnvironment.h"
#include "Engine/Source/Gx/CGxDeviceD3d9Ex/CGxDeviceD3d9Ex.h"
#include "Engine/Source/Gx/Gx.h"
#include "Engine/Source/Gx/CGxDevice/CGxShader.h"
#include "Engine/Source/ModernAssets/WxlSurfaceLights/LightShaderPack.hpp"
#include "Engine/Source/ModernAssets/WxlVisuals/gpu_timing.h"
#include <unordered_map>
#include "Engine/Source/FFX/EffectGlow.h"
#include "Engine/Source/ConsoleBase/ConsoleVar.h"
#include "WoW/Source/WorldClient/MapObjDef.h"
#include "WoW/Source/WorldClient/MapObj.h"
#include "WoW/Source/WorldClient/MapEntity.h"
#include "Engine/Source/Model2/M2Scene.h"
#include "Engine/Source/Model2/M2Light.h"
#include "Engine/Source/Model2/M2Model.h"
#include "Engine/Source/Model2/M2Shared.h"
#include "Engine/Source/Services/ParticleSystem2.h"
#include "Engine/Source/Model2/M2Data.h"
#include "Engine/Source/Model2/M2ModelLight.h"
#include "Engine/Source/ModernAssets/NativeVisualHooks.h"
#include "WxlWmoMaterials.h"
#include "WxlGrassInstancing.h"
#include "WxlSurfaceLights.h"
#include "Engine/Source/ModernAssets/WxlVisuals/renderer.h"
#include "Engine/Source/ModernAssets/WxlVisuals/transparent_fog.h"
#include "WoW/Source/WorldClient/DayNight.h"
#include "Engine/Source/ModernAssets/WxlVisuals/water_renderer.h"
#include "Engine/Source/ModernAssets/WxlVisuals/water_classify.h"
#include "Engine/Source/ModernAssets/WxlVisuals/native_config.h"
#include "Engine/Source/ModernAssets/WxlVisuals/selection_policy.h"
#include "Engine/Source/ModernAssets/WxlVisuals/atmosphere_options.h"
#include "Engine/Source/ModernAssets/WxlVisuals/atmosphere_regions.h"
#include "Engine/Source/ModernAssets/WxlVisuals/log.h"
#include "WoW/Source/WorldClient/World.h"
#include "WoW/Source/WorldClient/DayNight__DNInfo.h"
#include "WoW/Source/LiquidSystem/Liquid.h"
#include "WoW/Source/DB/WowClientDB.h"
#include "WoW/Source/DB/Rec/LiquidTypeRec.h"
#include "WoW/Source/DB/Rec/CreatureModelDataRec.h"
#include "WoW/Source/Ui/WorldFrame.h"
#include "WoW/Source/UIUtil/Camera.h"
#include "Engine/Source/ModernAssets/WxlVisuals/player_lantern.h"
#include "Engine/Source/ModernAssets/WxlVisuals/night_glow.h"
#include "WoW/Source/DB/Rec/AreaTableRec.h"
#include "WoW/Source/DB/DBClient/DBClient.h"
#include "WoW/Source/ObjectMgrClient/ObjectMgrClient.h"
#include "WoW/Source/Object/ObjectClient/Unit_C.h"
#include "WoW/Source/Object/ObjectClient/Player_C.h"
#include "WoW/Source/Object/ObjectClient/Corpse_C.h"
#include "Engine/Source/Os/TimeManager.h"
#include "Engine/Source/ModernAssets/WxlVisuals/corpse_light_list.h"
#include "Engine/Source/ModernAssets/WxlVisuals/cloud_shade.h"
#include "Engine/Source/ModernAssets/WxlVisuals/spell_light_list.h"
#include "WoW/Source/Object/ObjectClient/Effect_C.h"
#include "WoW/Source/Object/ObjectClient/Missile_C.h"
#include "WoW/Source/Game/GameClient/PlayerName.h"
#include <vector>
#include "WoW/Source/Ui/GameUI.h"
#include "WoW/Source/WorldClient/MapWeather.h"
#include "WoW/Source/WorldClient/Map.h"
#include "Engine/Source/ModernAssets/WxlVisuals/ground_floor.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>

namespace {
Renderer atmosphere;
WaterRenderer water;
DepthCopy depthCopy;
IDirect3DDevice9* device=nullptr;
IDirect3DTexture9* readableDepth=nullptr;
IDirect3DSurface9* boundDepth=nullptr;
D3DSURFACE_DESC depthDescription={};
Config config;
Config baseline;
Config selectedConfig;
namespace env = wxl::grass::environment;
env::Values options;
wxl::atmosphere::RegionalFog regionalFog;
wxl::atmosphere::PlayerLantern frameLantern;
wxl::atmosphere::CorpseLightList corpseLights;
double corpseScanAt = -1;
int corpseMap = -1;
float surfaceWetness = 0;
bool terrainShadows = false;
float surfaceCloud[4] = {};
wxl::atmosphere::SpellLightList spellLights;
double spellScanAt = -1;
int spellMap = -1;
wxl::atmosphere::NightGlowHandoff nightGlow;
bool worldDepthReady = false;
int regionalFogArea = -1;
wxl::atmosphere::GroundFloor groundFloor;
uint64_t floorFrame = 0;
double floorMs = 0, floorMaxMs = 0;
unsigned floorFrames = 0, floorProbes = 0, floorMisses = 0;
bool optionsChanged = true;
const char* visualStatus = "Waiting for a world frame";
DNFogInfo originalFog;
bool configured=false,worldActive=false,replacingFog=false,waterActive=false,depthFailed=false;
uint64_t frameNumber=0;
FrameInputs cameraFrame={};
bool cameraCaptured=false,atmosphereFailed=false,targetValid=false;
float cameraTarget[3]={};
// Transparent fog: the fog is composited once the liquid pass ends and the see-through
// M2 batches that follow use a linear stock fog fitted to it.
bool fogComposited=false,glareDrawnEarly=false;
StockFogFit stockFit;
unsigned transparentBatches=0;
// CPU time (ms) spent in the fog submit paths, reported with the periodic fog line so the
// early composite and the whole-frame pass can be compared on the same machine.
double earlyMs=0,finishMs=0,batchMs=0;unsigned earlyFrames=0,finishFrames=0;
double NowMs(){return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();}
WaterContactFrame contactFrame={};bool contactsCaptured=false;unsigned contactsVisited=0,contactsInLiquid=0;
namespace sl = wxl::surfacelights;
sl::LightShaderPack softParticlePack;
bool softPackTried = false;
std::unordered_map<const sl::LightPackEntry*, IDirect3DVertexShader9*> softVertices;
std::unordered_map<const sl::LightPackEntry*, IDirect3DPixelShader9*> softPixels;
IDirect3DTexture9* opaqueParticleDepth = nullptr;
IDirect3DSurface9* opaqueParticleSource = nullptr;
DepthCopy opaqueParticleCopy;
D3DSURFACE_DESC opaqueParticleExtent{};
D3DVIEWPORT9 opaqueParticleViewport{};
C44Matrix opaqueParticleProjection;
GpuTimer particleResolveTimer("soft particle resolve"), particleDrawTimer("soft particle first draw");
unsigned softDraws = 0, softLastDraws = 0;
bool softParticleTiming = false;
const char* softStatus = "Soft particles off";

void ReleaseParticleDepth() {
    opaqueParticleCopy.Detach();
    if (opaqueParticleDepth) opaqueParticleDepth->Release();
    opaqueParticleDepth = nullptr;
    opaqueParticleSource = nullptr;
}

template<class Shader, class Create>
Shader* SoftShader(const CGxShader* stock, uint8_t kind,
                   std::unordered_map<const sl::LightPackEntry*, Shader*>& cache, Create create) {
    if (!stock || !stock->m_valid || !stock->m_apiSpecific) return nullptr;
    const auto* variant = softParticlePack.Find(kind, stock->m_code.m_data, stock->m_code.m_count);
    if (!variant) return nullptr;
    auto [found, inserted] = cache.emplace(variant, nullptr);
    if (inserted) create(reinterpret_cast<const DWORD*>(variant->bytecode.data()), &found->second);
    return found->second;
}

bool DrawSoftParticle(void* raw, int type, int base, unsigned minimum, unsigned count,
                      unsigned start, unsigned primitives) {
    auto* gx = static_cast<CGxDeviceD3d9Ex*>(g_theGxDevicePtr);
    auto* d = static_cast<IDirect3DDevice9*>(raw);
    if (d != device || !opaqueParticleDepth || std::memcmp(&gx->m_projNative, &opaqueParticleProjection, sizeof(C44Matrix))) return false;
    D3DVIEWPORT9 vp{};
    if (FAILED(d->GetViewport(&vp)) || std::memcmp(&vp, &opaqueParticleViewport, sizeof(vp))) return false;
    IDirect3DSurface9* depth = nullptr;
    if (FAILED(d->GetDepthStencilSurface(&depth))) return false;
    const bool sameDepth = depth == opaqueParticleSource;
    if (depth) depth->Release();
    if (!sameDepth) return false;
    const auto stock = [&](EGxRenderState state) {
        return static_cast<CGxShader*>(gx->m_appRenderStates[state].m_value.m_data.p);
    };
    const auto* stockVs = stock(GxRs_VertexShader);
    const auto* stockPs = stock(GxRs_PixelShader);
    auto* vs = SoftShader(stockVs, 0, softVertices,
        [&](const DWORD* code, IDirect3DVertexShader9** out) { d->CreateVertexShader(code, out); });
    auto* ps = SoftShader(stockPs, 1, softPixels,
        [&](const DWORD* code, IDirect3DPixelShader9** out) { d->CreatePixelShader(code, out); });
    if (!vs || !ps) return false;

    constexpr DWORD slot = 15;
    constexpr DWORD firstConstant = 219;
    constexpr D3DSAMPLERSTATETYPE states[] = {
        D3DSAMP_MINFILTER, D3DSAMP_MAGFILTER, D3DSAMP_MIPFILTER,
        D3DSAMP_ADDRESSU, D3DSAMP_ADDRESSV, D3DSAMP_SRGBTEXTURE
    };
    constexpr DWORD sample[] = {D3DTEXF_POINT, D3DTEXF_POINT, D3DTEXF_NONE, D3DTADDRESS_CLAMP, D3DTADDRESS_CLAMP, FALSE};
    DWORD saved[std::size(states)]{};
    float original[8]{};
    IDirect3DBaseTexture9* previousTexture = nullptr;
    if (FAILED(d->GetPixelShaderConstantF(firstConstant, original, 2)) ||
        FAILED(d->GetTexture(slot, &previousTexture))) return false;
    for (unsigned i = 0; i < std::size(states); ++i) {
        if (FAILED(d->GetSamplerState(slot, states[i], &saved[i]))) {
            if (previousTexture) previousTexture->Release();
            return false;
        }
    }

    const float depthRange = vp.MaxZ - vp.MinZ;
    const float profile[] = {
        1.0f / opaqueParticleExtent.Width, 1.0f / opaqueParticleExtent.Height, 0, 0,
        vp.MinZ + depthRange * gx->m_projNative.c2, depthRange * gx->m_projNative.d2,
        1.0f / static_cast<float>(options.v[env::kSoftParticleDistance]), NativeVisualHooks::softParticleMode == 2 ? 1.0f : 0.0f
    };
    bool ready = SUCCEEDED(d->SetVertexShader(vs)) && SUCCEEDED(d->SetPixelShader(ps)) &&
                 SUCCEEDED(d->SetTexture(slot, opaqueParticleDepth)) &&
                 SUCCEEDED(d->SetPixelShaderConstantF(firstConstant, profile, 2));
    for (unsigned i = 0; i < std::size(states); ++i)
        ready = SUCCEEDED(d->SetSamplerState(slot, states[i], sample[i])) && ready;
    const bool timing = softParticleTiming && ready && softDraws == 0;
    if (timing) particleDrawTimer.Begin(d);
    const bool drew = ready && SUCCEEDED(d->DrawIndexedPrimitive(static_cast<D3DPRIMITIVETYPE>(type),
        base, minimum, count, start, primitives));
    if (timing) particleDrawTimer.End();
    if (drew) ++softDraws;
    d->SetVertexShader(static_cast<IDirect3DVertexShader9*>(stockVs->m_apiSpecific));
    d->SetPixelShader(static_cast<IDirect3DPixelShader9*>(stockPs->m_apiSpecific));
    d->SetPixelShaderConstantF(firstConstant, original, 2);
    d->SetTexture(slot, previousTexture);
    if (previousTexture) previousTexture->Release();
    for (unsigned i = 0; i < std::size(states); ++i) d->SetSamplerState(slot, states[i], saved[i]);
    return drew;
}

SceneDepth Depth() { return {readableDepth,boundDepth,&depthCopy}; }
CGxDeviceD3d9Ex* Backend() {
    auto* gx=g_theGxDevicePtr;
    return gx && gx->m_api==GxApi_D3d9Ex ? static_cast<CGxDeviceD3d9Ex*>(gx):nullptr;
}
uint32_t Colour(const CImVector& c) { uint32_t value;std::memcpy(&value,&c,sizeof(value));return value; }
void Configure() {
    if(configured)return;configured=true;
    std::error_code error;std::filesystem::create_directories("Logs",error);
    LogOpen("Logs/VisualEnvironment.log");
    const auto executable=std::filesystem::canonical("/proc/self/exe",error);
    const auto directory=error?std::filesystem::current_path():executable.parent_path();
    const auto assets=directory/"Utils/WarcraftXL/visuals";
    config=ReadNativeVisualConfig((assets/"WarcraftXLVisuals.ini").string());
    VisualSelection selection;
#define SELECT(member,key) selection.member=WxlProfileEnabled(key,selection.member)
    SELECT(enabled,"WXL_VISUAL_ENVIRONMENT");SELECT(water,"WXL_MODERN_WATER");
    SELECT(multisampling,"WXL_VISUAL_MULTISAMPLING");SELECT(densityVariation,"WXL_DENSITY_VARIATION");
    SELECT(distanceFog,"WXL_DISTANCE_FOG");SELECT(distanceHaze,"WXL_DISTANCE_HAZE");
    SELECT(forwardScattering,"WXL_FORWARD_SCATTERING");SELECT(localLights,"WXL_LOCAL_LIGHTS");
    SELECT(interiorTransitions,"WXL_INTERIOR_TRANSITIONS");SELECT(godRays,"WXL_GOD_RAYS");
    SELECT(groundMist,"WXL_GROUND_MIST");SELECT(transparentFog,"WXL_TRANSPARENT_FOG");
    SELECT(glowCompensation,"WXL_GLOW_COMPENSATION");SELECT(extendedViewDistance,"WXL_EXTENDED_VIEW_DISTANCE");
    SELECT(waterRipples,"WXL_WATER_RIPPLES");
#undef SELECT
    selection.fogDensityScale=WxlProfileNumber("WXL_FOG_DENSITY_SCALE",0.5f,0.0f,2.0f);
    ApplyVisualSelection(config,selection);
    // Quarter-resolution fog only: quality 2 and 3 cost 1-2 ms more for no visible gain, so the profile's Quality is ignored.
    config.quality=1;
    baseline = config;
    // The FTA profile keeps the client's own splash and wake sprites (the selection
    // policy forces WaterClientSplashes on); modern ripples are added to them.
    // TransparentFog stays opt-in (WXL_TRANSPARENT_FOG) and needs the distance fog
    // to replace the stock fog (WXL_DISTANCE_FOG); the selection policy enforces both.
    LogSetLevel(config.logLevel);
    const bool data=GlobalWaterData().Load((assets/"waterdata.bin").string());
    VF_LOG_INFO("native visuals: enabled=%d water=%d data=%d quality=%d atmosphere=%d",config.enable,config.water,data,config.waterQuality,VisualFogEnabled(config));
    NativeVisualHooks::deviceLost=&WxlReleaseModernVisuals;
}
bool PrepareDepth() {
    auto* gx=Backend();if(!gx||!gx->m_d3dDevice)return false;
    if(device!=gx->m_d3dDevice){WxlReleaseModernVisuals();device=gx->m_d3dDevice;}
    IDirect3DSurface9* current=nullptr;
    if(FAILED(device->GetDepthStencilSurface(&current))||!current)return false;
    D3DSURFACE_DESC desc={};current->GetDesc(&desc);
    if(current==boundDepth){current->Release();return readableDepth!=nullptr&&!depthFailed;}
    depthCopy.Detach();if(readableDepth){readableDepth->Release();readableDepth=nullptr;}
    if(boundDepth)boundDepth->Release();boundDepth=current;depthDescription=desc;depthFailed=false;
    const auto fail=[&](const char* why){depthFailed=true;VF_LOG_ERROR("native visuals depth unavailable: %s",why);return false;};
    if(desc.MultiSampleType!=D3DMULTISAMPLE_NONE&&!config.multisampling)return fail("multisampling disabled by profile");
    IDirect3D9* factory=nullptr;D3DDEVICE_CREATION_PARAMETERS creation={};
    DepthCopyProbe capability;
    if(SUCCEEDED(device->GetDirect3D(&factory))&&factory&&SUCCEEDED(device->GetCreationParameters(&creation)))
        capability=ProbeDepthCopy(factory,creation.AdapterOrdinal,creation.DeviceType);
    if(factory)factory->Release();
    if(capability.method!=DepthCopyMethod::Resz)return fail(capability.unavailable);
    // Test private surfaces; never overwrite this frame's world depth to probe it.
    IDirect3DSurface9* testSurface=nullptr;IDirect3DTexture9* testTexture=nullptr;
    const auto intz=static_cast<D3DFORMAT>(MAKEFOURCC('I','N','T','Z'));
    bool passed=false;
    if(SUCCEEDED(device->CreateDepthStencilSurface(32,16,desc.Format,desc.MultiSampleType,desc.MultiSampleQuality,FALSE,&testSurface,nullptr)) &&
       SUCCEEDED(device->CreateTexture(32,16,1,D3DUSAGE_DEPTHSTENCIL,intz,D3DPOOL_DEFAULT,&testTexture,nullptr))) {
        DepthCopy probe;
        if(probe.Attach(DepthCopyMethod::Resz,testSurface,testTexture)) {
            const auto test=TestDepthCopy(device,probe,testSurface,testTexture);passed=test.passed;
            VF_LOG_INFO("native depth RESZ readiness: passed=%d expected=%g,%g read=%g,%g samples=%u",passed,test.expected[0],test.expected[1],test.read[0],test.read[1],unsigned(desc.MultiSampleType));
        }
    }
    if(testSurface)testSurface->Release();if(testTexture)testTexture->Release();
    if(!passed)return fail("RESZ GPU readback check failed");
    if(FAILED(device->CreateTexture(desc.Width,desc.Height,1,D3DUSAGE_DEPTHSTENCIL,intz,D3DPOOL_DEFAULT,&readableDepth,nullptr)) ||
       !depthCopy.Attach(DepthCopyMethod::Resz,boundDepth,readableDepth))return fail("INTZ scene depth allocation failed");
    return true;
}
void CaptureNativeInterior(LocalLightInputs& output) {
    output.cameraInterior=false;output.interiorBlend=0.0f;
    const auto* instance=CWorldScene::s_viewerMapObjDef;
    auto& ids=CWorldScene::s_viewerMapObjGroups;
    if(!instance||!ids.Count())return;
    const auto* root=instance->m_owner;
    if(!root||!root->m_isGroupLoaded||ids.Count()>64||root->m_mapObjGroupCount<1||
        root->m_mapObjGroupCount>int(std::size(root->m_mapObjGroupArray)))return;
    for(uint32_t i=0;i<ids.Count();++i){
        if(ids[i]>=uint32_t(root->m_mapObjGroupCount))return;
        const auto* group=root->m_mapObjGroupArray[ids[i]];
        if(!group)return;
        if((group->m_unkLoadedFlag&1)&&!(group->m_flags&0x48))output.cameraInterior=true;
    }
    const auto* info=DayNight::GetInfo();
    if(output.cameraInterior&&std::isfinite(info->m_interiorFogBlend))output.interiorBlend=std::clamp(info->m_interiorFogBlend,0.0f,1.0f);
}
bool CaptureFrame(FrameInputs& frame,bool snapshot=true) {
    auto* gx=Backend();auto* info=DayNight::GetInfo();const auto* camera=World::GetCamPos();
    if(!gx||!device||!info||!camera)return false;
    if(snapshot&&cameraCaptured){frame=cameraFrame;return true;}
    C44Matrix view;GxXformView(view);
    std::memcpy(frame.cameraRelativeView,&view,sizeof(frame.cameraRelativeView));
    std::memcpy(frame.glProjection,&gx->m_projection,sizeof(frame.glProjection));
    frame.camPos[0]=camera->x;frame.camPos[1]=camera->y;frame.camPos[2]=camera->z;
    for(unsigned i=0;i<3;++i)frame.camTarget[i]=targetValid?cameraTarget[i]:frame.camPos[i]+(&info->m_cameraDir.x)[i];
    device->GetViewport(&frame.viewport);
    frame.dayFraction=info->m_dayProgression;
    frame.lightIsMoon=frame.dayFraction<0.2013889f||frame.dayFraction>0.9236111f;
    const auto& body=frame.lightIsMoon?DayNight::g_planets.m_moon.m_position:DayNight::g_planets.m_sun.m_position;
    const float direction[3]={body.x-info->m_cameraPos.x,body.y-info->m_cameraPos.y,body.z-info->m_cameraPos.z};
    const float length=std::sqrt(direction[0]*direction[0]+direction[1]*direction[1]+direction[2]*direction[2]);
    if(!(length>0.001f))return false;
    for(unsigned i=0;i<3;++i)frame.toLight[i]=direction[i]/length;
    const auto& fog=replacingFog?originalFog:info->m_fogInterior;
    frame.fogColor=Colour(fog.m_color);frame.sunColor=Colour(info->m_bands.m_sunCloudColors[0]);
    auto direct = info->m_bands.m_diffuse;
    if (surfaceCloud[0] > 0 && std::isfinite(camera->x) && std::isfinite(camera->y)) {
        const float shade = wxl::atmosphere::CloudShade(surfaceCloud, camera->x, camera->y);
        direct.r = static_cast<uint8_t>(direct.r * shade);
        direct.g = static_cast<uint8_t>(direct.g * shade);
        direct.b = static_cast<uint8_t>(direct.b * shade);
    }
    frame.directColor=Colour(direct);frame.ambientColor=Colour(info->m_bands.m_ambient);
    frame.fogStart=fog.m_start;frame.fogEnd=fog.m_end;
    frame.zoneFogDistance=info->m_bands.m_fogEnd;frame.farClip=info->m_farClip;
    frame.inLiquid=World::SceneCamLiquidStatus(nullptr)!=0;
    if(!frame.inLiquid&&FFX::s_cvarFFX&&FFX::s_cvarFFX->m_intValue&&
       dynamic_cast<EffectGlow*>(FFX::s_effect)&&FFX::s_effect->m_cvar&&FFX::s_effect->m_cvar->m_intValue&&std::isfinite(info->m_bands.m_glow))
        frame.clientGlowAmount=std::clamp(info->m_bands.m_glow,0.0f,1.0f);
    frame.mapId=World::QueryMapId();
    const auto* weather = World::s_weather;
    frame.fogWindWeather = config.weatherDrift && weather &&
        (weather->m_rain || weather->m_snow || weather->m_sand) && std::isfinite(weather->m_intensity)
        ? std::clamp(weather->m_intensity, 0.0f, 1.0f) : 0.0f;
    frame.regionalFogArea = regionalFogArea;
    frame.regionalFogProfile = regionalFogArea >= 0 ? regionalFog.ProfileArea() : 0;
    CaptureNativeInterior(frame.localLights);
    frame.localLights.capture=LocalLightCapture::UnsupportedClient;
    for(float value:frame.cameraRelativeView)if(!std::isfinite(value))return false;
    for(float value:frame.glProjection)if(!std::isfinite(value))return false;
    for(float value:frame.camPos)if(!std::isfinite(value))return false;
    return frame.farClip>10.0f;
}
struct ContactScan {WaterContactFrame frame;float centre[3];unsigned visited=0,inLiquid=0;};
int32_t VisitContact(WOWGUID guid,void* context) {
    auto& scan=*static_cast<ContactScan*>(context);
    if(++scan.visited>4096){scan.frame.truncated=true;return 0;}
    auto* unit=static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(guid,TYPE_UNIT,__FILE__,__LINE__));
    if(!unit||!unit->m_worldObject)return 1;
    int type=0,unused=0,crosses=0;float surface=0;
    if(!World::QueryObjectLiquid(unit->m_worldObject,&type,&surface,&unused)||
       !World::QueryObjectLiquidFlags(unit->m_worldObject,&crosses,&unused)||!crosses)return 1;
    ++scan.inLiquid;
    WaterContact contact;contact.guid=guid.m_guid;contact.surface=surface;
    contact.onTransport=unit->GetTransportGUID()!=0;
    // Transported units report transport-local positions; the world entity holds the world one.
    const auto pos=contact.onTransport?unit->m_worldObject->m_position:unit->GetPosition();
    contact.position[0]=pos.x;contact.position[1]=pos.y;contact.position[2]=pos.z;
    contact.radius=unit->m_movementData.m_collisionRadius;contact.height=unit->m_movementData.m_collisionHeight;
    contact.swimming=(unit->m_movementData.m_flags&0x200000)!=0;
    engine::SelectWaterContact(scan.frame,contact,scan.centre);
    return 1;
}
// Names the model each captured point light belongs to, once per change of the set (the first 40 changes, so a
// busy fight cannot fill the log).
void TraceLightOwners(CM2Scene* scene,const LocalLightInputs& lights) {
    static uint64_t logged=0;
    static unsigned events=0;
    if(!scene||events>=40)return;
    uint64_t set=lights.pointLightCount;
    for(uint32_t i=0;i<lights.pointLightCount;++i)set=set*1099511628211ull^lights.pointLights[i].nativeId;
    if(set==logged)return;
    logged=set;
    if(!lights.pointLightCount)return;
    ++events;
    for(uint32_t i=0;i<lights.pointLightCount;++i) {
        const auto* target=reinterpret_cast<const CM2Light*>(lights.pointLights[i].nativeId);
        if (!target)
            continue;
        const char* owner="not found among the scene's models";unsigned slot=0,count=0;
        const CM2Model* found=nullptr;
        for(CM2Model* model=scene->m_modelList;model;model=model->m_sceneNext) {
            if(!(model->m_fFlags&1)||!model->m_lights||!model->m_shared||!model->m_shared->m_data)continue;
            count=model->m_shared->m_data->m_lights.m_count;
            bool match=false;
            for(unsigned k=0;k<count&&k<256;++k)if(&model->m_lights[k].m_light==target){slot=k;match=true;break;}
            if(match){owner=model->m_shared->m_fileName;found=model;break;}
        }
        VF_LOG_INFO("  local light %u owner: %s (light %u of %u), last animated %d frames ago",i,owner,slot,count,
                    int(scene->m_frameStamp-target->m_stamp));
        if(found)
            VF_LOG_INFO("    model %p: parent %s, flags %08x/%08x, attachment %u, sequence callback %d, event callback %d, "
                        "last anim time %u of scene time %u, model age %d ms",
                        static_cast<const void*>(found),
                        found->m_attachParent&&found->m_attachParent->m_shared?found->m_attachParent->m_shared->m_fileName:"(none)",
                        found->m_flags,found->m_fFlags,found->m_attachmentId,found->m_sequenceCallback!=nullptr,
                        found->m_eventCallback!=nullptr,found->m_lastAnimTime,scene->m_time,
                        int(scene->m_time-found->m_lastAnimTime));
    }
}
void CaptureNativePointLights(FrameInputs& frame) {
    // Called after opaque M2 draws have joined animation workers. This is the
    // native light grid used by model lighting, not fixed Windows addresses.
    auto* scene=World::s_m2Scene;
    auto& output=frame.localLights;output.capture=LocalLightCapture::Captured;
    // cameraFrame outlives the frame and CaptureFrame does not touch the light list, so without this each frame's
    // lights were added to the previous frames': one fading effect light filled all eight slots with copies of
    // itself (and kept lights that had since gone), which the fog then paid for.
    output.pointLightCount=0;
    for(auto& light:output.pointLights)light=LocalPointLight{};
    if(!config.localLights)return;
    const bool preview = options.v[env::kAtmosphereOverride] != 0;
    const bool nativeLights = (!preview || options.v[env::kLights]) && scene && scene->m_lightGrid;
    if (!nativeLights && frameLantern.strength <= 0)
        return;
    if (nativeLights) {
        PointLightUpload upload;upload.linear=config.colorSpace!=0;upload.intensity=config.localLightIntensity;
        unsigned visited=0;
        for(unsigned bucket=0;bucket<4096;++bucket) {
            unsigned links=0;
            for(auto* light=scene->m_lightGrid[bucket];light;light=light->m_lightNext) {
                if(++links>512||++visited>8192){output.pointLightCount=0;output.capture=LocalLightCapture::DamagedTable;return;}
                if(light->m_scene!=scene||light->m_type!=M2LIGHT_POINT||!light->m_visible)continue;
                LocalPointLight candidate;candidate.nativeId=reinterpret_cast<uintptr_t>(light);
                std::memcpy(candidate.position,&light->m_pos,sizeof(candidate.position));
                std::memcpy(candidate.color,&light->m_dirColor,sizeof(candidate.color));
                candidate.attenuation[0]=light->m_constantAttenuation;
                candidate.attenuation[1]=light->m_linearAttenuation;
                candidate.attenuation[2]=light->m_quadraticAttenuation;
                bool valid=true;for(float coefficient:candidate.attenuation)
                    if(!std::isfinite(coefficient)||coefficient<0||coefficient>1000000.0f)valid=false;
                if(valid)engine::SelectLocalPointLight(output,candidate,frame.camPos,upload);
            }
        }
    }
    if (frameLantern.strength > 0 && output.pointLightCount < kMaxLocalPointLights) {
        LocalPointLight lantern;
        std::memcpy(lantern.position, frameLantern.position, sizeof(lantern.position));
        std::memcpy(lantern.color, frameLantern.Color, sizeof(lantern.color));
        lantern.attenuation[0] = 1.0f;
        lantern.attenuation[2] = 0.08f;
        engine::SelectLocalPointLight(output, lantern, frame.camPos, {config.colorSpace != 0, frameLantern.strength});
    }
    TraceLightOwners(scene,output);
}
void FitM2BatchFog(float* start,float* end,float* exponent,uint32_t* colour) {
    const double began=NowMs();
    M2BatchFogArgs args={*start,*end,*exponent,colour};
    ApplyStockFogFit(stockFit,args);
    *start=args.start;*end=args.end;*exponent=args.exponent;
    if(args.colour!=colour)*colour=*args.colour;
    ++transparentBatches;batchMs+=NowMs()-began;
}
void DisarmTransparentFog() {
    NativeVisualHooks::m2BatchFog=nullptr;
    fogComposited=false;glareDrawnEarly=false;stockFit={};
}
bool CaptureResidentFloor(int map, float x, float y, wxl::atmosphere::FloorSample& sample) {
    int32_t waterType;
    if (!CMap::QueryResidentFloor(map, x, y, sample.height, waterType))
        return false;
    using wxl::atmosphere::FloorSurface;
    if (waterType != 0) {
        const auto* type = g_liquidTypeDB.GetRecord(waterType);
        sample.surface = type && type->m_soundBank == kLiquidSoundBankOcean ? FloorSurface::Sea : FloorSurface::FreshWater;
    }
    return true;
}

void PrepareGroundMist(FrameInputs& frame) {
    Profiler::Scope profile(Profiler::SpikeZone(Profiler::GroundMist));
    if (config.groundFog <= 0 || config.density <= 0 || config.fogDensityScale <= 0 ||
        frame.inLiquid || frame.localLights.cameraInterior || CMap::bDungeon)
        return;
    if (floorFrame != frameNumber) {
        floorFrame = frameNumber;
        const double began = NowMs();
        Profiler::Scope floorProfile(Profiler::SpikeZone(Profiler::ResidentFogFloor));
        const auto stats = groundFloor.Update(frame.mapId, frame.camPos, began * 0.001, CaptureResidentFloor);
        const double elapsed = NowMs() - began;
        floorMs += elapsed;
        floorMaxMs = std::max(floorMaxMs, elapsed);
        floorProbes += stats.probes;
        floorMisses += stats.misses;
        if (++floorFrames == 300) {
            VF_LOG_INFO("ground mist floor: cpu mean %.4f ms max %.4f ms, probes=%u misses=%u known=%u/1024",
                        floorMs / floorFrames, floorMaxMs, floorProbes, floorMisses, groundFloor.Known());
            floorMs = floorMaxMs = 0;
            floorFrames = floorProbes = floorMisses = 0;
        }
    }
    if (groundFloor.HasGrid())
        frame.groundFloor = &groundFloor;
}

void RestoreWaterDepthWrite() {
    NativeVisualHooks::waterDepthWriteOverride=false;
    if(auto* gx=Backend();gx&&device)device->SetRenderState(D3DRS_ZWRITEENABLE,gx->m_deviceStates[CGxDeviceD3d9Ex::Ds_ZWriteEnable]);
}
}
const char* WxlSoftParticleStatus() {
    static char text[128];
    if (!options.v[env::kSoftParticles]) return "Soft particles off";
    std::snprintf(text, sizeof(text), "%s, %u billboards last frame", softStatus, softLastDraws);
    return text;
}

void WxlBeginSoftParticles() {
    softDraws = 0;
    if (!options.v[env::kAtmosphereOverride] || !options.v[env::kSoftParticles]) return;
    softStatus = "Soft particles waiting for readable world depth";
    auto* gx = Backend();
    if (!worldActive || !device || !gx || !boundDepth || depthCopy.Method() != DepthCopyMethod::Resz) return;
    if (!softPackTried) {
        softPackTried = true;
        std::error_code error;
        const auto executable = std::filesystem::canonical("/proc/self/exe", error);
        const auto directory = error ? std::filesystem::current_path() : executable.parent_path();
        std::string why;
        if (!softParticlePack.Load((directory / "Utils/WarcraftXL/visuals/soft_particles.pack").string(), why))
            VF_LOG_INFO("soft particles: %s", why.c_str());
    }
    softStatus = "Soft particle shader packs unavailable";
    if (!softParticlePack.Count(sl::kGroupModel)) return;
    const auto& p = gx->m_projNative;
    if (p.a3 || p.b3 || p.c3 != 1.0f || p.d3 || p.a2 || p.b2 ||
        !std::isfinite(p.c2) || !std::isfinite(p.d2) || !(p.d2 < 0.0f)) return;
    if (FAILED(device->GetViewport(&opaqueParticleViewport)) ||
        !(opaqueParticleViewport.MaxZ > opaqueParticleViewport.MinZ)) return;
    if (opaqueParticleSource != boundDepth) {
        ReleaseParticleDepth();
        const auto format = static_cast<D3DFORMAT>(MAKEFOURCC('I','N','T','Z'));
        if (FAILED(device->CreateTexture(depthDescription.Width, depthDescription.Height, 1,
                D3DUSAGE_DEPTHSTENCIL, format, D3DPOOL_DEFAULT, &opaqueParticleDepth, nullptr)) ||
            !opaqueParticleCopy.Attach(depthCopy.Method(), boundDepth, opaqueParticleDepth)) {
            ReleaseParticleDepth();
            softStatus = "Soft particle depth allocation failed";
            return;
        }
        opaqueParticleSource = boundDepth;
        opaqueParticleExtent = depthDescription;
    }
    static const bool timing = WxlProfileEnabled("WXL_SOFT_PARTICLE_TIMING", false);
    softParticleTiming = timing;
    if (timing) particleResolveTimer.Begin(device);
    const bool copied = opaqueParticleCopy.Copy(device);
    if (timing) particleResolveTimer.End();
    if (!copied) {
        softStatus = "Soft particle depth copy failed";
        return;
    }
    opaqueParticleProjection = p;
    NativeVisualHooks::softParticleDraw = &DrawSoftParticle;
    NativeVisualHooks::softParticleScene = World::s_m2Scene;
    softStatus = "Soft particles available";
}

void WxlEndSoftParticles() {
    if (NativeVisualHooks::softParticleScene && softParticleTiming && frameNumber % 300 == 1) {
        char resolve[96], draw[96];
        DescribeGpuTime(particleResolveTimer, device, resolve, sizeof(resolve));
        DescribeGpuTime(particleDrawTimer, device, draw, sizeof(draw));
        VF_LOG_INFO("native soft particles: %s; %s; %u draws this frame", resolve, draw, softDraws);
    }
    NativeVisualHooks::softParticleDraw = nullptr;
    NativeVisualHooks::softParticleScene = nullptr;
    NativeVisualHooks::softParticleMode = 0;
    softLastDraws = softDraws;
}

void WxlReleaseModernVisuals() {
    WxlEndSoftParticles();
    ReleaseParticleDepth();
    for (auto [key, shader] : softVertices) if (shader) shader->Release();
    for (auto [key, shader] : softPixels) if (shader) shader->Release();
    softVertices.clear();
    softPixels.clear();
    particleResolveTimer.Release();
    particleDrawTimer.Release();
    nightGlow.Reset();
    NativeVisualHooks::weaponEffectScene = nullptr;
    corpseLights.count = 0;
    corpseScanAt = -1;
    spellScanAt = -1;
    spellLights.count = 0;
    frameLantern = {};
    WxlWmoRelease();
    WxlGrassInstancingRelease();
    WxlSurfaceLightsRelease();
    if(device){NativeVisualHooks::waterDepthWriteOverride=false;water.Abort(device);}
    DisarmTransparentFog();
    water.ReleaseAll();atmosphere.ReleaseAll();depthCopy.Detach();
    if(readableDepth)readableDepth->Release();if(boundDepth)boundDepth->Release();
    readableDepth=nullptr;boundDepth=nullptr;device=nullptr;
    worldActive=false;replacingFog=false;waterActive=false;depthFailed=false;cameraCaptured=false;atmosphereFailed=false;
}
void WxlSetAtmosphereOptions(const env::Values& values) {
    options = values;
    NativeVisualHooks::weaponEffectIntensity =
        options.v[env::kAtmosphereOverride] && options.v[env::kWeaponEffects] ?
        static_cast<float>(options.v[env::kWeaponEffectIntensity]) : 1.0f;
    optionsChanged = true;
}
bool WxlAtmosphereSupported() { return Backend() != nullptr; }
const char* WxlAtmosphereStatus() { return visualStatus; }

static wxl::atmosphere::AreaPath ReadRegionPath(int map) {
    wxl::atmosphere::AreaPath path{};
    int area = CGGameUI::m_subzoneID;
    const auto* subzone = area > 0 ? g_areaTableDB.GetRecord(area) : nullptr;
    if (!subzone || subzone->m_continentID != map)
        area = CGGameUI::m_areaID;
    for (size_t i = 0; i < path.size() && area > 0; ++i) {
        const auto* row = g_areaTableDB.GetRecord(area);
        if (!row || row->m_continentID != map || std::find(path.begin(), path.begin() + i, area) != path.begin() + i)
            break;
        path[i] = area;
        area = row->m_parentAreaID;
    }
    return path;
}

const wxl::atmosphere::PlayerLantern& WxlFrameLantern() { return frameLantern; }
const wxl::atmosphere::CorpseLightList& WxlFrameCorpseLights() { return corpseLights; }
float WxlSurfaceWetness() { return surfaceWetness; }
bool WxlTerrainShadows() { return terrainShadows; }
float WxlFootprintStrength() {
    return options.v[env::kAtmosphereOverride] && options.v[env::kCosmeticFootprints] ?
        static_cast<float>(options.v[env::kFootprintStrength]) : 0;
}
const float* WxlSurfaceCloud() { return surfaceCloud; }
const wxl::atmosphere::SpellLightList& WxlFrameSpellLights() { return spellLights; }

// Counters are summed over a logged interval.
enum SpellLightReason {
    NoSpell, Faint, NoModel, NoAsset, Authored, Emitters, NotAdditive, Dark,
    Accepted, MissileFrames, Far, Full, MeshFallback, SpellLightReasonCount
};
static unsigned spellWhy[SpellLightReasonCount];
static wxl::atmosphere::SpellLightList effectLights;   // rebuilt at 10 Hz
static wxl::atmosphere::SpellLightList missileLights;  // rebuilt every frame, with priority

static bool ProjectileMeshLight(const CM2Model& model, const M2Data& asset, float (&rgb)[3]) {
    const auto* skin = model.m_shared->m_skinData;
    if (!skin) return false;
    // A visible additive mesh can carry a projectile's glow even if it has
    // no additive particles. Leave ordinary opaque and alpha-blended meshes dark.
    for (unsigned i = 0; i < std::min(skin->m_batches.m_count, 32u); ++i) {
        const auto& batch = skin->m_batches[i];
        if (batch.m_materialIndex >= asset.m_materials.m_count) continue;
        const auto blend = asset.m_materials[batch.m_materialIndex].m_blendMode;
        if (blend != M2BLEND_NO_ALPHA_ADD && blend != M2BLEND_ADD && blend != M2BLEND_BLEND_ADD) continue;
        if (model.m_colors && batch.m_colorIndex < asset.m_colors.m_count) {
            const auto& track = model.m_colors[batch.m_colorIndex];
            const auto& tint = track.m_colorTrack.m_currentValue;
            const float weight = 0.40f * std::clamp(track.m_alphaTrack.m_currentValue, 0.0f, 1.0f);
            rgb[0] = weight * std::clamp(tint.x, 0.0f, 1.0f);
            rgb[1] = weight * std::clamp(tint.y, 0.0f, 1.0f);
            rgb[2] = weight * std::clamp(tint.z, 0.0f, 1.0f);
            if (std::max({rgb[0], rgb[1], rgb[2]}) > 0.01f) return true;
        } else {
            rgb[0] = rgb[1] = rgb[2] = 0.32f;
            return true;
        }
    }
    return false;
}

static void AddSpellModelLight(const CM2Model* model, const float* world, const float* eye,
                               wxl::atmosphere::SpellLightList& into, bool projectile) {
    if (!model || !(model->m_fFlags & 1) || !model->m_shared) { ++spellWhy[NoModel]; return; }
    const float fade = model->m_alpha;
    if (!(fade > 0.1f)) { ++spellWhy[Faint]; return; }
    const auto* asset = model->m_shared->m_data;
    if (!asset) { ++spellWhy[NoAsset]; return; }
    if (asset->m_lights.m_count) { ++spellWhy[Authored]; return; }
    const float dx = world[0] - eye[0], dy = world[1] - eye[1], dz = world[2] - eye[2];
    constexpr float distance = wxl::atmosphere::SpellLightList::MaxDistance;
    if (!(dx * dx + dy * dy + dz * dz < distance * distance)) { ++spellWhy[Far]; return; }

    float rgb[3] = {}, brightest = 0;
    bool additiveEmitter = false;
    for (unsigned i = 0; i < std::min(asset->m_particles.m_count, 4u); ++i) {
        const auto& emitter = asset->m_particles[i];
        ++spellWhy[Emitters];
        if (emitter.m_blendMode != M2PARTICLEBLEND_ADD &&
            emitter.m_blendMode != M2PARTICLEBLEND_NO_ALPHA_ADD &&
            emitter.m_blendMode != M2PARTICLEBLEND_BLEND_ADD) { ++spellWhy[NotAdditive]; continue; }
        additiveEmitter = true;
        auto* runtime = model->m_particleEmitters ? model->m_particleEmitters[i] : nullptr;
        float sample[3] = {};
        if (runtime && runtime->SampleSpellLight(sample)) {
            const float peak = std::max({sample[0], sample[1], sample[2]});
            if (peak > brightest) {
                brightest = peak;
                std::copy_n(sample, 3, rgb);
            }
        }
    }
    if (brightest == 0) {
        if (!projectile || additiveEmitter || !ProjectileMeshLight(*model, *asset, rgb)) {
            ++spellWhy[Dark];
            return;
        }
        ++spellWhy[MeshFallback];
    }
    ++spellWhy[into.Add(world, rgb, eye, fade) ? Accepted : Full];
}

static void UpdateSpellLights(bool enabled) {
    const auto* camera = World::GetCamPos();
    if (!enabled || !camera) {
        spellLights.count = 0;
        effectLights.count = 0;
        missileLights.count = 0;
        spellScanAt = -1;
        return;
    }
    const float eye[3] = {camera->x, camera->y, camera->z};
    const double now = NowMs();
    const int map = World::QueryMapId();
    unsigned objects = 0, effects = 0;
    const bool scanEffects = map != spellMap || now >= spellScanAt;
    if (scanEffects) {
        spellMap = map;
        spellScanAt = now + 100;
        effectLights.count = 0;
        struct Scan { const float* eye; unsigned objects = 0, effects = 0; } scan{eye};
        ClntObjMgrEnumVisibleObjects([](WOWGUID guid, void* data) -> int32_t {
            auto& scan = *static_cast<Scan*>(data);
            if (++scan.objects > 512)
                return 0;
            auto* object = ClntObjMgrObjectPtr(guid, TYPE_OBJECT, __FILE__, __LINE__);
            if (!object || !object->m_worldObject)
                return 1;
            unsigned links = 0;
            for (const auto* effect = object->m_effectList; effect && links++ < 8; effect = effect->m_next) {
                if (++scan.effects > 64)
                    return 0;
                if (!effect->m_spellId) { ++spellWhy[NoSpell]; continue; }
                const auto position = effect->m_mapEntity ? effect->m_mapEntity->m_position : object->m_worldObject->m_position;
                const float world[] = {position.x, position.y, position.z + (effect->m_mapEntity ? 0.25f : 1.0f)};
                AddSpellModelLight(effect->m_model, world, scan.eye, effectLights, false);
            }
            return 1;
        }, &scan);
        objects = std::min(scan.objects, 512u);
        effects = std::min(scan.effects, 64u);
    }

    // Projectiles move quickly, so they are read every frame from the in-flight list (a handful of entries).
    missileLights.count = 0;
    unsigned missiles = 0;
    for (const CMissile* missile = CMissile::s_inFlightMissileList; missile && missiles < 16; missile = missile->m_next) {
        ++missiles;
        ++spellWhy[MissileFrames];
        const float world[] = {missile->m_position.x, missile->m_position.y, missile->m_position.z};
        if (std::isfinite(world[0]) && std::isfinite(world[1]) && std::isfinite(world[2]))
            AddSpellModelLight(missile->m_model, world, eye, missileLights, true);
    }

    // Projectiles first, then the lights from object effects, skipping any within a yard of one already taken.
    spellLights = missileLights;
    for (unsigned i = 0; i < effectLights.count && spellLights.count < spellLights.lights.size(); ++i) {
        const auto& candidate = effectLights.lights[i];
        bool duplicate = false;
        for (unsigned j = 0; j < spellLights.count && !duplicate; ++j) {
            const auto& taken = spellLights.lights[j];
            const float x = candidate.position[0] - taken.position[0], y = candidate.position[1] - taken.position[1];
            const float z = candidate.position[2] - taken.position[2];
            duplicate = x * x + y * y + z * z < 1.0f;
        }
        if (!duplicate)
            spellLights.lights[spellLights.count++] = candidate;
    }

    const float brightness = static_cast<float>(options.v[env::kSpellLightStrength]);
    const float reach = static_cast<float>(options.v[env::kSpellLightRange]);
    for (unsigned i = 0; i < spellLights.count; ++i) {
        for (float& channel : spellLights.lights[i].colour)
            channel *= brightness;
        spellLights.lights[i].reach = reach;
    }
    static unsigned scans = 0;
    if (scanEffects && ++scans % 50 == 1) {
        VF_LOG_INFO("spell light prototype: %u/%zu sources, %u objects, %u effects, scan %.4f ms",
                    spellLights.count, spellLights.lights.size(), objects, effects, NowMs() - now);
        VF_LOG_INFO("spell light reasons: nospell=%u faint=%u nomodel=%u noasset=%u authored=%u emitters=%u "
                    "notadd=%u dark=%u accepted=%u missile-frames=%u far=%u full=%u mesh=%u",
                    spellWhy[NoSpell], spellWhy[Faint], spellWhy[NoModel], spellWhy[NoAsset], spellWhy[Authored],
                    spellWhy[Emitters], spellWhy[NotAdditive], spellWhy[Dark], spellWhy[Accepted],
                    spellWhy[MissileFrames], spellWhy[Far], spellWhy[Full], spellWhy[MeshFallback]);
        std::memset(spellWhy, 0, sizeof(spellWhy));
    }
}

static void UpdateCorpseLights(bool enabled) {
    const auto* eye = World::GetCamPos();
    if (!enabled || !eye) {
        corpseLights.count = 0;
        corpseScanAt = -1;
        return;
    }
    const int map = World::QueryMapId();
    const double now = NowMs();
    if (map == corpseMap && now < corpseScanAt)
        return;
    corpseMap = map;
    corpseScanAt = now + 250;
    corpseLights.count = 0;
    struct Scan { float eye[3]; uint32_t time; unsigned visited; } scan{{eye->x, eye->y, eye->z}, OsGetAsyncTimeMs(), 0};
    ClntObjMgrEnumVisibleObjects([](WOWGUID guid, void* context) -> int32_t {
        auto& scan = *static_cast<Scan*>(context);
        if (++scan.visited > 1024)
            return 0;
        auto* object = ClntObjMgrObjectPtr(guid, TYPE_OBJECT, __FILE__, __LINE__);
        if (!object || !object->m_worldObject)
            return 1;
        const auto type = object->m_obj->m_type;
        const bool lootable = (type & TYPE_CORPSE) ? static_cast<CGCorpse_C*>(object)->CanBeLooted() != 0
                          : (type & TYPE_UNIT) && static_cast<CGUnit_C*>(object)->CanBeLooted(scan.time);
        if (lootable) {
            const auto position = object->m_worldObject->m_position;
            const float world[] = {position.x, position.y, position.z};
            corpseLights.Add(guid.m_guid, world, scan.eye);
        }
        return 1;
    }, &scan);
    static unsigned scans = 0;
    if (++scans % 20 == 1)
        VF_LOG_INFO("corpse lights: %u/%zu candidates, %u visible objects, scan %.4f ms",
                    corpseLights.count, corpseLights.lights.size(), std::min(scan.visited, 1024u), NowMs() - now);
}

static wxl::atmosphere::PlayerLantern CapturePlayerLantern(CGPlayer_C& player) {
    const float strength = float(options.v[env::kLanternStrength]);
    if (!options.v[env::kAtmosphereOverride] || !options.v[env::kLantern] || !(strength > 0))
        return {};
    auto* camera = CGWorldFrame::GetActiveCamera();
    auto* rider = player.m_worldModel;
    if (!camera || camera->IsFirstPersonView() || player.IsDeadOrGhost() ||
        player.IsPlayerModelHidden() || (player.m_movementData.m_flags & 0x200000) ||
        !rider || !(rider->m_fFlags & 1) || !(rider->m_fFlags & 0x8000) || !rider->m_scene ||
        !(rider->m_alpha > 0.05f))
        return {};

    // Use the rider's pose. The unit's root model is the mount while mounted.
    rider->Animate();
    const C44Matrix world = rider->m_matrixF4 * rider->m_scene->m_viewInv;
    float matrix[16];
    std::memcpy(matrix, &world, sizeof(matrix));
    const auto* shape = player.GetModelData();
    return wxl::atmosphere::BeltLantern(matrix, shape ? shape->m_collisionHeight : player.m_height,
                                      shape ? shape->m_collisionWidth : 0.0f, strength);
}

namespace {
bool deferNames = false;
std::vector<PLAYERNAMEDESC*> deferredNames;
}
// The view matrix at the time the first name was queued. Inside the scene it carries the camera translation;
// later in the frame the engine renders camera-relative with a zero translation, which would put names (positioned
// in world coordinates) far off screen.
C44Matrix deferredNameView;
bool WxlDeferPlayerName(PLAYERNAMEDESC* desc) {
    if (!deferNames || !desc || deferredNames.size() >= 512)
        return false;
    if (deferredNames.empty())
        GxXformView(deferredNameView);
    deferredNames.push_back(desc);
    return true;
}
void WxlDrawDeferredPlayerNames() {
    // Called straight after the night composite, in the same frame, so every queued descriptor is still alive.
    if (deferredNames.empty())
        return;
    std::vector<PLAYERNAMEDESC*> names;
    names.swap(deferredNames);
    C44Matrix current;
    GxXformView(current);
    GxXformSetView(deferredNameView);
    for (PLAYERNAMEDESC* desc : names)
        desc->Render();
    GxXformSetView(current);
    deferredNames = std::move(names);
    deferredNames.clear();
}
bool WxlBeginVisualWorld(bool underwater,const float* target) {
    NativeVisualHooks::weaponEffectScene = World::s_m2Scene;
    Configure();worldActive=false;replacingFog=false;cameraCaptured=false;++frameNumber;
    DisarmTransparentFog();
    worldDepthReady = false;
    surfaceWetness = 0;
    terrainShadows = false;
    surfaceCloud[0] = 0;
    frameLantern = {};
    if (optionsChanged) {
        selectedConfig = wxl::atmosphere::ApplyOptions(baseline, options);
        optionsChanged = false;
        atmosphereFailed = false;
    }
    config = selectedConfig;
    targetValid=target!=nullptr;contactsCaptured=false;
    for(unsigned i=0;targetValid&&i<3;++i){cameraTarget[i]=target[i];if(!std::isfinite(target[i]))targetValid=false;}
    auto* player = ClntObjMgrGetActivePlayerObj();
    const bool previewLights = config.enable && config.hooks && player && !underwater &&
                               options.v[env::kAtmosphereOverride];
    deferNames = previewLights && (options.v[env::kNight] || options.v[env::kGlow]);
    deferredNames.clear();
    UpdateCorpseLights(previewLights && options.v[env::kCorpseLights]);
    UpdateSpellLights(previewLights && options.v[env::kSpellLights]);
    if(!config.enable||!config.hooks||underwater||!player){
        visualStatus = underwater ? "Stock underwater rendering" : "World effects inactive";
        return false;
    }
    auto* info=DayNight::GetInfo();if(!info)return false;
    regionalFogArea = -1;
    if (options.v[env::kAtmosphereOverride] && options.v[env::kRegions] && config.density > 0) {
        const int map = World::QueryMapId();
        const auto* weather = World::s_weather;
        const unsigned kind = !weather ? 0 : weather->m_rain ? 1 : weather->m_snow ? 2 : weather->m_sand ? 3 : 0;
        const auto path = ReadRegionPath(map);
        regionalFogArea = path.front();
        regionalFog.Apply(config, map, path, info->m_dayProgression,
            kind, weather ? weather->m_intensity : 0.0f, NowMs() * 0.001);
    } else {
        regionalFog.Reset();
    }
    terrainShadows = previewLights && options.v[env::kTerrainShadows] && player->IsOutdoors();
    if (options.v[env::kAtmosphereOverride] && options.v[env::kWetSurfaces] && player->IsOutdoors()) {
        const auto* weather = World::s_weather;
        if (weather && weather->m_rain && std::isfinite(weather->m_intensity))
            surfaceWetness = std::clamp(weather->m_intensity, 0.0f, 1.0f);
    }
    if (options.v[env::kAtmosphereOverride] && options.v[env::kCloudShadows] && player->IsOutdoors()) {
        const double seconds = NowMs() * 0.001;
        const float hour = info->m_dayProgression * 24.0f;
        if (std::isfinite(hour)) {
            surfaceCloud[0] = 0.24f * std::clamp(std::min(hour - 6.0f, 19.0f - hour), 0.0f, 1.0f);
            surfaceCloud[1] = static_cast<float>(std::fmod(seconds * 0.017, 1.0));
            surfaceCloud[2] = static_cast<float>(std::fmod(seconds * 0.009, 1.0));
            surfaceCloud[3] = 0.006f;
        }
    }
    if (frameNumber % 300 == 1 && (surfaceCloud[0] > 0 || surfaceWetness > 0))
        VF_LOG_INFO("surface weather: wetness=%.3f cloud shade=%.3f phase=%.3f,%.3f",
                    surfaceWetness, surfaceCloud[0], surfaceCloud[1], surfaceCloud[2]);
    frameLantern = CapturePlayerLantern(*player);
    originalFog=info->m_fogInterior;
    if(!PrepareDepth()){visualStatus = "Readable world depth unavailable";return false;}
    visualStatus = options.v[env::kAtmosphereOverride] ? "Atmosphere controls active" : "Using launch profile";
    worldActive=true;
    D3DVIEWPORT9 viewport={};device->GetViewport(&viewport);
    replacingFog=!atmosphereFailed&&VisualFogEnabled(config)&&config.stockFog==1&&atmosphere.ReadyToRender(device,Depth(),viewport);
    return replacingFog;
}
void WxlBeginModernWater() {
    if(!worldActive||!config.water||waterActive)return;
    FrameInputs frame={};if(!CaptureFrame(frame))return;
    WaterInputs inputs={};const auto* info=DayNight::GetInfo();
    for(unsigned i=0;i<6;++i)inputs.skyColors[i]=Colour(info->m_bands.m_skyColors[i]);
    for(unsigned i=0;i<2;++i){inputs.riverColors[i]=Colour(info->m_bands.m_waterColors[i]);inputs.oceanColors[i]=Colour(info->m_bands.m_waterColors[i+2]);}
    inputs.stockFogApplies=!replacingFog;
    if (frame.lightIsMoon && options.v[env::kAtmosphereOverride] && options.v[env::kWaterLampGlints])
        WxlSelectWaterLamps(frame.camPos, inputs.lamps);
    const auto* weather = World::s_weather;
    if (config.waterRainRings && weather && weather->m_rain && std::isfinite(weather->m_intensity)) {
        inputs.rain = std::clamp(weather->m_intensity, 0.0f, 1.0f);
        inputs.residentWater = [](int map, float x, float y) {
            float height;
            int32_t waterType = 0;
            return CMap::QueryResidentFloor(map, x, y, height, waterType) && waterType != 0;
        };
    }
    if(config.waterRipples>0){
        if(!contactsCaptured){ContactScan scan;std::copy(frame.camTarget,frame.camTarget+3,scan.centre);ClntObjMgrEnumVisibleObjects(VisitContact,&scan);contactFrame=scan.frame;contactsCaptured=true;contactsVisited=scan.visited;contactsInLiquid=scan.inLiquid;}
        inputs.contacts=contactFrame;
    }
    waterActive=water.Begin(device,Depth(),frame,inputs,config);
    if(waterActive){NativeVisualHooks::waterDepthWriteOverride=true;device->SetRenderState(D3DRS_ZWRITEENABLE,TRUE);}
    else if(frameNumber%300==1)VF_LOG_INFO("native water skipped: %s",water.LastSkipReason());
}
void WxlTagModernWater(const Liquid::CMaterialSettings* settings) {
    if(!waterActive)return;
    WaterClass classification=WaterClass::None;
    auto& bank=Liquid::CSettingsBank::s_settings;
    for(uint32_t i=0;i<bank.Count();++i)if(bank[i]==settings){
        if(const auto* record=Liquid::s_liquidTypeDB->GetRecord(i))classification=ClassifyLiquid(i,record->m_soundBank,record->m_materialID,settings->m_textureNames[0]);
        break;
    }
    water.Tag(device,classification);
}
void WxlEndModernWater() {
    if(!waterActive)return;
    RestoreWaterDepthWrite();waterActive=false;
    const bool shaded=water.End(device,Depth());
    worldDepthReady |= shaded;
    if(frameNumber%300==1)VF_LOG_INFO("native water: shaded=%d classes=%u waves=%d ripples=%d contacts=%u (units %u, in liquid %u) skip=%s",shaded,water.ShadedClasses(),water.WavesSimulated(),water.RipplesAvailable(),contactFrame.count,contactsVisited,contactsInLiquid,water.LastSkipReason());
}
void WxlCaptureVisualCamera() {
    if(worldActive){cameraCaptured=CaptureFrame(cameraFrame,false);if(cameraCaptured)CaptureNativePointLights(cameraFrame);}
}
bool WxlGlareDrawn() { return glareDrawnEarly; }
void WxlBeginTransparentFog() {
    if(!worldActive||!replacingFog||!config.transparentFog||atmosphereFailed||fogComposited)return;
    const double began=NowMs();
    struct Timed{double began;~Timed(){earlyMs+=NowMs()-began;++earlyFrames;}} timed{began};
    const auto idle=[&](const char* why){if(frameNumber%300==1)VF_LOG_INFO("native transparent fog: drawn after the world because %s",why);};
    if(!cameraCaptured){idle("the camera was not captured");return;}
    FrameInputs frame={};
    if(!CaptureFrame(frame)||frame.inLiquid){idle("the camera frame is unavailable or under water");return;}
    D3DVIEWPORT9 viewport={};device->GetViewport(&viewport);
    if(!atmosphere.ReadyToRender(device,Depth(),viewport)){idle(atmosphere.LastSkipReason());return;}
    PrepareGroundMist(frame);
    // The glare is drawn first so its occlusion query reads the finished depth; the
    // frame then skips its own glare call.
    DayNight::RenderGlares();glareDrawnEarly=true;
    if(!atmosphere.Render(device,Depth(),frame,config,FogPass::BeforeTransparents)){
        // The whole-frame pass at the end of the world gets the next chance.
        idle(atmosphere.LastSkipReason());return;
    }
    worldDepthReady = true;
    fogComposited=true;stockFit=atmosphere.LastStockFogFit();
    if(frameNumber%300==1){
        VF_LOG_INFO("native transparent fog: composited early, fit=%d start=%.1f end=%.1f colour=%08X batches last frame=%u",stockFit.fogs,stockFit.start,stockFit.end,stockFit.colour,transparentBatches);
        VF_LOG_INFO("native fog cpu (per frame, last 300): early submit %.3f ms, late god rays %.3f ms, batch hook %.3f ms (%u frames)",earlyFrames?earlyMs/earlyFrames:0.0,finishFrames?finishMs/finishFrames:0.0,earlyFrames?batchMs/earlyFrames:0.0,earlyFrames);
        earlyMs=finishMs=batchMs=0;earlyFrames=finishFrames=0;
    }
    transparentBatches=0;
    NativeVisualHooks::m2BatchFog=&FitM2BatchFog;
}
void WxlFinishVisualWorld() {
    if (!worldActive) {
        nightGlow.Reset();
        frameLantern = {};
        return;
    }
    const double began=NowMs();
    struct Timed{double began;~Timed(){finishMs+=NowMs()-began;++finishFrames;}} timed{began};
    FrameInputs frame={};
    if(fogComposited){
        NativeVisualHooks::m2BatchFog=nullptr;
        if(!atmosphere.RenderGodRaysAfterWorld(device,Depth()))VF_LOG_ERROR("native god rays after the world skipped: %s",atmosphere.LastSkipReason());
    } else if(!atmosphereFailed&&VisualFogEnabled(config)&&CaptureFrame(frame)){
        PrepareGroundMist(frame);
        const bool rendered=atmosphere.Render(device,Depth(),frame,config);
        worldDepthReady |= rendered;
        if(rendered)atmosphere.RenderGodRaysAfterWorld(device,Depth());
        else {atmosphereFailed=true;VF_LOG_ERROR("native atmosphere disabled until device rebuild: %s",atmosphere.LastSkipReason());}
        if(frameNumber%300==1){
            VF_LOG_INFO("native atmosphere: rendered=%d replacing=%d skip=%s",rendered,replacingFog,atmosphere.LastSkipReason());
            VF_LOG_INFO("native fog cpu (per frame, last 300): whole-frame submit %.3f ms (%u frames)",finishFrames?finishMs/finishFrames:0.0,finishFrames);
            finishMs=0;finishFrames=0;
        }
    }
    bool glowRequested = false;
    if (options.v[env::kAtmosphereOverride] && (options.v[env::kNight] || options.v[env::kGlow]) &&
        cameraCaptured && !cameraFrame.localLights.cameraInterior) {
        if (auto* player = ClntObjMgrGetActivePlayerObj(); player && player->IsOutdoors()) {
            const float hour = cameraFrame.dayFraction * 24.0f;
            const float weight = std::clamp(std::max((hour - 18.0f) / 4.0f, (7.0f - hour) / 3.0f), 0.0f, 1.0f);
            const float darkness = options.v[env::kNight] ? weight * float(options.v[env::kNightStrength]) : 0.0f;
            float glow = options.v[env::kGlow] ? weight * float(options.v[env::kGlowStrength]) : 0.0f;
            glowRequested = glow > 0.0f;
            if (glowRequested) {
                // Use the stock effect only when it actually contributes bloom to this frame.
                const bool stockGlow = cameraFrame.clientGlowAmount >= (1.0f / 255.0f) &&
                    FFX::s_cvarFFX && FFX::s_cvarFFX->m_intValue &&
                    dynamic_cast<EffectGlow*>(FFX::s_effect) && FFX::s_effect->Valid();
                glow *= nightGlow.Advance(began * 0.001, stockGlow);
            }
            if ((darkness > 0.0f || glow > 0.0f) &&
                !atmosphere.RenderNightLighting(device, Depth(), cameraFrame, darkness, glow, worldDepthReady))
                visualStatus = "Night lighting unavailable";
        }
    }
    if (!glowRequested)
        nightGlow.Reset();
    if (frameLantern.strength > 0 && cameraCaptured && !cameraFrame.inLiquid)
        atmosphere.RenderLanternHalo(device, Depth(), cameraFrame, frameLantern);
    frameLantern = {};
    worldActive=false;
}
#else
bool WxlBeginVisualWorld(bool,const float*){return false;}
void WxlCaptureVisualCamera(){}
void WxlFinishVisualWorld(){}
void WxlBeginTransparentFog(){}
bool WxlGlareDrawn(){return false;}
void WxlBeginModernWater(){}
void WxlTagModernWater(const Liquid::CMaterialSettings*){}
void WxlEndModernWater(){}
void WxlReleaseModernVisuals(){}
#endif
