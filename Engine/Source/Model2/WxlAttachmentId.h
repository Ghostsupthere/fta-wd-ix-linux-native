#ifndef ENGINE_SOURCE_MODEL2_WXLATTACHMENTID_H
#define ENGINE_SOURCE_MODEL2_WXLATTACHMENTID_H

#include <cstdint>

// Preserve slot ownership separately from the native M2 attachment point.
constexpr uint32_t WxlCustomAttachment(uint32_t slot, uint32_t point) {
    return 0xE1000000u | ((slot & 0xffu) << 16) | (point & 0xffffu);
}
constexpr uint32_t WxlComponentAttachment(uint32_t slot, uint32_t point) {
    return 0xE2000000u | ((slot & 0xffu) << 16) | (point & 0xffffu);
}
constexpr bool WxlIsCustomAttachment(uint32_t id) {
    return (id & 0xff000000u) == 0xE1000000u || (id & 0xff000000u) == 0xE2000000u;
}
constexpr uint32_t WxlAttachmentPoint(uint32_t id) { return WxlIsCustomAttachment(id) ? (id & 0xffffu) : id; }
constexpr uint32_t WxlAttachmentSlot(uint32_t id) { return (id >> 16) & 0xffu; }
constexpr bool WxlIsCollectionAttachment(uint32_t id) {
    return (id >= 0xF000u && id < 0xF000u + 12u) || (id & 0xff000000u) == 0xE1000000u;
}
#endif
