#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#ifdef NDEBUG
#error Assertions must be enabled
#endif
struct C44Matrix { float m[16]; };
unsigned allocation, failAt, live;
void* Allocate(size_t size) {
    if (++allocation == failAt) return nullptr;
    void* ptr = std::malloc(size);
    assert(ptr);
    ++live;
    return ptr;
}
void Free(void* ptr) {
    assert(ptr && live);
    --live;
    std::free(ptr);
}
void* SMemAlloc(size_t size, const char*, int, int) { return Allocate(size); }
void SMemFree(void* ptr, const char*, int, int) { Free(ptr); }
namespace SIMD {
void* AllocA16(size_t size, const char*, int) { return Allocate(size); }
void FreeA16(void* ptr) { Free(ptr); }
}
struct Counts {
    struct Array { unsigned m_count = 1; } m_bones, m_textureTransforms;
};
struct Model {
    bool m_instanceLoadFailed = false;
    C44Matrix* m_boneMatrices = nullptr;
    C44Matrix* m_textureMatrices = nullptr;
    bool AllocateForLoad(Counts* data) {
        unsigned emitterBytes = 16;
        unsigned emitterOffset = 16;
#include "model_allocations.inc"

        Free(memory);
        return true;
    }
};
int main() {
    Counts data;
    for (failAt = 1; failAt <= 3; ++failAt) {
        allocation = 0;
        Model model;
        assert(!model.AllocateForLoad(&data));
        assert(model.m_instanceLoadFailed && !model.m_boneMatrices && !model.m_textureMatrices);
        assert(live == 0);
    }
    allocation = 0;
    Model succeeded;
    assert(succeeded.AllocateForLoad(&data));
    assert(!succeeded.m_instanceLoadFailed && succeeded.m_boneMatrices && succeeded.m_textureMatrices);
    Free(succeeded.m_boneMatrices);
    Free(succeeded.m_textureMatrices);
    assert(live == 0);
}
