// Native adaptation of supplied wxl-loot-beam; GPL-3.0-or-later.
#pragma once
#include "LootBeamPolicy.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>
namespace wxl::scripts::lootbeam {
struct Corpse { uint64_t guid=0; float position[3]{}; float distanceSquared=0; uint32_t hint=0; };
struct Track { Corpse corpse; float fade=0; bool seen=false; };
struct Scene {
    float camera[3]{}, view[16]{}, projection[16]{};
    std::array<Corpse,kMaximumBeams> candidates{};
    unsigned count=0, visited=0;
};
// CSimpleCamera uses eye=(0,0,0): geometry elsewhere in the native world
// is camera-relative. Procedural beams retain world coordinates, so include
// that missing translation in the view, leaving its rotation unchanged.
inline void RebaseWorldView(Scene& scene) {
    for (unsigned c=0;c<4;++c)
        scene.view[12+c]-=scene.camera[0]*scene.view[c]+scene.camera[1]*scene.view[4+c]+scene.camera[2]*scene.view[8+c];
}
inline bool FiniteScene(const Scene& scene) {
    for (float v:scene.camera) if (!std::isfinite(v)) return false;
    for (float v:scene.view) if (!std::isfinite(v)) return false;
    for (float v:scene.projection) if (!std::isfinite(v)) return false;
    return std::abs(scene.projection[5])>0.001f && std::abs(scene.projection[11])>0.001f;
}
inline void Consider(Scene& scene, Corpse corpse) {
    if (!corpse.guid || !HasRemainingLoot(corpse.hint)) return;
    float d=0;
    for (unsigned i=0;i<3;++i) { if (!std::isfinite(corpse.position[i])) return; const float v=corpse.position[i]-scene.camera[i]; d+=v*v; }
    if (!std::isfinite(d) || d>kMaximumDistance*kMaximumDistance) return;
    corpse.distanceSquared=d;
    for (unsigned i=0;i<scene.count;++i) if (scene.candidates[i].guid==corpse.guid) return;
    if (scene.count<kMaximumBeams) scene.candidates[scene.count++]=corpse;
    else {
        auto farthest=std::max_element(scene.candidates.begin(),scene.candidates.end(),[](const Corpse& a,const Corpse& b){return a.distanceSquared<b.distanceSquared;});
        if (d<farthest->distanceSquared) *farthest=corpse;
    }
}
        inline uint32_t Colour(uint32_t rgb, float alpha, float whiten = 0)
        {
            auto channel = [whiten](uint32_t input)
            {
                return uint32_t(float(input) * (1 - whiten) + 255.0f * whiten);
            };
            return (uint32_t(std::clamp(alpha, 0.0f, 1.0f)*255) << 24) |
                (channel((rgb >> 16) & 255) << 16) | (channel((rgb >> 8) & 255) << 8) | channel(rgb & 255);
        }

        template<class Vertex>
        inline void Quad(std::vector<Vertex>& vertices, Vertex const& a, Vertex const& b, Vertex const& c, Vertex const& d)
        {
            vertices.insert(vertices.end(), {a, b, c, a, c, d});
        }

        // Soft horizontal shoulders and tapered vertical falloff are procedural; no MPQ assets are required.
        template<class Vertex>
        inline void Shaft(std::vector<Vertex>& vertices, Track const& track, Scene const& scene, double seconds)
        {
            float const dx = scene.camera[0] - track.corpse.position[0];
            float const dy = scene.camera[1] - track.corpse.position[1];
            float const length = std::sqrt(dx*dx + dy*dy);
            float const sx = length > 0.001f ? -dy / length : 1.0f;
            float const sy = length > 0.001f ? dx / length : 0.0f;
            uint32_t const rgb = TierColour(track.corpse.hint);
            float const distance = std::sqrt(track.corpse.distanceSquared);
            float const fade = track.fade * std::clamp((kMaximumDistance - distance) / 25.0f, 0.0f, 1.0f);
            float const phase = float(std::sin(seconds * 2.1 + double(track.corpse.guid & 255)));
            float const pulse = 0.88f + 0.12f*phase;
            constexpr float horizontal[] = {-1, -0.65f, -0.25f, 0, 0.25f, 0.65f, 1};
            constexpr float strength[] = {0, 0.10f, 0.45f, 0.70f, 0.45f, 0.10f, 0};
            constexpr float vertical[] = {0, 0.04f, 0.15f, 0.35f, 0.65f, 1};
            constexpr float intensity[] = {0, 1, 0.8f, 0.45f, 0.15f, 0};
            auto vertex = [&](unsigned x, unsigned y)
            {
                float const lateral = horizontal[x] * 0.8f;
                return Vertex{track.corpse.position[0] + sx*lateral, track.corpse.position[1] + sy*lateral,
                    track.corpse.position[2] + 0.25f + vertical[y]*18.0f,
                    Colour(rgb, fade*pulse*strength[x]*intensity[y], x == 3 ? 0.4f : 0.0f)};
            };
            for (unsigned y = 0; y < 5; ++y)
                for (unsigned x = 0; x < 6; ++x)
                    Quad(vertices, vertex(x,y), vertex(x+1,y), vertex(x+1,y+1), vertex(x,y+1));

            // Eight deterministic motes per corpse. No unbounded emitter allocations or particle lifetimes.
            for (unsigned mote = 0; mote < 8; ++mote)
            {
                double const seed = double((track.corpse.guid & 1023) + mote*127);
                float const life = float(std::fmod(seconds * 0.17 + seed * 0.037, 1.0));
                float const envelope = std::sin(life * 3.14159265f);
                float const x = track.corpse.position[0] + float(std::sin(seconds + seed))*0.45f;
                float const y = track.corpse.position[1] + float(std::cos(seconds*0.8 + seed))*0.45f;
                float const z = track.corpse.position[2] + 0.5f + life*7.0f;
                float const size = 0.06f + 0.035f*envelope;
                Vertex center{x,y,z,Colour(rgb, fade*envelope*0.8f, 0.65f)};
                for (unsigned side = 0; side < 8; ++side)
                {
                    auto rim = [&](unsigned index)
                    {
                        float const angle = float(index)*0.78539816f;
                        float const right = std::cos(angle)*size, up = std::sin(angle)*size;
                        return Vertex{x + scene.view[0]*right + scene.view[1]*up,
                            y + scene.view[4]*right + scene.view[5]*up,
                            z + scene.view[8]*right + scene.view[9]*up, Colour(rgb, 0)};
                    };
                    vertices.insert(vertices.end(), {center, rim(side), rim(side+1)});
                }
            }
        }


// Diagnostic only: distinguish behind-camera/out-of-frustum geometry from
// eligible geometry submitted to the world draw. Uses native row-vector math.
template<class Vertex> inline std::array<unsigned,2> ClipCounts(const Scene& scene,const std::vector<Vertex>& vertices) {
    std::array<unsigned,2> result{};
    for (const auto& v:vertices) {
        const float point[]{v.x,v.y,v.z,1};float view[4]{},clip[4]{};
        for (unsigned c=0;c<4;++c) for (unsigned r=0;r<4;++r) view[c]+=point[r]*scene.view[r*4+c];
        for (unsigned c=0;c<4;++c) for (unsigned r=0;r<4;++r) clip[c]+=view[r]*scene.projection[r*4+c];
        if (!(clip[3]>0) || !std::isfinite(clip[3])) continue;
        ++result[0];
        if (std::abs(clip[0])<=clip[3] && std::abs(clip[1])<=clip[3] && clip[2]>=0 && clip[2]<=clip[3]) ++result[1];
    }
    return result;
}
class State {
public:
    std::array<Track,kMaximumBeams> tracks{};
    uint64_t lastScan=0,lastFrame=0,lastSceneTime=0;
    bool scanned=false,framed=false,hasScene=false;
    Scene lastScene{};
    void Clear() { *this=State{}; }
    bool NeedScan(uint64_t now) const { return !scanned || now<lastScan || now-lastScan>=100; }
    template<class Vertex> void Update(Scene scene,bool fresh,bool scanComplete,uint64_t now,std::vector<Vertex>& vertices) {
        vertices.clear();
        scene.count=std::min(scene.count,kMaximumBeams);
        fresh=fresh && FiniteScene(scene);
        if (fresh) { lastScene=scene;lastSceneTime=now;hasScene=true; }
        else {
            if (!hasScene || now<lastSceneTime || now-lastSceneTime>200) return;
            scene=lastScene;
        }
        const float dt=framed && now>=lastFrame ? std::min(float(now-lastFrame)*0.001f,0.1f) : 0.016f;
        lastFrame=now;framed=true;
        if (fresh && scanComplete && NeedScan(now)) {
            lastScan=now;scanned=true;
            for (auto& track:tracks) track.seen=false;
            for (unsigned i=0;i<scene.count;++i)
                for (auto& track:tracks) if (track.corpse.guid==scene.candidates[i].guid) { track.corpse=scene.candidates[i];track.seen=true;break; }
            for (unsigned i=0;i<scene.count;++i) {
                auto existing=std::find_if(tracks.begin(),tracks.end(),[&](const Track& t){return t.corpse.guid==scene.candidates[i].guid;});
                if (existing!=tracks.end()) continue;
                auto free=std::find_if(tracks.begin(),tracks.end(),[](const Track& t){return !t.seen;});
                if (free!=tracks.end()) *free={scene.candidates[i],0,true};
            }
        }
        vertices.reserve(kMaximumBeams*372);
        for (auto& track:tracks) {
            if (!track.corpse.guid) continue;
            track.fade=std::clamp(track.fade+(track.seen?dt*4:-dt*6),0.0f,1.0f);
            if (!track.seen && track.fade==0) {track={};continue;}
            const float dx=track.corpse.position[0]-scene.camera[0],dy=track.corpse.position[1]-scene.camera[1],dz=track.corpse.position[2]-scene.camera[2];
            track.corpse.distanceSquared=dx*dx+dy*dy+dz*dz;
            if (track.corpse.distanceSquared<kMaximumDistance*kMaximumDistance) Shaft(vertices,track,scene,double(now)/1000);
        }
    }
};
}
