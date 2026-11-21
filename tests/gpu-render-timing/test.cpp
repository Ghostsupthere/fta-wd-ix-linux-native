#include "Engine/Source/Gx/CGxDeviceD3d9Ex/GpuRenderTimer.h"

#include <cassert>
#include <cstring>

struct FakeDevice : IDirect3DDevice9 {
    UINT64 ticks = 100;
    UINT64 frequency = 1000;
    bool ready = false;
    BOOL disjoint = false;
    bool failCreate = false;
    bool failIssue = false;
    uint32_t creates = 0;
    uint32_t live = 0;
    uint32_t reads = 0;
    uint32_t issues = 0;

    HRESULT CreateQuery(D3DQUERYTYPE type, IDirect3DQuery9** output) override;
};

struct FakeQuery : IDirect3DQuery9 {
    FakeDevice& device;
    D3DQUERYTYPE type;
    UINT64 value = 0;
    DWORD issued = 0;

    FakeQuery(FakeDevice& device, D3DQUERYTYPE type) : device(device), type(type) {}

    HRESULT Issue(DWORD flags) override {
        ++device.issues;
        if (device.failIssue) return -1;

        if (flags == D3DISSUE_BEGIN) {
            assert(type == D3DQUERYTYPE_TIMESTAMPDISJOINT);
        } else {
            assert(flags == D3DISSUE_END);
            if (type == D3DQUERYTYPE_TIMESTAMPDISJOINT) assert(issued == D3DISSUE_BEGIN);
            value = type == D3DQUERYTYPE_TIMESTAMP ? device.ticks : device.frequency;
        }
        issued = flags;
        return D3D_OK;
    }

    HRESULT GetData(void* data, DWORD size, DWORD flags) override {
        assert(flags == 0 && issued == D3DISSUE_END);
        ++device.reads;
        if (!device.ready) return S_FALSE;
        if (type == D3DQUERYTYPE_TIMESTAMPDISJOINT) {
            assert(size == sizeof(device.disjoint));
            std::memcpy(data, &device.disjoint, size);
        } else {
            assert(size == sizeof(value));
            std::memcpy(data, &value, size);
        }
        return D3D_OK;
    }

    void Release() override {
        --device.live;
        delete this;
    }
};

HRESULT FakeDevice::CreateQuery(D3DQUERYTYPE type, IDirect3DQuery9** output) {
    ++creates;
    if (failCreate && creates % 2 == 0) return -1;
    *output = new FakeQuery(*this, type);
    ++live;
    return D3D_OK;
}

static void RenderFrame(GpuRenderTimer& timer, FakeDevice& device) {
    timer.Begin(&device);
    Profiler::EndFrame();
    device.ticks += 3;
    timer.End();
}

int main() {
    FakeDevice device;
    GpuRenderTimer timer;
    Profiler::Summary summary;

    RenderFrame(timer, device);
    Profiler::EndFrame();
    assert(Profiler::Summarize(10, summary) == 1 && !summary.gpuRenderSamples);
    timer.Poll();
    assert(device.reads == 1);
    device.ready = true;
    timer.Poll();
    assert(Profiler::Summarize(10, summary) == 1);
    assert(summary.gpuRenderSamples == 1 && summary.gpuRenderMs == 3.0);

    Profiler::EndFrame();
    assert(Profiler::Summarize(1, summary) == 1 && !summary.gpuRenderSamples);
    assert(Profiler::Summarize(2, summary) == 2 && summary.gpuRenderSamples == 1);

    timer.Reset();
    assert(!device.live);
    device.disjoint = true;
    RenderFrame(timer, device);
    Profiler::EndFrame();
    timer.Poll();
    assert(Profiler::Summarize(1, summary) == 1 && !summary.gpuRenderSamples);

    timer.Reset();
    device.disjoint = false;
    RenderFrame(timer, device);
    Profiler::EndFrame();
    Profiler::Reset();
    Profiler::EndFrame();
    Profiler::EndFrame();
    timer.Poll();
    assert(Profiler::Summarize(1, summary) == 1 && !summary.gpuRenderSamples);

    timer.Reset();
    device.ready = false;
    const auto issues = device.issues;
    for (uint32_t i = 0; i < 40; ++i) RenderFrame(timer, device);
    assert(device.issues - issues == 1 + 8 * 4);
    device.ready = true;
    Profiler::EndFrame();
    timer.Poll();
    assert(Profiler::Summarize(100, summary) && summary.gpuRenderSamples == 8);

    timer.Reset();
    RenderFrame(timer, device);
    for (uint32_t i = 0; i < 241; ++i) Profiler::EndFrame();
    timer.Poll();
    assert(Profiler::Summarize(240, summary) == 240 && !summary.gpuRenderSamples);

    timer.Reset();
    device.creates = 0;
    device.failCreate = true;
    for (uint32_t i = 0; i < 9; ++i) RenderFrame(timer, device);
    assert(!device.live && device.creates == 2);

    timer.Reset();
    device.failCreate = false;
    device.failIssue = true;
    RenderFrame(timer, device);
    assert(!device.live);
}
