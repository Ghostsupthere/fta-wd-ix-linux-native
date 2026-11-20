#include "AccountData.h"
#include <cstring>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <malloc.h>
#include "Engine/Source/Base/CDataStore.h"
#include "Engine/Source/ConsoleBase/ConsoleVar.h"
#include "Engine/Source/ConsoleBase/Types.h"
#include "Engine/Source/Storm/H/storm.h"
#include "Engine/Source/Storm/Source/MD5.h"
#include "WoW/Source/Glue/CGlueMgr.h"
#include "WoW/Source/WowSvcs/WowSvcsClient/ClientServices.h"
#include "ThirdParty/zlib/zlib.h"
#include "WoW/Source/Client.h"
#include "WoW/Source/Ui/ExtendedMacroFormat.h"
#include "WoW/Source/Ui/ScriptEvents.h"
#include "Engine/Source/FrameScript/FrameScript.h"
#include "Engine/Source/Event/Event.h"

// STATIC: 0x00AD94E0
static AccountDataInfo s_accountData[] = {
    { false, true, nullptr, "config-cache.wtf", "config-cache.old", "synchronizeConfig" },
    { false, false, nullptr, "config-cache.wtf", "config-cache.old", "synchronizeConfig" },
    { false, true, nullptr, "bindings-cache.wtf", "bindings-cache.old", "synchronizeBindings" },
    { false, false, nullptr, "bindings-cache.wtf", "bindings-cache.old", "synchronizeBindings" },
    { false, true, nullptr, "macros-cache.txt", "macros-cache.old", "synchronizeMacros" },
    { false, false, nullptr, "macros-cache.txt", "macros-cache.old", "synchronizeMacros" },
    { false, false, "layout-local.txt", nullptr, nullptr, nullptr, true },
    { false, false, nullptr, "chat-cache.txt", "chat-cache.old", nullptr }
};

// STATIC: 0x00C7BFF0
static uint32_t s_accountDataTime;

// STATIC: 0x00C7BFF8
static int32_t s_clearGlobalConfig;

// STATIC: 0x00C7BFFC
static int32_t s_clearCharacterConfig;

namespace {

constexpr uint32_t WrathMacroType = 4;
constexpr std::string_view LocalMacroMagic = "FTA LOCAL MACROS 1 ";

struct MacroSync {
    MacroAccountLoadFunc callback{};
    std::string data;
    std::string legacy;
    std::string sent;
    uint32_t revision{};
    uint32_t requestedType{};
    uint32_t loadTimer{};
    bool diskChecked{};
    bool diskReadable{true};
    bool legacyLoaded{};
    bool dirty{};
    bool canUpload{};
    bool conflict{};
};
MacroSync macroSync[2];

bool IsWrathMacroType(uint32_t type) {
    return type == WrathMacroType || type == WrathMacroType + 1;
}

bool MacroSyncEnabled() {
    CVar* setting = CVar::Lookup("synchronizeMacros");
    return s_cvSynchronizeSettings && s_cvSynchronizeSettings->m_intValue && (!setting || setting->m_intValue);
}

std::filesystem::path MacroCachePath(uint32_t scope) {
    using std::filesystem::u8path;
    std::filesystem::path path("WTF/Account");
    const char* names[] = {ClientServices::GetAccountName(), ClientServices::GetSelectedRealmName(),
        scope ? ClientServices::GetCharacterName() : nullptr};
    const size_t components = scope ? 3 : 2;
    for (size_t i = 0; i < components; ++i) {
        const char* name = names[i];
        if (!name || !*name) {
            return {};
        }
        const std::string_view part(name);
        if (part.back() == '.' || part.back() == ' ' || part.find_first_of("\\/:<>|?*\"") != std::string_view::npos) {
            return {};
        }
        path /= u8path(part.begin(), part.end());
    }
    return path / "macros-cache.fta";
}

void MacroNotice(const char* message) {
    if (FrameScript_GetContext()) {
        FrameScript_SignalEvent(SCRIPT_EVENT_UI_ERROR_MESSAGE, "%s", message);
    }
}

bool WriteMacroCache(uint32_t scope, std::string_view data, uint32_t revision, bool dirty) {
    const auto path = MacroCachePath(scope);
    if (path.empty()) {
        return false;
    }
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) {
        return false;
    }
    auto temporary = path;
    temporary += ".tmp-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        output << LocalMacroMagic << revision << ' ' << (dirty ? '1' : '0') << '\n';
        output.write(data.data(), static_cast<std::streamsize>(data.size()));
        output.close();
        if (!output) {
            std::filesystem::remove(temporary, error);
            return false;
        }
    }
    std::filesystem::rename(temporary, path, error);
    if (error) {
        std::filesystem::remove(temporary, error);
        return false;
    }
    return true;
}

void ReadMacroCache(uint32_t scope) {
    auto& local = macroSync[scope];
    if (local.diskChecked) {
        return;
    }
    local.diskChecked = true;
    const auto path = MacroCachePath(scope);
    if (path.empty()) {
        return;
    }
    std::error_code error;
    const bool exists = std::filesystem::exists(path, error);
    if (error || !exists) {
        local.diskReadable = !error;
        return;
    }

    local.diskReadable = false;
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    const auto size = input.tellg();
    if (size <= 0 || size > static_cast<std::streamoff>(ExtendedMacros::CollectionBytes + 64)) {
        return;
    }
    std::string contents(static_cast<size_t>(size), '\0');
    input.seekg(0);
    if (!input.read(contents.data(), static_cast<std::streamsize>(contents.size()))) {
        return;
    }

    std::string_view file = contents;
    if (file.substr(0, LocalMacroMagic.size()) != LocalMacroMagic) {
        return;
    }
    file.remove_prefix(LocalMacroMagic.size());
    const auto header = ExtendedMacros::NextLine(file);
    const auto space = header.find(' ');
    uint32_t revision = 0;
    std::vector<ExtendedMacros::Record> records;
    if (space == std::string_view::npos || !ExtendedMacros::ReadUnsigned(header.substr(0, space), revision) ||
        (header.substr(space + 1) != "0" && header.substr(space + 1) != "1") ||
        !ExtendedMacros::Decode(file, scope, records)) {
        return;
    }

    local.data.assign(file);
    local.revision = revision;
    local.dirty = header.substr(space + 1) == "1";
    local.diskReadable = true;
}

void UploadMacroCollection(uint32_t scope) {
    auto& state = macroSync[scope];
    if (!state.canUpload || !state.dirty || !state.sent.empty() || state.conflict || !MacroSyncEnabled() ||
        !ClientServices::s_currentConnection) {
        return;
    }

    const auto size = static_cast<uint32_t>(state.data.size());
    std::vector<uint8_t> compressed(compressBound(size));
    uint32_t bytes = static_cast<uint32_t>(compressed.size());
    if (zlib_compress(compressed.data(), &bytes, reinterpret_cast<const uint8_t*>(state.data.data()), size, 1) ||
        bytes > ExtendedMacros::MaxClientPacket - 16) {
        MacroNotice("Macros were saved locally. The collection could not be prepared for synchronization.");
        return;
    }

    CDataStore packet;
    packet.Put(static_cast<uint32_t>(CMSG_UPDATE_ACCOUNT_DATA));
    packet.Put(ExtendedMacros::AccountWireType + scope);
    packet.Put(state.revision);
    packet.Put(size);
    packet.PutData(compressed.data(), bytes);
    packet.Finalize();
    state.sent = state.data;
    ClientServices::Send2(&packet);
}

void PublishMacroLoad(uint32_t scope);
static int32_t MacroLoadTimeout(const void*, void* param) {
    const auto scope = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(param));
    macroSync[scope].loadTimer = 0;
    macroSync[scope].requestedType = 0;
    s_accountData[WrathMacroType + scope].m_downloading = false;
    PublishMacroLoad(scope);
    return 1;
}

void CancelMacroLoadTimer(uint32_t scope) {
    auto& timer = macroSync[scope].loadTimer;
    if (timer) {
        ClientKillTimer(timer, MacroLoadTimeout, "MacroLoadTimeout");
        timer = 0;
    }
}

void RequestMacroCollection(uint32_t scope, uint32_t type) {
    CancelMacroLoadTimer(scope);
    auto& state = macroSync[scope];
    state.requestedType = type;
    s_accountData[WrathMacroType + scope].m_downloading = true;
    state.loadTimer = ClientSetTimer(5000, MacroLoadTimeout, reinterpret_cast<void*>(static_cast<uintptr_t>(scope)));
    CDataStore packet;
    packet.Put(static_cast<uint32_t>(CMSG_REQUEST_ACCOUNT_DATA));
    packet.Put(type);
    packet.Finalize();
    ClientServices::Send2(&packet);
}

} // namespace

// VERIFY: 4934bdac auto
// OFFSET: 0x006B8AB0
static int32_t CopyAccountFile(bool global, const char* source, const char* dest) {
    char sourcePath[260];
    char destPath[260];

    const char* accountName = ClientServices::GetAccountName();

    if (!accountName || !source || !dest) {
        return 0;
    }

    if (global) {
        SStrPrintf(sourcePath, sizeof(sourcePath), "WTF\\Account\\%s\\%s", accountName, source);
        SStrPrintf(destPath, sizeof(destPath), "WTF\\Account\\%s\\%s", accountName, dest);
    } else {
        SStrPrintf(sourcePath, sizeof(sourcePath), "WTF\\Account\\%s\\%s\\%s\\%s", accountName, ClientServices::GetSelectedRealmName(), ClientServices::GetCharacterName(), source);
        SStrPrintf(destPath, sizeof(destPath), "WTF\\Account\\%s\\%s\\%s\\%s", accountName, ClientServices::GetSelectedRealmName(), ClientServices::GetCharacterName(), dest);
    }

    return OsCopyFile(sourcePath, destPath, 0);
}

// VERIFY: e89d2711 auto
// OFFSET: 0x006B8B90
static HOSFILE OpenAccountFile(bool global, const char* fileName, bool write) {
    char path[260];

    const char* accountName = ClientServices::GetAccountName();

    if (!accountName || !fileName) {
        return OS_INVALID_FILE;
    }

    if (global) {
        SStrPrintf(path, sizeof(path), "WTF\\Account\\%s", accountName);
    } else {
        SStrPrintf(path, sizeof(path), "WTF\\Account\\%s\\%s\\%s", accountName, ClientServices::GetSelectedRealmName(), ClientServices::GetCharacterName());
    }

    if (write && !OsDirectoryExists(path)) {
        OsCreateDirectory(path, 1);
    }

    SStrPack(path, "\\", sizeof(path));
    SStrPack(path, fileName, sizeof(path));

    if (write) {
        return OsCreateFile(path, OS_GENERIC_WRITE, 0, OS_CREATE_ALWAYS, OS_FILE_ATTRIBUTE_NORMAL, OS_FILE_TYPE_ANY);
    }

    return OsCreateFile(path, OS_GENERIC_READ, 0, OS_OPEN_EXISTING, OS_FILE_ATTRIBUTE_NORMAL, OS_FILE_TYPE_ANY);
}

namespace {

void PublishMacroLoad(uint32_t scope) {
    auto& state = macroSync[scope];
    auto callback = state.callback;
    if (!callback || s_accountData[WrathMacroType + scope].m_downloading) {
        return;
    }
    state.callback = nullptr;
    ReadMacroCache(scope);
    if (!state.diskReadable || !state.data.empty() || state.legacyLoaded) {
        callback(scope, state.data.empty() ? state.legacy : state.data, state.diskReadable);
        return;
    }
    const auto file = OpenAccountFile(scope == 0, "macros-cache.txt", false);
    if (file == OS_INVALID_FILE) {
        callback(scope, {}, true);
        return;
    }
    const uint64_t size = OsGetFileSize(file);
    bool success = size <= ExtendedMacros::CollectionBytes;
    std::string legacy;
    if (success && size) {
        legacy.resize(static_cast<size_t>(size));
        uint32_t read = 0;
        success = OsReadFile(file, legacy.data(), static_cast<uint32_t>(size), &read) && read == size;
    }
    OsCloseFile(file);
    callback(scope, legacy, success);
}

int32_t HandleMacroCollection(CDataStore* message, const WOWGUID& guid, uint32_t type) {
    const bool native = ExtendedMacros::IsWireType(type);
    const uint32_t scope = native ? ExtendedMacros::Scope(type) : type - WrathMacroType;
    auto& info = s_accountData[WrathMacroType + scope];
    auto& state = macroSync[scope];
    if (!info.m_active || type != state.requestedType || (scope && guid != CGlueMgr::GetActive())) {
        message->m_read = message->m_size;
        return 1;
    }
    uint32_t revision = 0;
    uint32_t size = 0;
    message->Get(revision);
    message->Get(size);
    const uint32_t bytes = message->m_size - message->m_read;
    void* compressed = nullptr;
    message->GetDataInSitu(compressed, bytes);
    std::string data;
    bool valid = size <= ExtendedMacros::CollectionBytes && (size == 0 || bytes != 0);
    if (valid && size) {
        data.resize(size);
        uint32_t decoded = size;
        valid = !zlib_uncompress(reinterpret_cast<uint8_t*>(data.data()), &decoded,
            static_cast<const uint8_t*>(compressed), bytes) && decoded == size;
    }
    if (valid && native && revision) {
        std::vector<ExtendedMacros::Record> records;
        valid = ExtendedMacros::Decode(data, scope, records);
    }

    CancelMacroLoadTimer(scope);
    state.requestedType = 0;
    info.m_downloading = false;
    if (valid && state.diskReadable) {
        state.canUpload = native;
        if (state.dirty && native) {
            if (revision && state.data == data) {
                if (WriteMacroCache(scope, state.data, revision, false)) {
                    state.revision = revision;
                    state.dirty = false;
                }
            } else if (state.revision != revision) {
                state.conflict = true;
                MacroNotice("Your local macros have unsynced changes and the realm has a newer version. Both copies were kept.");
            }
        } else if (native && revision && (state.revision != revision || state.data != data)) {
            if (WriteMacroCache(scope, data, revision, false)) {
                state.data = std::move(data);
                state.revision = revision;
            } else {
                // Do not allow a newer download to be overwritten after a failed cache write.
                state.canUpload = false;
                MacroNotice("The realm's macro collection could not be saved to your local cache.");
                if (state.data.empty()) {
                    state.data = std::move(data);
                    state.revision = revision;
                }
            }
        } else if (state.data.empty() && (info.m_serverTime >= info.m_time && !info.m_changed)) {
            state.legacy = std::move(data);
            state.legacyLoaded = true;
        }
    }
    PublishMacroLoad(scope);
    UploadMacroCollection(scope);
    return 1;
}

} // namespace

void LoadMacroAccountData(uint32_t scope, MacroAccountLoadFunc callback) {
    macroSync[scope].callback = callback;
    PublishMacroLoad(scope);
}

bool SaveMacroAccountData(uint32_t scope, std::string data) {
    auto& state = macroSync[scope];
    if (!state.diskReadable || !WriteMacroCache(scope, data, state.revision, true)) {
        MacroNotice("Your macro changes could not be written. Check that the WTF folder is writable.");
        return false;
    }
    state.data = std::move(data);
    state.legacy.clear();
    state.dirty = true;
    UploadMacroCollection(scope);
    return true;
}

void ClearMacroAccountDataCallbacks() {
    for (auto& state : macroSync) {
        state.callback = nullptr;
    }
}

// VERIFY: e5cad552 auto
// OFFSET: 0x006B8CA0
static void MD5AccountFile(const char* fileName, uint8_t* digest, bool global) {
    memset(digest, 0, 16);

    HOSFILE file = OpenAccountFile(global, fileName, false);

    if (file == OS_INVALID_FILE) {
        return;
    }

    if (OsGetFileSize(file)) {
        MD5_CTX context;
        MD5Init(&context);

        uint8_t buffer[1024];
        uint32_t bytesRead;

        do {
            OsReadFile(file, buffer, sizeof(buffer), &bytesRead);
            MD5Update(&context, buffer, bytesRead);
        } while (bytesRead);

        MD5Final(digest, &context);
    }

    OsCloseFile(file);
}

// VERIFY: 862af76a manual
// OFFSET: 0x006B8D40
static void LoadCachedChecksums(bool global) {
    uint32_t count = 0;

    for (uint32_t i = 0; i < sizeof(s_accountData) / sizeof(s_accountData[0]); i++) {
        if (s_accountData[i].m_global == global) {
            count++;
        }
    }

    HOSFILE file = OpenAccountFile(global, "cache.md5", false);

    if (file != OS_INVALID_FILE) {
        if (OsGetFileSize(file) == (sizeof(s_accountData[0].m_md5) + sizeof(s_accountData[0].m_time)) * count) {
            for (uint32_t i = 0; i < sizeof(s_accountData) / sizeof(s_accountData[0]); i++) {
                AccountDataInfo& info = s_accountData[i];

                if (info.m_global == global) {
                    uint32_t bytesRead;
                    OsReadFile(file, info.m_md5, sizeof(info.m_md5), &bytesRead);
                    OsReadFile(file, &info.m_time, sizeof(info.m_time), &bytesRead);
                }
            }
        }

        OsCloseFile(file);
    }

    for (uint32_t i = 0; i < sizeof(s_accountData) / sizeof(s_accountData[0]); i++) {
        AccountDataInfo& info = s_accountData[i];

        if (info.m_global == global) {
            uint8_t digest[16];
            MD5AccountFile(info.m_cacheFile, digest, info.m_global);
            info.m_changed = memcmp(digest, info.m_md5, sizeof(info.m_md5)) != 0;
        }
    }
}

// VERIFY: e5255567 manual
// OFFSET: 0x006B8EE0
static void SaveCachedChecksums(bool global) {
    HOSFILE file = OpenAccountFile(global, "cache.md5", true);

    if (file == OS_INVALID_FILE) {
        return;
    }

    for (uint32_t i = 0; i < sizeof(s_accountData) / sizeof(s_accountData[0]); i++) {
        AccountDataInfo& info = s_accountData[i];

        if (info.m_global == global) {
            uint32_t bytesWritten;
            OsWriteFile(file, info.m_md5, sizeof(info.m_md5), &bytesWritten);
            OsWriteFile(file, &info.m_time, sizeof(info.m_time), &bytesWritten);
        }
    }

    OsCloseFile(file);
}

// VERIFY: f49c9f62 auto
// OFFSET: 0x006B8F50
static void UpdateCachedChecksum(int32_t type, int32_t time) {
    AccountDataInfo& info = s_accountData[type];

    uint8_t digest[16];
    MD5AccountFile(info.m_cacheFile, digest, info.m_global);
    memcpy(info.m_md5, digest, sizeof(info.m_md5));

    info.m_changed = false;
    info.m_time = time;
    info.m_serverTime = time;

    SaveCachedChecksums(info.m_global);
}

// VERIFY: 72a9a2be auto
// OFFSET: 0x006B8FC0
static int32_t Packet_SMSG_UPDATE_ACCOUNT_DATA_COMPLETE(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg) {
    uint32_t type;
    msg->Get(type);

    uint32_t result;
    msg->Get(result);

    if (ExtendedMacros::IsWireType(type) && msg->m_size - msg->m_read == sizeof(uint32_t)) {
        uint32_t revision = 0;
        msg->Get(revision);
        const auto scope = ExtendedMacros::Scope(type);
        auto& state = macroSync[scope];
        if (!state.sent.empty()) {
            const bool saved = result == static_cast<uint32_t>(ExtendedMacros::SaveResult::Saved) &&
                revision && revision >= state.revision;
            if (saved) {
                state.revision = revision;
                state.dirty = state.data != state.sent;
                if (!WriteMacroCache(scope, state.data, revision, state.dirty)) {
                    MacroNotice("Macros reached the realm, but the local synchronization state could not be saved.");
                }
            } else {
                state.conflict = result == static_cast<uint32_t>(ExtendedMacros::SaveResult::Conflict);
                if (state.conflict) {
                    MacroNotice("Your macro collection changed on another client. Both copies were kept.");
                } else if (result == static_cast<uint32_t>(ExtendedMacros::SaveResult::OverCapacity)) {
                    MacroNotice("Your macros exceed this realm's collection limit. Your local changes were kept.");
                } else {
                    MacroNotice("The realm could not save your macros. Your local changes were kept.");
                }
            }
            state.sent.clear();
            if (saved) {
                UploadMacroCollection(scope);
            }
        }
    }
    return 1;
}

// VERIFY: 682b8825 manual
// OFFSET: 0x006B9020
void AccountDataDestroy(bool global) {
    const uint32_t scope = global ? 0 : 1;
    CancelMacroLoadTimer(scope);
    macroSync[scope] = {};
    for (uint32_t i = 0; i < sizeof(s_accountData) / sizeof(s_accountData[0]); i++) {
        if (s_accountData[i].m_global == global) {
            s_accountData[i].m_active = false;
        }
    }
}

// VERIFY: 58c5c9b7 auto
// OFFSET: 0x006B9050
void LoadAccountData(int32_t type, ACCOUNTDATALOADFUNC func, void* param) {
    AccountDataInfo& info = s_accountData[type];

    if (info.m_downloading) {
        info.m_loadFunc = func;
        info.m_loadParam = param;
        return;
    }

    HOSFILE localFile = OpenAccountFile(info.m_global, info.m_localFile, false);
    HOSFILE cacheFile = OpenAccountFile(info.m_global, info.m_cacheFile, false);

    func(localFile, cacheFile, param);

    if (localFile != OS_INVALID_FILE) {
        OsCloseFile(localFile);
    }

    if (cacheFile != OS_INVALID_FILE) {
        OsCloseFile(cacheFile);
    }
}

// VERIFY: ac27b46e auto
// OFFSET: 0x006B90E0
void SetClearConfigData(int32_t clear) {
    s_clearGlobalConfig = clear;
    s_clearCharacterConfig = clear;
}

// VERIFY: 61aba3c9 manual
// OFFSET: 0x006B9100
void UploadAccountData(int32_t type) {
    if (IsWrathMacroType(type)) {
        UploadMacroCollection(type - WrathMacroType);
        return;
    }
    HOSFILE file = OpenAccountFile(s_accountData[type].m_global, s_accountData[type].m_cacheFile, false);
    uint32_t size = file == OS_INVALID_FILE ? 0 : static_cast<uint32_t>(OsGetFileSize(file));

    uint32_t time = s_accountDataTime++;

    CDataStore msg;
    msg.Put(static_cast<uint32_t>(CMSG_UPDATE_ACCOUNT_DATA));
    msg.Put(static_cast<uint32_t>(type));
    msg.Put(time);
    msg.Put(size);

    if (size) {
        void* allocated = nullptr;
        uint8_t* data;

        if (size > 0x1000) {
            allocated = SMemAlloc(size, __FILE__, __LINE__, 0);
            data = static_cast<uint8_t*>(allocated);
        } else {
            data = static_cast<uint8_t*>(alloca(size));
        }

        uint32_t bytesRead;
        OsReadFile(file, data, size, &bytesRead);

        uint32_t compressedSize = compressBound(size);
        uint8_t* compressedAllocated = nullptr;
        uint8_t* compressed;

        if (compressedSize > 0x2000) {
            compressedAllocated = static_cast<uint8_t*>(SMemAlloc(compressedSize, __FILE__, __LINE__, 0));
            compressed = compressedAllocated;
        } else {
            compressed = static_cast<uint8_t*>(alloca(compressedSize));
        }

        zlib_compress(compressed, &compressedSize, data, size, 1);
        msg.PutData(compressed, compressedSize);

        if (allocated) {
            SMemFree(allocated, __FILE__, __LINE__, 0);
        }

        if (compressedAllocated) {
            SMemFree(compressedAllocated, __FILE__, __LINE__, 0);
        }
    }

    msg.Finalize();
    ClientServices::Send2(&msg);

    if (file != OS_INVALID_FILE) {
        OsCloseFile(file);
    }

    UpdateCachedChecksum(type, time);
}

// VERIFY: 7b7683e2 manual
// OFFSET: 0x006B92C0
void DownloadAccountData(int32_t type) {
    CDataStore msg;
    msg.Put(static_cast<uint32_t>(CMSG_REQUEST_ACCOUNT_DATA));
    msg.Put(static_cast<uint32_t>(type));
    msg.Finalize();
    ClientServices::Send2(&msg);

    s_accountData[type].m_downloading = true;
}

// VERIFY: e1dd25e9 manual
// OFFSET: 0x006B9340
static int32_t SaveTimerHandler(const void* data, void* param) {
    int32_t type = static_cast<int32_t>(reinterpret_cast<intptr_t>(param));
    AccountDataInfo& info = s_accountData[type];
    uint8_t digest[16];
    MD5AccountFile(info.m_cacheFile, digest, info.m_global);

    if (!memcmp(digest, info.m_md5, sizeof(digest))) {
        info.m_changed = false;
        info.m_time = info.m_serverTime;
        SaveCachedChecksums(info.m_global);
    } else {
        UploadAccountData(type);
    }

    info.m_saveTimer = 0;
    return 1;
}

// VERIFY: c002fa95 auto
// OFFSET: 0x006B9440
static void ScheduleAccountDataSave(int32_t type) {
    AccountDataInfo& info = s_accountData[type];
    info.m_changed = true;

    if (!s_cvSynchronizeSettings->m_intValue) {
        return;
    }

    CVar* cvar = CVar::Lookup(info.m_cvarName);

    if ((cvar && !cvar->m_intValue) || info.m_localOnly) {
        return;
    }

    if (info.m_saveTimer) {
        ClientKillTimer(info.m_saveTimer, SaveTimerHandler, "SaveTimerHandler");
        info.m_saveTimer = 0;
    }

    info.m_saveTimer = ClientSetTimer(30000, SaveTimerHandler, reinterpret_cast<void*>(static_cast<intptr_t>(type)));
}

// VERIFY: fe34ba05 manual
// OFFSET: 0x006B94C0
static int32_t Packet_SMSG_ACCOUNT_DATA_TIMES(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg) {
    uint32_t serverTime = 0;
    msg->Get(serverTime);
    s_accountDataTime = serverTime;

    uint8_t unk;
    msg->Get(unk);

    uint32_t mask;
    msg->Get(mask);

    uint32_t count = 0;

    for (uint32_t i = 0; i < sizeof(s_accountData) / sizeof(s_accountData[0]); i++) {
        if (mask & (1 << i)) {
            count++;
        }
    }

    if (msg->m_size - msg->m_read != count * sizeof(uint32_t)) {
        return 1;
    }

    for (int32_t type = 0; type < static_cast<int32_t>(sizeof(s_accountData) / sizeof(s_accountData[0])); type++) {
        AccountDataInfo& info = s_accountData[type];

        if (info.m_localOnly) {
            if (mask & (1 << type)) {
                uint32_t discard = 0;
                msg->Get(discard);
            }
        } else {
            if (!(mask & (1 << type))) {
                continue;
            }

            uint32_t typeTime = 0;
            msg->Get(typeTime);
            info.m_serverTime = typeTime;
        }

        if (!info.m_active) {
            continue;
        }

        if (!unk && info.m_global && info.m_serverTime < info.m_time) {
            info.m_serverTime = info.m_time;
        }

        info.m_downloading = false;

        if (IsWrathMacroType(type)) {
            const auto scope = type - WrathMacroType;
            auto& state = macroSync[scope];
            ReadMacroCache(scope);
            if (MacroSyncEnabled() && state.diskReadable && unk == ExtendedMacros::AccountDataCapability) {
                RequestMacroCollection(scope, ExtendedMacros::AccountWireType + scope);
            } else if (MacroSyncEnabled() && state.diskReadable && state.data.empty() &&
                       !info.m_changed && info.m_time < info.m_serverTime) {
                RequestMacroCollection(scope, type);
            } else {
                PublishMacroLoad(scope);
            }
            continue;
        }

        if (s_cvSynchronizeSettings->m_intValue) {
            CVar* var = CVar::Lookup(info.m_cvarName);

            if ((!var || var->m_intValue) && !info.m_localOnly) {
                if (info.m_changed || info.m_time > info.m_serverTime) {
                    UploadAccountData(type);
                } else if (info.m_time < info.m_serverTime) {
                    DownloadAccountData(type);
                }
            }
        }

        if (!info.m_downloading && info.m_loadFunc) {
            LoadAccountData(type, info.m_loadFunc, info.m_loadParam);
        }
    }

    return 1;
}

// VERIFY: d5e85999 manual
// OFFSET: 0x006B9730
static int32_t Packet_SMSG_UPDATE_ACCOUNT_DATA(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg) {
    WOWGUID guid;
    msg->Get(guid.m_guid);

    uint32_t type;
    msg->Get(type);
    if (ExtendedMacros::IsWireType(type) || IsWrathMacroType(type)) {
        if (msg->m_size - msg->m_read < 2 * sizeof(uint32_t)) {
            msg->m_read = msg->m_size;
            return 1;
        }
        return HandleMacroCollection(msg, guid, type);
    }
    if (type >= std::size(s_accountData)) {
        msg->m_read = msg->m_size;
        return 1;
    }

    AccountDataInfo& info = s_accountData[type];

    if ((guid != CGlueMgr::GetActive() && !info.m_global) || !info.m_downloading) {
        msg->m_read = msg->m_size;
        return 1;
    }

    uint32_t serverTime = 0;
    msg->Get(serverTime);

    if (info.m_changed || info.m_time < static_cast<int32_t>(serverTime)) {
        HOSFILE file = OpenAccountFile(info.m_global, info.m_cacheFile, true);

        if (file != OS_INVALID_FILE) {
            uint32_t size;
            msg->Get(size);

            if (size) {
                uint32_t compressedSize = msg->m_size - msg->m_read;
                void* compressed;
                msg->GetDataInSitu(compressed, compressedSize);

                uint8_t* buffer;
                bool stackBuffer;

                if (size >= 0x2000) {
                    buffer = static_cast<uint8_t*>(SMemAlloc(size, __FILE__, __LINE__, 0));
                    stackBuffer = false;
                } else {
                    buffer = static_cast<uint8_t*>(alloca(size));
                    stackBuffer = true;
                }

                uint32_t uncompressedSize = size;

                if (!zlib_uncompress(buffer, &uncompressedSize, static_cast<const uint8_t*>(compressed), compressedSize)) {
                    uint32_t bytesWritten;
                    OsWriteFile(file, buffer, uncompressedSize, &bytesWritten);
                }

                if (!stackBuffer) {
                    SMemFree(buffer, __FILE__, __LINE__, 0);
                }
            }

            OsCloseFile(file);
            UpdateCachedChecksum(type, serverTime);
        }
    } else {
        msg->m_read = msg->m_size;
    }

    info.m_downloading = false;

    if (info.m_loadFunc) {
        LoadAccountData(type, info.m_loadFunc, info.m_loadParam);
    }

    return 1;
}

// VERIFY: d11d72c6 auto
// OFFSET: 0x006B9900
void AccountDataInitializeBasicSystem() {
    s_cvSynchronizeSettings = CVar::Register("synchronizeSettings", "Whether client settings should be stored on the server", 0, "1", nullptr, CATEGORY_DEFAULT, false, nullptr, false);
    ClientServices::SetMessageHandler(SMSG_ACCOUNT_DATA_TIMES, Packet_SMSG_ACCOUNT_DATA_TIMES, nullptr);
    ClientServices::SetMessageHandler(SMSG_UPDATE_ACCOUNT_DATA, Packet_SMSG_UPDATE_ACCOUNT_DATA, nullptr);
    ClientServices::SetMessageHandler(SMSG_UPDATE_ACCOUNT_DATA_COMPLETE, Packet_SMSG_UPDATE_ACCOUNT_DATA_COMPLETE, nullptr);
    ClientServices::SetMessageHandler(SMSG_CHECK_FOR_BOTS, Packet_SMSG_CHECK_FOR_BOTS, nullptr);
}

// VERIFY: 04a1fda3 auto
// OFFSET: 0x006B8FF0
void AccountDataShutdownBasicSystem() {
    ClientServices::ClearMessageHandler(SMSG_ACCOUNT_DATA_TIMES);
    ClientServices::ClearMessageHandler(SMSG_UPDATE_ACCOUNT_DATA);
    ClientServices::ClearMessageHandler(SMSG_UPDATE_ACCOUNT_DATA_COMPLETE);
    ClientServices::ClearMessageHandler(SMSG_CHECK_FOR_BOTS);
}

// VERIFY: bc2420a0 manual
// OFFSET: 0x006B9970
void AccountDataInitialize(bool global) {
    const uint32_t scope = global ? 0 : 1;
    CancelMacroLoadTimer(scope);
    macroSync[scope] = {};
    for (uint32_t i = 0; i < sizeof(s_accountData) / sizeof(s_accountData[0]); i++) {
        CVar::Register(s_accountData[i].m_cvarName, nullptr, 0, "1", nullptr, CATEGORY_DEFAULT, false, nullptr, false);
    }

    for (uint32_t i = 0; i < sizeof(s_accountData) / sizeof(s_accountData[0]); i++) {
        AccountDataInfo& info = s_accountData[i];

        if (info.m_global == global) {
            info.m_downloading = true;
            memset(info.m_md5, 0, sizeof(info.m_md5));
            info.m_time = 0;
            info.m_serverTime = 0;
            info.m_active = true;
        }
    }

    LoadCachedChecksums(global);

    for (uint32_t i = 0; i < sizeof(s_accountData) / sizeof(s_accountData[0]); i++) {
        AccountDataInfo& info = s_accountData[i];

        if (info.m_global != global) {
            continue;
        }

        HOSFILE file = OpenAccountFile(info.m_global, info.m_oldFile, false);

        if (file == OS_INVALID_FILE) {
            CopyAccountFile(info.m_global, info.m_cacheFile, info.m_oldFile);
        } else {
            OsCloseFile(file);
        }
    }

    uint32_t bytesWritten;

    if (global) {
        if (s_clearGlobalConfig) {
            HOSFILE file = OpenAccountFile(s_accountData[0].m_global, s_accountData[0].m_cacheFile, true);

            if (file != OS_INVALID_FILE) {
                OsWriteFile(file, "", 0, &bytesWritten);
                OsCloseFile(file);
            }

            s_accountData[0].m_changed = true;
            s_accountData[0].m_downloading = false;
        }
    } else if (s_clearCharacterConfig) {
        HOSFILE file = OpenAccountFile(s_accountData[1].m_global, s_accountData[1].m_cacheFile, true);

        if (file != OS_INVALID_FILE) {
            OsWriteFile(file, "", 0, &bytesWritten);
            OsCloseFile(file);
        }

        s_accountData[1].m_downloading = false;
        s_accountData[1].m_changed = true;
    }
}

// VERIFY: 8182067a auto
// OFFSET: 0x006B9B90
void AccountDataFlush() {
    UploadMacroCollection(0);
    UploadMacroCollection(1);
    for (int32_t i = 0; i < static_cast<int32_t>(sizeof(s_accountData) / sizeof(s_accountData[0])); i++) {
        if (s_accountData[i].m_saveTimer) {
            ClientKillTimer(s_accountData[i].m_saveTimer, SaveTimerHandler, "SaveTimerHandler");
            SaveTimerHandler(nullptr, reinterpret_cast<void*>(static_cast<intptr_t>(i)));
        }
    }
}

// VERIFY: aad4b10d auto
// OFFSET: 0x006B9BE0
void SaveAccountData(int32_t type, ACCOUNTDATALOADFUNC func, void* param) {
    AccountDataInfo& info = s_accountData[type];

    if (info.m_downloading) {
        return;
    }

    HOSFILE localFile = OpenAccountFile(info.m_global, info.m_localFile, true);
    HOSFILE cacheFile = OpenAccountFile(info.m_global, info.m_cacheFile, true);
    func(localFile, cacheFile, param);

    if (localFile != OS_INVALID_FILE) {
        OsCloseFile(localFile);
    }

    if (cacheFile != OS_INVALID_FILE) {
        OsCloseFile(cacheFile);
    }

    ScheduleAccountDataSave(type);
}

// VERIFY: 7c0bceb5 manual
// OFFSET: 0x006B9C60
void UploadAccountData() {
    AccountDataFlush();

    for (uint32_t i = 0; i < sizeof(s_accountData) / sizeof(s_accountData[0]); i++) {
        AccountDataInfo& info = s_accountData[i];
        if (IsWrathMacroType(i)) {
            continue;
        }
        info.m_downloading = false;

        if (!IsWrathMacroType(i) && (info.m_changed || info.m_time != info.m_serverTime)) {
            UploadAccountData(i);
        }
    }
}

// VERIFY: 5a1491a5 manual
// OFFSET: 0x006B9CA0
void DownloadAccountData() {
    AccountDataFlush();

    for (uint32_t i = 0; i < sizeof(s_accountData) / sizeof(s_accountData[0]); i++) {
        AccountDataInfo& info = s_accountData[i];

        if (!IsWrathMacroType(i) && (info.m_changed || info.m_time != info.m_serverTime) && !info.m_downloading) {
            DownloadAccountData(i);
        }
    }
}
