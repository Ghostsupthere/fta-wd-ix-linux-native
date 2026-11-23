#if defined(WOWDECOMP_LINUX)
#include "CharacterSelection.h"
#include "CGlueMgr.h"
#include "Engine/Source/ConsoleBase/ConsoleVar.h"
#include "Engine/Source/Storm/Source/MD5.h"
#include "WoW/Source/Client.h"
#include "WoW/Source/WowServices/Types.h"
#include "WoW/Source/WowSvcs/WowSvcsClient/ClientServices.h"

#include <algorithm>
#include <charconv>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <string>
#include <unistd.h>
#include <vector>

namespace {
constexpr uint32_t MaxCharacters = 255;

std::filesystem::path OrderPath() {
    const auto* realm = ClientServices::GetSelectedRealm();
    if (!realm || !CGlueMgr::m_accountName[0] || !realm->m_realmAddress[0]) return {};
    MD5_CTX context;
    MD5Init(&context);
    for (const char* part : {static_cast<const char*>(CGlueMgr::m_accountName),
                             static_cast<const char*>(realm->m_realmAddress),
                             static_cast<const char*>(realm->m_realmName)})
        MD5Update(&context, reinterpret_cast<const uint8_t*>(part), std::strlen(part) + 1);
    uint8_t digest[16];
    MD5Final(digest, &context);
    constexpr char hex[] = "0123456789abcdef";
    std::string key;
    for (uint8_t byte : digest) {
        key += hex[byte >> 4];
        key += hex[byte & 15];
    }
    return std::filesystem::path("WTF") / "WXL" / "RosterOrder" / (key + ".txt");
}

std::vector<uint64_t> ReadOrder(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file || file.tellg() > MaxCharacters * 17) return {};
    file.seekg(0);
    std::vector<uint64_t> order;
    for (std::string line; std::getline(file, line);) {
        uint64_t id = 0;
        const auto end = line.data() + line.size();
        const auto result = std::from_chars(line.data(), end, id, 16);
        if (line.size() != 16 || result.ec != std::errc{} || result.ptr != end || !id) return {};
        order.push_back(id);
    }
    return file.eof() ? order : std::vector<uint64_t>{};
}

bool SaveOrder(const std::filesystem::path& path, const std::vector<uint64_t>& order) {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) return false;
    auto temporary = path;
    temporary += "." + std::to_string(getpid()) + ".tmp";
    std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
    file << std::hex << std::setfill('0');
    for (const auto id : order) file << std::setw(16) << id << '\n';
    file.close();
    if (file) {
        std::filesystem::rename(temporary, path, error);
        if (!error) return true;
    }
    std::filesystem::remove(temporary, error);
    return false;
}
}

void CCharacterSelection::ApplySavedOrder() {
    if (s_characterList.Count() < 2 || s_characterList.Count() > MaxCharacters) return;
    const auto path = OrderPath();
    if (path.empty()) return;
    // The server roster has just been enumerated. Character displays have
    // not been constructed, so rotate only the character records.
    unsigned next = 0;
    for (const auto id : ReadOrder(path)) {
        for (unsigned index = next; index < s_characterList.Count(); ++index) {
            if (s_characterList[index].m_characterInfo.m_guid.m_guid != id) continue;
            for (unsigned i = index; i > next; --i)
                std::swap(s_characterList[i].m_characterInfo, s_characterList[i - 1].m_characterInfo);
            ++next;
            break;
        }
    }
}

int32_t CCharacterSelection::MoveCharacter(int32_t direction) {
    const auto count = static_cast<int32_t>(s_characterList.Count());
    const auto from = s_selectionIndex;
    if ((direction != -1 && direction != 1) || count > MaxCharacters || from < 0 || from >= count ||
        std::strcmp(CGlueMgr::m_currentScreen, "charselect") || CGlueMgr::m_idleState != IDLE_NONE)
        return 0;
    const auto to = from + direction;
    if (to < 0 || to >= count) return 0;
    const auto path = OrderPath();
    if (path.empty()) return 0;

    std::vector<uint64_t> order;
    order.reserve(count);
    for (int i = 0; i < count; ++i) order.push_back(s_characterList[i].m_characterInfo.m_guid.m_guid);
    std::swap(order[from], order[to]);
    if (!SaveOrder(path, order)) return -1;

    auto& first = s_characterList[from];
    auto& second = s_characterList[to];
    if (CGlueMgr::m_characterInfo == &first.m_characterInfo) CGlueMgr::m_characterInfo = &second.m_characterInfo;
    else if (CGlueMgr::m_characterInfo == &second.m_characterInfo) CGlueMgr::m_characterInfo = &first.m_characterInfo;
    std::swap(first.m_characterInfo, second.m_characterInfo);
    std::swap(first.m_component, second.m_component);
    std::swap(first.m_petModel, second.m_petModel);
    std::swap(first.m_petAlpha, second.m_petAlpha);
    std::swap(first.unused_0194, second.unused_0194);
    s_selectionIndex = to;
    if (g_lastCharacterIndexVar) {
        const int last = g_lastCharacterIndexVar->m_intValue;
        if (last == from || last == to) {
            const auto value = std::to_string(last == from ? to : from);
            g_lastCharacterIndexVar->Set(value.c_str(), true, false, false, true);
            CVar::SaveAll();
        }
    }
    return to + 1;
}
#endif
