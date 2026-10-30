#include "WoW/Common/ComponentCore/WxlCollectionSidecar.h"
#if !defined(WXL_SIDECAR_PARSER_ONLY)
#include "Engine/Source/BlizzardCore/SFile2-Core.h"
#endif

#include <algorithm>
#include <cctype>
#include <fstream>
#include <limits>
#include <mutex>

namespace WxlCollectionSidecar {
namespace {
std::string lower(std::string_view value) {
    std::string out(value);
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

bool rows(std::string_view csv, std::vector<std::vector<std::string>>& out) {
    std::vector<std::string> row;
    std::string field;
    bool quoted = false;
    bool afterQuote = false;
    for (size_t i = 0; i < csv.size(); ++i) {
        const char c = csv[i];
        if (quoted) {
            if (c == '"' && i + 1 < csv.size() && csv[i + 1] == '"') { field += '"'; ++i; }
            else if (c == '"') { quoted = false; afterQuote = true; }
            else field += c;
        } else if (c == '"') {
            if (!field.empty() || afterQuote) return false;
            quoted = true;
        } else if (c == ',' || c == '\n') {
            row.push_back(trim(field)); field.clear(); afterQuote = false;
            if (c == '\n') { out.push_back(std::move(row)); row.clear(); }
        } else if (c == '\r' && i + 1 < csv.size() && csv[i + 1] == '\n') {
            continue;
        } else {
            if (afterQuote && c != ' ' && c != '\t') return false;
            if (!afterQuote) field += c;
        }
    }
    if (quoted) return false;
    if (!field.empty() || !row.empty() || afterQuote) { row.push_back(trim(field)); out.push_back(std::move(row)); }
    return true;
}

bool number(std::string_view s, uint32_t& out) {
    if (s.empty()) return false;
    uint32_t n = 0;
    for (char c : s) {
        if (c < '0' || c > '9' || n > (std::numeric_limits<uint32_t>::max() - (c - '0')) / 10) return false;
        n = n * 10 + (c - '0');
    }
    out = n;
    return true;
}

int slot(std::string_view name) {
    uint32_t n = 0;
    // WarcraftXL's model slots put cape at 9 and tabard at 10; the native
    // CCharacterComponent ITEM_SLOT enum has them in the opposite order.
    if (number(name, n)) {
        if (n == 9) return 10;
        if (n == 10) return 9;
        return n < 9 ? static_cast<int>(n) : -2;
    }
    const std::string s = lower(name);
    if (s.empty()) return -1;
    const auto starts = [&](const char* prefix) { return s.rfind(prefix, 0) == 0; };
    if (starts("head")) return 0;
    if (starts("shoulder")) return 1;
    if (starts("shirt")) return 2;
    if (starts("chest") || starts("robe")) return 3;
    if (starts("waist") || starts("belt")) return 4;
    if (starts("leg") || starts("pant")) return 5;
    if (starts("foot") || starts("feet") || starts("boot")) return 6;
    if (starts("bracer") || starts("wrist")) return 7;
    if (starts("glove") || starts("hand")) return 8;
    if (starts("cape") || starts("back") || starts("cloak")) return 10;
    if (starts("tabard")) return 9;
    return -2;
}

bool geosets(std::string_view text, std::vector<uint16_t>& result) {
    while (!text.empty()) {
        const size_t comma = text.find(',');
        const std::string part = trim(std::string(text.substr(0, comma)));
        uint32_t id = 0;
        if (!number(part, id) || id > 65535 || result.size() == 16) return false;
        result.push_back(static_cast<uint16_t>(id));
        if (comma == std::string_view::npos) break;
        text.remove_prefix(comma + 1);
        if (text.empty()) return false;
    }
    return true;
}
} // namespace

bool ParseModels(std::string_view csv, std::vector<Model>& result) {
    std::vector<std::vector<std::string>> table;
    if (!rows(csv, table) || table.empty()) return false;
    if (!table[0].empty() && table[0][0].size() >= 3 && table[0][0].compare(0, 3, "\xEF\xBB\xBF") == 0)
        table[0][0].erase(0, 3);
    auto column = [&](std::initializer_list<const char*> names) -> int {
        for (const char* name : names)
            for (size_t i = 0; i < table[0].size(); ++i)
                if (lower(table[0][i]) == lower(name)) return static_cast<int>(i);
        return -1;
    };
    const int display = column({"DisplayID"}), model = column({"Model", "ModelStem", "ModelName"});
    if (display < 0 || model < 0) return false;
    const int cslot = column({"Slot"}), texture = column({"Texture", "TextureStem", "ModelTexture"});
    const int folder = column({"Folder"}), suffix = column({"SuffixPolicy"}), texturePolicy = column({"TexturePolicy"});
    const int flags = column({"Flags"}), attach = column({"Attach"}), geo = column({"Geosets"});
    std::vector<Model> parsed;
    for (size_t i = 1; i < table.size(); ++i) {
        const auto& row = table[i];
        const auto field = [&](int c) -> std::string { return c >= 0 && static_cast<size_t>(c) < row.size() ? row[c] : ""; };
        Model entry;
        if (!number(field(display), entry.display) || !entry.display) continue;
        entry.model = field(model);
        entry.slot = slot(field(cslot));
        if (entry.model.empty() || entry.slot == -2) continue;
        entry.texture = field(texture); entry.folder = field(folder);
        entry.suffixPolicy = field(suffix); entry.texturePolicy = field(texturePolicy);
        uint32_t val = 0;
        if (number(field(flags), val)) entry.flags = val;
        if (number(field(attach), val) && val <= 65535) entry.attach = static_cast<int>(val);
        std::string explicitGeo = field(geo);
        const size_t colon = entry.model.find(':');
        if (colon != std::string::npos) {
            if (explicitGeo.empty()) explicitGeo = entry.model.substr(colon + 1);
            entry.model.resize(colon);
        }
        if (entry.model.empty() || (!explicitGeo.empty() && !geosets(explicitGeo, entry.geosets))) continue;
        parsed.push_back(std::move(entry));
    }
    result = std::move(parsed);
    return true;
}

IconSettings ParseIconSettings(std::string_view icon) {
    IconSettings settings;
    if (icon.empty() || (icon.front() != ':' && (icon.front() < '0' || icon.front() > '9'))) return settings;
    size_t start = 0;
    for (uint32_t column = 0; column < 2; ++column) {
        const size_t end = icon.find(':', start);
        const auto field = icon.substr(start, end == std::string_view::npos ? end : end - start);
        const size_t pipe = field.find('|');
        uint32_t left = 0, right = 0;
        const bool validLeft = number(field.substr(0, pipe), left);
        const bool validRight = pipe == std::string_view::npos || number(field.substr(pipe + 1), right);
        if (validLeft && validRight && (left <= 0xFFFF || left == UINT32_MAX) &&
            (pipe == std::string_view::npos || right <= 0xFFFF || right == UINT32_MAX)) {
            // The supplied provider also selects the left value; sided-slot
            // selection is not implemented in that provider.
            settings.attachment[column] = left;
            settings.explicitAttachment[column] = true;
        }
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    const size_t first = icon.find(':');
    if (first == std::string_view::npos) return settings;
    const size_t second = icon.find(':', first + 1);
    if (second == std::string_view::npos) return settings;
    const size_t third = icon.find(':', second + 1);
    const auto flagText = icon.substr(second + 1, third == std::string_view::npos ? third : third - second - 1);
    uint32_t parsed = 0;
    if (!flagText.empty() && number(flagText, parsed)) settings.flags = parsed;
    if (third != std::string_view::npos) {
        const auto candidate = icon.substr(third + 1);
        if (candidate.size() < 32 && candidate.find_first_of("/\\.") == std::string_view::npos)
            settings.folder.assign(candidate);
    }
    return settings;
}

void ParseIconSettings(std::string_view icon, uint32_t& flags, std::string& folder) {
    auto settings = ParseIconSettings(icon);
    flags = settings.flags;
    folder = std::move(settings.folder);
}

void AnalyzeModelList(std::string_view list, bool& collection, bool& normal) {
    for (uint32_t part = 0; !list.empty() && part < 16; ++part) {
        const size_t end = list.find(';');
        const auto token = list.substr(0, end);
        if (!token.empty()) {
            if (token.find(':') != std::string_view::npos || lower(token).rfind("collections_", 0) == 0) collection = true;
            else normal = true;
        }
        if (end == std::string_view::npos) break;
        list.remove_prefix(end + 1);
    }
}

uint32_t CollectionAttachment(const IconSettings& settings, uint32_t column, bool mixed, bool gloveBody) {
    if (column >= 2 || mixed || gloveBody || !settings.explicitAttachment[column]) return 19;
    return settings.attachment[column];
}

uint32_t ComponentAttachment(const Model& entry, uint32_t nativeSlot, bool collection) {
    // Native enum places tabard before cape, unlike WarcraftXL's model slots.
    static constexpr uint16_t defaults[] = {11, 6, 34, 34, 53, 9, 47, 3, 1, 34, 12, 3};
    if (nativeSlot >= std::size(defaults)) return UINT32_MAX;
    const std::string name = lower(entry.model);
    if (collection && nativeSlot == 8 && name.find("_glove") != std::string::npos) return 19;
    if (entry.attach >= 0) return static_cast<uint32_t>(entry.attach);
    if (!collection) {
        if (name.rfind("lshoulder_", 0) == 0 || name.find("_shoulder_l") != std::string::npos) return 6;
        if (name.rfind("rshoulder_", 0) == 0 || name.find("_shoulder_r") != std::string::npos) return 5;
        std::string stem = name;
        if (stem.size() >= 4 && (stem.compare(stem.size()-4, 4, ".mdx") == 0 ||
                                stem.compare(stem.size()-3, 3, ".m2") == 0)) stem.resize(stem.find_last_of('.'));
        if (stem.find("shoulder") != std::string::npos && stem.size() >= 2) {
            if (stem.compare(stem.size()-2, 2, "_l") == 0) return 6;
            if (stem.compare(stem.size()-2, 2, "_r") == 0) return 5;
        }
        if (name.rfind("cape_", 0) == 0) return 12;
        if (name.rfind("tabard_", 0) == 0) return 34;
    }
    return defaults[nativeSlot];
}

std::string ComponentFolder(std::string_view model, bool collection, std::string_view custom,
                            std::string_view slotFolder) {
    if (!custom.empty()) return std::string(custom);
    if (collection) return "Collections";
    const std::string name = lower(model);
    if (name.rfind("lshoulder_", 0) == 0 || name.rfind("rshoulder_", 0) == 0 ||
        name.rfind("shoulder_", 0) == 0 || name.find("_shoulder_l") != std::string::npos ||
        name.find("_shoulder_r") != std::string::npos) return "Shoulder";
    if (name.rfind("cape_", 0) == 0) return "Cape";
    if (name.rfind("tabard_", 0) == 0) return "Tabard";
    return std::string(slotFolder);
}

bool BuildModelPath(std::string_view stem, std::string_view race, std::string_view gender,
                    uint32_t flags, std::string_view folder, std::string& result) {
    if (stem.empty() || stem.size() >= 180 || stem.find_first_of("/\\") != std::string_view::npos ||
        folder.size() >= 32 || folder.find_first_of("/\\.") != std::string_view::npos) return false;
    const bool addRace = !(flags & 0x4), addGender = !(flags & 0x2);
    std::string path = "Item\\ObjectComponents\\";
    path += folder.empty() ? "Collections" : folder;
    path += '\\';
    if (flags & 0x20) { path += stem; path += '\\'; }
    path += stem;
    if (addRace || addGender) {
        path += '_';
        if (addRace) path += race;
        if (addRace && addGender && (flags & 0x40)) path += '_';
        if (addGender) path += gender;
    }
    path += ".m2";
    if (path.size() >= 260) return false;
    result = std::move(path);
    return true;
}

bool BuildTexturePath(const Model& entry, std::string_view stem, std::string_view race,
                      std::string_view gender, std::string& result, uint32_t dbcFlags) {
    if (entry.texture.empty() || entry.texture.size() >= 180 || stem.empty() || stem.size() >= 180 ||
        stem.find_first_of("/\\") != std::string_view::npos ||
        entry.folder.find_first_of("/\\.") != std::string::npos || entry.folder.size() >= 32) return false;
    const std::string policy = lower(entry.texturePolicy);
    uint32_t flags = 0;
    if (policy == "dbc" || policy == "slot") flags = dbcFlags;
    else if (policy.empty() || policy == "exact" || policy == "none") flags = 0;
    else if (policy == "retail" || policy == "new") flags = 0x8 | 0x10 | 0x40;
    else if (policy == "legacy" || policy == "race") flags = 0x8 | 0x10;
    else return false;
    std::string folder = entry.folder.empty() ? "Collections" : entry.folder;
    std::string texture = entry.texture;
    const size_t slash = texture.find_first_of("/\\");
    if (slash != std::string::npos) {
        const std::string prefix = lower(std::string_view(texture).substr(0, slash));
        static const char* const known[] = {"head", "shoulder", "shirt", "chest", "waist", "leg", "foot",
                                            "bracer", "glove", "cape", "tabard", "collections", "weapon"};
        if (std::find(std::begin(known), std::end(known), prefix) == std::end(known) ||
            texture.find_first_of("/\\", slash + 1) != std::string::npos) return false;
        folder = texture.substr(0, slash);
        texture.erase(0, slash + 1);
        if (texture.empty()) return false;
    }
    std::string path = "Item\\ObjectComponents\\" + folder + "\\";
    if (flags & 0x20) { path += stem; path += '\\'; }
    if (texture.size() >= 4 && lower(std::string_view(texture).substr(texture.size() - 4)) == ".blp")
        texture.resize(texture.size() - 4);
    path += texture;
    if ((flags & 0x8) || (flags & 0x10)) {
        path += '_';
        if (flags & 0x8) path += race;
        if ((flags & 0x58) == 0x58) path += '_';
        if (flags & 0x10) path += gender;
    }
    path += ".blp";
    if (path.size() >= 260) return false;
    result = std::move(path);
    return true;
}

bool ParseMaterials(std::string_view csv, std::vector<Material>& result) {
    std::vector<std::vector<std::string>> table;
    if (!rows(csv, table) || table.empty()) return false;
    if (!table[0].empty() && table[0][0].size() >= 3 && table[0][0].compare(0, 3, "\xEF\xBB\xBF") == 0)
        table[0][0].erase(0, 3);
    auto column = [&](const char* name) -> int {
        for (size_t i = 0; i < table[0].size(); ++i)
            if (lower(table[0][i]) == lower(name)) return static_cast<int>(i);
        return -1;
    };
    const int display = column("DisplayID"), texture = column("Texture");
    if (display < 0 || texture < 0) return false;
    const int index = column("ModelIndex"), col = column("ModelColumn"), type = column("TextureType");
    const int model = column("Model"), folder = column("Folder"), mode = column("TargetMode");
    std::vector<Material> parsed;
    for (size_t i = 1; i < table.size(); ++i) {
        const auto& row = table[i];
        const auto field = [&](int c) -> std::string { return c >= 0 && static_cast<size_t>(c) < row.size() ? row[c] : ""; };
        Material entry;
        if (!number(field(display), entry.display) || !entry.display || !number(field(type), entry.textureType)) continue;
        entry.texture = field(texture); entry.model = field(model); entry.folder = field(folder); entry.targetMode = field(mode);
        if (entry.texture.empty() || lower(entry.targetMode).find("hide") != std::string::npos ||
            lower(entry.texture).rfind("hide", 0) == 0 || lower(entry.texture).rfind("__hide__", 0) == 0) continue;
        number(field(index), entry.modelIndex);
        const auto parseColumn = [](const std::string& text) -> int {
            const std::string value = lower(text);
            if (value == "0" || value.find("modelname_1") != std::string::npos ||
                value.find("modeltexture_1") != std::string::npos || value.find("model_1") != std::string::npos ||
                value.find("texture_1") != std::string::npos) return 0;
            if (value == "1" || value == "2" || value.find("modelname_2") != std::string::npos ||
                value.find("modeltexture_2") != std::string::npos || value.find("model_2") != std::string::npos ||
                value.find("texture_2") != std::string::npos) return 1;
            return -1;
        };
        entry.modelColumn = parseColumn(field(col));
        if (entry.modelColumn < 0) entry.modelColumn = parseColumn(field(index));
        parsed.push_back(std::move(entry));
    }
    result = std::move(parsed);
    return true;
}

void LoadLooseSidecars(const std::filesystem::path& dataDirectory,
                       std::vector<Model>& models, std::vector<Material>& materials) {
    namespace fs = std::filesystem;
    std::error_code ec;
    std::vector<fs::path> archives;
    for (fs::directory_iterator it(dataDirectory, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code typeError;
        if (it->is_directory(typeError) && lower(it->path().extension().string()) == ".mpq")
            archives.push_back(it->path());
    }
    std::sort(archives.begin(), archives.end());
    for (const auto& archive : archives) {
        // Windows paths are case-insensitive; match directory and file names on
        // Linux without assuming how an extracted overlay was capitalized.
        std::error_code dirError;
        for (fs::directory_iterator dir(archive, dirError), end; !dirError && dir != end; dir.increment(dirError)) {
            std::error_code typeError;
            if (!dir->is_directory(typeError) || lower(dir->path().filename().string()) != "dbfilesclient") continue;
            std::error_code fileError;
            for (fs::directory_iterator file(dir->path(), fileError), fileEnd; !fileError && file != fileEnd; file.increment(fileError)) {
                const std::string name = lower(file->path().filename().string());
                if (name != "wxlitemdisplaymodels.csv" && name != "wxlitemdisplaymodelmaterials.csv") continue;
                std::error_code sizeError;
                if (!file->is_regular_file(sizeError)) continue;
                const auto size = file->file_size(sizeError);
                if (sizeError || size > 16u * 1024u * 1024u) continue;
                std::ifstream input(file->path(), std::ios::binary);
                std::string bytes(static_cast<size_t>(size), '\0');
                if (!input.read(bytes.data(), static_cast<std::streamsize>(size))) continue;
                if (name == "wxlitemdisplaymodels.csv") {
                    std::vector<Model> parsed;
                    if (ParseModels(bytes, parsed)) models.insert(models.end(), parsed.begin(), parsed.end());
                } else {
                    std::vector<Material> parsed;
                    if (ParseMaterials(bytes, parsed)) materials.insert(materials.end(), parsed.begin(), parsed.end());
                }
            }
        }
    }
}

#if !defined(WXL_SIDECAR_PARSER_ONLY)
const std::vector<Model>& Models() {
    static std::once_flag once;
    static std::vector<Model> models;
    std::call_once(once, [] {
        for (const char* path : {"WXLItemDisplayModels.csv", "DBFilesClient\\WXLItemDisplayModels.csv"}) {
            if (!SFile::FileExists(path)) continue;
            void* data = nullptr;
            uint32_t size = 0;
            if (!SFile::LoadFile(path, &data, &size, 0, nullptr) || !data) continue;
            std::vector<Model> parsed;
            if (size <= 16u * 1024u * 1024u && ParseModels(std::string_view(static_cast<const char*>(data), size), parsed))
                models.insert(models.end(), parsed.begin(), parsed.end());
            SFile::Unload(data);
        }
        std::error_code error;
        const auto executable = std::filesystem::canonical("/proc/self/exe", error);
        if (!error) {
            std::vector<Material> ignored;
            LoadLooseSidecars(executable.parent_path() / "Data", models, ignored);
        }
    });
    return models;
}

const std::vector<Material>& Materials() {
    static std::once_flag once;
    static std::vector<Material> materials;
    std::call_once(once, [] {
        for (const char* path : {"WXLItemDisplayModelMaterials.csv", "DBFilesClient\\WXLItemDisplayModelMaterials.csv"}) {
            if (!SFile::FileExists(path)) continue;
            void* data = nullptr;
            uint32_t size = 0;
            if (!SFile::LoadFile(path, &data, &size, 0, nullptr) || !data) continue;
            std::vector<Material> parsed;
            if (size <= 16u * 1024u * 1024u && ParseMaterials(std::string_view(static_cast<const char*>(data), size), parsed))
                materials.insert(materials.end(), parsed.begin(), parsed.end());
            SFile::Unload(data);
        }
        std::error_code error;
        const auto executable = std::filesystem::canonical("/proc/self/exe", error);
        if (!error) {
            std::vector<Model> ignored;
            LoadLooseSidecars(executable.parent_path() / "Data", ignored, materials);
        }
    });
    return materials;
}
#endif
} // namespace WxlCollectionSidecar
