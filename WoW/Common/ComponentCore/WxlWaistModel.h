#ifndef WOW_COMMON_COMPONENTCORE_WXLWAISTMODEL_H
#define WOW_COMMON_COMPONENTCORE_WXLWAISTMODEL_H

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>

namespace WxlWaistModel {
inline std::string Normalize(std::string_view value) {
    std::string result(value);
    for (char& c : result) c = c == '/' ? '\\' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return result;
}

inline bool BuildPath(std::string_view name, bool model, std::string& output) {
    std::string path(name);
    std::replace(path.begin(), path.end(), '/', '\\');
    if (path.empty() || path.front() == '\\' || path.find("..") != std::string::npos ||
        path.find_first_of(":;\r\n") != std::string::npos) return false;
    if (path.find('\\') == std::string::npos) path = "Item\\ObjectComponents\\Waist\\" + path;
    const size_t dot = path.find_last_of('.'), slash = path.find_last_of('\\');
    const std::string extension = dot != std::string::npos && (slash == std::string::npos || dot > slash)
        ? Normalize(std::string_view(path).substr(dot)) : std::string();
    if (extension.empty()) path += model ? ".mdx" : ".blp";
    else if (model ? (extension != ".mdx" && extension != ".m2") : extension != ".blp") return false;
    if (path.size() >= 260) return false;
    output = std::move(path);
    return true;
}

inline bool TextureMatches(std::string_view stored, std::string_view requested) {
    auto key = [](std::string_view value) {
        std::string normalized = Normalize(value);
        if (normalized.size() >= 4 && normalized.compare(normalized.size() - 4, 4, ".blp") == 0)
            normalized.resize(normalized.size() - 4);
        return normalized;
    };
    const std::string actual = key(stored);
    return !actual.empty() && actual != "solidtexture" && actual == key(requested);
}

struct RetryGate {
    uint32_t started = 0;
    uint32_t next = 0;
    uint32_t attempts = 0;

    bool Expired(uint32_t now) const { return now - started >= 10000 || attempts >= 7; }
    bool Due(uint32_t now) const { return static_cast<int32_t>(now - next) >= 0; }
    void Failed(uint32_t now) {
        ++attempts;
        next = now + std::min<uint32_t>(50u << std::min<uint32_t>(attempts - 1, 5), 1000u);
    }
};
} // namespace WxlWaistModel
#endif
