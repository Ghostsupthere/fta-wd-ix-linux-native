// Match the existing native D3D9 translation unit's Windows type boundary.
#if defined(WOWDECOMP_LINUX)
#define WOWDECOMP_GX_DXVK
#include <d3d9.h>
#endif
#include "WoW/Source/WorldClient/Linux/WxlLootBeam.h"
#if defined(WOWDECOMP_LINUX)
#include "Engine/Source/Gx/CGxDeviceD3d9Ex/CGxDeviceD3d9Ex.h"
#include "Engine/Source/Gx/Gx.h"
#include "Engine/Source/ModernAssets/WxlLootBeam/BeamRenderer.hpp"
#include "Engine/Source/ModernAssets/WxlLootBeam/LootBeamScene.hpp"
#include "WoW/Source/WorldClient/Linux/WxlVisualEnvironment.h"
#include "WoW/Source/WorldClient/World.h"
#include "WoW/Source/ObjectMgrClient/ObjectMgrClient.h"
#include "WoW/Source/Object/ObjectClient/Player_C.h"
#include "WoW/Source/Object/ObjectClient/Unit_C.h"
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace {
namespace beam = wxl::scripts::lootbeam;
static_assert(offsetof(CGUnitData,m_unitFieldHealth)+OBJECT_END*4==0x18*4);
static_assert(offsetof(CGUnitData,m_unitDynamicFlags)+OBJECT_END*4==0x4F*4);
static_assert(offsetof(CGUnitData,m_unitFieldPadding)+OBJECT_END*4==0x93*4);
beam::State state;
std::vector<beam::Vertex> vertices;
struct ScanStats { unsigned units=0,dead=0,lootable=0,hinted=0,zeroHints=0,invalidHints=0,candidates=0; uint32_t firstHint=0; } stats;
uint64_t lastReport=0;
bool reported=false;
std::string lastReportStage;
void Report(uint64_t now,const char* stage,int api) {
    if (reported && now>=lastReport && now-lastReport<100) return;
    if (reported && lastReportStage==stage && now>=lastReport && now-lastReport<3000) return;
    std::error_code error;std::filesystem::create_directories("Logs",error);
    std::ofstream file("Logs/LootBeam.log",reported ? std::ios::app : std::ios::trunc);
    if (!file) return;
    const auto clip=beam::ClipCounts(state.lastScene,vertices);
    file<<"stage="<<stage<<" api="<<api<<" units="<<stats.units<<" dead="<<stats.dead
        <<" lootable="<<stats.lootable<<" hinted="<<stats.hinted<<" zeroHints="<<stats.zeroHints
        <<" invalidHints="<<stats.invalidHints<<" firstHint="<<stats.firstHint
        <<" candidates="<<stats.candidates<<" vertices="<<vertices.size()
        <<" clipFront="<<clip[0]<<" clipInside="<<clip[1]<<'\n';
    lastReport=now;lastReportStage=stage;reported=true;
}
bool Enabled() {
    static const bool markerDisabled=[] {
        std::error_code error;
        const auto executable=std::filesystem::canonical("/proc/self/exe",error);
        if (error) return false;
        const auto local=executable.parent_path()/"WarcraftXL_loot_beam.disable";
        if (std::filesystem::exists(local,error)) return true;
        error.clear();
        const auto data=std::filesystem::canonical(executable.parent_path()/"Data",error);
        return !error && std::filesystem::exists(data.parent_path()/"WarcraftXL_loot_beam.disable",error);
    }();
    return !markerDisabled && WxlProfileEnabled("WXL_LOOT_BEAM",true);
}
int32_t Visit(WOWGUID guid,void* context) {
    auto& scene=*static_cast<beam::Scene*>(context);
    if (++scene.visited>4096) return 0;
    auto* unit=static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(guid,TYPE_UNIT,__FILE__,__LINE__));
    if (!unit || !unit->m_obj || !unit->m_unit) return 1;
    ++stats.units;
    if ((unit->m_obj->m_type&TYPE_PLAYER) || unit->m_unit->m_unitFieldHealth!=0) return 1;
    ++stats.dead;
    if (!beam::Lootable(unit->m_obj->m_type,unit->m_unit->m_unitFieldHealth,unit->m_unit->m_unitDynamicFlags)) return 1;
    if (!stats.lootable) stats.firstHint=unit->m_unit->m_unitFieldPadding;
    ++stats.lootable;
    const auto hint=unit->m_unit->m_unitFieldPadding;
    if (!hint) ++stats.zeroHints;
    else if (!beam::HasRemainingLoot(hint)) ++stats.invalidHints;
    if (!beam::HasRemainingLoot(hint)) return 1;
    ++stats.hinted;
    const auto position=unit->GetPosition();
    beam::Corpse corpse;
    corpse.guid=guid.m_guid;corpse.hint=unit->m_unit->m_unitFieldPadding;
    corpse.position[0]=position.x;corpse.position[1]=position.y;corpse.position[2]=position.z;
    beam::Consider(scene,corpse);
    return 1;
}
}
void WxlClearLootBeams() { state.Clear();vertices.clear();stats={}; }
void WxlDrawLootBeams() {
    const uint64_t now=std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    auto* gx=g_theGxDevicePtr;
    const int api=gx?int(gx->m_api):-1;
    if (!Enabled()) {WxlClearLootBeams();Report(now,"disabled",api);return;}
    if (!gx || gx->m_api!=GxApi_D3d9Ex) {Report(now,"unsupported-backend",api);return;}
    auto* device=static_cast<CGxDeviceD3d9Ex*>(gx)->m_d3dDevice;
    if (!device) {Report(now,"no-device",api);return;}
    beam::Scene scene;
    bool fresh=false,scanned=false;
    if (ClntObjMgrGetActivePlayerObj()) {
        const auto* camera=World::GetCamPos();
        if (camera) {
            scene.camera[0]=camera->x;scene.camera[1]=camera->y;scene.camera[2]=camera->z;
            C44Matrix view;GxXformView(view);
            std::memcpy(scene.view,&view,sizeof(scene.view));
            // This is already the native D3D depth projection. Do not repeat
            // the Windows hook's GL [-w,w] -> D3D [0,w] conversion.
            std::memcpy(scene.projection,&gx->m_projNative,sizeof(scene.projection));
            beam::RebaseWorldView(scene);
            fresh=beam::FiniteScene(scene);
            if (fresh && state.NeedScan(now)) { stats={};ClntObjMgrEnumVisibleObjects(Visit,&scene);stats.candidates=scene.count;scanned=true; }
        }
    }
    state.Update(scene,fresh,scanned,now,vertices);
    if (!vertices.empty()) {
        float minX,maxX,minY,maxY,minZ,maxZ;
        GxXformViewport(minX,maxX,minY,maxY,minZ,maxZ);
        const float depthRange[]{minZ,maxZ};
        beam::DrawResult result;
        beam::Draw(device,vertices.data(),static_cast<unsigned>(vertices.size()),state.lastScene.view,state.lastScene.projection,depthRange,&result);
        Report(now,beam::DrawResultName(result),api);
    } else {
        const char* stage=!fresh ? "no-valid-camera-or-player" :
            !stats.lootable ? "no-lootable-corpses" : !stats.hinted ? "missing-or-invalid-loot-hints" : "outside-range-or-faded";
        Report(now,stage,api);
    }
}
#else
void WxlClearLootBeams() {}
void WxlDrawLootBeams() {}
#endif
