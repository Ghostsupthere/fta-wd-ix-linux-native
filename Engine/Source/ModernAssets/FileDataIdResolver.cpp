#include "Engine/Source/ModernAssets/FileDataIdResolver.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace {
struct Entry {
    uint32_t id;
    std::streamoff offset;
};

struct Resolver {
    std::once_flag init;
    std::string source;
    std::vector<Entry> index;
    std::unordered_map<uint32_t, std::string> cache;
    std::mutex cacheLock;

    static bool ParseId(const std::string& line, uint32_t& id) {
        const size_t separator = line.find(';');
        if (!separator || separator == std::string::npos) {
            return false;
        }

        uint64_t value = 0;
        for (size_t i = 0; i < separator; ++i) {
            const unsigned char ch = static_cast<unsigned char>(line[i]);
            if (!std::isdigit(ch)) {
                return false;
            }
            value = value * 10 + ch - '0';
            if (value > UINT32_MAX) {
                return false;
            }
        }

        id = static_cast<uint32_t>(value);
        return id != 0 && separator + 1 < line.size();
    }

    void Initialize() {
        std::vector<std::filesystem::path> candidates = {
            "Utils/WarcraftXL/FileDataID.csv",
            "Utils/WarcraftXL/listfile.txt",
        };

        std::error_code error;
        const std::filesystem::path data = std::filesystem::canonical("Data", error);
        if (!error && data.has_parent_path()) {
            candidates.emplace_back(data.parent_path() / "Utils/WarcraftXL/FileDataID.csv");
            candidates.emplace_back(data.parent_path() / "Utils/WarcraftXL/listfile.txt");
        }

        for (const std::filesystem::path& candidate : candidates) {
            std::ifstream file(candidate, std::ios::binary);
            if (!file) {
                continue;
            }

            std::vector<Entry> parsed;
            std::string line;
            uint32_t previous = 0;
            bool sorted = true;
            while (file) {
                const std::streamoff offset = file.tellg();
                if (!std::getline(file, line)) {
                    break;
                }
                uint32_t id = 0;
                if (!ParseId(line, id)) {
                    continue;
                }
                if (!parsed.empty() && id < previous) {
                    sorted = false;
                    break;
                }
                previous = id;
                parsed.push_back({id, offset});
            }

            if (sorted && !parsed.empty()) {
                source = candidate.string();
                index = std::move(parsed);
                return;
            }
        }
    }

    bool Resolve(uint32_t id, std::string& path) {
        path.clear();
        std::call_once(init, [this] { Initialize(); });
        if (!id || source.empty()) {
            return false;
        }

        {
            std::lock_guard<std::mutex> lock(cacheLock);
            const auto hit = cache.find(id);
            if (hit != cache.end()) {
                path = hit->second;
                return true;
            }
        }

        const auto it = std::lower_bound(index.begin(), index.end(), id,
            [](const Entry& entry, uint32_t wanted) { return entry.id < wanted; });
        if (it == index.end() || it->id != id) {
            return false;
        }

        std::ifstream file(source, std::ios::binary);
        std::string line;
        if (!file || !(file.seekg(it->offset) && std::getline(file, line))) {
            return false;
        }
        const size_t separator = line.find(';');
        if (separator == std::string::npos || separator + 1 >= line.size()) {
            return false;
        }

        path.assign(line.data() + separator + 1, line.size() - separator - 1);
        std::replace(path.begin(), path.end(), '/', '\\');
        if (path.front() == '\\' || path.find("..") != std::string::npos) {
            path.clear();
            return false;
        }

        std::lock_guard<std::mutex> lock(cacheLock);
        cache.emplace(id, path);
        return true;
    }
};

Resolver& GetResolver() {
    static Resolver resolver;
    return resolver;
}
} // namespace

bool ResolveModernAssetFileDataId(uint32_t id, std::string& path) {
    return GetResolver().Resolve(id, path);
}
