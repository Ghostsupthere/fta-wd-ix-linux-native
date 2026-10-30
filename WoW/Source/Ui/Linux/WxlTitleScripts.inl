// Native title Lua bindings, shared with the Lua regression fixture.
#ifndef WOW_SOURCE_UI_WXLTITLESCRIPTS_INL
#define WOW_SOURCE_UI_WXLTITLESCRIPTS_INL

// VERIFY: bd9176fa manual
// OFFSET: 0x0051B410
static int32_t Script_IsTitleKnown(lua_State* L) {
    if (!lua_isnumber(L, 1)) {
        luaL_error(L, "Usage: IsTitleKnown(titleMaskID)");
        return 0;
    }

    CGPlayer_C* player = static_cast<CGPlayer_C*>(ClntObjMgrObjectPtr(ClntObjMgrGetActivePlayer(), TYPE_PLAYER, __FILE__, __LINE__));
    const uint32_t words = player ? sizeof(player->m_player->m_playerFieldKnownTitles) /
        sizeof(player->m_player->m_playerFieldKnownTitles[0]) : 0;
    const double requested = lua_tonumber(L, 1);
    const uint32_t mask = requested > 0 && requested < double(words) * 64 ? static_cast<uint32_t>(requested) : 0;
    const bool known = player && mask && (mask >> 6) < words &&
        (player->m_player->m_playerFieldKnownTitles[mask >> 6] & (1ull << (mask & 63)));
    // Wrath FrameXML explicitly compares this API against numeric zero.
    lua_pushnumber(L, known ? 1 : 0);

    return 1;
}

// FrameXML enumerates title-mask indices, not sparse DBC record IDs.
static int32_t Script_GetNumTitles(lua_State* L) {
    int32_t maximum = 0;
    for (int32_t i = 0; i < g_charTitlesDB.m_numRecords; ++i) {
        const CharTitlesRec* rec = g_charTitlesDB.GetRecordByIndex(i);
        if (rec && rec->m_maskID > maximum) maximum = rec->m_maskID;
    }
    lua_pushnumber(L, maximum);
    return 1;
}

// VERIFY: a134affc manual
// OFFSET: 0x0051B4E0
static int32_t Script_GetTitleName(lua_State* L) {
    if (!lua_isnumber(L, 1)) {
        luaL_error(L, "Usage: GetTitleName(titleMaskID)");
        return 0;
    }

    const double requested = lua_tonumber(L, 1);
    if (!(requested >= 0 && requested <= INT32_MAX)) return 0;
    int32_t maskId = static_cast<int32_t>(requested);
    if (!maskId) { lua_pushliteral(L, ""); return 1; }
    CGPlayer_C* player = static_cast<CGPlayer_C*>(ClntObjMgrObjectPtr(ClntObjMgrGetActivePlayer(), TYPE_PLAYER, __FILE__, __LINE__));
    int32_t gender = player ? player->m_player->m_playerBytes3[0] : 0;
    char title[256];
    title[0] = 0;
    CharTitlesRec* rec = nullptr;

    for (int32_t i = 0; i < g_charTitlesDB.m_numRecords; i++) {
        CharTitlesRec* candidate = g_charTitlesDB.GetRecordByIndex(i);

        if (candidate && candidate->m_maskID == maskId) {
            rec = candidate;
            break;
        }
    }

    if (!rec) {
        return 0;
    }

    if (gender == 1) {
        strcpy(title, strcmp(rec->m_name1, "") ? rec->m_name1 : rec->m_name);
    } else {
        strcpy(title, strcmp(rec->m_name, "") ? rec->m_name : rec->m_name1);
    }

    char stripped[256];
    int32_t length = 0;
    bool skipping = false;

    for (uint32_t i = 0; i < SStrLen(title); i++) {
        char c = title[i];

        if (!c) {
            break;
        }

        if (c == '%') {
            skipping = true;
        } else if (!skipping) {
            stripped[length++] = c;
        } else if (c == ' ') {
            skipping = false;
            stripped[length++] = c;
        }
    }

    stripped[length] = 0;
    lua_pushstring(L, stripped);
    return 1;
}

#endif
