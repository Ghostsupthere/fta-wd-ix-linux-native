// Native port of the supplied wxl-character-customizer (GPL-3.0-or-later, Copyright (C) 2026
// WarcraftXL). The Lua contract and the selection, druid-preview and eye-halo rules follow
// scripts/wxl-character-customizer; see WxlCharacterCustomizer.h for how the fixed-address detours
// became direct hooks in the creation code.
#include "WoW/Source/Glue/Linux/WxlCharacterCustomizer.h"

#if defined(WOWDECOMP_LINUX)

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>

#include "Engine/Source/Frame/CScriptObject.h"
#include "Engine/Source/Frame/CSimpleModel.h"
#include "Engine/Source/FrameScript/FrameScript.h"
#include "Engine/Source/Model2/M2Model.h"
#include "Engine/Source/Model2/M2Scene.h"
#include "Engine/Source/ModernAssets/NativeDiagnostic.h"
#include "Engine/Source/ModernAssets/WxlCustomizer/AppearanceSelection.hpp"
#include "Engine/Source/ModernAssets/WxlCustomizer/EyeHaloChoice.hpp"
#include "Engine/Source/Tempest/C3Vector.h"
#include "Engine/Source/lua-5.1/src/lauxlib.h"
#include "Engine/Source/lua-5.1/src/lua.h"
#include "WoW/Common/ComponentCore/CharacterComponent.h"
#include "WoW/Common/ComponentCore/ComponentUtils.h"
#include "WoW/Source/DB/Rec/CharSectionsRec.h"
#include "WoW/Source/Glue/CSimpleModelFFX.h"
#include "WoW/Source/Glue/CharacterCreation.h"
#include "WoW/Source/Glue/CharacterSelection.h"

namespace {

namespace customizer = wxl::customizer;
namespace halo = wxl::customizer::halo;

// M2Cache keys by basename unless this flag is set; race variants deliberately share filenames.
constexpr uint32_t kFullPathCacheKey = 0x10;
constexpr uint32_t kEyeHaloAttachment = 11;
constexpr uint32_t kDruidClass = 11;
constexpr const char* kEyeHaloThemes[] = {"", "Fel", "WhiteArcane", "Sunfire", "Twilight", "Sanguine"};

bool g_hideArmor = false;  // UI-thread only; never saved or sent to the server

CCharacterComponent* Character() { return CCharacterCreation::m_character; }
CSimpleModelFFX* Frame() { return CCharacterCreation::m_charCustomizeFrame; }

// The registered CharacterCreate frame passed as Lua argument 1, without raising a Lua error.
bool IsCreationFrame(lua_State* L, int index) {
    if (!Frame() || lua_type(L, index) != LUA_TTABLE) return false;
    lua_rawgeti(L, index, 0);
    void* object = lua_touserdata(L, -1);
    lua_settop(L, -2);
    return object && object == static_cast<void*>(static_cast<CScriptObject*>(Frame()));
}

// The creation preview's character, only for the registered frame and a race/sex the DBC tables cover.
CCharacterComponent* CreationCharacter(lua_State* L) {
    CCharacterComponent* character = Character();
    if (!character || !IsCreationFrame(L, 1)) return nullptr;
    const CharacterPreferences& preferences = character->m_data.m_preferences;
    if (preferences.m_raceID == 0 || preferences.m_raceID > 36 || preferences.m_sexID > 1) return nullptr;
    // Check before any row lookup or counts[race * 2 + sex]: a damaged model cannot index past them.
    if (preferences.m_raceID * 2 + preferences.m_sexID >= CCharacterComponent::s_chrVarArrayLength) return nullptr;
    return character;
}

// --- appearance state and options ----------------------------------------------------------------

int GetState(lua_State* L) {
    CCharacterComponent* character = CreationCharacter(L);
    if (!character) return 0;
    const CharacterPreferences& p = character->m_data.m_preferences;
    // Race, zero-based sex, raw skin, face, hair style, hair color, facial feature, preview state.
    lua_pushnumber(L, p.m_raceID);
    lua_pushnumber(L, p.m_sexID);
    lua_pushnumber(L, p.m_skinID);
    lua_pushnumber(L, p.m_faceId);
    lua_pushnumber(L, p.m_hairStyleId);
    lua_pushnumber(L, p.m_hairColorId);
    lua_pushnumber(L, p.m_facialHairId);
    lua_pushboolean(L, g_hideArmor ? 1 : 0);
    lua_pushnumber(L, p.m_classId);  // class invalidates the UI's available-options cache
    return 9;
}

// Reads the loaded DBC index. Never cycles or alters the preview to count options.
bool IsAvailable(CCharacterComponent* character, uint32_t part, uint32_t value, uint32_t* resolvedFace = nullptr) {
    st_race* sections = CCharacterComponent::s_chrVarArray;
    if (!sections || part > 4 || value > 255) return false;
    const CharacterPreferences& p = character->m_data.m_preferences;
    const uint32_t race = p.m_raceID, sex = p.m_sexID;
    const COMPONENT_SELECTION source = GetSelectionFromContext(COMPONENT_CONTEXT_0, static_cast<int32_t>(p.m_classId));
    const auto valid = [&](uint32_t section, uint32_t variation, uint32_t color) {
        CharSectionsRec* row = ComponentGetSectionsRecord(sections, race, sex, static_cast<CHARTEXTUREVARIATIONS>(section), variation, color, nullptr);
        return row && ComponentFlagsMatch(row->m_flags, source);
    };
    const uint32_t skin = p.m_skinID, face = p.m_faceId, style = p.m_hairStyleId, color = p.m_hairColorId, feature = p.m_facialHairId;
    bool texturedFeatures = false;
    ComponentGetSectionsRecord(sections, race, sex, CHARTEXTUREVAR_FACIALHAIR, feature, color, &texturedFeatures);
    switch (part) {
        case 0: {
            uint32_t selectedFace = face;
            if (!customizer::ResolveSkinFace(race, value, face, valid, selectedFace)) return false;
            if (resolvedFace) *resolvedFace = selectedFace;
            return true;
        }
        case 1:
            return valid(1, value, skin) && valid(0, 0, skin) && valid(4, 0, skin);
        case 2:
            return valid(3, value, color);
        case 3:
            return valid(3, style, value) && (!texturedFeatures || valid(2, feature, value));
        case 4: {
            const uint32_t* counts = CCharacterComponent::s_characterFacialHairStylesList;
            if (!counts) return false;
            const uint32_t count = counts[race * 2 + sex];
            return (value < count || (count == 0 && value == 0)) && (!texturedFeatures || valid(2, value, color));
        }
        default:
            return false;
    }
}

int GetOptions(lua_State* L) {
    CCharacterComponent* character = CreationCharacter(L);
    if (!character) return 0;
    for (uint32_t part = 0; part < 5; ++part) {
        std::string options;
        for (uint32_t value = 0; value < 256; ++value) {
            if (!IsAvailable(character, part, value)) continue;
            if (!options.empty()) options += ',';
            options += std::to_string(value);
        }
        lua_pushstring(L, options.c_str());
    }
    return 5;
}

bool WholeNumber(lua_State* L, int index, double maximum, uint32_t& out) {
    if (lua_type(L, index) != LUA_TNUMBER) return false;
    const double value = lua_tonumber(L, index);
    if (!std::isfinite(value) || value < 0 || value > maximum || value != std::floor(value)) return false;
    out = static_cast<uint32_t>(value);
    return true;
}

int SetPart(lua_State* L) {
    CCharacterComponent* character = CreationCharacter(L);
    uint32_t part = 0, value = 0;
    if (!character || !WholeNumber(L, 2, 4, part) || !WholeNumber(L, 3, 255, value)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    CharacterPreferences& p = character->m_data.m_preferences;
    uint32_t selectedFace = p.m_faceId;
    if (!IsAvailable(character, part, value, &selectedFace)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    switch (part) {
        case 0:
            // The skin setter calls the face setter synchronously, and an OLD-face/NEW-skin pair with
            // no row is dereferenced before control returns. Publish the validated destination face
            // first; the skin setter then rebuilds its textures for that exact pair.
            if (customizer::HasVariableFaceSets(p.m_raceID) && p.m_faceId != selectedFace) p.m_faceId = selectedFace;
            character->SetSkinColor(static_cast<int32_t>(value), true, true, nullptr);
            break;
        case 1: character->SetFace(static_cast<int32_t>(value), true, nullptr); break;
        case 2: character->SetHairStyle(static_cast<int32_t>(value), nullptr); break;
        case 3: character->SetHairColor(static_cast<int32_t>(value), true, nullptr); break;
        case 4: character->SetBeardStyle(static_cast<int32_t>(value), true, nullptr); break;
    }
    CCharacterCreation::m_prevSkinIndex = p.m_skinID;
    character->RenderPrep(1);
    const uint32_t current[] = {p.m_skinID, p.m_faceId, p.m_hairStyleId, p.m_hairColorId, p.m_facialHairId};
    bool changed = current[part] == value;
    if (part == 0 && customizer::HasVariableFaceSets(p.m_raceID)) changed = changed && p.m_faceId == selectedFace;
    lua_pushboolean(L, changed ? 1 : 0);
    return 1;
}

int SetSkin(lua_State* L) {
    CCharacterComponent* character = CreationCharacter(L);
    uint32_t target = 0;
    if (!character || !WholeNumber(L, 2, 255, target)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    CharacterPreferences& p = character->m_data.m_preferences;
    const uint32_t start = p.m_skinID;
    // Native cycling checks this race/sex/class, current face and underwear and preserves every other
    // part. Rendering is deferred until the final valid selection is reached.
    for (uint32_t step = 0; p.m_skinID != target && step < 256; ++step) {
        if (!character->SetNextSkin(COMPONENT_CONTEXT_0) || p.m_skinID == start) break;
    }
    const bool selected = p.m_skinID == target;
    // An unavailable target completes the native ring and returns to the original skin.
    CCharacterCreation::m_prevSkinIndex = p.m_skinID;
    character->RenderPrep(1);
    lua_pushboolean(L, selected ? 1 : 0);
    return 1;
}

// The same slots and weapon attachments the native outfit path clears. The model is independent of
// inventory, character-list records and character-create packets.
void ClearPreviewEquipment(CCharacterComponent* character) {
    for (uint32_t slot = 0; slot < NUM_ITEM_SLOTS; ++slot) character->RemoveItem(static_cast<ITEM_SLOT>(slot));
    CM2Model* model = character->m_data.m_model;
    if (!model) return;
    CCharacterComponent::RemoveHandItem(model, INVSLOT_MAINHAND, SHEATHE_NONE, false);
    CCharacterComponent::RemoveHandItem(model, INVSLOT_OFFHAND, SHEATHE_NONE, false);
    CCharacterComponent::RemoveHandItem(model, INVSLOT_OFFHAND, SHEATHE_NONE, true);
    CCharacterComponent::RemoveHandItem(model, INVSLOT_RANGED, SHEATHE_NONE, false);
}

int SetArmorHidden(lua_State* L) {
    CCharacterComponent* character = CreationCharacter(L);
    if (!character || lua_type(L, 2) != LUA_TNUMBER) {
        lua_pushboolean(L, 0);
        return 1;
    }
    g_hideArmor = lua_tonumber(L, 2) == 1;
    if (g_hideArmor) ClearPreviewEquipment(character);
    else CCharacterCreation::Dress();
    character->RenderPrep(1);
    lua_pushboolean(L, 1);
    return 1;
}

// --- creation scene: druid form and eye halo -------------------------------------------------------

CM2Model* g_form = nullptr;  // one reference acquired from CM2Scene::CreateModel
const CCharacterComponent* g_formCharacter = nullptr;
const CSimpleModelFFX* g_formFrame = nullptr;
std::string g_formPath;

CM2Model* g_halo = nullptr;  // independent owned child; never replaces the helmet attachment
const CCharacterComponent* g_haloCharacter = nullptr;
const CSimpleModelFFX* g_haloFrame = nullptr;
const CM2Model* g_haloHuman = nullptr;
std::string g_haloPath;
uint32_t g_haloTheme = 0;

void Detach(CM2Model* model) {
    if (model && model->m_attachParent) model->DetachFromParent();
}

void Attach(CM2Model* model, CM2Model* parent) { model->AttachToParent(parent, 0, nullptr, 0); }

void Pose(float facing) {
    if (g_form) g_form->SetWorldTransform(C3Vector(0.0f, 0.0f, 0.0f), facing, 1.0f);
}

void ClearHalo() {
    if (g_halo) {
        // Detach this child only: removing attachment 11 wholesale would also take a helmet or goggles.
        Detach(g_halo);
        g_halo->Release();
    }
    g_halo = nullptr;
    g_haloCharacter = nullptr;
    g_haloFrame = nullptr;
    g_haloHuman = nullptr;
    g_haloPath.clear();
}

bool ShowHalo(uint32_t theme) {
    if (theme == 0) {
        ClearHalo();
        return true;
    }
    CCharacterComponent* character = Character();
    CSimpleModelFFX* frame = Frame();
    if (theme >= std::size(kEyeHaloThemes) || !character || !frame) return false;
    const uint32_t race = character->m_data.m_preferences.m_raceID, sex = character->m_data.m_preferences.m_sexID;
    if (!halo::IsHaloRace(race) || sex > 1) {
        ClearHalo();
        return false;
    }
    if (g_form) {
        ClearHalo();  // suspended while the druid animal preview is visible
        return true;
    }
    CM2Model* human = character->m_data.m_model;
    if (!human) return false;
    const std::string path = std::string("Spells\\AshenEyeHalos\\") + (sex == 1 ? "Female_" : "Male_") + kEyeHaloThemes[theme] + ".m2";
    if (g_halo && g_haloCharacter == character && g_haloFrame == frame && g_haloHuman == human && g_haloPath == path) {
        if (g_halo->m_attachParent != human) {
            Detach(g_halo);
            g_halo->AttachToParent(human, kEyeHaloAttachment, nullptr, 0);
        }
        return true;
    }
    // Release before acquiring a replacement, including a same-address native parent rebuild.
    ClearHalo();
    CM2Scene* scene = frame->GetScene();
    CM2Model* model = scene ? scene->CreateModel(path.c_str(), kFullPathCacheKey) : nullptr;
    if (!model) return false;
    g_halo = model;
    g_haloCharacter = character;
    g_haloFrame = frame;
    g_haloHuman = human;
    g_haloPath = path;
    model->SetBoneSequence(0xFFFFFFFF, 0, 0, 0, 1.0f, 1, 1);
    model->AttachToParent(human, kEyeHaloAttachment, nullptr, 0);
    WxlNativeDiagnostic("character-customizer: eye halo race=%u sex=%u theme=%u", race, sex, theme);
    return true;
}

void ClearForm(bool restore) {
    if (!g_form) return;
    Detach(g_form);
    g_form->Release();
    g_form = nullptr;
    // Keep the native character object and appearance cache intact: only the visible attachment
    // changes, so creation packets, skin/face controls and equipment stay native.
    if (restore && Character() == g_formCharacter && Frame() == g_formFrame && Character()) {
        CM2Model* human = Character()->m_data.m_model;
        CM2Model* background = Frame()->m_model;
        if (human && background) Attach(human, background);
    }
    g_formCharacter = nullptr;
    g_formFrame = nullptr;
    g_formPath.clear();
}

bool ShowForm(const char* path) {
    CCharacterComponent* character = Character();
    CSimpleModelFFX* frame = Frame();
    if (!character || !frame || character->m_data.m_preferences.m_classId != kDruidClass) return false;
    CM2Model* human = character->m_data.m_model;
    CM2Model* background = frame->m_model;
    if (!human || !background) return false;
    if (g_form && g_formCharacter == character && g_formFrame == frame && g_formPath == path) {
        // The background can be refreshed directly: reattach without loading another M2.
        if (g_form->m_attachParent != background) {
            Detach(human);
            Detach(g_form);
            Attach(g_form, background);
        }
        return true;
    }
    CM2Scene* scene = frame->GetScene();
    CM2Model* form = scene ? scene->CreateModel(path, kFullPathCacheKey) : nullptr;
    if (!form) return false;
    ClearHalo();
    ClearForm(true);
    g_form = form;
    g_formFrame = frame;
    g_formCharacter = character;
    g_formPath = path;
    // The lighting callback and idle animation the native BuildModel gives the character.
    frame->m_lightArray2[0].unk_01BC = 1;
    form->m_lightingCallback = &CCharacterSelection::GenericLightingCallback;
    form->m_lightingArg = frame->m_lightArray2;
    form->SetBoneSequence(0xFFFFFFFF, 0, 0, 0, 1.0f, 1, 1);
    Pose(CCharacterCreation::m_charFacing);
    Detach(human);
    Attach(form, background);
    WxlNativeDiagnostic("character-customizer: druid preview race=%u sex=%u model=%s", character->m_data.m_preferences.m_raceID, character->m_data.m_preferences.m_sexID, path);
    return true;
}

bool ReadyFlag() { return true; }

int Ready(lua_State* L) {
    lua_pushboolean(L, ReadyFlag() ? 1 : 0);
    return 1;
}

int SetDruidForm(lua_State* L) {
    size_t length = 0;
    const char* path = lua_type(L, 2) == LUA_TSTRING ? lua_tolstring(L, 2, &length) : nullptr;
    bool success = false;
    if (Frame() && IsCreationFrame(L, 1) && path) {
        if (length == 0) {
            ClearForm(true);
            ShowHalo(g_haloTheme);
            success = true;
        } else if (length < 240 && std::strlen(path) == length && std::strncmp(path, "Creature\\DinkleDruid\\", 21) == 0 && length > 24 &&
                   std::strcmp(path + length - 3, ".m2") == 0 && !std::strstr(path, "..") && !std::strchr(path, ':') && !std::strchr(path, '/')) {
            success = ShowForm(path);
        }
    }
    // The add-on repeats this call every 0.15 s; log only when the request or its result changes.
    static std::string lastRequest;
    static bool lastSuccess = false;
    const std::string request = path ? std::string(path, length) : std::string("<none>");
    if (request != lastRequest || success != lastSuccess) {
        WxlNativeDiagnostic("character-customizer: SetDruidForm '%s' -> %s", request.c_str(), success ? "ok" : "rejected");
        lastRequest = request;
        lastSuccess = success;
    }
    lua_pushboolean(L, success ? 1 : 0);
    return 1;
}

int SetEyeHalo(lua_State* L) {
    bool success = false;
    if (Frame() && IsCreationFrame(L, 1) && lua_type(L, 2) == LUA_TNUMBER) {
        const double value = lua_tonumber(L, 2);
        if (std::isfinite(value) && value >= 0 && value < std::size(kEyeHaloThemes) && std::floor(value) == value) {
            g_haloTheme = static_cast<uint32_t>(value);
            success = ShowHalo(g_haloTheme);
        }
    }
    lua_pushboolean(L, success ? 1 : 0);
    return 1;
}

// --- persisted eye-halo choice ------------------------------------------------------------------------

std::string g_account;

struct Trial {
    std::string account, realm, name;
    unsigned theme = 0, race = 0, sex = 0;
    std::time_t staged = 0;
} g_trial;

std::string Text(lua_State* L, int index, size_t limit) {
    if (lua_type(L, index) != LUA_TSTRING) return {};
    size_t length = 0;
    const char* text = lua_tolstring(L, index, &length);
    if (!text || !length || length > limit || std::strlen(text) != length) return {};
    for (size_t i = 0; i < length; ++i)
        if (static_cast<unsigned char>(text[i]) < 32) return {};
    return std::string(text, length);
}

std::filesystem::path IniPath() {
    std::error_code error;
    const auto executable = std::filesystem::canonical("/proc/self/exe", error);
    if (error) return {};
    return executable.parent_path() / "WTF" / "WarcraftXL-EyeHalo.ini";
}

std::string ReadIni() {
    const auto path = IniPath();
    std::ifstream input(path, std::ios::binary);
    if (path.empty() || !input) return {};
    std::ostringstream text;
    text << input.rdbuf();
    return text.str();
}

bool WriteIni(const std::string& text) {
    const auto path = IniPath();
    if (path.empty()) return false;
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    // Write beside the file and rename, so a crash never leaves a truncated table.
    const auto temporary = std::filesystem::path(path).concat(".tmp");
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) return false;
        output << text;
        if (!output) return false;
    }
    std::filesystem::rename(temporary, path, error);
    return !error;
}

bool ReadChoice(const std::string& key, unsigned& theme, unsigned& race, unsigned& sex) {
    if (key.empty()) return false;
    const auto value = halo::IniGet(ReadIni(), "CreatedCharacters", key);
    return value && halo::ParseEntry(*value, theme, race, sex, static_cast<uint64_t>(std::time(nullptr)));
}

int SetAccount(lua_State* L) {
    const std::string account = Text(L, 1, 128);
    if (!account.empty()) g_account = account;
    return 0;
}

int SameName(lua_State* L) {
    const std::string first = halo::Key("name", "name", Text(L, 1, 48));
    lua_pushboolean(L, !first.empty() && first == halo::Key("name", "name", Text(L, 2, 48)) ? 1 : 0);
    return 1;
}

int Stage(lua_State* L) {
    Trial trial;
    trial.name = Text(L, 2, 48);
    trial.realm = Text(L, 3, 128);
    trial.account = Text(L, 4, 128);
    const double theme = lua_type(L, 5) == LUA_TNUMBER ? lua_tonumber(L, 5) : -1;
    bool success = false;
    CCharacterComponent* character = Character();
    if (IsCreationFrame(L, 1) && character && !halo::Key(trial.account, trial.realm, trial.name).empty() && std::isfinite(theme) &&
        theme >= 0 && theme <= 5 && std::floor(theme) == theme) {
        trial.race = character->m_data.m_preferences.m_raceID;
        trial.sex = character->m_data.m_preferences.m_sexID;
        trial.theme = static_cast<unsigned>(theme);
        trial.staged = std::time(nullptr);
        success = halo::IsHaloRace(trial.race) && trial.sex <= 1;
    }
    g_trial = success ? trial : Trial{};
    if (success) g_account = trial.account;
    lua_pushboolean(L, success ? 1 : 0);
    return 1;
}

int Commit(lua_State* L) {
    const std::string name = Text(L, 1, 48);
    const std::string key = halo::Key(g_trial.account, g_trial.realm, name);
    bool success = false;
    if (!key.empty() && key == halo::Key(g_trial.account, g_trial.realm, g_trial.name) && std::time(nullptr) - g_trial.staged <= 120 &&
        g_account == g_trial.account) {
        const std::string value = halo::FormatEntry(g_trial.theme, static_cast<uint64_t>(std::time(nullptr)), g_trial.race, g_trial.sex);
        success = WriteIni(halo::IniSet(ReadIni(), "CreatedCharacters", key, value.c_str()));
    }
    g_trial = {};
    lua_pushboolean(L, success ? 1 : 0);
    return 1;
}

int Pending(lua_State* L) {
    unsigned theme = 0, race = 0, sex = 0;
    if (!ReadChoice(halo::Key(g_account, Text(L, 2, 128), Text(L, 1, 48)), theme, race, sex)) return 0;
    lua_pushnumber(L, theme);
    lua_pushnumber(L, race);
    lua_pushnumber(L, sex);
    return 3;
}

int Confirm(lua_State* L) {
    const std::string key = halo::Key(g_account, Text(L, 2, 128), Text(L, 1, 48));
    unsigned theme = 0, race = 0, sex = 0;
    const double accepted = lua_type(L, 3) == LUA_TNUMBER ? lua_tonumber(L, 3) : -1;
    const bool success = ReadChoice(key, theme, race, sex) && accepted == theme && WriteIni(halo::IniSet(ReadIni(), "CreatedCharacters", key, nullptr));
    lua_pushboolean(L, success ? 1 : 0);
    return 1;
}

constexpr struct {
    const char* name;
    int (*function)(lua_State*);
} kFunctions[] = {
    {"WXL_CharacterCreateGetState", &GetState},
    {"WXL_CharacterCreateGetOptions", &GetOptions},
    {"WXL_CharacterCreateSetPart", &SetPart},
    {"WXL_CharacterCreateSetSkin", &SetSkin},
    {"WXL_CharacterCreateSetArmorHidden", &SetArmorHidden},
    {"WXL_CharacterCreateDruidPreviewReady", &Ready},
    {"WXL_CharacterCreateSetDruidForm", &SetDruidForm},
    {"WXL_CharacterCreateEyeHaloReady", &Ready},
    {"WXL_CharacterCreateSetEyeHalo", &SetEyeHalo},
    {"WXL_EyeHaloSetAccount", &SetAccount},
    {"WXL_EyeHaloSameName", &SameName},
    {"WXL_EyeHaloStageCreatedChoice", &Stage},
    {"WXL_EyeHaloCommitCreatedChoice", &Commit},
    {"WXL_EyeHaloPendingCreatedChoice", &Pending},
    {"WXL_EyeHaloConfirmCreatedChoice", &Confirm},
};

}  // namespace

void WxlRegisterCharacterCustomizerLua() {
    for (const auto& entry : kFunctions) FrameScript_RegisterFunction(entry.name, entry.function);
}

void WxlUnregisterCharacterCustomizerLua() {
    // The scene children belong to the creation frame. With the frame gone their scene may be too,
    // so drop the pointers instead of releasing into it.
    if (Frame()) {
        ClearHalo();
        ClearForm(false);
    } else {
        g_halo = g_form = nullptr;
        g_haloCharacter = g_formCharacter = nullptr;
        g_haloFrame = g_formFrame = nullptr;
        g_haloHuman = nullptr;
        g_haloPath.clear();
        g_formPath.clear();
    }
    for (const auto& entry : kFunctions) FrameScript_UnregisterFunction(entry.name);
}

bool WxlCharacterCreateHidesArmor() {
    CCharacterComponent* character = Character();
    if (!g_hideArmor || !character || !Frame()) return false;
    ClearPreviewEquipment(character);
    return true;
}

void WxlCharacterCreateAfterDress() {
    // Tuskarr: the cape geoset is a backpack whose UVs cannot use cloak textures. Suppress only this
    // playable-race preview slot; the shared NPC mesh stays intact.
    CCharacterComponent* character = Character();
    if (character && Frame() && character->m_data.m_preferences.m_raceID == 17) character->RemoveItem(ITEMSLOT_CAPE);
}

void WxlCreationSceneBeforeBuild() {
    ClearHalo();
    ClearForm(true);  // race/sex changes may destroy the native character immediately
}

void WxlCreationSceneFacing(float facing) { Pose(facing); }

void WxlCreationSceneDestroy(const CCharacterComponent* component) {
    if (component == g_haloCharacter) ClearHalo();
    if (component == g_formCharacter) ClearForm(false);
}

WxlCreationBackgroundScope::WxlCreationBackgroundScope() {
    std::snprintf(m_preview, sizeof(m_preview), "%s", g_formPath.c_str());
    ClearHalo();
    ClearForm(true);
}

WxlCreationBackgroundScope::~WxlCreationBackgroundScope() {
    if (m_preview[0]) ShowForm(m_preview);
    else ShowHalo(g_haloTheme);
}

#endif  // WOWDECOMP_LINUX
