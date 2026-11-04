#if defined(WOWDECOMP_LINUX)
#define WOWDECOMP_GX_DXVK
#include <d3d9.h>
#endif
#include "WxlModernVisuals.h"
#if defined(WOWDECOMP_LINUX)
#include "WxlVisualEnvironment.h"
#include "Engine/Source/Gx/CGxDeviceD3d9Ex/CGxDeviceD3d9Ex.h"
#include "Engine/Source/Gx/Gx.h"
#include "Engine/Source/FFX/EffectGlow.h"
#include "Engine/Source/ConsoleBase/ConsoleVar.h"
#include "WoW/Source/WorldClient/MapObjDef.h"
#include "WoW/Source/WorldClient/MapObj.h"
#include "WoW/Source/WorldClient/MapEntity.h"
#include "Engine/Source/Model2/M2Scene.h"
#include "Engine/Source/Model2/M2Light.h"
#include "Engine/Source/Model2/M2Model.h"
#include "Engine/Source/Model2/M2Shared.h"
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
#include "Engine/Source/ModernAssets/WxlVisuals/log.h"
#include "WoW/Source/WorldClient/World.h"
#include "WoW/Source/WorldClient/DayNight__DNInfo.h"
#include "WoW/Source/LiquidSystem/Liquid.h"
#include "WoW/Source/DB/WowClientDB.h"
#include "WoW/Source/DB/Rec/LiquidTypeRec.h"
#include "WoW/Source/ObjectMgrClient/ObjectMgrClient.h"
#include "WoW/Source/Object/ObjectClient/Unit_C.h"
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
    frame.directColor=Colour(info->m_bands.m_diffuse);frame.ambientColor=Colour(info->m_bands.m_ambient);
    frame.fogStart=fog.m_start;frame.fogEnd=fog.m_end;
    frame.zoneFogDistance=info->m_bands.m_fogEnd;frame.farClip=info->m_farClip;
    frame.inLiquid=World::SceneCamLiquidStatus(nullptr)!=0;
    if(!frame.inLiquid&&FFX::s_cvarFFX&&FFX::s_cvarFFX->m_intValue&&
       dynamic_cast<EffectGlow*>(FFX::s_effect)&&FFX::s_effect->m_cvar&&FFX::s_effect->m_cvar->m_intValue&&std::isfinite(info->m_bands.m_glow))
        frame.clientGlowAmount=std::clamp(info->m_bands.m_glow,0.0f,1.0f);
    frame.mapId=World::QueryMapId();
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
                    int(scene->m_frameStamp-reinterpret_cast<const CM2Light*>(lights.pointLights[i].nativeId)->m_stamp));
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
    if(!config.localLights||!scene||!scene->m_lightGrid)return;
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
void RestoreWaterDepthWrite() {
    NativeVisualHooks::waterDepthWriteOverride=false;
    if(auto* gx=Backend();gx&&device)device->SetRenderState(D3DRS_ZWRITEENABLE,gx->m_deviceStates[CGxDeviceD3d9Ex::Ds_ZWriteEnable]);
}
}
void WxlReleaseModernVisuals() {
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
bool WxlBeginVisualWorld(bool underwater,const float* target) {
    Configure();worldActive=false;replacingFog=false;cameraCaptured=false;++frameNumber;
    DisarmTransparentFog();
    targetValid=target!=nullptr;contactsCaptured=false;
    for(unsigned i=0;targetValid&&i<3;++i){cameraTarget[i]=target[i];if(!std::isfinite(target[i]))targetValid=false;}
    if(!config.enable||!config.hooks||underwater||!ClntObjMgrGetActivePlayerObj())return false;
    auto* info=DayNight::GetInfo();if(!info)return false;
    originalFog=info->m_fogInterior;
    if(!PrepareDepth())return false;
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
    // The glare is drawn first so its occlusion query reads the finished depth; the
    // frame then skips its own glare call.
    DayNight::RenderGlares();glareDrawnEarly=true;
    if(!atmosphere.Render(device,Depth(),frame,config,FogPass::BeforeTransparents)){
        // The whole-frame pass at the end of the world gets the next chance.
        idle(atmosphere.LastSkipReason());return;
    }
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
    if(!worldActive)return;
    const double began=NowMs();
    struct Timed{double began;~Timed(){finishMs+=NowMs()-began;++finishFrames;}} timed{began};
    FrameInputs frame={};
    if(fogComposited){
        NativeVisualHooks::m2BatchFog=nullptr;
        if(!atmosphere.RenderGodRaysAfterWorld(device,Depth()))VF_LOG_ERROR("native god rays after the world skipped: %s",atmosphere.LastSkipReason());
    } else if(!atmosphereFailed&&VisualFogEnabled(config)&&CaptureFrame(frame)){
        const bool rendered=atmosphere.Render(device,Depth(),frame,config);
        if(rendered)atmosphere.RenderGodRaysAfterWorld(device,Depth());
        else {atmosphereFailed=true;VF_LOG_ERROR("native atmosphere disabled until device rebuild: %s",atmosphere.LastSkipReason());}
        if(frameNumber%300==1){
            VF_LOG_INFO("native atmosphere: rendered=%d replacing=%d skip=%s",rendered,replacingFog,atmosphere.LastSkipReason());
            VF_LOG_INFO("native fog cpu (per frame, last 300): whole-frame submit %.3f ms (%u frames)",finishFrames?finishMs/finishFrames:0.0,finishFrames);
            finishMs=0;finishFrames=0;
        }
    }
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
