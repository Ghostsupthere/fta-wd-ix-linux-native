// Production scene adapter with a mounted-file API and native layout stubs.
#include "Engine/Source/ModernAssets/NativeItemPhysics.h"
#include "Engine/Source/ModernAssets/ModernPhysics.h"
#include "WoW/Common/ComponentCore/WxlEquipmentOwner.h"
#include "Engine/Source/Model2/WxlSceneModels.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstring>
#include <map>
#include <memory>
#include <unordered_set>
#include <cstdio>
#include <new>

struct C44Matrix {
    float values[16]{};
    C44Matrix() { for (uint32_t i=0; i<4; ++i) values[i*5]=1; }
};
struct Bone {
    struct { float x=0,y=0,z=0; } m_pivot;
    uint16_t m_parentIndex=0xFFFF;
    uint32_t m_boneNameCRC=1;
    int32_t m_boneId=-1;
};
using M2CompBone = Bone;
struct M2Data {
    struct {
        uint32_t m_count=1; Bone bone;
        const Bone& operator[](uint32_t i) const { assert(!i); return bone; }
        const Bone* Data() const { return &bone; }
    } m_bones;
    struct { uint32_t m_count=0; const uint16_t* Data() const { return nullptr; } } m_boneIndicesById;
};
struct Shared { M2Data* m_data; const char* m_fileName; };
class CM2Scene;
struct WxlModelBoneCache;
class CM2Model {
public:
    Shared* m_shared = nullptr;
    C44Matrix* m_boneMatrices = nullptr;
    C44Matrix m_matrixF4;
    CM2Scene* m_scene = nullptr;
    CM2Model *m_attachParent=nullptr, *m_attachList=nullptr, *m_attachNext=nullptr,
        *m_animateNext=nullptr, *m_drawNext=nullptr;
    uint32_t m_attachmentId=0xFFFFFFFF, m_fFlags=1;
    WxlItemPhysicsState* m_wxlItemPhysics=nullptr;
    WxlModelBoneCache* m_wxlBoneRemap=nullptr;
    void RefreshWxlCollectionPalette();
    ~CM2Model();
};
class CM2Scene {
public:
    CM2Model *m_animateList=nullptr, *m_drawList=nullptr;
    C44Matrix m_viewInv;
    uint32_t m_time=0;
};
static uint32_t now=0, reads=0;
static bool enabled=true, belts=true, resolverReady=false;
static std::map<std::string,std::vector<uint8_t>> files;
struct SFileImpl { const std::vector<uint8_t>* bytes; };
struct SFile {
    static bool OpenEx(void*, const char* path, int, SFileImpl** output) {
        ++reads;
        auto found=files.find(path);
        if (found==files.end()) return false;
        *output=new SFileImpl{&found->second}; return true;
    }
};
static void SFileCloseFile(SFileImpl* file) { delete file; }
static uint32_t SFileGetFileSize(SFileImpl* file,uint32_t* high) { *high=0; return file->bytes->size(); }
static bool SFileReadFile(SFileImpl* file,void* out,uint32_t size,uint32_t* got,void*,int) {
    assert(size==file->bytes->size()); std::memcpy(out,file->bytes->data(),size); *got=size; return true;
}
static uint32_t OsGetAsyncTimeMs() { return now; }
static bool WxlProfileEnabled(const char* key,bool) { return std::strstr(key,"BELTS") ? belts : enabled; }
static void WxlNativeDiagnostic(const char*,...) {}
static bool ResolveModernAssetFileDataId(uint32_t id,std::string& path) {
    assert(id==77); path="resolved.phys"; return resolverReady;
}
#include "Engine/Source/ModernAssets/NativeItemPhysics.inl"
#include "Engine/Source/Model2/WxlCollectionPalette.inl"
CM2Model::~CM2Model() { RetireNativeItemPhysics(this); delete m_wxlBoneRemap; }

template<class T> static void Put(std::vector<uint8_t>& bytes,size_t at,T value) { std::memcpy(bytes.data()+at,&value,sizeof(value)); }
static void Chunk(std::vector<uint8_t>& bytes,const char* tag,std::vector<uint8_t> payload,bool reverse=true) {
    auto at=bytes.size(); bytes.resize(at+8+payload.size());
    for (uint32_t i=0; i<4; ++i) bytes[at+i]=tag[reverse ? 3-i : i];
    Put(bytes,at+4,uint32_t(payload.size())); std::copy(payload.begin(),payload.end(),bytes.begin()+at+8);
}
static std::vector<uint8_t> Phys() {
    std::vector<uint8_t> bytes,body(28),shape(32),sphere(16);
    Put(body,0,uint16_t(1)); Put(body,24,int32_t(1));
    Put(shape,0,uint16_t(2)); Put(shape,8,.6f); Put(shape,16,1.f); Put(sphere,12,.1f);
    Chunk(bytes,"PHYS",std::vector<uint8_t>(2)); Chunk(bytes,"BODY",body);
    Chunk(bytes,"SHP2",shape); Chunk(bytes,"SPHS",sphere); return bytes;
}
struct Fixture {
    M2Data data;
    Shared shared{&data,"Item\\ObjectComponents\\Waist\\Belt.m2"};
    C44Matrix palette;
    CM2Scene scene;
    CM2Model model;
    Fixture() {
        model.m_shared=&shared; model.m_boneMatrices=&palette; model.m_scene=&scene;
        scene.m_animateList=scene.m_drawList=&model;
    }
    void Tick(uint32_t elapsed=16) {
        RestoreNativeItemPhysics(&scene);
        assert(palette.values[14]==0); // Last simulation never feeds native animation.
        now+=elapsed; scene.m_time+=elapsed;
        ApplyNativeItemPhysics(&scene);
    }
};
int main() {
    {
        Fixture parent, child;
        child.model.m_attachParent=&parent.model;
        child.model.m_attachmentId=0xF000;
        parent.palette.values[14]=7;
        child.model.RefreshWxlCollectionPalette();
        assert(child.palette.values[14]==7 && child.model.m_wxlBoneRemap);
        auto* cache=child.model.m_wxlBoneRemap;
        parent.palette.values[14]=8;
        child.model.RefreshWxlCollectionPalette();
        assert(child.palette.values[14]==8 && child.model.m_wxlBoneRemap==cache);
        child.model.m_attachmentId=WxlComponentAttachment(0,11);
        parent.palette.values[14]=9;
        child.model.RefreshWxlCollectionPalette(); assert(child.palette.values[14]==8);
        child.model.m_attachmentId=WxlCustomAttachment(0,11);
        M2Data replacement;
        parent.shared.m_data=&replacement;
        child.model.RefreshWxlCollectionPalette();
        assert(child.palette.values[14]==9 && child.model.m_wxlBoneRemap->parent==&replacement);
    }
    {
        CM2Scene scene;
        CM2Model root, child, grandchild;
        root.m_scene=child.m_scene=grandchild.m_scene=&scene;
        root.m_attachList=&child; child.m_attachParent=&root;
        child.m_attachList=&grandchild; grandchild.m_attachParent=&child;
        scene.m_animateList=&child; scene.m_drawList=&grandchild;
        std::vector<CM2Model*> order;
        WxlVisitSceneModels(&scene,[&](CM2Model* model) { order.push_back(model); });
        assert((order==std::vector<CM2Model*>{&root,&child,&grandchild}));
        root.m_attachParent=&grandchild; order.clear();
        WxlVisitSceneModels(&scene,[&](CM2Model* model) { order.push_back(model); });
        assert(order.empty()); // A corrupt ancestry cycle cannot loop the frame.
    }
    const char* companion="item\\objectcomponents\\waist\\belt.phys";
    files[companion]=Phys();
    {
        Fixture f;
        for (uint32_t i=0; i<61; ++i) f.Tick();
        assert(f.palette.values[14]<-.1f && f.model.m_wxlItemPhysics->nativePose[14]==0);
        // Camera motion changes native placement; world position stays invariant.
        RestoreNativeItemPhysics(&f.scene);
        f.palette.values[12]=f.model.m_matrixF4.values[12]=-100;
        f.scene.m_viewInv.values[12]=100;
        now+=16; f.scene.m_time+=16; ApplyNativeItemPhysics(&f.scene);
        double world[16]; domino::World(f.palette.values,f.scene.m_viewInv.values,world);
        assert(std::abs(world[3])<1.e-5 && world[11]<-.1);
        RestoreNativeItemPhysics(&f.scene);
        WxlInvalidateWorldEquipment();
        f.Tick(); assert(std::abs(f.palette.values[14])<1.e-5);
        belts=false; f.Tick(); belts=true;
        assert(!f.model.m_wxlItemPhysics);
        f.Tick(); assert(f.model.m_wxlItemPhysics);
        RetireNativeItemPhysics(&f.model);
        assert(!f.model.m_wxlItemPhysics && f.palette.values[14]==0);
    }
    {
        Fixture f; f.model.m_fFlags|=0x1000;
        f.Tick(); assert(!(f.model.m_fFlags&0x1000) && !f.model.m_wxlItemPhysics->applied);
        f.Tick(); assert(f.model.m_wxlItemPhysics->applied);
    }
    {
        Fixture f; f.Tick();
        RestoreNativeItemPhysics(&f.scene); now+=16; ApplyNativeItemPhysics(&f.scene);
        assert(f.palette.values[14]==0); // A recently paused native clock stays paused.
        RestoreNativeItemPhysics(&f.scene); now+=100; ApplyNativeItemPhysics(&f.scene);
        assert(f.palette.values[14]<0 && f.model.m_wxlItemPhysics->idleAdvanced);
        RestoreNativeItemPhysics(&f.scene); now+=16; f.scene.m_time+=116; ApplyNativeItemPhysics(&f.scene);
        assert(!f.model.m_wxlItemPhysics->idleAdvanced);
        f.Tick(2500); assert(std::abs(f.palette.values[14])<1.e-5);
        f.Tick(); assert(f.palette.values[14]<0); // Idle retirement resets once, then resumes.
    }
    files[companion]={1,2,3};
    {
        Fixture f; f.Tick();
        assert(!f.model.m_wxlItemPhysics->solver && f.palette.values[14]==0);
        const auto previous=reads; f.Tick(); assert(reads==previous);
    }
    files.clear();
    std::vector<uint8_t> md21,id(4); Put(id,0,uint32_t(77));
    Chunk(md21,"MD21",std::vector<uint8_t>(8),false); Chunk(md21,"PFID",id,false);
    files["item\\objectcomponents\\waist\\belt.m2"]=md21;
    {
        Fixture f; f.Tick();
        assert(f.model.m_wxlItemPhysics->loaded==ModernPhysicsLoadResult::PendingFileDataId);
        const auto previous=reads; f.Tick(500); assert(reads==previous);
        resolverReady=true; files["resolved.phys"]=Phys(); f.Tick(500);
        assert(f.model.m_wxlItemPhysics->solver);
    }
}
