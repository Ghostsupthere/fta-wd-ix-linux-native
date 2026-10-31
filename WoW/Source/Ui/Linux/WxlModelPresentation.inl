// Production Lua bridge, also compiled by the native API/Lua fixture.
namespace {
CM2Model* LocalEquipmentRoot() {
    const auto* player = ClntObjMgrGetActivePlayerObj();
    return player ? player->m_worldModel : nullptr;
}
CSimpleModel* PreviewFrame(lua_State* L) {
    if (!lua_istable(L, 1)) return nullptr;
    lua_rawgeti(L, 1, 0);
    auto* object = static_cast<CScriptObject*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    return object && object->IsA(CSimpleModel::GetObjectType()) ? static_cast<CSimpleModel*>(object) : nullptr;
}
int SetView(lua_State* L) {
    auto* frame = PreviewFrame(L);
    NativePreviewView view;
    const bool accepted = frame && lua_isnumber(L, 2) && lua_isnumber(L, 3) &&
        NormalizeNativePreview(lua_tonumber(L, 2), lua_tonumber(L, 3),
            lua_isnumber(L, 4) ? lua_tonumber(L, 4) : 0, view);
    if (accepted) { frame->m_wxlPreviewView = view; frame->m_wxlPreviewGeneration = NativePreviewGeneration(); }
    lua_pushboolean(L, accepted); return 1;
}
int ResetViews(lua_State*) { ResetNativePreviewCameras(); return 0; }
int ScaleAvailable(lua_State* L) { lua_pushboolean(L, true); return 1; }
int SetScales(lua_State* L) {
    const bool accepted = lua_isnumber(L, 1) && lua_isnumber(L, 2) &&
        SetNativeEquipmentScales(lua_tonumber(L, 1), lua_tonumber(L, 2));
    float weapon, shield; GetNativeEquipmentScales(weapon, shield);
    lua_pushboolean(L, accepted); lua_pushnumber(L, weapon); lua_pushnumber(L, shield); return 3;
}
int RegisterFrame(lua_State* L) {
    auto* frame = PreviewFrame(L);
    if (frame) {
        frame->m_wxlEquipmentPreview = !lua_isnumber(L, 2) || lua_tonumber(L, 2) != 0;
        frame->m_wxlEquipmentGeneration = NativeEquipmentPreviewGeneration();
        if (frame->m_model) {
            frame->m_model->m_wxlEquipmentPreviewOwner = frame->m_wxlEquipmentPreview;
            frame->m_model->m_wxlEquipmentPreviewGeneration = frame->m_wxlEquipmentGeneration;
        }
    }
    lua_pushboolean(L, frame != nullptr); return 1;
}
int ClearFrames(lua_State* L) { ClearNativeEquipmentPreviews(); lua_pushboolean(L, true); return 1; }
#include "WoW/Source/Ui/Linux/WxlWeaponFlipLua.inl"
FrameScript_Method methods[]{
    {"WarcraftXL_IsBackSheathFlipAvailable", WeaponFlipAvailable},
    {"WarcraftXL_IsHandWeaponFlipAvailable", WeaponFlipAvailable},
    {"WarcraftXL_SetBackSheathFlipEnabled", SetFlip<1>},
    {"WarcraftXL_SetMainHandWeaponFlipEnabled", SetFlip<2>},
    {"WarcraftXL_SetOffHandWeaponFlipEnabled", SetFlip<4>},
    {"WarcraftXL_SetMainHandReverseGripEnabled", SetFlip<8>},
    {"WarcraftXL_SetOffHandReverseGripEnabled", SetFlip<16>},
    {"WarcraftXL_IsBackSheathFlipEligible", FlipEligible<1>},
    {"WarcraftXL_IsMainHandWeaponFlipEligible", FlipEligible<2>},
    {"WarcraftXL_IsOffHandWeaponFlipEligible", FlipEligible<4>},
    {"WarcraftXL_InvalidateWeaponFlipEligibility", RefreshWeaponPlacement},
    {"WarcraftXL_RefreshWeaponPlacement", RefreshWeaponPlacement},
    {"WarcraftXL_BeginRemoteBackSheathSync", BeginRemote<false>},
    {"WarcraftXL_SetRemoteBackSheathFlip", SetRemoteBack},
    {"WarcraftXL_EndRemoteBackSheathSync", EndRemote<false>},
    {"WarcraftXL_ClearAllRemoteBackSheathFlips", ClearRemote<false>},
    {"WarcraftXL_BeginRemoteHandWeaponFlipSync", BeginRemote<true>},
    {"WarcraftXL_SetRemoteHandWeaponFlip", SetRemoteHands},
    {"WarcraftXL_EndRemoteHandWeaponFlipSync", EndRemote<true>},
    {"WarcraftXL_ClearAllRemoteHandWeaponFlips", ClearRemote<true>},
    {"WXL_SetModelPreviewCamera", SetView}, {"WXL_SetModelPreviewCameraAt", SetView},
    {"WXL_ResetModelPreviewCameras", ResetViews},
    {"WarcraftXL_IsEquipmentScaleAvailable", ScaleAvailable},
    {"WarcraftXL_SetLocalEquipmentScale", SetScales},
    {"WarcraftXL_RegisterBackSheathUiFrame", RegisterFrame},
    {"WarcraftXL_ClearBackSheathUiFrames", ClearFrames},
};
}
void WxlRegisterModelPresentationLua() {
    ResetNativeWeaponPresentation();
    SetNativeWeaponFeatureEnabled(WxlProfileEnabled("WXL_BACK_SHEATH_FLIP", true));
    SetNativeRemoteWeaponResolver(RemoteEquipmentRoot);
    ResetNativePreviewCameras(); ClearNativeEquipmentPreviews(); SetNativeEquipmentScales(1, 1);
    SetNativeEquipmentResolver(LocalEquipmentRoot);
    for (const auto& method : methods) FrameScript_RegisterFunction(method.m_name, method.m_method);
}
void WxlUnregisterModelPresentationLua() {
    SetNativeRemoteWeaponResolver(nullptr); ResetNativeWeaponPresentation();
    SetNativeEquipmentResolver(nullptr); SetNativeEquipmentScales(1, 1);
    ResetNativePreviewCameras(); ClearNativeEquipmentPreviews();
    for (const auto& method : methods) FrameScript_UnregisterFunction(method.m_name);
}
