#include "WoW/Common/ComponentCore/WxlWeaponSidecar.h"
#if !defined(WXL_SIDECAR_PARSER_ONLY)
#include "Engine/Source/BlizzardCore/SFile2-Core.h"
#endif

#include <algorithm>
#include <cctype>
#include <fstream>
#include <limits>
#include <mutex>

namespace WxlWeaponSidecar {
namespace {
std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

// Letters and digits only, lowercase: "Model1OffhandPath" and "model1_offhand_path" match.
std::string key(std::string_view name) {
    std::string out;
    for (unsigned char c : name)
        if (std::isalnum(c)) out += static_cast<char>(std::tolower(c));
    return out;
}

// Rows may be shorter than the header (the shipped sample drops trailing empty cells).
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

int column(const std::vector<std::string>& header, const char* name) {
    const std::string wanted = key(name);
    for (size_t i = 0; i < header.size(); ++i)
        if (key(header[i]) == wanted) return static_cast<int>(i);
    return -1;
}

std::string cell(const std::vector<std::string>& row, int index) {
    return index >= 0 && static_cast<size_t>(index) < row.size() ? row[static_cast<size_t>(index)] : std::string();
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

bool truthy(const std::string& value) {
    const std::string v = key(value);
    return v == "true" || v == "yes" || v == "1";
}
}  // namespace

GeosetSpec ParseGeosets(std::string_view cell) {
    GeosetSpec spec;
    const auto begin = cell.find_first_not_of(" \t");
    if (begin == std::string_view::npos) return spec;   // empty cell: no filter
    spec.filter = true;
    spec.ids.push_back(0);                              // the base geoset is always implied
    size_t i = begin;
    while (i < cell.size() && spec.ids.size() < 16) {
        while (i < cell.size() && (cell[i] == ' ' || cell[i] == '\t' || cell[i] == ',')) ++i;
        if (i >= cell.size() || cell[i] < '0' || cell[i] > '9') break;
        uint32_t value = 0;
        while (i < cell.size() && cell[i] >= '0' && cell[i] <= '9') value = value * 10u + static_cast<uint32_t>(cell[i++] - '0');
        const uint16_t id = static_cast<uint16_t>(value);
        if (std::find(spec.ids.begin(), spec.ids.end(), id) == spec.ids.end()) spec.ids.push_back(id);
    }
    return spec;
}

bool ParseWeapons(std::string_view csv, std::vector<Weapon>& result) {
    std::vector<std::vector<std::string>> table;
    if (!rows(csv, table) || table.empty()) return false;
    const auto& header = table[0];
    const int display = column(header, "DisplayID");
    const int model1 = column(header, "Model1Path");
    if (display < 0 || model1 < 0) return false;
    const int model2 = column(header, "Model2Path");
    const int geoset1 = column(header, "Geoset1"), geoset2 = column(header, "Geoset2");
    const int offhand1 = column(header, "Model1OffhandPath"), offhand2 = column(header, "Model2OffhandPath");
    const int offhandGeoset1 = column(header, "Geoset1Offhand"), offhandGeoset2 = column(header, "Geoset2Offhand");
    int mirror = column(header, "NoAutoMirror");
    if (mirror < 0) mirror = column(header, "AutoMirror");

    std::vector<Weapon> parsed;
    for (size_t r = 1; r < table.size(); ++r) {
        const auto& row = table[r];
        Weapon entry;
        if (!number(cell(row, display), entry.display) || !entry.display) continue;
        entry.model[0] = cell(row, model1);
        if (entry.model[0].empty()) continue;   // Model1Path is required
        if (std::any_of(parsed.begin(), parsed.end(), [&](const Weapon& w) { return w.display == entry.display; })) continue;
        entry.model[1] = cell(row, model2);
        entry.offhandModel[0] = cell(row, offhand1);
        entry.offhandModel[1] = cell(row, offhand2);
        entry.geoset[0] = ParseGeosets(cell(row, geoset1));
        entry.geoset[1] = ParseGeosets(cell(row, geoset2));
        entry.offhandGeoset[0] = ParseGeosets(cell(row, offhandGeoset1));
        entry.offhandGeoset[1] = ParseGeosets(cell(row, offhandGeoset2));
        entry.mirrorOffhand = truthy(cell(row, mirror));
        parsed.push_back(std::move(entry));
    }
    result = std::move(parsed);
    return true;
}

bool ParseTextures(std::string_view csv, std::vector<Texture>& result) {
    std::vector<std::vector<std::string>> table;
    if (!rows(csv, table) || table.empty()) return false;
    const auto& header = table[0];
    const int display = column(header, "DisplayID"), modelColumn = column(header, "ModelColumn");
    const int type = column(header, "TextureType"), path = column(header, "TexturePath");
    if (display < 0 || modelColumn < 0 || type < 0 || path < 0) return false;

    std::vector<Texture> parsed;
    for (size_t r = 1; r < table.size(); ++r) {
        const auto& row = table[r];
        Texture entry;
        if (!number(cell(row, display), entry.display) || !entry.display) continue;
        if (!number(cell(row, modelColumn), entry.column) || entry.column > 1) continue;
        if (!number(cell(row, type), entry.type)) continue;
        entry.path = cell(row, path);
        if (entry.path.empty()) continue;
        if (std::any_of(parsed.begin(), parsed.end(), [&](const Texture& t) {
                return t.display == entry.display && t.column == entry.column && t.type == entry.type; })) continue;
        parsed.push_back(std::move(entry));
    }
    result = std::move(parsed);
    return true;
}

namespace {
const Weapon* find(const std::vector<Weapon>& weapons, uint32_t display) {
    for (const Weapon& w : weapons) if (w.display == display) return &w;
    return nullptr;
}

void collect(const std::vector<Texture>& textures, uint32_t display, std::vector<Texture>& out) {
    out.clear();
    for (const Texture& t : textures)
        if (t.display == display && t.column == 0) out.push_back(t);
}
}  // namespace

bool Resolve(const std::vector<Weapon>& weapons, const std::vector<Texture>& textures,
             uint32_t display, bool offhand, Resolution& out) {
    const Weapon* weapon = find(weapons, display);
    if (!weapon) return false;
    Resolution result;
    collect(textures, display, result.textures);

    if (offhand) {
        if (!weapon->offhandModel[0].empty()) {
            result.model = weapon->offhandModel[0];
            result.geoset = weapon->offhandGeoset[0];
            result.mirror = weapon->mirrorOffhand;
            out = std::move(result);
            return true;
        }
        if (weapon->mirrorOffhand) {
            result.model = weapon->model[0];
            result.geoset = weapon->offhandGeoset[0].filter ? weapon->offhandGeoset[0] : weapon->geoset[0];
            result.mirror = true;
            out = std::move(result);
            return true;
        }
    }

    // A column with a known model but nothing to change keeps the stock attach.
    if (result.textures.empty() && !weapon->geoset[0].filter) return false;
    result.model = weapon->model[0];
    result.geoset = weapon->geoset[0];
    out = std::move(result);
    return true;
}

void LoadLooseSidecars(const std::filesystem::path& dataDirectory,
                       std::vector<Weapon>& weapons, std::vector<Texture>& textures) {
    namespace fs = std::filesystem;
    std::error_code ec;
    std::vector<fs::path> archives;
    for (fs::directory_iterator it(dataDirectory, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code typeError;
        std::string extension = it->path().extension().string();
        for (char& c : extension) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (it->is_directory(typeError) && extension == ".mpq") archives.push_back(it->path());
    }
    std::sort(archives.begin(), archives.end());
    for (const auto& archive : archives) {
        std::error_code dirError;
        for (fs::directory_iterator dir(archive, dirError), end; !dirError && dir != end; dir.increment(dirError)) {
            std::error_code typeError;
            std::string folder = dir->path().filename().string();
            for (char& c : folder) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (!dir->is_directory(typeError) || folder != "dbfilesclient") continue;
            std::error_code fileError;
            for (fs::directory_iterator file(dir->path(), fileError), fileEnd; !fileError && file != fileEnd; file.increment(fileError)) {
                std::string name = file->path().filename().string();
                for (char& c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                if (name != "wxlweaponmodels.csv" && name != "wxlweapontextures.csv") continue;
                std::error_code sizeError;
                if (!file->is_regular_file(sizeError)) continue;
                const auto size = file->file_size(sizeError);
                if (sizeError || size > 16u * 1024u * 1024u) continue;
                std::ifstream input(file->path(), std::ios::binary);
                std::string bytes(static_cast<size_t>(size), '\0');
                if (!input.read(bytes.data(), static_cast<std::streamsize>(size))) continue;
                if (name == "wxlweaponmodels.csv") {
                    std::vector<Weapon> parsed;
                    if (ParseWeapons(bytes, parsed)) weapons.insert(weapons.end(), parsed.begin(), parsed.end());
                } else {
                    std::vector<Texture> parsed;
                    if (ParseTextures(bytes, parsed)) textures.insert(textures.end(), parsed.begin(), parsed.end());
                }
            }
        }
    }
}

#if !defined(WXL_SIDECAR_PARSER_ONLY)
namespace {
struct Tables {
    std::vector<Weapon> weapons;
    std::vector<Texture> textures;
};

// Lowest-priority first-seen wins when the same display appears in several files, so the order is
// the upstream search order: the archive root, DBFilesClient, then loose Data/*.mpq overlays.
const Tables& tables() {
    static std::once_flag once;
    static Tables loaded;
    std::call_once(once, [] {
        const auto read = [](const char* path, std::string& bytes) {
            if (!SFile::FileExists(path)) return false;
            void* data = nullptr;
            uint32_t size = 0;
            if (!SFile::LoadFile(path, &data, &size, 0, nullptr) || !data) return false;
            const bool fits = size <= 16u * 1024u * 1024u;
            if (fits) bytes.assign(static_cast<const char*>(data), size);
            SFile::Unload(data);
            return fits;
        };
        for (const char* path : {"WXLWeaponModels.csv", "DBFilesClient\\WXLWeaponModels.csv"}) {
            std::string bytes;
            std::vector<Weapon> parsed;
            if (read(path, bytes) && ParseWeapons(bytes, parsed)) loaded.weapons.insert(loaded.weapons.end(), parsed.begin(), parsed.end());
        }
        for (const char* path : {"WXLWeaponTextures.csv", "DBFilesClient\\WXLWeaponTextures.csv"}) {
            std::string bytes;
            std::vector<Texture> parsed;
            if (read(path, bytes) && ParseTextures(bytes, parsed)) loaded.textures.insert(loaded.textures.end(), parsed.begin(), parsed.end());
        }
        std::error_code error;
        const auto executable = std::filesystem::canonical("/proc/self/exe", error);
        if (!error) LoadLooseSidecars(executable.parent_path() / "Data", loaded.weapons, loaded.textures);
        // Concatenated files can repeat a display or texture key; the first entry wins.
        std::vector<Weapon> weapons;
        for (Weapon& w : loaded.weapons)
            if (!find(weapons, w.display)) weapons.push_back(std::move(w));
        loaded.weapons = std::move(weapons);
        std::vector<Texture> textures;
        for (Texture& t : loaded.textures)
            if (std::none_of(textures.begin(), textures.end(), [&](const Texture& e) {
                    return e.display == t.display && e.column == t.column && e.type == t.type; }))
                textures.push_back(std::move(t));
        loaded.textures = std::move(textures);
    });
    return loaded;
}
}  // namespace

bool Resolve(uint32_t display, bool offhand, Resolution& out) {
    const Tables& t = tables();
    return !t.weapons.empty() && Resolve(t.weapons, t.textures, display, offhand, out);
}
#endif
}  // namespace WxlWeaponSidecar
