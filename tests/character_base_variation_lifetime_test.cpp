#include "WoW/Common/ComponentCore/CharacterComponent.h"
#include <cassert>
#include <new>
#include <vector>

static std::vector<HTEXTURECACHE> released;
void TextureCacheDestroyTexture(HTEXTURECACHE texture) { released.push_back(texture); }

int main() {
    struct Storage {
        alignas(CharacterBaseVariation) unsigned char bytes[sizeof(CharacterBaseVariation)];
        uintptr_t guard[4];
    } storage{};
    for (uintptr_t& value : storage.guard) value = 0x1234;
    auto* variation = new (storage.bytes) CharacterBaseVariation();
    variation->m_texture[0] = reinterpret_cast<HTEXTURECACHE>(1);
    variation->m_texture[1] = nullptr;
    variation->m_texture[2] = reinterpret_cast<HTEXTURECACHE>(2);
    variation->~CharacterBaseVariation();
    assert((released == std::vector<HTEXTURECACHE>{reinterpret_cast<HTEXTURECACHE>(1), reinterpret_cast<HTEXTURECACHE>(2)}));
    for (uintptr_t value : storage.guard) assert(value == 0x1234);
}
