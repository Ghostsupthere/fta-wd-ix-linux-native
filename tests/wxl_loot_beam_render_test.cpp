#include "tests/wxl_loot_beam_com_stubs.h"
#include "Engine/Source/ModernAssets/WxlLootBeam/BeamRenderer.hpp"
#include <cassert>
#include <cstring>
#include <map>
#include <cstdio>
using namespace wxl::scripts::lootbeam;
struct Surface : StrictIDirect3DSurface9 {
    int refs=1;D3DSURFACE_DESC desc{};
    Surface() {desc.Width=800;desc.Height=600;}
    ULONG STDMETHODCALLTYPE AddRef() override {return ++refs;}
    ULONG STDMETHODCALLTYPE Release() override {return --refs;}
    HRESULT STDMETHODCALLTYPE GetDesc(D3DSURFACE_DESC* out) override {*out=desc;return S_OK;}
};
struct Buffer : StrictIDirect3DVertexBuffer9 {
    int refs=1;
    ULONG STDMETHODCALLTYPE AddRef() override {return ++refs;}
    ULONG STDMETHODCALLTYPE Release() override {return --refs;}
};
struct Pipeline {
    std::map<D3DRENDERSTATETYPE,DWORD> render;
    std::map<DWORD,IDirect3DBaseTexture9*> textures;
    std::map<DWORD,DWORD> stages;
    IDirect3DVertexShader9* vs=nullptr;IDirect3DPixelShader9* ps=nullptr;
    DWORD fvf=71,freq=3;
    bool operator==(const Pipeline& b) const {return render==b.render && textures==b.textures && stages==b.stages && vs==b.vs && ps==b.ps && fvf==b.fvf && freq==b.freq;}
};
static int liveBlocks=0;
struct Device;
struct Block : StrictIDirect3DStateBlock9 {
    Device& device;Pipeline captured;
    explicit Block(Device& d):device(d){++liveBlocks;}
    ~Block(){--liveBlocks;}
    ULONG STDMETHODCALLTYPE Release() override {delete this;return 0;}
    HRESULT STDMETHODCALLTYPE Capture() override;
    HRESULT STDMETHODCALLTYPE Apply() override;
};
struct Device : StrictIDirect3DDevice9 {
    Surface colour,depth;Buffer buffer;Pipeline state;
    IDirect3DVertexBuffer9* stream=&buffer;UINT offset=17,stride=44;
    std::map<D3DTRANSFORMSTATETYPE,D3DMATRIX> matrices;
    D3DVIEWPORT9 viewport{2,3,700,500,.1f,.9f};
    bool failCapture=false,failTransform=false,failDraw=false,failSet=false;
    int draws=0,blocks=0,applies=0;
    Device() {state.render[D3DRS_ZWRITEENABLE]=TRUE;state.render[D3DRS_FOGENABLE]=TRUE;
        D3DMATRIX m{};m._11=m._22=m._33=m._44=1;matrices[D3DTS_WORLD]=matrices[D3DTS_VIEW]=matrices[D3DTS_PROJECTION]=m;
        state.textures[0]=reinterpret_cast<IDirect3DBaseTexture9*>(uintptr_t(0x1234));}
    HRESULT STDMETHODCALLTYPE GetRenderTarget(DWORD,IDirect3DSurface9** out) override {*out=&colour;colour.AddRef();return S_OK;}
    HRESULT STDMETHODCALLTYPE GetDepthStencilSurface(IDirect3DSurface9** out) override {*out=&depth;depth.AddRef();return S_OK;}
    HRESULT STDMETHODCALLTYPE CreateStateBlock(D3DSTATEBLOCKTYPE,IDirect3DStateBlock9** out) override {++blocks;*out=new Block(*this);return S_OK;}
    HRESULT STDMETHODCALLTYPE GetStreamSource(UINT,IDirect3DVertexBuffer9** out,UINT* o,UINT* s) override {*out=stream;stream->AddRef();*o=offset;*s=stride;return S_OK;}
    HRESULT STDMETHODCALLTYPE SetStreamSource(UINT,IDirect3DVertexBuffer9* in,UINT o,UINT s) override {stream=in;offset=o;stride=s;return S_OK;}
    HRESULT STDMETHODCALLTYPE GetTransform(D3DTRANSFORMSTATETYPE which,D3DMATRIX* out) override {if(failTransform)return E_FAIL;*out=matrices[which];return S_OK;}
    HRESULT STDMETHODCALLTYPE SetTransform(D3DTRANSFORMSTATETYPE which,const D3DMATRIX* in) override {matrices[which]=*in;return S_OK;}
    HRESULT STDMETHODCALLTYPE GetViewport(D3DVIEWPORT9* out) override {*out=viewport;return S_OK;}
    HRESULT STDMETHODCALLTYPE SetViewport(const D3DVIEWPORT9* in) override {viewport=*in;return S_OK;}
    HRESULT STDMETHODCALLTYPE GetRenderState(D3DRENDERSTATETYPE which,DWORD* out) override {*out=state.render[which];return S_OK;}
    HRESULT STDMETHODCALLTYPE SetRenderState(D3DRENDERSTATETYPE which,DWORD value) override {state.render[which]=value;return failSet && which==D3DRS_CULLMODE?E_FAIL:S_OK;}
    HRESULT STDMETHODCALLTYPE SetTexture(DWORD which,IDirect3DBaseTexture9* value) override {state.textures[which]=value;return S_OK;}
    HRESULT STDMETHODCALLTYPE SetTextureStageState(DWORD stage,D3DTEXTURESTAGESTATETYPE which,DWORD value) override {state.stages[stage*100+which]=value;return S_OK;}
    HRESULT STDMETHODCALLTYPE SetVertexShader(IDirect3DVertexShader9* value) override {state.vs=value;return S_OK;}
    HRESULT STDMETHODCALLTYPE SetPixelShader(IDirect3DPixelShader9* value) override {state.ps=value;return S_OK;}
    HRESULT STDMETHODCALLTYPE SetFVF(DWORD value) override {state.fvf=value;return S_OK;}
    HRESULT STDMETHODCALLTYPE SetStreamSourceFreq(UINT,DWORD value) override {state.freq=value;return S_OK;}
    HRESULT STDMETHODCALLTYPE DrawPrimitiveUP(D3DPRIMITIVETYPE type,UINT count,const void*,UINT bytes) override {
        assert(type==D3DPT_TRIANGLELIST && count==1 && bytes==sizeof(Vertex));
        assert(viewport.MinZ==.2f && viewport.MaxZ==.8f);
        assert(state.render[D3DRS_ZFUNC]==D3DCMP_LESSEQUAL);
        assert(!state.render[D3DRS_ZWRITEENABLE] && state.render[D3DRS_ZENABLE] && !state.render[D3DRS_FOGENABLE]);
        assert(state.render[D3DRS_DESTBLEND]==D3DBLEND_ONE && !state.textures[0]);
        ++draws;stream=nullptr;offset=stride=0;return failDraw?E_FAIL:S_OK;
    }
};
HRESULT Block::Capture() {if(device.failCapture)return E_FAIL;captured=device.state;return S_OK;}
HRESULT Block::Apply() {++device.applies;device.state=captured;return S_OK;}
static void Check(Device& d,bool expected,DrawResult expectedResult=DrawResult::Drawn) {
    const auto before=d.state;const auto viewport=d.viewport;const auto matrices=d.matrices;
    const float view[16]{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    const float projection[16]{1,0,0,0,0,1,0,0,0,0,1,1,0,0,-1,0};
    const Vertex vertices[3]{};const float range[]{.2f,.8f};
    DrawResult result=DrawResult::InvalidInput;
    assert(Draw(&d,vertices,3,view,projection,range,&result)==expected);
    assert(result==expectedResult);
    assert(d.state==before && !std::memcmp(&d.viewport,&viewport,sizeof(viewport)));
    for (const auto& pair:matrices) assert(!std::memcmp(&d.matrices[pair.first],&pair.second,sizeof(D3DMATRIX)));
    assert(d.stream==&d.buffer && d.offset==17 && d.stride==44);
    assert(d.colour.refs==1 && d.depth.refs==1 && d.buffer.refs==1);
}
int main() {
    Device d;Check(d,true);assert(d.draws==1 && d.applies==1);
    d.failDraw=true;Check(d,false,DrawResult::DrawFailed);assert(d.draws==2 && d.applies==2);d.failDraw=false;
    d.failSet=true;Check(d,false,DrawResult::StateFailed);assert(d.draws==2 && d.applies==3);d.failSet=false;
    d.failCapture=true;Check(d,false,DrawResult::CaptureFailed);assert(d.draws==2 && d.applies==3);d.failCapture=false;
    d.failTransform=true;Check(d,false,DrawResult::CaptureFailed);assert(d.draws==2 && d.applies==3);d.failTransform=false;
    d.depth.desc.Width=801;const auto blocks=d.blocks;Check(d,false,DrawResult::DepthMismatch);assert(d.blocks==blocks);
    d.depth.desc.Width=800;d.depth.desc.MultiSampleQuality=1;Check(d,false,DrawResult::DepthMismatch);assert(d.blocks==blocks);
    assert(liveBlocks==0);
    std::puts("loot-beam production renderer state/stream restoration and failure checks passed");
}
