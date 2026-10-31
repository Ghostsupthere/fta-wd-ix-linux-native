#include "Engine/Source/ModernAssets/NativeModelPresentation.h"
#include "Engine/Source/ModernAssets/WxlEquipment/EquipmentScale.hpp"
#include "Engine/Source/Model2/WxlAttachmentId.h"
#include "Engine/Source/lua-5.1/src/lua.h"
#include "Engine/Source/lua-5.1/src/lauxlib.h"
#include "Engine/Source/lua-5.1/src/lualib.h"
#include <algorithm>
#include <atomic>
#include <array>
#include <memory>
#include <cassert>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <limits>

// Native API stubs; compile the actual state, attachment policy and Lua bridge.
class C44Matrix {
public:
    float a0=1,a1=0,a2=0,a3=0,b0=0,b1=1,b2=0,b3=0,c0=0,c1=0,c2=1,c3=0,d0=3,d1=4,d2=5,d3=1;
};
struct CM2Shared { char m_fileName[260]{}; };
class CM2Model {
public:
    CM2Model* m_attachParent = nullptr;
    CM2Model* m_attachList = nullptr;
    CM2Model* m_attachNext = nullptr;
    CM2Shared* m_shared = nullptr;
    uint32_t m_attachmentId = 1;
    bool m_wxlEquipmentPreviewOwner = false;
    uint64_t m_wxlEquipmentPreviewGeneration = 0;
    C44Matrix m_matrixF4;
};
#include "Engine/Source/ModernAssets/NativeModelPresentation.inl"
struct CScriptObject { int type = 1; bool IsA(int candidate) const { return type == candidate; } };
class CSimpleModel : public CScriptObject {
public:
    static int GetObjectType() { return 1; }
    CM2Model* m_model = nullptr;
    NativePreviewView m_wxlPreviewView{};
    uint64_t m_wxlPreviewGeneration = 0, m_wxlEquipmentGeneration = 0;
    bool m_wxlEquipmentPreview = false;
};
struct CGPlayer_C { CM2Model* m_worldModel = nullptr; } player;
static CGPlayer_C* ClntObjMgrGetActivePlayerObj() { return &player; }
struct WOWGUID { uint64_t m_guid; };
constexpr int TYPE_PLAYER = 1;
static CGPlayer_C remotePlayer;
static uint64_t aliasPlayerGuid = 0;
static uint64_t remotePlayerGuid = 0x123456780000002aull;
static WOWGUID ClntObjMgrGetActivePlayer() { return {1}; }
static CGPlayer_C* ClntObjMgrObjectPtr(WOWGUID guid, int, const char*, int) {
    return (guid.m_guid == remotePlayerGuid || (aliasPlayerGuid && guid.m_guid == aliasPlayerGuid)) ? &remotePlayer : nullptr;
}
struct FrameScript_Method { const char* m_name; lua_CFunction m_method; };
static lua_State* context;
static void FrameScript_RegisterFunction(const char* name, lua_CFunction method) {
    lua_pushcfunction(context, method); lua_setglobal(context, name);
}
static void FrameScript_UnregisterFunction(const char* name) { lua_pushnil(context); lua_setglobal(context, name); }
uint64_t OsGetAsyncTimeClocks() { return 0; }
int64_t OsGetAsyncClocksPerSecond() { return 1; }
void OsValidateFunctionPointer(const void* pointer) { assert(pointer); }
static bool profileWeaponEnabled = true;
static bool WxlProfileEnabled(const char*, bool) { return profileWeaponEnabled; }
#include "WoW/Source/Ui/Linux/WxlModelPresentation.inl"

static void Check(lua_State* L, const char* script) {
    if (luaL_dostring(L, script)) { std::fprintf(stderr, "%s\n", lua_tostring(L, -1)); std::abort(); }
}
static void ExposeFrame(lua_State* L, CSimpleModel& frame) {
    lua_newtable(L); lua_pushlightuserdata(L, &frame); lua_rawseti(L, -2, 0); lua_setglobal(L, "frame");
}
int main() {
    context = luaL_newstate(); assert(context); luaopen_base(context); lua_settop(context, 0);
    WxlRegisterModelPresentationLua();
    CM2Model local, remote, ui, child;
    CM2Shared data; std::strcpy(data.m_fileName, "Item\\ObjectComponents\\Weapon\\Sword.m2");
    child.m_shared = &data; child.m_attachParent = &local;
    player.m_worldModel = &local; RefreshNativeEquipmentOwner();
    CSimpleModel frame; frame.m_model = &ui; ExposeFrame(context, frame);
    Check(context, R"(
        assert(WarcraftXL_IsEquipmentScaleAvailable() == true)
        local ok,w,s = WarcraftXL_SetLocalEquipmentScale(10,0)
        assert(ok and w == 1.5 and s > 0.66 and s < 0.67)
        assert(not WarcraftXL_SetLocalEquipmentScale(0/0,1))
        assert(not WarcraftXL_SetLocalEquipmentScale(1,0/0))
        assert(not WarcraftXL_SetLocalEquipmentScale('bad',1))
        assert(WarcraftXL_RegisterBackSheathUiFrame(frame,1))
        assert(not WarcraftXL_RegisterBackSheathUiFrame({},1))
        assert(WXL_SetModelPreviewCameraAt(frame,3,0.25,-0.5))
        assert(not WXL_SetModelPreviewCameraAt(frame,0/0,0,0))
    )");
    assert(frame.m_wxlPreviewView.zoom == 3 && frame.m_wxlPreviewView.pan == .25f);
    assert(frame.m_wxlPreviewView.panX == -.5f);
    assert(frame.m_wxlPreviewGeneration == NativePreviewGeneration());
    C44Matrix projection; projection.c3 = -1;
    const auto original = projection;
    ApplyNativePreview(projection, frame.m_wxlPreviewView);
    assert(projection.a0 == 3 && projection.b1 == 3 && projection.c0 == .5f && projection.c1 == -.25f);
    assert(projection.c2 == original.c2 && projection.d2 == original.d2 && original.a0 == 1);
    NativePreviewView clamp;
    assert(NormalizeNativePreview(100,100,-100,clamp) && clamp.zoom == 8 && clamp.pan == 8 && clamp.panX == -8);
    for (unsigned i=0; i<100; ++i) {
        child.m_matrixF4 = C44Matrix{}; // Animation supplies a fresh pose each frame.
        ApplyNativeEquipmentScale(&child);
        assert(child.m_matrixF4.a0 == 1.5f && child.m_matrixF4.b1 == 1.5f && child.m_matrixF4.c2 == 1.5f);
        assert(child.m_matrixF4.d0 == 3 && child.m_matrixF4.d1 == 4 && child.m_matrixF4.d2 == 5 && child.m_matrixF4.d3 == 1);
    }
    child.m_attachParent = &remote; child.m_matrixF4 = C44Matrix{}; ApplyNativeEquipmentScale(&child);
    assert(child.m_matrixF4.a0 == 1); // Another world player is excluded.
    player.m_worldModel = &remote; RefreshNativeEquipmentOwner();
    child.m_attachParent = &local; ApplyNativeEquipmentScale(&child); assert(child.m_matrixF4.a0 == 1);
    child.m_attachParent = &remote; ApplyNativeEquipmentScale(&child); assert(child.m_matrixF4.a0 == 1.5f);
    child.m_matrixF4 = C44Matrix{};
    child.m_attachParent = &ui; ApplyNativeEquipmentScale(&child); assert(child.m_matrixF4.a0 == 1.5f);
    Check(context, "assert(WarcraftXL_ClearBackSheathUiFrames())");
    child.m_matrixF4 = C44Matrix{}; ApplyNativeEquipmentScale(&child); assert(child.m_matrixF4.a0 == 1);
    Check(context, "assert(WarcraftXL_RegisterBackSheathUiFrame(frame,1))");
    child.m_attachmentId = 28; ApplyNativeEquipmentScale(&child);
    assert(std::fabs(child.m_matrixF4.a0 - 1.0f/1.5f) < 1e-6f); // Shield anchor selects shield setting.
    std::strcpy(data.m_fileName, "Item\\ObjectComponents\\Collections\\body.m2");
    child.m_matrixF4 = C44Matrix{}; ApplyNativeEquipmentScale(&child); assert(child.m_matrixF4.a0 == 1);
    const auto epoch = NativePreviewGeneration();
    Check(context, "WXL_ResetModelPreviewCameras()"); assert(epoch != NativePreviewGeneration());

    // Weapon orientation exercises the production animation policy and Lua API.
    player.m_worldModel = &local; local.m_attachList = &child;
    remotePlayer.m_worldModel = &remote;
    auto pose = [&](CM2Model* owner, uint32_t attachment, const char* path) {
        child.m_attachParent = owner; child.m_attachmentId = attachment;
        std::strcpy(data.m_fileName, path); child.m_matrixF4 = C44Matrix{};
        ApplyNativeEquipmentScale(&child);
        assert(child.m_matrixF4.d0 == 3 && child.m_matrixF4.d1 == 4 && child.m_matrixF4.d2 == 5 && child.m_matrixF4.d3 == 1);
    };
    auto basis = [&](float x, float y, float z) {
        assert(child.m_matrixF4.a0 == x && child.m_matrixF4.b1 == y && child.m_matrixF4.c2 == z);
    };
    const char* sword = "Item\\ObjectComponents\\Weapon\\Sword_1H_Test.m2";
    const char* twoHand = "Item\\ObjectComponents\\Weapon\\Sword_2H_Test.m2";
    Check(context, R"(
        assert(WarcraftXL_IsBackSheathFlipAvailable() and WarcraftXL_IsHandWeaponFlipAvailable())
        assert(WarcraftXL_SetLocalEquipmentScale(1,1))
        local ok,enabled = WarcraftXL_SetBackSheathFlipEnabled(1)
        assert(ok and enabled)
        ok,enabled = WarcraftXL_SetBackSheathFlipEnabled(0/0)
        assert(not ok and enabled)
        assert(not WarcraftXL_SetMainHandWeaponFlipEnabled('bad'))
    )");
    RefreshNativeEquipmentOwner();
    pose(&local,26,sword); basis(1,-1,-1);
    Check(context, "assert(WarcraftXL_IsBackSheathFlipEligible() and WarcraftXL_IsMainHandWeaponFlipEligible())");
    pose(&local,1,sword); basis(1,1,1);
    Check(context, "assert(WarcraftXL_SetMainHandWeaponFlipEnabled(1))");
    pose(&local,1,sword); basis(1,-1,-1);
    pose(&local,2,sword); basis(1,1,1);
    Check(context, "assert(WarcraftXL_SetMainHandReverseGripEnabled(1))");
    for (unsigned i=0;i<100;++i) { pose(&local,1,sword); basis(-1,-1,1); }
    child.m_matrixF4 = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
    ApplyNativeEquipmentScale(&child);
    assert(child.m_matrixF4.a0 == -1 && child.m_matrixF4.a3 == -4 &&
           child.m_matrixF4.b0 == -5 && child.m_matrixF4.b3 == -8 &&
           child.m_matrixF4.c0 == 9 && child.m_matrixF4.c3 == 12 &&
           child.m_matrixF4.d0 == 13 && child.m_matrixF4.d3 == 16);
    Check(context, "assert(WarcraftXL_SetOffHandWeaponFlipEnabled(1) and WarcraftXL_SetOffHandReverseGripEnabled(1))");
    pose(&local,2,sword); basis(-1,-1,1);
    pose(&local,32,sword); basis(1,1,1); // Hip eligibility never changes hip placement.
    Check(context, "assert(WarcraftXL_IsMainHandWeaponFlipEligible())");
    pose(&local,1,twoHand); basis(1,1,1); // Drawn two-handers ignore back/hand transforms.
    Check(context, "assert(WarcraftXL_IsBackSheathFlipEligible() and not WarcraftXL_IsMainHandWeaponFlipEligible())");
    pose(&local,27,twoHand); basis(1,-1,-1);
    pose(&local,1,"Item\\ObjectComponents\\Shield\\shield_1h.m2"); basis(1,1,1);
    Check(context, "assert(not WarcraftXL_IsMainHandWeaponFlipEligible())");
    pose(&local,1,"Item\\ObjectComponents\\Collections\\sword_1h.m2"); basis(1,1,1);
    pose(&ui,1,sword); basis(-1,-1,1);
    Check(context, "assert(WarcraftXL_ClearBackSheathUiFrames())");
    pose(&ui,1,sword); basis(1,1,1);
    Check(context, "assert(WarcraftXL_RegisterBackSheathUiFrame(frame,1))");
    pose(&ui,1,sword); basis(-1,-1,1);
    pose(&remote,26,sword); basis(1,1,1);
    Check(context, R"(
        assert(not WarcraftXL_SetRemoteBackSheathFlip(1,0,1)) -- Local GUID rejected.
        assert(not WarcraftXL_SetRemoteBackSheathFlip(-1,0,1))
        assert(not WarcraftXL_SetRemoteBackSheathFlip(0/0,0,1))
        assert(not WarcraftXL_SetRemoteBackSheathFlip(1e40,0,1))
        assert(not WarcraftXL_SetRemoteBackSheathFlip(42.5,305419896,1))
        assert(not WarcraftXL_SetRemoteBackSheathFlip(42,305419896,0/0))
        assert(not WarcraftXL_SetRemoteHandWeaponFlip(42,305419896,1,'bad'))
        assert(WarcraftXL_SetRemoteBackSheathFlip(42,305419896,1))
    )");
    RefreshNativeEquipmentOwner(); pose(&remote,26,sword); basis(1,-1,-1);
    Check(context, "assert(WarcraftXL_SetLocalEquipmentScale(1.5,1))");
    pose(&remote,26,sword); basis(1,-1,-1); // Remote flips do not inherit local sizing.
    pose(&local,1,sword); basis(-1.5f,-1.5f,1.5f); // Local scaling composes with both rotations.
    Check(context, "assert(WarcraftXL_SetLocalEquipmentScale(1,1))");
    Check(context, "assert(WarcraftXL_BeginRemoteBackSheathSync() and WarcraftXL_SetRemoteBackSheathFlip(42,305419896,0))");
    RefreshNativeEquipmentOwner(); pose(&remote,26,sword); basis(1,-1,-1); // Staging stays invisible.
    Check(context, "assert(WarcraftXL_EndRemoteBackSheathSync())");
    RefreshNativeEquipmentOwner(); pose(&remote,26,sword); basis(1,1,1);
    Check(context, R"(
        assert(WarcraftXL_SetRemoteHandWeaponFlip(42,305419896,1,0,1,0))
        assert(WarcraftXL_BeginRemoteBackSheathSync() and WarcraftXL_BeginRemoteHandWeaponFlipSync())
        assert(WarcraftXL_SetRemoteBackSheathFlip(42,305419896,1))
        assert(WarcraftXL_SetRemoteHandWeaponFlip(42,305419896,0,1,0,1))
    )");
    RefreshNativeEquipmentOwner(); pose(&remote,1,sword); basis(-1,-1,1);
    Check(context, "assert(WarcraftXL_EndRemoteBackSheathSync())");
    RefreshNativeEquipmentOwner(); pose(&remote,26,sword); basis(1,-1,-1);
    pose(&remote,1,sword); basis(-1,-1,1); // Ending back sync preserves active hands.
    Check(context, "assert(WarcraftXL_EndRemoteHandWeaponFlipSync())");
    RefreshNativeEquipmentOwner(); pose(&remote,1,sword); basis(1,1,1);
    pose(&remote,2,sword); basis(-1,-1,1);
    CM2Model replacement;
    remotePlayer.m_worldModel = &replacement; RefreshNativeEquipmentOwner();
    pose(&remote,26,sword); basis(1,1,1);
    pose(&replacement,26,sword); basis(1,-1,-1);
    remotePlayer.m_worldModel = nullptr; RefreshNativeEquipmentOwner();
    pose(&replacement,26,sword); basis(1,1,1);
    remotePlayer.m_worldModel = &remote; RefreshNativeEquipmentOwner();
    pose(&remote,26,sword); basis(1,-1,-1);
    Check(context, "assert(WarcraftXL_BeginRemoteBackSheathSync() and WarcraftXL_EndRemoteBackSheathSync())");
    RefreshNativeEquipmentOwner(); pose(&remote,26,sword); basis(1,1,1);
    pose(&remote,2,sword); basis(-1,-1,1); // Missing back snapshot leaves hand state intact.
    Check(context, "assert(WarcraftXL_ClearAllRemoteHandWeaponFlips())");
    RefreshNativeEquipmentOwner(); pose(&remote,2,sword); basis(1,1,1);
    // Ambiguous GUIDs resolving to the same root retain stock orientation.
    aliasPlayerGuid = remotePlayerGuid + 1;
    assert(SetNativeRemoteWeaponFlags(remotePlayerGuid,false,1));
    assert(SetNativeRemoteWeaponFlags(aliasPlayerGuid,false,1));
    RefreshNativeEquipmentOwner(); pose(&remote,26,sword); basis(1,1,1);
    ClearNativeRemoteWeaponSync(false); aliasPlayerGuid = 0;
    // World teardown drops active and staged remote state, preserving local choices.
    assert(SetNativeRemoteWeaponFlags(remotePlayerGuid,false,1));
    BeginNativeRemoteWeaponSync(true);
    assert(SetNativeRemoteWeaponFlags(remotePlayerGuid,true,2));
    RefreshNativeEquipmentOwner(); pose(&remote,26,sword); basis(1,-1,-1);
    InvalidateNativeWeaponWorld();
    assert(GetNativeWeaponFlip(1) && !backSync && !handSync);
    RefreshNativeEquipmentOwner(); pose(&remote,26,sword); basis(1,1,1);
    pose(&local,26,sword); basis(1,-1,-1);
    for (uint64_t guid=2;guid<82;++guid) assert(SetNativeRemoteWeaponFlags(guid,false,1));
    assert(!SetNativeRemoteWeaponFlags(9999,false,1));
    assert(SetNativeRemoteWeaponFlags(2,false,0) && SetNativeRemoteWeaponFlags(9999,false,1));
    ClearNativeRemoteWeaponSync(false);
    Check(context, "assert(WarcraftXL_InvalidateWeaponFlipEligibility() and WarcraftXL_RefreshWeaponPlacement())");
    WxlUnregisterModelPresentationLua();
    assert(localModel.load() == nullptr && !GetNativeWeaponFlip(31));
    Check(context,"assert(WarcraftXL_SetBackSheathFlipEnabled == nil and WarcraftXL_SetRemoteHandWeaponFlip == nil)");
    float weapon, shield; GetNativeEquipmentScales(weapon,shield); assert(weapon == 1 && shield == 1);
    Check(context, "assert(WarcraftXL_SetLocalEquipmentScale == nil and WXL_SetModelPreviewCameraAt == nil)");
    profileWeaponEnabled = false; WxlRegisterModelPresentationLua();
    Check(context, R"(
        assert(not WarcraftXL_IsBackSheathFlipAvailable() and not WarcraftXL_IsHandWeaponFlipAvailable())
        assert(not WarcraftXL_SetBackSheathFlipEnabled(1))
        assert(not WarcraftXL_SetRemoteBackSheathFlip(42,305419896,1))
        assert(WarcraftXL_IsEquipmentScaleAvailable()) -- Independent native sizing is preserved.
    )");
    SetNativeWeaponFlip(31,true); pose(&local,26,sword); basis(1,1,1);
    WxlUnregisterModelPresentationLua();
    lua_close(context);
}
