#ifndef ENGINE_SOURCE_MODEL2_WXLBONEBUDGET_H
#define ENGINE_SOURCE_MODEL2_WXLBONEBUDGET_H
#include <cstdint>
constexpr uint32_t WxlShaderBoneBudget = (256 - 31) / 3;
constexpr uint32_t WxlBoneInstances(uint32_t bones, uint32_t available = WxlShaderBoneBudget) {
    return bones ? available / bones : 0;
}
#endif
