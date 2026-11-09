// Link stubs for tests/wxl_particle_jobs_test.cpp. The test links the real particle emitter code (and Tempest, Storm)
// but not the renderer, the model loader or the file system: those are only reached by drawing and by particle models,
// which the test never touches. Each stub aborts if it is ever called, so a test that wanders into them fails loudly.
// ParticleSystemManager::GetScaler (the density setting) returns 1 and fixed16(float) is the real conversion.
#include <cstdint>
#include <cstdio>
#include <cstdlib>

[[noreturn]] static void Unexpected(const char* name) {
    std::fprintf(stderr, "wxl_particle_jobs_test: unexpected call to %s\n", name);
    std::abort();
}

alignas(16) static char s_fakeDevice[64];
void* g_theGxDevicePtr = s_fakeDevice;  // only its (stubbed) member functions are called

extern "C" void _Z11HandleCloseP9HOBJECT__() { Unexpected("_Z11HandleCloseP9HOBJECT__"); }
extern "C" uint32_t _Z12GxVertexSize21EGxVertexBufferFormat(int format) {  // Engine/Source/Gx/Gx.cpp s_vertexSize
    static const uint32_t sizes[14] = { 12, 24, 28, 32, 36, 40, 44, 16, 24, 32, 20, 28, 48, 48 };
    return sizes[format];
}
extern "C" void _Z15GxPrimVertexPtrP6CGxBuf21EGxVertexBufferFormat() { Unexpected("_Z15GxPrimVertexPtrP6CGxBuf21EGxVertexBufferFormat"); }
extern "C" void _Z15HandleDuplicateP9HOBJECT__() { Unexpected("_Z15HandleDuplicateP9HOBJECT__"); }
extern "C" void* _Z15TextureGetGxTexP8CTextureiP7CStatus(void*, int, void*) { return reinterpret_cast<void*>(1); }  // "loaded"
extern "C" void* _Z16GetModuleHandleAPKc(const char*) { return nullptr; }
extern "C" uint32_t _Z20GxVertexAttribOffset21EGxVertexBufferFormat15EGxVertexAttrib(int format, int attrib) {  // s_vertexAttribOffset
    static const int32_t offsets[14][14] = {
        { 0, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 }, { 0, -1, -1, 12, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
        { 0, -1, -1, 12, 24, -1, -1, -1, -1, -1, -1, -1, -1, -1 }, { 0, -1, -1, 12, -1, -1, 24, -1, -1, -1, -1, -1, -1, -1 },
        { 0, -1, -1, 12, 24, -1, 28, -1, -1, -1, -1, -1, -1, -1 }, { 0, -1, -1, 12, -1, -1, 24, 32, -1, -1, -1, -1, -1, -1 },
        { 0, -1, -1, 12, 24, -1, 28, 36, -1, -1, -1, -1, -1, -1 }, { 0, -1, -1, -1, 12, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
        { 0, -1, -1, -1, 12, -1, 16, -1, -1, -1, -1, -1, -1, -1 }, { 0, -1, -1, -1, 12, -1, 16, 24, -1, -1, -1, -1, -1, -1 },
        { 0, -1, -1, -1, -1, -1, 12, -1, -1, -1, -1, -1, -1, -1 }, { 0, -1, -1, -1, -1, -1, 12, 20, -1, -1, -1, -1, -1, -1 },
        { 0, 12, 16, 20, -1, -1, 32, 40, -1, -1, -1, -1, -1, -1 }, { 0, -1, -1, 12, 24, 28, 32, 40, -1, -1, -1, -1, -1, -1 } };
    return static_cast<uint32_t>(offsets[format][attrib]);
}
extern "C" void _ZN13CShaderEffect17SetDefaultShadersEj() { Unexpected("_ZN13CShaderEffect17SetDefaultShadersEj"); }
extern "C" void _ZN13CShaderEffect18SetTexMtx_IdentityEj() { Unexpected("_ZN13CShaderEffect18SetTexMtx_IdentityEj"); }
extern "C" void _ZN13CShaderEffect21UpdateWorldViewMatrixEv() { Unexpected("_ZN13CShaderEffect21UpdateWorldViewMatrixEv"); }
extern "C" void _ZN21ParticleSystemManager11GetInstanceEv() { Unexpected("_ZN21ParticleSystemManager11GetInstanceEv"); }
extern "C" void _ZN21ParticleSystemManager16DuplicateEmitterEPK17CParticleEmitter2i() { Unexpected("_ZN21ParticleSystemManager16DuplicateEmitterEPK17CParticleEmitter2i"); }
extern "C" float _ZN21ParticleSystemManager9GetScalerEv() { return 1.0f; }
extern "C" void _ZN5SFile10InitializeEv() {}
extern "C" void _ZN5SFile7DestroyEv() {}
extern "C" void _ZN7fixed16C1Ef(int16_t* self, float value) {  // Engine/Source/Model2/M2Data.cpp
    if (value < 1.0f) {
        *self = value > -1.0f ? static_cast<int16_t>(value * 32767.0f + 0.5f) : static_cast<int16_t>(-32767);
    } else {
        *self = 0x7FFF;
    }
}
extern "C" void _ZN8CM2Model11WaitForLoadEPKc() { Unexpected("_ZN8CM2Model11WaitForLoadEPKc"); }
extern "C" void _ZN8CM2Model17GetBoundingSphereEv() { Unexpected("_ZN8CM2Model17GetBoundingSphereEv"); }
extern "C" void _ZN8CM2Model17SetLoadedCallbackEPFvPS_PvES1_() { Unexpected("_ZN8CM2Model17SetLoadedCallbackEPFvPS_PvES1_"); }
extern "C" void _ZN8CM2Model7ReleaseEv() { Unexpected("_ZN8CM2Model7ReleaseEv"); }
extern "C" void _ZN8CM2Model8IsLoadedEii() { Unexpected("_ZN8CM2Model8IsLoadedEii"); }
extern "C" void _ZN8CM2Scene11CreateModelEPKcj() { Unexpected("_ZN8CM2Scene11CreateModelEPKcj"); }
extern "C" void _ZN9CGxDevice10BufDestroyERP6CGxBuf() { Unexpected("_ZN9CGxDevice10BufDestroyERP6CGxBuf"); }
extern "C" void _ZN9CGxDevice10PoolCreateE13EGxPoolTarget12EGxPoolUsagei15EGxPoolHintBitsPKc() { Unexpected("_ZN9CGxDevice10PoolCreateE13EGxPoolTarget12EGxPoolUsagei15EGxPoolHintBitsPKc"); }
extern "C" void _ZN9CGxDevice12PrimIndexPtrEP6CGxBuf() { Unexpected("_ZN9CGxDevice12PrimIndexPtrEP6CGxBuf"); }
alignas(16) char g_testCaps[512];  // CGxCaps, all zero: m_colorFormat (offset 0x14) 0 = argb; a test can set 1 for rgba
extern "C" void* _ZN9CGxDevice4CapsEv() { return g_testCaps; }
extern "C" void _ZN9CGxDevice9BufCreateEP7CGxPooljjj() { Unexpected("_ZN9CGxDevice9BufCreateEP7CGxPooljjj"); }
extern "C" void _ZN9CGxDevice9BufStreamE13EGxPoolTargetjj() { Unexpected("_ZN9CGxDevice9BufStreamE13EGxPoolTargetjj"); }

// Storm's allocator, replaced so that every new byte is 0xA5 instead of whatever the heap held. The engine's
// SMemAlloc does not zero memory, so a field a constructor or CreateParticle leaves unset reads as heap garbage there;
// with a fixed pattern two emitters start identical and the test can compare them bit for bit. (Particle arrays grow
// through SMemReAlloc, so the grown part is filled too.)
#include <malloc.h>
#include <cstring>
#include <new>

void* SMemAlloc(unsigned long bytes, const char*, int, unsigned int) {
    void* p = std::malloc(bytes ? bytes : 1);
    if (p) std::memset(p, 0xA5, malloc_usable_size(p));
    return p;
}

int SMemFree(void* p, const char*, int, unsigned int) {
    std::free(p);
    return 1;
}

void* SMemReAlloc(void* p, unsigned long bytes, const char* file, int line, unsigned int flags) {
    if (!p) return SMemAlloc(bytes, file, line, flags);
    const size_t old = malloc_usable_size(p);
    void* q = std::realloc(p, bytes ? bytes : 1);
    if (q) {
        const size_t now = malloc_usable_size(q);
        if (now > old) std::memset(static_cast<char*>(q) + old, 0xA5, now - old);
    }
    return q;
}

int SMemIsValidPointer(void*, unsigned long, unsigned int) { return 1; }
void SMemDestroy() {}
void SMemInitialize() {}
void SMemSetDebugFlags(unsigned int, unsigned char) {}
void* operator new(size_t n) { void* p = SMemAlloc(n, nullptr, 0, 0); if (!p) throw std::bad_alloc(); return p; }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
