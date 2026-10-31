#include "native_config.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cctype>
#include <fstream>
#include <unordered_map>
namespace {
struct IntSetting
{
    const char* key;
    int Config::*value;
    int lo;
    int hi;
};

struct FloatSetting
{
    const char* key;
    float Config::*value;
    float lo;
    float hi;
};

struct BoolSetting
{
    const char* key;
    bool Config::*value;
};

const IntSetting kIntSettings[] = {
    {"Quality", &Config::quality, 1, 3},     {"StockFog", &Config::stockFog, 0, 1},
    {"DataMode", &Config::dataMode, 0, 1},   {"ColorSpace", &Config::colorSpace, 0, 1},
    {"DebugView", &Config::debugView, 0, 3}, {"LogLevel", &Config::logLevel, 0, 2},
    {"ClassicPhase", &Config::classicPhase, 0, 1},
};

const IntSetting kLookIntSettings[] = {
    {"ForeverGlow", &Config::foreverGlow, 0, 1},
};

const FloatSetting kLookFloatSettings[] = {
    {"ColorGrading", &Config::colorGrading, 0.0f, 1.0f},
};

const IntSetting kWaterIntSettings[] = {
    {"WaterQuality", &Config::waterQuality, 1, 3},
    {"WaterDebugView", &Config::waterDebugView, 0, 6},
};

const FloatSetting kFloatSettings[] = {
    {"Density", &Config::density, 0.0f, 10.0f},
    {"FogDensityScale", &Config::fogDensityScale, 0.0f, 2.0f},
    {"Haze", &Config::haze, 0.0f, 10.0f},
    {"GroundFog", &Config::groundFog, 0.0f, 10.0f},
    {"FarFog", &Config::farFog, 0.0f, 10.0f},
    {"SunScatter", &Config::sunScatter, 0.0f, 10.0f},
    {"Ambient", &Config::ambient, 0.0f, 10.0f},
    {"Exposure", &Config::exposure, 0.0f, 10.0f},
    {"ClassicExposure", &Config::classicExposure, 0.0f, 10.0f},
    {"LocalLightIntensity", &Config::localLightIntensity, 0.0f, 8.0f},
    {"LocalLightPhase", &Config::localLightPhase, -0.9f, 0.9f},
    {"InteriorDensity", &Config::interiorDensity, 0.0f, 1.0f},
    {"NoiseAmount", &Config::noiseAmount, 0.0f, 1.0f},
    {"NoiseScale", &Config::noiseScale, 0.001f, 1.0f},
    {"NoiseWindSpeed", &Config::noiseWindSpeed, 0.0f, 10.0f},
    {"GodRays", &Config::godRays, 0.0f, 4.0f},
    {"FarClipMax", &Config::farClipMax, 0.0f, kEngineFarClipMax},
    {"MaxDistance", &Config::maxDistance, 200.0f, 5000.0f},
    {"Temporal", &Config::temporal, 0.0f, 0.97f},
};

const FloatSetting kWaterFloatSettings[] = {
    {"WaterWaves", &Config::waterWaves, 0.0f, 2.0f},
    {"WaterWind", &Config::waterWind, 0.5f, 10.0f},
    {"WaterFoam", &Config::waterFoam, 0.0f, 2.0f},
    {"WaterReflections", &Config::waterReflections, 0.0f, 2.0f},
    {"WaterSpecular", &Config::waterSpecular, 0.0f, 4.0f},
    {"WaterClarity", &Config::waterClarity, 0.25f, 4.0f},
    {"WaterZoneColors", &Config::waterZoneColors, 0.0f, 1.0f},
    {"WaterRipples", &Config::waterRipples, 0.0f, 2.0f},
};

const BoolSetting kBoolSettings[] = {
    {"GlowCompensation", &Config::glowCompensation}, {"LocalLights", &Config::localLights},
    {"InteriorAware", &Config::interiorAware},       {"Underwater", &Config::underwater},
    {"LiquidDepth", &Config::liquidDepth},           {"SunMarker", &Config::sunMarker},
    {"Water", &Config::water},                       {"Multisampling", &Config::multisampling},
    {"ClassicNoise", &Config::classicNoise},       {"TransparentFog", &Config::transparentFog},
};

const BoolSetting kWaterBoolSettings[] = {
    {"WaterClientSplashes", &Config::waterClientSplashes},
};

template <typename Visit>
void ForEachIntSetting(Visit visit)
{
    for (const IntSetting& s : kIntSettings)
        visit(s);
    for (const IntSetting& s : kLookIntSettings)
        visit(s);
    for (const IntSetting& s : kWaterIntSettings)
        visit(s);
}

template <typename Visit>
void ForEachFloatSetting(Visit visit)
{
    for (const FloatSetting& s : kFloatSettings)
        visit(s);
    for (const FloatSetting& s : kLookFloatSettings)
        visit(s);
    for (const FloatSetting& s : kWaterFloatSettings)
        visit(s);
}

template <typename Visit>
void ForEachBoolSetting(Visit visit)
{
    for (const BoolSetting& s : kBoolSettings)
        visit(s);
    for (const BoolSetting& s : kWaterBoolSettings)
        visit(s);
}


template <typename Setting, size_t N>
bool SameValues(const Setting (&settings)[N], const Config& a, const Config& b)
{
    for (const Setting& s : settings)
        if (a.*s.value != b.*s.value)
            return false;
    return true;
}


std::string Clean(std::string s) {
    auto comment=s.find_first_of(";#");if(comment!=std::string::npos)s.resize(comment);
    s.erase(std::remove_if(s.begin(),s.end(),[](unsigned char c){return std::isspace(c);}),s.end());
    std::transform(s.begin(),s.end(),s.begin(),[](unsigned char c){return std::tolower(c);});return s;
}
}
Config ReadNativeVisualConfig(const std::string& path) {
    Config c;std::unordered_map<std::string,float> values;std::ifstream f(path);std::string line;bool section=false;
    while(std::getline(f,line)) {
        line=Clean(line);if(line.empty())continue;
        if(line[0]=='['){section=line=="[coavolfog]";continue;}if(!section)continue;
        auto equal=line.find('=');if(equal==std::string::npos)continue;
        const char* start=line.c_str()+equal+1;char* end=nullptr;float value=std::strtof(start,&end);
        if(end!=start&&!*end&&std::isfinite(value))values[line.substr(0,equal)]=value;
    }
    auto number=[&](const char* key,float fallback,float lo,float hi) {
        auto i=values.find(Clean(key));return i==values.end()?fallback:std::clamp(i->second,lo,hi);
    };
    c.enable=number("Enable",c.enable,0,1)!=0;c.hooks=number("EngineHooks",c.hooks,0,1)!=0;
    ForEachIntSetting([&](const IntSetting& s){c.*s.value=static_cast<int>(number(s.key,c.*s.value,s.lo,s.hi));});
    ForEachFloatSetting([&](const FloatSetting& s){c.*s.value=number(s.key,c.*s.value,s.lo,s.hi);});
    ForEachBoolSetting([&](const BoolSetting& s){c.*s.value=number(s.key,c.*s.value,0,1)!=0;});
    return c;
}

bool SameFogSettings(const Config& a, const Config& b)
{
    return SameValues(kIntSettings, a, b) && SameValues(kFloatSettings, a, b) && SameValues(kBoolSettings, a, b);
}

