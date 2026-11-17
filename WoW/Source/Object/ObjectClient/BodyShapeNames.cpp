#include "WoW/Source/Object/ObjectClient/BodyShapeNames.h"

#include <cctype>
#include <map>
#include <string>

#include "Engine/Source/Os/TimeManager.h"
#include "WoW/Source/ObjectMgrClient/ObjectMgrClient.h"
#include "WoW/Source/Object/ObjectClient/Unit_C.h"

namespace BodyShapeNames {

namespace {

std::map<std::string, BodyShape::Values>& Named() {
    static std::map<std::string, BodyShape::Values> named;
    return named;
}

uint32_t s_lastResolve = 0;
bool s_dirty = false;

std::string Lower(const char* text) {
    std::string out = text ? text : "";
    for (char& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

int32_t BindVisible(WOWGUID guid, void*) {
    CGObject_C* object = ClntObjMgrObjectPtr(guid, TYPE_PLAYER, __FILE__, __LINE__);

    if (!object || guid == ClntObjMgrGetActivePlayer()) {
        return 1;
    }

    const char* realm = nullptr;
    const char* name = static_cast<CGUnit_C*>(object)->GetUnitName(&realm, 1);

    if (name && name[0]) {
        auto found = Named().find(Lower(name));

        if (found != Named().end()) {
            BodyShape::SetUnit(guid.m_guid, found->second);
        }
    }

    return 1;
}

} // namespace

void Set(const char* name, const BodyShape::Values& values) {
    if (!name || !name[0]) {
        return;
    }

    Named()[Lower(name)] = values;
    s_dirty = true;
}

void Clear(const char* name) {
    if (!name) {
        Named().clear();
        BodyShape::Units().clear();
        return;
    }

    std::string key = Lower(name);
    Named().erase(key);
    // The guid entry is dropped the same way a name is bound: find the player that carries this name.
    s_dirty = true;

    for (auto it = BodyShape::Units().begin(); it != BodyShape::Units().end();) {
        CGObject_C* object = ClntObjMgrObjectPtr(WOWGUID { it->first }, TYPE_PLAYER, __FILE__, __LINE__);
        const char* realm = nullptr;
        const char* unitName = object ? static_cast<CGUnit_C*>(object)->GetUnitName(&realm, 1) : nullptr;

        if (!object || (unitName && Lower(unitName) == key)) {
            it = BodyShape::Units().erase(it);
        } else {
            ++it;
        }
    }
}

void Resolve() {
    if (Named().empty()) {
        return;
    }

    uint32_t now = OsGetAsyncTimeMs();

    if (!s_dirty && now - s_lastResolve < 1000) {
        return;
    }

    s_lastResolve = now;
    s_dirty = false;
    ClntObjMgrEnumVisibleObjects(BindVisible, nullptr);
}

} // namespace BodyShapeNames
