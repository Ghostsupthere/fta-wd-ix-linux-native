#include "Engine/Source/Base/Profiler.h"
#include <cassert>
#include <chrono>
#include <cmath>
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
                    assert(Profiler::UiDetailZone(Profiler::WorldUpdateMapPreparation, Profiler::WorldUpdateMap)
                        == Profiler::Count);
                    Profiler::Scope map(Profiler::UiDetailZone(Profiler::WorldUpdateMap, Profiler::WorldFrameUpdate));
                    {
                        Profiler::Scope prepare(Profiler::UiDetailZone(Profiler::WorldUpdateMapPreparation, Profiler::WorldUpdateMap));
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
        assert(s.zones[Profiler::PlayerNameUpdate].calls == detailCalls);
        assert(s.zones[Profiler::WorldTextRender].calls == detailCalls);
        assert(s.zones[Profiler::WorldTextRender].selfDraws == detailCalls);
        assert(s.zones[Profiler::UiRender].selfDraws == (detailCalls ? 0.0 : 6.0));
        assert(s.zones[Profiler::WorldRender].selfDraws == 1.0);
        assert(s.zones[Profiler::UiCallbackScroll].selfDraws == detailCalls);
        assert(s.zones[Profiler::UiCallbackUnitModel].selfDraws == detailCalls);
        assert(s.drawCalls == 7.0 && s.triangles == 133.0);
    }
    return 0;
}
