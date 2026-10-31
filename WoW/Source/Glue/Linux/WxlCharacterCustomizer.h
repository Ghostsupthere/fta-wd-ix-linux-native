#ifndef WOW_SOURCE_GLUE_LINUX_WXLCHARACTERCUSTOMIZER_H
#define WOW_SOURCE_GLUE_LINUX_WXLCHARACTERCUSTOMIZER_H

// Native port of WarcraftXL's wxl-character-customizer: the WXL_CharacterCreate* and WXL_EyeHalo*
// Lua functions used by the deployed GlueXML (WXL_CharacterCustomization.lua and friends), the
// creation-scene druid-form and eye-halo previews, and the persisted eye-halo choice. The Windows
// module detours five client functions at fixed addresses; here the creation code calls these
// hooks directly. Extended-race emotes live in SoundInterface2Emotes.cpp.

#if defined(WOWDECOMP_LINUX)

class CCharacterComponent;

void WxlRegisterCharacterCustomizerLua();
void WxlUnregisterCharacterCustomizerLua();

// CCharacterCreation::Dress: true when the preview must stay unequipped (caller returns).
bool WxlCharacterCreateHidesArmor();
// After a normal Dress: the Tuskarr cape geoset is a backpack that cannot use cloak textures.
void WxlCharacterCreateAfterDress();

// Creation-scene lifecycle (druid form and eye halo are child models of the creation scene).
void WxlCreationSceneBeforeBuild();
void WxlCreationSceneFacing(float facing);
void WxlCreationSceneDestroy(const CCharacterComponent* component);

// Wraps CCharacterCreation::SetCharCustomizeModel: the previews are released before the background
// changes and shown again against the new one.
struct WxlCreationBackgroundScope {
    WxlCreationBackgroundScope();
    ~WxlCreationBackgroundScope();
    WxlCreationBackgroundScope(const WxlCreationBackgroundScope&) = delete;
    WxlCreationBackgroundScope& operator=(const WxlCreationBackgroundScope&) = delete;

private:
    char m_preview[256];
};

#endif  // WOWDECOMP_LINUX

#endif
