#define WOWDECOMP_GX_DXVK
#include "NativePlatform.h"
#include "msaa_depth.h"
#include "fullscreen_triangle.h"
#include "log.h"
#include "ps_depth_check.h"
#include "vs_fullscreen.h"
#include <climits>
#include <cmath>
#include <cstring>
namespace {
constexpr D3DFORMAT kResz=static_cast<D3DFORMAT>(MAKEFOURCC('R','E','S','Z'));
constexpr DWORD kReszCopyCode=0x7FA05000;
constexpr float kFirstTestDepth=.3125f,kSecondTestDepth=.75f,kTestTolerance=1.f/65535.f,kClearedDepth=1.f;
constexpr double kQueryWaitSeconds=.5;
constexpr DWORD kMaxRenderTargets=4,kAllColourChannels=0xF;
int g_forcedMethod=kDepthCopyMethodFromDriver;
template <typename T>
void SafeRelease(T*& p)
{
    if (p)
    {
        p->Release();
        p = nullptr;
    }
}

const D3DRENDERSTATETYPE kReszStates[] = {D3DRS_ZENABLE, D3DRS_ZWRITEENABLE, D3DRS_COLORWRITEENABLE, D3DRS_POINTSIZE};
constexpr int kReszStateCount = sizeof(kReszStates) / sizeof(kReszStates[0]);

struct ReszSavedState
{
    IDirect3DBaseTexture9* texture = nullptr;
    IDirect3DVertexShader9* vertexShader = nullptr;
    IDirect3DPixelShader9* pixelShader = nullptr;
    IDirect3DVertexDeclaration9* declaration = nullptr;
    DWORD fvf = 0;
    IDirect3DVertexBuffer9* stream = nullptr;
    UINT streamOffset = 0;
    UINT streamStride = 0;
    DWORD states[kReszStateCount] = {};
};

void SaveReszState(IDirect3DDevice9* dev, ReszSavedState& saved)
{
    dev->GetTexture(0, &saved.texture);
    dev->GetVertexShader(&saved.vertexShader);
    dev->GetPixelShader(&saved.pixelShader);
    dev->GetVertexDeclaration(&saved.declaration);
    dev->GetFVF(&saved.fvf);
    dev->GetStreamSource(0, &saved.stream, &saved.streamOffset, &saved.streamStride);
    for (int i = 0; i < kReszStateCount; ++i)
        dev->GetRenderState(kReszStates[i], &saved.states[i]);
}

void RestoreReszState(IDirect3DDevice9* dev, ReszSavedState& saved)
{
    for (int i = 0; i < kReszStateCount; ++i)
        dev->SetRenderState(kReszStates[i], saved.states[i]);
    dev->SetTexture(0, saved.texture);
    dev->SetVertexShader(saved.vertexShader);
    dev->SetPixelShader(saved.pixelShader);
    dev->SetVertexDeclaration(saved.declaration);
    if (saved.fvf)
        dev->SetFVF(saved.fvf);
    dev->SetStreamSource(0, saved.stream, saved.streamOffset, saved.streamStride);
    SafeRelease(saved.texture);
    SafeRelease(saved.vertexShader);
    SafeRelease(saved.pixelShader);
    SafeRelease(saved.declaration);
    SafeRelease(saved.stream);
}

bool ResolveThroughResz(IDirect3DDevice9* dev, IDirect3DTexture9* destination)
{
    // Native DXVK resolves directly from the bound depth surface. Windows'
    // historical dummy point draw is unnecessary and would compile an unused
    // fixed-function pipeline, touch stencil and disturb vertex-stream state.
    IDirect3DBaseTexture9* savedTexture=nullptr;DWORD savedPointSize=0;
    if(FAILED(dev->GetTexture(0,&savedTexture))||FAILED(dev->GetRenderState(D3DRS_POINTSIZE,&savedPointSize))) {
        SafeRelease(savedTexture);return false;
    }
    const bool copied=SUCCEEDED(dev->SetTexture(0,destination))&&SUCCEEDED(dev->SetRenderState(D3DRS_POINTSIZE,kReszCopyCode));
    dev->SetRenderState(D3DRS_POINTSIZE,savedPointSize);dev->SetTexture(0,savedTexture);SafeRelease(savedTexture);
    return copied;
}

struct DepthReadback
{
    IDirect3DVertexShader9* vertexShader = nullptr;
    IDirect3DPixelShader9* pixelShader = nullptr;
    IDirect3DVertexDeclaration9* declaration = nullptr;
    IDirect3DTexture9* target = nullptr;
    IDirect3DSurface9* targetSurface = nullptr;
    IDirect3DSurface9* systemCopy = nullptr;

    DepthReadback() = default;
    DepthReadback(const DepthReadback&) = delete;
    DepthReadback& operator=(const DepthReadback&) = delete;
    ~DepthReadback()
    {
        SafeRelease(systemCopy);
        SafeRelease(targetSurface);
        SafeRelease(target);
        SafeRelease(declaration);
        SafeRelease(pixelShader);
        SafeRelease(vertexShader);
    }
};

bool CreateDepthReadback(IDirect3DDevice9* dev, UINT texels, DepthReadback& r)
{
    static const D3DVERTEXELEMENT9 kElements[] = {
        {0, 0, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
        D3DDECL_END(),
    };
    return SUCCEEDED(dev->CreateVertexShader(reinterpret_cast<const DWORD*>(g_vs_fullscreen), &r.vertexShader)) &&
           SUCCEEDED(dev->CreatePixelShader(reinterpret_cast<const DWORD*>(g_ps_depth_check), &r.pixelShader)) &&
           SUCCEEDED(dev->CreateVertexDeclaration(kElements, &r.declaration)) &&
           SUCCEEDED(dev->CreateTexture(texels, 1, 1, D3DUSAGE_RENDERTARGET, D3DFMT_R32F, D3DPOOL_DEFAULT, &r.target,
                                        nullptr)) &&
           SUCCEEDED(r.target->GetSurfaceLevel(0, &r.targetSurface)) &&
           SUCCEEDED(dev->CreateOffscreenPlainSurface(texels, 1, D3DFMT_R32F, D3DPOOL_SYSTEMMEM, &r.systemCopy,
                                                      nullptr));
}

struct SavedDeviceState
{
    IDirect3DStateBlock9* state = nullptr;
    IDirect3DSurface9* targets[kMaxRenderTargets] = {};
    IDirect3DSurface9* depth = nullptr;
    IDirect3DVertexBuffer9* stream = nullptr;
    UINT streamOffset = 0;
    UINT streamStride = 0;
};

bool SaveDeviceState(IDirect3DDevice9* dev, SavedDeviceState& saved)
{
    if (FAILED(dev->CreateStateBlock(D3DSBT_ALL, &saved.state)) || !saved.state)
        return false;
    for (DWORD i = 0; i < kMaxRenderTargets; ++i)
        dev->GetRenderTarget(i, &saved.targets[i]);
    dev->GetDepthStencilSurface(&saved.depth);
    dev->GetStreamSource(0, &saved.stream, &saved.streamOffset, &saved.streamStride);
    return true;
}

void RestoreDeviceState(IDirect3DDevice9* dev, SavedDeviceState& saved)
{
    if (saved.targets[0])
        dev->SetRenderTarget(0, saved.targets[0]);
    for (DWORD i = 1; i < kMaxRenderTargets; ++i)
        dev->SetRenderTarget(i, saved.targets[i]);
    dev->SetDepthStencilSurface(saved.depth);
    saved.state->Apply();
    dev->SetStreamSource(0, saved.stream, saved.streamOffset, saved.streamStride);
    for (IDirect3DSurface9*& target : saved.targets)
        SafeRelease(target);
    SafeRelease(saved.depth);
    SafeRelease(saved.stream);
    SafeRelease(saved.state);
}

void SetReadbackState(IDirect3DDevice9* dev, const DepthReadback& r, IDirect3DTexture9* depth)
{
    dev->SetRenderTarget(0, r.targetSurface);
    for (DWORD i = 1; i < kMaxRenderTargets; ++i)
        dev->SetRenderTarget(i, nullptr);
    dev->SetDepthStencilSurface(nullptr);
    dev->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
    dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    dev->SetRenderState(D3DRS_STENCILENABLE, FALSE);
    dev->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    dev->SetRenderState(D3DRS_COLORWRITEENABLE, kAllColourChannels);
    dev->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
    dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    dev->SetRenderState(D3DRS_FOGENABLE, FALSE);
    dev->SetRenderState(D3DRS_CLIPPLANEENABLE, 0);
    dev->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);
    dev->SetVertexShader(r.vertexShader);
    dev->SetPixelShader(r.pixelShader);
    dev->SetVertexDeclaration(r.declaration);
    dev->SetStreamSourceFreq(0, 1);
    dev->SetTexture(0, depth);
    dev->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    dev->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
    dev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
    dev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
    dev->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
    dev->SetSamplerState(0, D3DSAMP_MAXMIPLEVEL, 0);
    dev->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, FALSE);
}

void DrawDepthTexels(IDirect3DDevice9* dev, const D3DSURFACE_DESC& desc, const DepthTexel* texels, int count)
{
    for (int i = 0; i < count; ++i)
    {
        const D3DVIEWPORT9 texel = {static_cast<DWORD>(i), 0, 1, 1, 0.0f, 1.0f};
        const float uv[4] = {(texels[i].x + 0.5f) / desc.Width, (texels[i].y + 0.5f) / desc.Height, 0.0f, 0.0f};
        dev->SetViewport(&texel);
        dev->SetPixelShaderConstantF(0, uv, 1);
        DrawFullscreenTriangle(dev);
    }
}

bool CopyOutTexels(const DepthReadback& r, int count, float* values)
{
    D3DLOCKED_RECT locked = {};
    if (FAILED(r.systemCopy->LockRect(&locked, nullptr, D3DLOCK_READONLY)))
        return false;
    const auto* texels = static_cast<const float*>(locked.pBits);
    for (int i = 0; i < count; ++i)
        values[i] = texels[i];
    r.systemCopy->UnlockRect();
    return true;
}

double Seconds()
{
    static const double frequency = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return static_cast<double>(f.QuadPart);
    }();
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return static_cast<double>(now.QuadPart) / frequency;
}

template <typename T>
bool WaitForQuery(IDirect3DQuery9* query, T& value, double deadline)
{
    HRESULT result = S_FALSE;
    while ((result = query->GetData(&value, sizeof(value), D3DGETDATA_FLUSH)) == S_FALSE && Seconds() < deadline)
    {
    }
    return result == S_OK;
}

struct CopyTimer
{
    IDirect3DQuery9* disjoint = nullptr;
    IDirect3DQuery9* frequency = nullptr;
    IDirect3DQuery9* start = nullptr;
    IDirect3DQuery9* end = nullptr;

    CopyTimer() = default;
    CopyTimer(const CopyTimer&) = delete;
    CopyTimer& operator=(const CopyTimer&) = delete;
    ~CopyTimer()
    {
        SafeRelease(end);
        SafeRelease(start);
        SafeRelease(frequency);
        SafeRelease(disjoint);
    }
};

float TimedCopy(IDirect3DDevice9* dev, const DepthCopy& copy, bool& copied)
{
    CopyTimer timer;
    const bool timed = SUCCEEDED(dev->CreateQuery(D3DQUERYTYPE_TIMESTAMPDISJOINT, &timer.disjoint)) &&
                       SUCCEEDED(dev->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ, &timer.frequency)) &&
                       SUCCEEDED(dev->CreateQuery(D3DQUERYTYPE_TIMESTAMP, &timer.start)) &&
                       SUCCEEDED(dev->CreateQuery(D3DQUERYTYPE_TIMESTAMP, &timer.end));
    if (!timed)
    {
        copied = copy.Copy(dev);
        return -1.0f;
    }
    timer.disjoint->Issue(D3DISSUE_BEGIN);
    timer.frequency->Issue(D3DISSUE_END);
    timer.start->Issue(D3DISSUE_END);
    copied = copy.Copy(dev);
    timer.end->Issue(D3DISSUE_END);
    timer.disjoint->Issue(D3DISSUE_END);
    const double deadline = Seconds() + kQueryWaitSeconds;
    BOOL disjoint = TRUE;
    UINT64 ticksPerSecond = 0;
    UINT64 begin = 0;
    UINT64 finish = 0;
    const bool read = WaitForQuery(timer.disjoint, disjoint, deadline) &&
                      WaitForQuery(timer.frequency, ticksPerSecond, deadline) &&
                      WaitForQuery(timer.start, begin, deadline) && WaitForQuery(timer.end, finish, deadline);
    if (!read || disjoint || !ticksPerSecond || finish < begin)
        return -1.0f;
    return static_cast<float>(static_cast<double>(finish - begin) * 1000.0 / static_cast<double>(ticksPerSecond));
}

void ClearDepthHalves(IDirect3DDevice9* dev, const D3DSURFACE_DESC& desc, float left, float right)
{
    const LONG middle = static_cast<LONG>(desc.Width / 2);
    const D3DRECT leftHalf = {0, 0, middle, static_cast<LONG>(desc.Height)};
    const D3DRECT rightHalf = {middle, 0, static_cast<LONG>(desc.Width), static_cast<LONG>(desc.Height)};
    dev->Clear(1, &leftHalf, D3DCLEAR_ZBUFFER, 0, left, 0);
    dev->Clear(1, &rightHalf, D3DCLEAR_ZBUFFER, 0, right, 0);
}

bool BindTestTargets(IDirect3DDevice9* dev, IDirect3DSurface9* source, const D3DSURFACE_DESC& desc)
{
    IDirect3DSurface9* backBuffer = nullptr;
    if (FAILED(dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backBuffer)))
        return false;
    dev->SetRenderTarget(0, backBuffer);
    backBuffer->Release();
    for (DWORD i = 1; i < kMaxRenderTargets; ++i)
        dev->SetRenderTarget(i, nullptr);
    dev->SetDepthStencilSurface(source);
    const D3DVIEWPORT9 whole = {0, 0, desc.Width, desc.Height, 0.0f, 1.0f};
    dev->SetViewport(&whole);
    dev->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    return true;
}

bool HasStencil(D3DFORMAT format)
{
    return format == D3DFMT_D15S1 || format == D3DFMT_D24S8 || format == D3DFMT_D24X4S4 || format == D3DFMT_D24FS8;
}

DWORD DepthClearFlags(D3DFORMAT format)
{
    return HasStencil(format) ? D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL : D3DCLEAR_ZBUFFER;
}

bool RunTestRounds(IDirect3DDevice9* dev, const DepthCopy& copy, IDirect3DSurface9* source,
                   IDirect3DTexture9* destination, const D3DSURFACE_DESC& desc, DepthCopyTest& test)
{
    const DepthTexel texels[2] = {{desc.Width / 4, desc.Height / 2}, {desc.Width * 3 / 4, desc.Height / 2}};
    const float rounds[2][2] = {{kFirstTestDepth, kSecondTestDepth}, {kSecondTestDepth, kFirstTestDepth}};
    bool passed = true;
    for (int round = 0; round < 2; ++round)
    {
        if (!BindTestTargets(dev, source, desc))
            return false;
        ClearDepthHalves(dev, desc, rounds[round][0], rounds[round][1]);
        bool copied = false;
        if (round == 0)
            test.copyMilliseconds = TimedCopy(dev, copy, copied);
        else
            copied = copy.Copy(dev);
        const bool read = copied && ReadDepthTexels(dev, destination, texels, 2, test.read);
        for (int i = 0; i < 2; ++i)
        {
            test.expected[i] = rounds[round][i];
            passed = passed && read && std::fabs(test.read[i] - rounds[round][i]) <= kTestTolerance;
        }
        if (!passed)
            return false;
    }
    BindTestTargets(dev, source, desc);
    dev->Clear(0, nullptr, DepthClearFlags(desc.Format), 0, kClearedDepth, 0);
    return true;
}

bool DepthCopyMethodForced()
{
    return g_forcedMethod != kDepthCopyMethodFromDriver;
}

DepthCopyProbe ForcedProbe()
{
    const auto forced = static_cast<DepthCopyMethod>(g_forcedMethod);
    return {forced, forced == DepthCopyMethod::None ? "no depth copy method (forced by the harness)" : ""};
}

DepthCopyProbe ProbeDriver(IDirect3D9* d3d, UINT adapter, D3DDEVTYPE type)
{
    D3DDISPLAYMODE mode = {};
    const D3DFORMAT adapterFormat =
        SUCCEEDED(d3d->GetAdapterDisplayMode(adapter, &mode)) ? mode.Format : D3DFMT_X8R8G8B8;
    if (SUCCEEDED(d3d->CheckDeviceFormat(adapter, type, adapterFormat, D3DUSAGE_RENDERTARGET, D3DRTYPE_SURFACE,
                                         kResz)))
        return {DepthCopyMethod::Resz, ""};
    return {DepthCopyMethod::None,"native DXVK RESZ depth copy unavailable"};
}
}

bool NativeNvapiFactory(IUnknown*) { return false; }

const char* DepthCopyMethodName(DepthCopyMethod method)
{
    switch (method)
    {
    case DepthCopyMethod::Nvapi:
        return "NVAPI";
    case DepthCopyMethod::Resz:
        return "RESZ";
    default:
        return "none";
    }
}

void ForceDepthCopyMethod(int method)
{
    g_forcedMethod = method;
}

bool SameSampleCount(const D3DSURFACE_DESC& target, const D3DSURFACE_DESC& depth)
{
    return target.MultiSampleType == depth.MultiSampleType && target.MultiSampleQuality == depth.MultiSampleQuality;
}

DepthCopyProbe ProbeDepthCopy(IDirect3D9* d3d, UINT adapter, D3DDEVTYPE type)
{
    return DepthCopyMethodForced() ? ForcedProbe() : ProbeDriver(d3d, adapter, type);
}

DepthCopyProbe DepthCopyProbes::Probe(IDirect3D9* d3d, UINT adapter, D3DDEVTYPE type)
{
    if (DepthCopyMethodForced())
        return ForcedProbe();
    for (int i = 0; i < m_count; ++i)
        if (m_probed[i].adapter == adapter && m_probed[i].type == type)
            return m_probed[i].probe;
    const DepthCopyProbe probe = ProbeDriver(d3d, adapter, type);
    if (m_count < kMaxProbed)
        m_probed[m_count++] = {adapter, type, probe};
    return probe;
}

DepthCopy::~DepthCopy()
{
    Detach();
}

bool DepthCopy::Attach(DepthCopyMethod method, IDirect3DSurface9* source, IDirect3DTexture9* destination)
{
    Detach();
    if (method == DepthCopyMethod::None || !source || !destination)
        return false;
    if (method == DepthCopyMethod::Nvapi) return false;
    source->AddRef();
    destination->AddRef();
    m_method = method;
    m_source = source;
    m_destination = destination;
    return true;
}

void DepthCopy::Detach()
{
    m_method = DepthCopyMethod::None;
    SafeRelease(m_source);
    SafeRelease(m_destination);
}

bool DepthCopy::Copy(IDirect3DDevice9* dev) const
{
    switch (m_method)
    {
    case DepthCopyMethod::Nvapi:
        return false;
    case DepthCopyMethod::Resz:
        return ResolveThroughResz(dev, m_destination);
    default:
        return false;
    }
}

bool ReadDepthTexels(IDirect3DDevice9* dev, IDirect3DTexture9* depth, const DepthTexel* texels, int count,
                     float* values)
{
    D3DSURFACE_DESC desc = {};
    DepthReadback r;
    if (!dev || !depth || count <= 0 || FAILED(depth->GetLevelDesc(0, &desc)) ||
        !CreateDepthReadback(dev, static_cast<UINT>(count), r))
        return false;
    SavedDeviceState saved;
    if (!SaveDeviceState(dev, saved))
        return false;
    const bool sceneOpened = SUCCEEDED(dev->BeginScene());
    SetReadbackState(dev, r, depth);
    DrawDepthTexels(dev, desc, texels, count);
    if (sceneOpened)
        dev->EndScene();
    RestoreDeviceState(dev, saved);
    return SUCCEEDED(dev->GetRenderTargetData(r.targetSurface, r.systemCopy)) && CopyOutTexels(r, count, values);
}

DepthCopyTest TestDepthCopy(IDirect3DDevice9* dev, const DepthCopy& copy, IDirect3DSurface9* source,
                            IDirect3DTexture9* destination)
{
    DepthCopyTest test;
    D3DSURFACE_DESC desc = {};
    SavedDeviceState saved;
    if (!dev || !source || !destination || FAILED(source->GetDesc(&desc)) || desc.Width < 4 ||
        !SaveDeviceState(dev, saved))
        return test;
    const bool sceneOpened = SUCCEEDED(dev->BeginScene());
    test.passed = RunTestRounds(dev, copy, source, destination, desc, test);
    if (sceneOpened)
        dev->EndScene();
    RestoreDeviceState(dev, saved);
    return test;
}
