#include "Engine/Source/ModernAssets/WxlLootBeam/LootBeamScene.hpp"
#include <cassert>
#include <limits>
#include <cstdio>
using namespace wxl::scripts::lootbeam;
struct Vertex {float x,y,z;uint32_t colour;};
static Scene Camera() {
    Scene s;for (unsigned i=0;i<4;++i) s.view[i*4+i]=s.projection[i*4+i]=1;
    s.projection[11]=1;s.projection[15]=0;return s;
}
static Corpse Body(uint64_t guid,float x,uint32_t hint=5) {Corpse c;c.guid=guid;c.position[0]=x;c.hint=hint;return c;}
int main() {
    assert(Lootable(8,0,1) && !Lootable(24,0,1) && !Lootable(8,1,1) && !Lootable(8,0,0));
    assert(!HasRemainingLoot(0) && !HasRemainingLoot(10) && HasRemainingLoot(9));
    assert(TierColour(0)==0xD8D8D8 && TierColour(5)==0xA335EE && TierColour(9)==0xFFCC40);
    Scene scene=Camera();assert(FiniteScene(scene));
    for (uint64_t i=100;i>0;--i) Consider(scene,Body(i,float(i)));
    assert(scene.count==32);
    for (const auto& c:scene.candidates) assert(c.guid>=1 && c.guid<=32);
    Consider(scene,Body(1,1));assert(scene.count==32);
    Consider(scene,Body(999,0,0));Consider(scene,Body(0,0));Consider(scene,Body(999,151));
    Consider(scene,Body(999,std::numeric_limits<float>::quiet_NaN()));assert(scene.count==32);
    std::vector<Vertex> clipVertices{{0,0,.5f,0},{2,0,.5f,0}};
    auto clipScene=Camera();clipScene.projection[11]=0;clipScene.projection[15]=1;
    const auto clip=ClipCounts(clipScene,clipVertices);assert(clip[0]==2 && clip[1]==1);
    // Native camera view has no eye translation. Northshire-sized world
    // coordinates must project the same as equivalent camera-local geometry.
    auto world=Camera();
    const float rotation[]{1,0,0,0,0,0,1,0,0,1,0,0,0,0,0,1};
    std::copy(rotation,rotation+16,world.view);
    const float perspective[]{1,0,0,0,0,1,0,0,0,0,100.f/99.9f,1,0,0,-10.f/99.9f,0};
    std::copy(perspective,perspective+16,world.projection);
    world.camera[0]=-8944;world.camera[1]=-134;world.camera[2]=88;
    std::vector<Vertex> point{{-8944,-124,88,0}};
    assert(ClipCounts(world,point)[1]==0); // Reproduces the original world-space mismatch.
    RebaseWorldView(world);
    assert(ClipCounts(world,point)[1]==1 && world.view[0]==1 && world.view[9]==1);
    State state;std::vector<Vertex> vertices;
    state.Update(scene,true,true,0,vertices);
    assert(state.scanned && !state.NeedScan(99) && state.NeedScan(100));
    assert(vertices.size()==32*372 && vertices.size()%3==0);
    for (const auto& v:vertices) assert(std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z));
    auto first=std::find_if(state.tracks.begin(),state.tracks.end(),[](const Track& t){return t.corpse.guid==1;});
    assert(first!=state.tracks.end());const auto fade=first->fade;
    // New corpse arrives first in scan order; retained tracks keep their fade.
    Scene changed=Camera();Consider(changed,Body(500,.5f));
    for (uint64_t i=1;i<=31;++i) Consider(changed,Body(i,float(i),2));
    state.Update(changed,true,true,100,vertices);
    first=std::find_if(state.tracks.begin(),state.tracks.end(),[](const Track& t){return t.corpse.guid==1;});
    assert(first!=state.tracks.end() && first->fade>fade && first->corpse.hint==2);
    assert(std::find_if(state.tracks.begin(),state.tracks.end(),[](const Track& t){return t.corpse.guid==500;})!=state.tracks.end());
    // Failed scans preserve tracks; completed empty scans fade them out.
    Scene empty=Camera();state.Update(empty,true,false,200,vertices);assert(!vertices.empty());
    Scene broken=Camera();broken.projection[5]=std::numeric_limits<float>::quiet_NaN();
    state.Update(broken,true,false,400,vertices);assert(!vertices.empty()); // Cached camera up to 200ms.
    state.Update(broken,false,false,401,vertices);assert(vertices.empty());
    state.Update(empty,true,true,500,vertices);
    for (uint64_t now=600;now<=1500;now+=100) state.Update(empty,true,true,now,vertices);
    assert(vertices.empty());for (const auto& t:state.tracks) assert(!t.corpse.guid);
    // Fresh hints override old quality, including zero with cached lootability.
    Scene one=Camera();Consider(one,Body(7,0,6));state.Update(one,true,true,1600,vertices);
    assert(!vertices.empty());one=Camera();Consider(one,Body(7,0,1));state.Update(one,true,true,1700,vertices);
    assert(state.tracks[0].corpse.hint==1);
    one=Camera();Consider(one,Body(7,0,0));assert(one.count==0);state.Update(one,true,true,1800,vertices);
    state.Update(one,true,true,1900,vertices);assert(vertices.empty());
    // Scene expiry and a backwards clock cannot reuse a future camera.
    state.Update(scene,true,true,2000,vertices);assert(!vertices.empty());
    state.Update(empty,false,false,1999,vertices);assert(vertices.empty());
    state.Clear();assert(!state.hasScene && !state.scanned && !state.framed);
    state.Update(empty,false,false,2100,vertices);assert(vertices.empty());
    std::puts("loot-beam policy, nearest selection, bounded geometry, fade/quality and lifecycle checks passed");
}
