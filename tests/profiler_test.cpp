#include "Engine/Source/Base/Profiler.h"
#include "Engine/Source/Tempest/C3Vector.h"
#include "Engine/Source/Tempest/C44Matrix.h"
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <thread>

static void Sleep(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

int main() {
    Profiler::Summary s;
    assert(Profiler::Summarize(10, s) == 0);

    // The first EndFrame only starts the clock; frames are recorded from the second one.
    Profiler::EndFrame();
    for (int frame = 0; frame < 5; ++frame) {
        {
            Profiler::Scope outer(Profiler::WorldRender);
            Sleep(4);
            {
                Profiler::Scope inner(Profiler::M2Animate);
                Sleep(6);
            }
            {
                Profiler::Scope inner(Profiler::M2Animate);
                Sleep(2);
            }
        }
        Profiler::CountDraw(100);
        Profiler::CountDraw(50);
        Profiler::EndFrame();
    }
    assert(Profiler::Summarize(100, s) == 5);
    assert(s.frames == 5);
    const auto& outer = s.zones[Profiler::WorldRender];
    const auto& inner = s.zones[Profiler::M2Animate];
    assert(inner.calls == 2.0);
    assert(inner.totalMs >= 7.5 && inner.totalMs < 14.0);
    assert(std::fabs(inner.selfMs - inner.totalMs) < 0.01);            // no nested zones inside
    assert(outer.totalMs >= outer.selfMs + inner.totalMs - 0.1);      // total includes the child
    assert(outer.selfMs >= 3.5 && outer.selfMs < 8.0);                 // the 4 ms outside the children
    assert(s.drawCalls == 2.0 && s.triangles == 150.0);
    assert(s.frameMs >= 11.5 && s.fps > 0.0 && s.fps < 90.0);
    assert(s.zones[Profiler::Terrain].calls == 0.0);

    // Averages cover only the most recent frames asked for.
    assert(Profiler::Summarize(2, s) == 2);

    // A scope on another thread records nothing.
    std::thread other([] { Profiler::Scope zone(Profiler::Terrain); Sleep(3); Profiler::CountDraw(999); });
    other.join();
    Profiler::EndFrame();
    assert(Profiler::Summarize(1, s) == 1 && s.zones[Profiler::Terrain].calls == 0.0 && s.triangles == 0.0);

    // Disabling clears the recording and stops new data.
    Profiler::SetEnabled(false);
    assert(Profiler::Summarize(10, s) == 0);
    { Profiler::Scope zone(Profiler::Terrain); }
    Profiler::EndFrame();
    assert(Profiler::Summarize(10, s) == 0);
    Profiler::SetEnabled(true);
    Profiler::Reset();
    assert(Profiler::Summarize(10, s) == 0);
    if (Profiler::M2DiagnosticsEnabled()) {
        using Type = Profiler::M2DrawType;
        Profiler::EndFrame();
        {
            Profiler::M2DrawScope doodad(Type::Doodad);
            Profiler::CountDraw(20);
            {
                Profiler::M2DrawScope particle(Type::Particle);
                Profiler::CountDraw(4);
            }
            Profiler::CountDraw(24);
        }
        Profiler::CountDraw(10);
        Profiler::EndFrame();
        assert(Profiler::Summarize(1, s) == 1 && s.drawCalls == 4.0 && s.triangles == 58.0);
        assert(s.m2DrawsByType[static_cast<uint32_t>(Type::Doodad)] == 2.0);
        assert(s.m2DrawsByType[static_cast<uint32_t>(Type::Particle)] == 1.0);
        Profiler::EndFrame();
        assert(Profiler::Summarize(2, s) == 2);
        assert(s.m2DrawsByType[static_cast<uint32_t>(Type::Doodad)] == 1.0);
    }
    Profiler::Reset();
    assert(Profiler::UiDetailZone(Profiler::UiTextures, Profiler::UiRender) == Profiler::Count);
    Profiler::EndFrame();
    {
        Profiler::Scope render(Profiler::UiRender);
        Profiler::Scope callbacks(Profiler::UiDetailZone(Profiler::UiCallbacks, Profiler::UiRender));
        Profiler::Scope textures(Profiler::UiDetailZone(Profiler::UiTextures, Profiler::UiRender));
        assert(Profiler::UiDetailZone(Profiler::UiLayout, Profiler::UiUpdate) == Profiler::Count);
        Profiler::CountDraw(2);
    }
    Profiler::EndFrame();
    assert(Profiler::Summarize(1, s) == 1 && s.zones[Profiler::UiRender].calls == 1.0);
    if (Profiler::UiDetailEnabled()) {
        assert(s.zones[Profiler::UiCallbacks].calls == 1.0);
        assert(s.zones[Profiler::UiTextures].selfDraws == 1.0);
        assert(s.zones[Profiler::UiRender].selfDraws == 0.0);
    } else {
        assert(s.zones[Profiler::UiCallbacks].calls == 0.0);
        assert(s.zones[Profiler::UiTextures].selfDraws == 0.0);
        assert(s.zones[Profiler::UiRender].selfDraws == 1.0);
    }
    {
        const double detailCalls = Profiler::UiDetailEnabled() ? 1.0 : 0.0;
        Profiler::Reset();
        Profiler::EndFrame();
        {
            Profiler::Scope render(Profiler::UiRender);
            Profiler::Scope callbacks(Profiler::UiDetailZone(Profiler::UiCallbacks, Profiler::UiRender));
            {
                Profiler::Scope world(Profiler::UiDetailZone(Profiler::UiCallbackWorld, Profiler::UiCallbacks));
                Profiler::CountDraw(3);
                {
                    Profiler::Scope update(Profiler::UiDetailZone(Profiler::WorldFrameUpdate, Profiler::UiCallbackWorld));
                    {
                        Profiler::Scope camera(Profiler::UiDetailZone(Profiler::WorldUpdateCamera, Profiler::WorldFrameUpdate));
                        assert(Profiler::UiDetailZone(Profiler::CameraUpdateCollision, Profiler::CameraUpdateTarget)
                            == Profiler::Count);
                        Profiler::Scope target(Profiler::UiDetailZone(Profiler::CameraUpdateTarget, Profiler::WorldUpdateCamera));
                        Profiler::Scope collision(Profiler::UiDetailZone(Profiler::CameraUpdateCollision, Profiler::CameraUpdateTarget));
                        Profiler::CountDraw(5);
                    }
                    assert(Profiler::UiDetailZone(Profiler::WorldUpdateMapPreparation, Profiler::WorldUpdateMap)
                        == Profiler::Count);
                    Profiler::Scope map(Profiler::UiDetailZone(Profiler::WorldUpdateMap, Profiler::WorldFrameUpdate));
                    {
                        Profiler::Scope prepare(Profiler::UiDetailZone(Profiler::WorldUpdateMapPreparation, Profiler::WorldUpdateMap));
                        assert(Profiler::UiDetailZone(Profiler::MapPrepareAreaTraversal, Profiler::MapPrepareAreas)
                            == Profiler::Count);
                        {
                            Profiler::Scope areas(Profiler::UiDetailZone(Profiler::MapPrepareAreas, Profiler::WorldUpdateMapPreparation));
                            Profiler::Scope traversal(Profiler::UiDetailZone(Profiler::MapPrepareAreaTraversal, Profiler::MapPrepareAreas));
                            Profiler::CountDraw(6);
                        }
                        Profiler::CountDraw(7);
                    }
                    Profiler::CountDraw(9);
                }
                {
                    Profiler::Scope names(Profiler::UiDetailZone(Profiler::PlayerNameUpdate, Profiler::UiCallbackWorld));
                }
                {
                    Profiler::Scope worldRender(Profiler::WorldRender);
                    Profiler::CountDraw(100);
                }
                {
                    Profiler::Scope text(Profiler::UiDetailZone(Profiler::WorldTextRender, Profiler::UiCallbackWorld));
                    Profiler::CountDraw(8);
                }
            }
            {
                Profiler::Scope scroll(Profiler::UiDetailZone(Profiler::UiCallbackScroll, Profiler::UiCallbacks));
                Profiler::CountDraw(2);
                assert(Profiler::UiDetailZone(Profiler::UiCallbacks, Profiler::UiRender) == Profiler::Count);
                Profiler::Scope portrait(Profiler::UiDetailZone(Profiler::UiCallbackUnitModel, Profiler::UiCallbacks));
                Profiler::CountDraw(4);
            }
        }
        Profiler::EndFrame();
        assert(Profiler::Summarize(1, s) == 1 && s.zones[Profiler::UiCallbacks].calls == detailCalls);
        assert(s.zones[Profiler::UiCallbacks].selfDraws == 0.0);
        assert(s.zones[Profiler::UiCallbackWorld].calls == detailCalls);
        assert(s.zones[Profiler::UiCallbackWorld].selfDraws == detailCalls);
        assert(s.zones[Profiler::WorldFrameUpdate].calls == detailCalls);
        assert(s.zones[Profiler::WorldFrameUpdate].selfDraws == 0.0);
        assert(s.zones[Profiler::WorldUpdateMap].calls == detailCalls);
        assert(s.zones[Profiler::WorldUpdateMap].selfDraws == detailCalls);
        assert(s.zones[Profiler::WorldUpdateMapPreparation].calls == detailCalls);
        assert(s.zones[Profiler::WorldUpdateMapPreparation].selfDraws == detailCalls);
        assert(s.zones[Profiler::WorldUpdateMap].totalMs >= s.zones[Profiler::WorldUpdateMapPreparation].totalMs);
        assert(s.zones[Profiler::MapPrepareAreas].calls == detailCalls);
        assert(s.zones[Profiler::MapPrepareAreaTraversal].calls == detailCalls);
        assert(s.zones[Profiler::MapPrepareAreaTraversal].selfDraws == detailCalls);
        assert(s.zones[Profiler::MapPrepareAreas].totalMs >= s.zones[Profiler::MapPrepareAreaTraversal].totalMs);
        assert(s.zones[Profiler::CameraUpdateTarget].calls == detailCalls);
        assert(s.zones[Profiler::CameraUpdateCollision].calls == detailCalls);
        assert(s.zones[Profiler::CameraUpdateCollision].selfDraws == detailCalls);
        assert(s.zones[Profiler::CameraUpdateTarget].totalMs >= s.zones[Profiler::CameraUpdateCollision].totalMs);
        assert(s.zones[Profiler::PlayerNameUpdate].calls == detailCalls);
        assert(s.zones[Profiler::WorldTextRender].calls == detailCalls);
        assert(s.zones[Profiler::WorldTextRender].selfDraws == detailCalls);
        assert(s.zones[Profiler::UiRender].selfDraws == (detailCalls ? 0.0 : 8.0));
        assert(s.zones[Profiler::WorldRender].selfDraws == 1.0);
        assert(s.zones[Profiler::UiCallbackScroll].selfDraws == detailCalls);
        assert(s.zones[Profiler::UiCallbackUnitModel].selfDraws == detailCalls);
        assert(s.drawCalls == 9.0 && s.triangles == 144.0);
    }
    Profiler::Reset();
    const bool workEnabled = Profiler::WorkZone(Profiler::MovementIdle) == Profiler::MovementIdle;
    const bool spikesEnabled = Profiler::SpikeZone(Profiler::Terrain) == Profiler::Terrain;
    Profiler::EndFrame();
    {
        Profiler::Scope idle(Profiler::WorkZone(Profiler::MovementIdle));
        Profiler::Scope move(Profiler::WorkZone(Profiler::MoveUnitUpdates));
        Profiler::MovementWorkScope work;
        auto* counters = Profiler::MovementWorkScope::Active();
        assert((counters != nullptr) == workEnabled);
        if (counters) {
            counters->units = 4;
            counters->changed = 2;
            counters->relinks = 3;
            counters->positionChanged = 2;
            counters->facingChanged = 1;
            counters->pitchChanged = 1;
            counters->rayTriangleTests = 56;
            counters->linkTriangleTests = 41;
            counters->linkBspFaces = 60;
            counters->candidateHit = 6;
            counters->candidateMiss = 4;
            counters->candidateDeferred = 2;
            counters->candidateUnsupportedRay = 8;
            counters->candidateSmallLeaf = 10;
            counters->ObserveDisplacement(0.0, true);
            counters->ObserveDisplacement(0.002 * 0.002, false);
            counters->ObserveDisplacement(0.01 * 0.01, false);
            counters->ObserveDisplacement(0.05 * 0.05, true);
            counters->ObserveDisplacement(-1.0, false);
            Profiler::MovementWorkScope nested;
            assert(nested.Active() == counters);
            ++counters->relinks;
        }
        // A worker cannot create a movement collector on the render owner's behalf.
        std::thread worker([] {
            Profiler::MovementWorkScope work;
            assert(!work.Active());
        });
        worker.join();
    }
    assert(!Profiler::MovementWorkScope::Active());
    assert(Profiler::ProbeChildZone(Profiler::TerrainD3dSubmit, Profiler::Terrain) == Profiler::Count);
    {
        Profiler::Scope terrain(Profiler::Terrain);
        Profiler::Scope chunk(Profiler::SpikeZone(Profiler::TerrainChunkSinglePass));
        Profiler::Scope submit(Profiler::ProbeChildZone(Profiler::TerrainD3dSubmit, Profiler::Terrain));
        Profiler::CountDraw(3);
    }
    Profiler::EndFrame();
    Profiler::EndFrame();
    assert(Profiler::Summarize(2, s) == 2);
    assert(s.movement.units == (workEnabled ? 2.0 : 0.0));
    assert(s.movement.changed == (workEnabled ? 1.0 : 0.0));
    assert(s.movement.relinks == (workEnabled ? 2.0 : 0.0));
    assert(s.movement.positionChanged == (workEnabled ? 1.0 : 0.0));
    assert(s.movement.facingChanged == (workEnabled ? 0.5 : 0.0));
    assert(s.movement.pitchChanged == (workEnabled ? 0.5 : 0.0));
    assert(s.movement.rayTriangleTests == (workEnabled ? 28.0 : 0.0));
    assert(s.movement.linkTriangleTests == (workEnabled ? 20.5 : 0.0));
    assert(s.movement.linkBspFaces == (workEnabled ? 30.0 : 0.0));
    assert(s.movement.candidateHit == (workEnabled ? 3.0 : 0.0));
    assert(s.movement.candidateMiss == (workEnabled ? 2.0 : 0.0));
    assert(s.movement.candidateDeferred == (workEnabled ? 1.0 : 0.0));
    assert(s.movement.candidateUnsupportedRay == (workEnabled ? 4.0 : 0.0));
    assert(s.movement.candidateSmallLeaf == (workEnabled ? 5.0 : 0.0));
    for (double n : s.movement.displacement) assert(n == (workEnabled ? 0.5 : 0.0));
    for (uint32_t i = 0; i < Profiler::MovementDisplacementBins; ++i) {
        assert(s.movement.relinkDisplacement[i] == (workEnabled && (i == 0 || i == 3) ? 0.5 : 0.0));
    }
    assert(s.zones[Profiler::MoveUnitUpdates].calls == (workEnabled ? 0.5 : 0.0));
    assert(s.zones[Profiler::TerrainD3dSubmit].calls == (spikesEnabled ? 0.5 : 0.0));
    assert(s.zones[Profiler::TerrainD3dSubmit].selfDraws == (spikesEnabled ? 0.5 : 0.0));
    assert(s.zones[Profiler::Terrain].selfDraws == (spikesEnabled ? 0.0 : 0.5));
    const auto optIn = [](const char* name) {
        const char* value = std::getenv(name);
        return value && std::strcmp(value, "1") == 0;
    };
    const char* interval = std::getenv("WXL_PROFILE_LOG");
    const bool periodic = interval && std::atof(interval) > 0;
    const bool animation = periodic && optIn("WXL_PROFILE_M2_ANIMATION");
    const bool cull = periodic && optIn("WXL_PROFILE_CULL_STABILITY");
    const bool buffers = periodic && optIn("WXL_PROFILE_BUFFER_WRITES");
    const bool batching = periodic && optIn("WXL_PROFILE_M2_BATCHING");
    const bool night = workEnabled && optIn("WXL_PROFILE_NIGHT_LIGHTS");
    using Stage = Profiler::M2AnimationStage;
    const auto parallel = static_cast<uint32_t>(Stage::Parallel);
    const auto serial = static_cast<uint32_t>(Stage::Serial);
    const int model = 1, otherModel = 2;
    const C3Vector position;
    const C44Matrix view, projection;
    C44Matrix changedProjection = projection;
    changedProjection.a0 += 1.0f;

    Profiler::Reset();
    Profiler::EndFrame();
    Profiler::ObserveCullCamera(0, position, view, projection);
    Profiler::ObserveCullCamera(0, position, view, changedProjection); // secondary view
    Profiler::CountM2Animation(&model, Stage::Parallel);
    std::thread animationWorker([&] {
        Profiler::CountM2Animation(&model, Stage::Parallel);
        Profiler::CountM2Animation(&otherModel, Stage::Parallel);
    });
    animationWorker.join();
    Profiler::CountM2Animation(&model, Stage::Serial);
    {
        Profiler::Scope lock(Profiler::BufferLockZone());
        Profiler::CountD3dBufferLock(512);
    }
    {
        Profiler::Scope lamps(Profiler::NightLightsZone(Profiler::StaticLampScan));
    }
    if (Profiler::M2BatchProbeEnabled()) {
        Profiler::CountM2BatchOpportunity(Profiler::M2BatchOpportunity::TransformOnly);
    }
    Profiler::EndFrame();
    assert(Profiler::Summarize(1, s) == 1);
    assert(s.cullCameraSamples == (cull ? 1.0 : 0.0) && s.cullCameraComparisons == 0.0);
    assert(s.m2AnimationCalls[parallel] == (animation ? 3.0 : 0.0));
    assert(s.m2RepeatedModels[parallel] == (animation ? 1.0 : 0.0));
    assert(s.m2MaxRepeats[parallel] == (animation ? 1 : 0));
    assert(s.m2AnimationCalls[serial] == (animation ? 1.0 : 0.0));
    assert(s.m2MaxRepeats[serial] == 0);
    assert(s.m2AnimationDropped == 0.0);
    assert(s.d3dBufferLocks == (buffers ? 1.0 : 0.0));
    assert(s.d3dBufferMappedBytes == (buffers ? 512.0 : 0.0));
    assert(s.zones[Profiler::D3dBufferLock].calls == (buffers || spikesEnabled ? 1.0 : 0.0));
    assert(s.zones[Profiler::StaticLampScan].calls == (night ? 1.0 : 0.0));
    assert(s.m2BatchCounts[static_cast<uint32_t>(Profiler::M2BatchOpportunity::TransformOnly)] == (batching ? 1.0 : 0.0));

    Profiler::ObserveCullCamera(0, position, view, projection);
    Profiler::CountM2Animation(&model, Stage::Parallel);
    Profiler::EndFrame();
    assert(Profiler::Summarize(1, s) == 1);
    assert(s.cullCameraUnchanged == (cull ? 1.0 : 0.0));
    assert(s.m2AnimationCalls[parallel] == (animation ? 1.0 : 0.0));
    assert(s.m2MaxRepeats[parallel] == 0); // table clears between presented frames
    assert(Profiler::Summarize(2, s) == 2 && s.m2MaxRepeats[parallel] == (animation ? 1 : 0));

    Profiler::ObserveCullCamera(0, position, view, changedProjection);
    Profiler::EndFrame();
    assert(Profiler::Summarize(1, s) == 1);
    assert(s.cullCameraComparisons == (cull ? 1.0 : 0.0) && s.cullCameraUnchanged == 0.0);
    Profiler::ObserveCullCamera(1, position, view, changedProjection);
    Profiler::EndFrame();
    assert(Profiler::Summarize(1, s) == 1 && s.cullCameraComparisons == 0.0);
    Profiler::EndFrame(); // no world camera on this presented frame
    Profiler::ObserveCullCamera(1, position, view, changedProjection);
    Profiler::EndFrame();
    assert(Profiler::Summarize(1, s) == 1 && s.cullCameraComparisons == 0.0);
    return 0;
}
