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
    return 0;
}
