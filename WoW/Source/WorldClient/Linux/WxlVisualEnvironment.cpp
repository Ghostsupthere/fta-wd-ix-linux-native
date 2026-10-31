#include "WxlModernVisuals.h"
#include "WoW/Source/WorldClient/Linux/WxlVisualEnvironment.h"
#include "WoW/Source/WorldClient/Linux/WxlFogRange.h"
#include "WoW/Source/WorldClient/DayNight__DNInfo.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

namespace {
std::once_flag s_profileOnce;
std::unordered_map<std::string, std::string> s_profile;

std::string Trim(std::string_view value) {
    size_t first = 0;
    while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first]))) ++first;
    size_t last = value.size();
    while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1]))) --last;
    return std::string(value.substr(first, last - first));
}

void LoadProfile() {
#if defined(WOWDECOMP_LINUX)
    std::error_code error;
    const std::filesystem::path executable = std::filesystem::canonical("/proc/self/exe", error);
    if (error) return;
    const std::filesystem::path data = std::filesystem::canonical(executable.parent_path() / "Data", error);
    if (error) return;
    std::ifstream file(data.parent_path() / "WarcraftXL.cfg");
    std::string line;
    while (std::getline(file, line)) {
        const std::string trimmed = Trim(line);
        if (trimmed.empty() || trimmed[0] == ';' || trimmed[0] == '#') continue;
        const size_t equal = trimmed.find('=');
        if (equal == std::string::npos) continue;
        const std::string key = Trim(std::string_view(trimmed).substr(0, equal));
        if (!key.empty()) s_profile[key] = Trim(std::string_view(trimmed).substr(equal + 1));
    }
#endif
}

const char* Value(const char* key) {
    if (!key || !*key) return nullptr;
    if (const char* environment = std::getenv(key); environment && *environment) return environment;
    std::call_once(s_profileOnce, LoadProfile);
    const auto it = s_profile.find(key);
    return it == s_profile.end() ? nullptr : it->second.c_str();
}
}  // namespace

bool WxlProfileEnabled(const char* key, bool fallback) {
    const char* value = Value(key);
    if (!value || !*value) return fallback;
    return value[0] != '0' && value[0] != 'n' && value[0] != 'N' &&
           value[0] != 'f' && value[0] != 'F';
}

float WxlProfileNumber(const char* key, float fallback, float minimum, float maximum) {
    const char* value = Value(key);
    if (!value || !*value) return fallback;
    char* end = nullptr;
    const float parsed = std::strtof(value, &end);
    if (!end || *end != '\0' || !std::isfinite(parsed)) return fallback;
    return std::clamp(parsed, minimum, maximum);
}

WxlWorldFogScope::WxlWorldFogScope(bool underwater,const float* target) {
#if defined(WOWDECOMP_LINUX)
    const bool replaceFog = WxlBeginVisualWorld(underwater,target);
    if (underwater || !WxlProfileEnabled("WXL_VISUAL_ENVIRONMENT", true)) return;
    auto* info = DayNight::GetInfo();
    if (!info) return;
    if (replaceFog) {
        m_outdoorStart = info->m_fog.m_start; m_outdoorEnd = info->m_fog.m_end;
        m_interiorStart = info->m_fogInterior.m_start; m_interiorEnd = info->m_fogInterior.m_end;
        m_groupStart = info->m_fogGroup.m_start; m_groupEnd = info->m_fogGroup.m_end;
        info->m_fog.m_start = info->m_fogInterior.m_start = info->m_fogGroup.m_start = 1.0e8f;
        info->m_fog.m_end = info->m_fogInterior.m_end = info->m_fogGroup.m_end = 2.0e8f;
        m_replaced = m_active = true;
        return;
    }
    const float scale = WxlProfileNumber("WXL_FOG_DENSITY_SCALE", 0.5f, 0.0f, 2.0f);
    float outdoor = 0.0f;
    float interior = 0.0f;
    if (!WxlScaleFogRange(info->m_fog.m_start, info->m_fog.m_end, scale, outdoor) ||
        !WxlScaleFogRange(info->m_fogInterior.m_start, info->m_fogInterior.m_end, scale, interior)) return;
    m_outdoorEnd = info->m_fog.m_end;
    m_interiorEnd = info->m_fogInterior.m_end;
    info->m_fog.m_end = outdoor;
    info->m_fogInterior.m_end = interior;
    m_active = true;
#else
    (void)underwater;(void)target;
#endif
}

WxlWorldFogScope::~WxlWorldFogScope() {
    if (!m_active) return;
    auto* info = DayNight::GetInfo();
    if (m_replaced) {
        info->m_fog.m_start = m_outdoorStart;
        info->m_fogInterior.m_start = m_interiorStart;
        info->m_fogGroup.m_start = m_groupStart;info->m_fogGroup.m_end = m_groupEnd;
    }
    info->m_fog.m_end = m_outdoorEnd;
    info->m_fogInterior.m_end = m_interiorEnd;
}
