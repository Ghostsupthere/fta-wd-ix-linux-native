#if defined(WOWDECOMP_ARCH_64)

#include <cstdint>
#ifdef WOWDECOMP_LINUX
#include "Engine/Source/Base/OsTypes.h"
#else
#include <windows.h>
#endif
#include "Engine/Source/BlizzardCore/Source/System/Lock/Lock.h"

// OFFSET: 0x00435C00
void Blizzard::Lock::EventCreate(Event& event) {
    event = CreateEventA(nullptr, FALSE, FALSE, nullptr);
}

// OFFSET: 0x00428320
void Blizzard::Lock::EventDestroy(Event& event) {
    CloseHandle(event);
}

// OFFSET: 0x00428340
void Blizzard::Lock::EventSet(Event& event) {
    SetEvent(event);
}

// OFFSET: 0x00428360
void Blizzard::Lock::EventWaitForever(Event& event) {
    WaitForSingleObject(event, INFINITE);
}

// OFFSET: 0x00428380
Blizzard::Lock::Event* Blizzard::Lock::EventWait(uint32_t timeout, Event& event) {
    return WaitForSingleObject(event, timeout) == WAIT_OBJECT_0 ? &event : nullptr;
}

// INLINED
Blizzard::Lock::EventObject::EventObject() {
    this->m_event = CreateEventA(nullptr, FALSE, FALSE, nullptr);
}

// OFFSET: 0x0042B390
Blizzard::Lock::EventObject::~EventObject() {
    EventDestroy(this->m_event);
}

#endif
