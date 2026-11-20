#include "BonusLootClient.h"
#include "BonusLootUIEmbedded.h"
#include "ChatFrame.h"
#include "GameUI.h"
#include "Types.h"
#include "Engine/Source/Base/CDataStore.h"
#include "Engine/Source/FrameScript/FrameScript.h"
#include "Engine/Source/Os/TimeManager.h"
#include "Engine/Source/lua-5.1/src/lua.h"
#include "WoW/Source/Net/Types.h"
#include "WoW/Source/ObjectMgrClient/ObjectMgrClient.h"
#include "WoW/Source/WowSvcs/WowSvcsClient/ClientServices.h"
#include <array>
#include <charconv>
#include <cstdio>
#include <string>
#include <string_view>

namespace BonusLootClient {
namespace {

constexpr std::string_view Prefix = "FTABONUS1\t";

struct Offer {
    uint32_t id = 0;
    uint32_t charm = 0;
    uint32_t deadline = 0;
    std::string boss;
};

struct Result {
    uint32_t id = 0;
    uint32_t value = 0;
    uint32_t count = 0;
    int32_t property = 0;
    char kind = 0;
};

Offer active;
Result outcome;
std::string problem;
bool supported = false;
bool pending = false;
uint32_t sentAt = 0;

template<class T>
bool ReadInteger(std::string_view value, T& number) {
    if (value.empty()) {
        return false;
    }
    const auto result = std::from_chars(value.data(), value.data() + value.size(), number);
    return result.ec == std::errc{} && result.ptr == value.data() + value.size();
}

bool Send(std::string_view command) {
    const auto player = ClntObjMgrGetActivePlayer();
    const auto* character = ClientServices::GetCharacterInfo();
    if (!player || !character || character->m_guid != player || !character->m_name[0] ||
        !ClientServices::s_currentConnection) {
        std::fprintf(stderr, "[BonusLoot] not sending %.1s: player=%d character=%d sameGuid=%d name=%d connection=%d\n",
            std::string(command).c_str(), player ? 1 : 0, character ? 1 : 0,
            character && character->m_guid == player ? 1 : 0, character && character->m_name[0] ? 1 : 0,
            ClientServices::s_currentConnection ? 1 : 0);
        return false;
    }
    std::string payload(Prefix);
    payload.append(command);
    CDataStore packet;
    packet.Put(static_cast<uint32_t>(CMSG_MESSAGECHAT));
    packet.Put(static_cast<uint32_t>(CHAT_MSG_WHISPER));
    packet.Put(0xFFFFFFFFu);
    packet.PutString(character->m_name);
    packet.PutString(payload.c_str());
    packet.Finalize();
    ClientServices::Send2(&packet);
    std::fprintf(stderr, "[BonusLoot] sent %.1s\n", std::string(command).c_str());
    return true;
}

int32_t Remaining() {
    return active.id ? static_cast<int32_t>(active.deadline - OsGetAsyncTimeMs()) : 0;
}

bool Waiting() {
    return pending && OsGetAsyncTimeMs() - sentAt < 3500;
}

void Changed() {
    FrameScript_SignalEvent(SCRIPT_EVENT_BONUS_LOOT_ROLL_UPDATE, nullptr);
}

void Number(lua_State* L, const char* field, double value) {
    lua_pushnumber(L, value);
    lua_setfield(L, -2, field);
}

void String(lua_State* L, const char* field, const char* value) {
    lua_pushstring(L, value);
    lua_setfield(L, -2, field);
}

int ScriptSupported(lua_State* L) {
    lua_pushboolean(L, supported);
    return 1;
}

int ScriptOffer(lua_State* L) {
    const int32_t remaining = Remaining();
    if (remaining <= 0) {
        return 0;
    }
    lua_createtable(L, 0, 6);
    Number(L, "offerID", active.id);
    Number(L, "charmItemID", active.charm);
    Number(L, "secondsRemaining", remaining / 1000.0);
    String(L, "bossName", active.boss.c_str());
    if (!problem.empty()) {
        String(L, "error", problem.c_str());
    }
    lua_pushboolean(L, Waiting());
    lua_setfield(L, -2, "pending");
    return 1;
}

int ScriptResult(lua_State* L) {
    if (!outcome.id) {
        return 0;
    }
    lua_createtable(L, 0, 6);
    Number(L, "offerID", outcome.id);
    if (outcome.kind == 'G') {
        String(L, "kind", "gold");
        Number(L, "copper", outcome.value);
    } else {
        String(L, "kind", outcome.kind == 'M' ? "mail" : "item");
        Number(L, "itemID", outcome.value);
        Number(L, "count", outcome.count);
        Number(L, "randomProperty", outcome.property);
    }
    return 1;
}

int Respond(lua_State* L, char action) {
    const double id = lua_tonumber(L, 1);
    bool sent = false;
    if (supported && id > 0 && id == active.id && Remaining() > 0 && !Waiting() &&
        CGGameUI::s_inWorld && CGGameUI::CanPerformAction(3)) {
        std::string command(1, action);
        command += '\t';
        command += std::to_string(active.id);
        sent = Send(command);
        if (sent) {
            problem.clear();
            pending = true;
            sentAt = OsGetAsyncTimeMs();
        }
    }
    lua_pushboolean(L, sent);
    return 1;
}

int ScriptConfirm(lua_State* L) {
    return Respond(L, 'T');
}

int ScriptPass(lua_State* L) {
    return Respond(L, 'P');
}

}

void Register(lua_State* state) {
    const struct { const char* name; lua_CFunction function; } methods[] = {
        {"IsSupported", ScriptSupported}, {"GetOffer", ScriptOffer}, {"GetLastResult", ScriptResult},
        {"Confirm", ScriptConfirm}, {"Pass", ScriptPass},
    };
    lua_createtable(state, 0, 5);
    for (const auto& method : methods) {
        lua_pushcfunction(state, method.function);
        lua_setfield(state, -2, method.name);
    }
    lua_setglobal(state, "C_BonusLoot");
}

void InstallUI() {
    FrameScript_Execute(kBonusLootUI, "FTA Native Bonus Loot", nullptr);
}

void EnterWorld() {
    Send("H");
}

void Reset() {
    active = {};
    outcome = {};
    problem.clear();
    supported = false;
    pending = false;
}

bool HandleMessage(const char* text, uint32_t length, uint8_t type, bool gm, WOWGUID sender, WOWGUID receiver) {
    if (!text || length < Prefix.size() || std::string_view(text, Prefix.size()) != Prefix) {
        return false;
    }
    const auto self = ClntObjMgrGetActivePlayer();
    // A character with GM chat switched on receives its own whispers in the GM message form (gm is true), so the flag is not
    // a reason to refuse: the message still has to be from this player to this player, and the realm validates every request.
    if (!self || sender != self || receiver != self || type != CHAT_MSG_WHISPER ||
        length > 192 || text[length - 1] != '\0') {
        std::fprintf(stderr, "[BonusLoot] dropped reply: self=%d sender=%d receiver=%d gm=%d type=%d length=%u nul=%d\n",
            self ? 1 : 0, sender == self ? 1 : 0, receiver == self ? 1 : 0, gm ? 1 : 0, int(type), length,
            length && text[length - 1] == '\0' ? 1 : 0);
        return true;
    }
    const std::string_view message(text, length - 1);
    if (message.find('\0') != std::string_view::npos) {
        return true;
    }
    auto rest = message.substr(Prefix.size());
    if (rest == "A" || rest == "D") {
        std::fprintf(stderr, "[BonusLoot] realm answered %s\n", std::string(rest).c_str());
        supported = rest == "A";
        if (!supported) {
            Reset();
        }
        Changed();
        return true;
    }
    if (!supported) {
        return true;
    }
    std::array<std::string_view, 6> values;
    size_t fields = 0;
    while (fields < values.size()) {
        const auto tab = rest.find('\t');
        values[fields++] = rest.substr(0, tab);
        if (tab == std::string_view::npos) {
            break;
        }
        rest.remove_prefix(tab + 1);
        if (fields == values.size()) {
            return true;
        }
    }

    uint32_t id = 0;
    if (fields < 2 || !ReadInteger(values[1], id) || !id) {
        return true;
    }
    if (values[0] == "O" && fields == 5) {
        uint32_t charm = 0, seconds = 0;
        if (ReadInteger(values[2], charm) && ReadInteger(values[3], seconds) &&
            charm && seconds && seconds <= 600 && values[4].size() <= 96) {
            if (active.id != id) {
                pending = false;
                problem.clear();
            }
            active = {id, charm, OsGetAsyncTimeMs() + seconds * 1000, std::string(values[4])};
            Changed();
        }
    } else if (values[0] == "R" && fields == 6) {
        Result result{id};
        if (values[2].size() == 1 && (values[2] == "I" || values[2] == "M" || values[2] == "G") &&
            ReadInteger(values[3], result.value) && ReadInteger(values[4], result.count) &&
            ReadInteger(values[5], result.property)) {
            result.kind = values[2].front();
            outcome = result;
            if (active.id == id) {
                active = {};
                pending = false;
                problem.clear();
            }
            Changed();
        }
    } else if (id == active.id) {
        if (values[0] == "C" && fields == 2) {
            active = {};
            pending = false;
            problem.clear();
            Changed();
        } else if (values[0] == "E" && fields == 3) {
            pending = false;
            if (values[2] == "inactive" || values[2] == "expired") {
                active = {};
            }
            problem.assign(values[2]);
            Changed();
        }
    }
    return true;
}

}
