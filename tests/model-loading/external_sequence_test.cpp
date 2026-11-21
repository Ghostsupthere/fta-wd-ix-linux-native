#include <array>
#include <cassert>
#include <cstdint>
#include <limits>
#include <strings.h>

static uint32_t s_sequenceIndex = 0xFFFFFFFF;
static uint8_t* s_sequenceData;
static uint32_t s_sequenceSize;
static bool s_validateSequence;

template <class T>
struct M2Array {
    uint32_t m_count = 0;
    uint32_t m_offset = 0;
    T* elements = nullptr;

    T& operator[](uint32_t index) const { return elements[index]; }
    void Relocate(const void* base) {
        elements = m_count ? reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(base) + m_offset) : nullptr;
        m_offset = std::numeric_limits<uint32_t>::max();
    }
};

struct M2TrackBase {
    uint16_t m_loopIndex = 0xFFFF;
    M2Array<M2Array<uint32_t>> m_sequenceTimes;
};

template <class T>
struct M2Track : M2TrackBase {
    M2Array<M2Array<T>> m_sequenceKeys;
};

struct M2Sequence {
    uint32_t m_flags = 0x10;
    uint16_t m_aliasNext;
};
struct M2Loop {
    uint32_t m_length;
};
struct M2TextureTransform {
    M2TrackBase m_translation;
};
struct M2Data {
    M2Array<M2Sequence> m_sequences;
    M2Track<uint32_t> track;
    M2TrackBase globalTrack;
    M2Array<M2Loop> m_loops;
    M2Array<M2TextureTransform> m_textureTransforms;
};

template <class T>
int32_t M2Init(uint8_t*, uint32_t, const M2Data&, M2Array<T>&) {
    assert(false);
    return 0;
}

#include "sequence_tracks.inc"

int32_t M2Init(uint8_t* base, uint32_t size, M2Data& data) {
    return M2Init(base, size, data, data.track) && M2Init(base, size, data, data.globalTrack);
}

struct CAsyncObject {
    void* m_buffer;
    uint32_t m_size;
};

struct CM2Shared {
    M2Data* m_data;
    uint32_t m_fileSize = sizeof(M2Data);
    const char* m_fileName = "test";
    int32_t FinishLoadingLowPrioritySequence(uint32_t sequenceIndex, CAsyncObject* object);
};

static void DebugLog(const char*, const char*, uint32_t) {}

#include "sequence_complete.inc"

static int SStrCmpI(const char* first, const char* second, std::size_t limit) {
    return strncasecmp(first, second, limit);
}

#include "shoulder_scroll.inc"

int main() {
    alignas(uint32_t) std::array<uint8_t, 64> buffer{};
    M2Sequence aliases[] = {{0x10, 1}, {0x10, 0}};
    M2Array<uint32_t> times[] = {{1, 0}, {1, 4}};
    M2Array<uint32_t> keys[] = {{2, 8}, {2, 64}};
    M2Array<uint32_t> globalTimes[] = {{1, 400}};
    M2Data data;
    data.m_sequences = {2, 0, aliases};
    data.track.m_sequenceTimes = {2, 0, times};
    data.track.m_sequenceKeys = {2, 0, keys};
    data.globalTrack.m_loopIndex = 0;
    data.globalTrack.m_sequenceTimes = {1, 0, globalTimes};
    CM2Shared shared{&data};
    CAsyncObject load{buffer.data(), buffer.size()};

    assert(!shared.FinishLoadingLowPrioritySequence(0, &load));
    assert(times[0].m_offset == 0 && times[1].m_offset == 4);
    assert(keys[0].m_offset == 8 && keys[1].m_offset == 64);
    assert(aliases[0].m_flags == 0x10 && aliases[1].m_flags == 0x10);

    keys[1].m_offset = 56;
    aliases[1].m_aliasNext = 1;
    assert(!shared.FinishLoadingLowPrioritySequence(0, &load));
    aliases[1].m_aliasNext = 8;
    assert(!shared.FinishLoadingLowPrioritySequence(0, &load));
    aliases[1].m_aliasNext = 0;

    assert(shared.FinishLoadingLowPrioritySequence(0, &load));
    assert(reinterpret_cast<void*>(times[1].elements) == buffer.data() + 4);
    assert(reinterpret_cast<void*>(keys[1].elements) == buffer.data() + 56);
    assert(aliases[0].m_flags == 0x20 && aliases[1].m_flags == 0x20);
    assert(globalTimes[0].m_offset == 400);
    assert(s_sequenceIndex == 0xFFFFFFFF && !s_sequenceData && !s_sequenceSize && !s_validateSequence);

    M2Loop loop{2};
    uint32_t scrollTimes[] = {0, 10000};
    M2Array<uint32_t> timestamps{2, 0, scrollTimes};
    M2TextureTransform scroll{{0, {1, 0, &timestamps}}};
    data.m_loops = {1, 0, &loop};
    data.m_textureTransforms = {1, 0, &scroll};
    RepairShoulderScrollPeriod(data, "OtherShoulder.m2");
    assert(loop.m_length == 2);
    RepairShoulderScrollPeriod(data, "Item/ObjectComponents/Shoulder/DKT6_BLOOD_LSHOULDER.M2");
    assert(loop.m_length == 10000);
    loop.m_length = 2;
    scrollTimes[1] = 9000;
    RepairShoulderScrollPeriod(data, "Item\\ObjectComponents\\Shoulder\\dkt6_unholy_rshoulder.m2");
    assert(loop.m_length == 2);
}
