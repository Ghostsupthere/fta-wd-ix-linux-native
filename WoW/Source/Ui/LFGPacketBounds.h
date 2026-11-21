#ifndef WOW_SOURCE_UI_LFGPACKETBOUNDS_H
#define WOW_SOURCE_UI_LFGPACKETBOUNDS_H

#include <cstdint>

// CDataStore::Get leaves its output untouched when a read runs past the end of a packet, so a count read from a
// short or malformed LFG packet keeps whatever the stack held and the parse loop can run billions of iterations
// (a 27 second freeze at world entry on 2026-11-20). Counts therefore start at zero and are limited to the number
// of entries the remaining bytes could possibly hold.
namespace LFGPacketBounds {

constexpr uint32_t RemainingBytes(uint32_t size, uint32_t read) {
    return size > read ? size - read : 0;
}

constexpr uint32_t ClampCount(uint32_t count, uint32_t size, uint32_t read, uint32_t minBytesPerEntry) {
    const uint32_t most = minBytesPerEntry ? RemainingBytes(size, read) / minBytesPerEntry : 0;
    return count > most ? most : count;
}

}

#endif
