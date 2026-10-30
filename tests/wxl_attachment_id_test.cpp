#include "Engine/Source/Model2/WxlAttachmentId.h"
#include <cassert>

int main() {
    const uint32_t a = WxlCustomAttachment(3, 34);
    const uint32_t b = WxlCustomAttachment(4, 34);
    assert(a != b && WxlIsCollectionAttachment(a));
    assert(WxlAttachmentPoint(a) == 34 && WxlAttachmentSlot(a) == 3);
    assert(WxlAttachmentPoint(b) == 34 && WxlAttachmentSlot(b) == 4);
    assert(WxlIsCollectionAttachment(0xF000) && WxlIsCollectionAttachment(0xF00B));
    assert(!WxlIsCollectionAttachment(0xF00C));
    assert(!WxlIsCollectionAttachment(34) && WxlAttachmentPoint(34) == 34);
    const uint32_t normal = WxlComponentAttachment(3, 34);
    assert(WxlIsCustomAttachment(normal) && !WxlIsCollectionAttachment(normal));
    assert(WxlAttachmentSlot(normal) == 3 && WxlAttachmentPoint(normal) == 34);
}
