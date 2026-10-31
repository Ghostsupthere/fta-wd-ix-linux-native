// Exercises the production gate/callback ordering and native-curve transformation.
#include "Engine/Source/ModernAssets/WxlGoblinGlider/GlidePolicy.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <random>
#include <vector>

namespace
{
    unsigned checks = 0;
    void Check(bool value, const char* message)
    {
        ++checks;
        if (!value) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
    }
    bool Near(float a, float b, float epsilon = 0.0001f) { return std::fabs(a - b) <= epsilon; }
    wxl::glider::NativeSample Air(uint32_t time = 0)
    {
        return {wxl::glider::kFalling | wxl::glider::kFeatherFall | 1, time, 25.0f, 1.2f, 0.0f, false};
    }
    wxl::glider::Grant MakeGrant(uint32_t now = 100)
    {
        wxl::glider::Grant grant;
        Check(grant.Start(19, 11, 25.0f, 120000, 7.0f, now), "valid server grant");
        return grant;
    }
    // Independent physical oracle for the native terminal-velocity curve, used only by tests.
    float Reference(float time, float terminal, float initial)
    {
        constexpr double gravity = 19.291105270385742;
        const double velocity = std::min<double>(initial, terminal);
        const double accelerated = std::min<double>(time, (terminal - velocity) / gravity);
        return static_cast<float>(velocity * accelerated + gravity * accelerated * accelerated / 2.0 +
            (time - accelerated) * terminal);
    }
    float Native(float time, int slow, float initial) { return Reference(time, slow ? 7.0f : 60.148003f, initial); }
}

int main()
{
    using namespace wxl::glider;
    auto grant = MakeGrant();
    Grant threeMinutes;
    Check(threeMinutes.Start(18, 11, 25, 180000, 7, 100), "three-minute server grant accepted");
    threeMinutes.ObserveAura(true, 100);
    Check(threeMinutes.Prepare(11, Air(), 101), "three-minute grant acknowledges initial native trajectory");
    threeMinutes.DidSteer();
    threeMinutes.ObserveAura(true, 179999);
    Check(threeMinutes.Prepare(11, Air(), 179999), "three-minute grant remains active before its deadline");
    threeMinutes.DidSteer();
    Check(!threeMinutes.Prepare(11, Air(), 180100), "three-minute grant expires exactly at its deadline");
    Grant excessiveDuration;
    Check(!excessiveDuration.Start(18, 11, 30, 180001, 7, 100), "longer unbounded grants rejected");
    std::vector<int> callbacks;
    const auto rebase = [&] { callbacks.push_back(1); };
    const auto heading = [&] { callbacks.push_back(2); };
    Check(!Steer(grant, 11, Air(), 101, rebase, heading), "no aura means zero native calls");
    Check(callbacks.empty(), "pending authorization cannot mutate movement");
    grant.ObserveAura(true, 101);
    auto sample = Air();
    sample.speed = 7;
    Check(!Steer(grant, 11, sample, 102, rebase, heading), "wait for actual server impulse speed");
    sample = Air();
    Check(Steer(grant, 11, sample, 103, rebase, heading), "enable on native falling impulse");
    Check(callbacks == std::vector<int>({1, 2}), "native rebase precedes direction rebuild");
    Check(grant.phase == Phase::Active, "ACK only after native callbacks");
    Check(!grant.Start(20, 11, 25, 120000, 7, 104), "second activation cannot extend flight");
    Check(!grant.Start(19, 11, 25, 120000, 7, 104), "duplicate token cannot renew deadline");
    grant.Stop(18);
    Check(grant.phase == Phase::Active, "stale STOP cannot stop another grant");
    grant.ObserveAura(false, 105);
    Check(!Steer(grant, 11, sample, 106, rebase, heading), "aura removal is immediate");
    Check(callbacks.size() == 2, "termination does not write flags or generate native motion");
    Check(!grant.Start(19, 11, 25, 120000, 7, 107), "revoked token cannot reactivate");

    for (uint32_t flag : {0x200u, 0x800u, 0x200000u, 0x4000000u, 0x8000000u})
    {
        grant = MakeGrant(); grant.ObserveAura(true, 100); sample = Air(); sample.flags |= flag;
        Check(!Steer(grant, 11, sample, 101, rebase, heading), "unsafe native mode fails closed");
    }
    grant = MakeGrant(); grant.ObserveAura(true, 100); sample = Air(); sample.transported = true;
    Check(!grant.Prepare(11, sample, 101), "transport GUID alone blocks gliding");
    grant = MakeGrant(); grant.ObserveAura(true, 100);
    Check(!grant.Prepare(12, Air(), 101), "player identity change blocks stale authorization");
    grant = MakeGrant(); grant.ObserveAura(true, 100);
    Check(!grant.Prepare(11, Air(), 1600), "pending activation expires without an ACK");
    grant = MakeGrant(); grant.ObserveAura(true, 100);
    Check(grant.Prepare(11, Air(500), 101), "valid active trajectory"); grant.DidSteer();
    Check(!grant.Prepare(11, Air(0), 102), "external knockback resetting fall clock wins");
    grant = MakeGrant(); grant.ObserveAura(true, 100);
    Check(grant.Prepare(11, Air(500), 101), "pre boundary trajectory"); grant.DidSteer();
    grant.DidRebaseFall();
    Check(grant.Prepare(11, Air(16), 102), "explicit native sink boundary rebase is accepted");
    sample = Air(17); sample.speed = 0;
    Check(grant.Prepare(11, sample, 103), "collision-clipped horizontal speed retains the authorized glide");
    Check(grant.HorizontalSpeed() == 25, "recovery uses only the original server horizontal speed");
    for (uint32_t flag : {0x1000000u, 0x40000000u})
    {
        grant = MakeGrant(); grant.ObserveAura(true, 100); sample = Air(); sample.flags |= flag;
        Check(Steer(grant, 11, sample, 101, rebase, heading), "flight permission and hover buffs allow gliding");
    }
    grant = MakeGrant(); grant.ObserveAura(true, 100);
    Check(grant.Prepare(11, Air(900), 101), "start wall-contact case"); grant.DidSteer();
    sample = Air(); sample.flags = kFeatherFall; sample.speed = 7;
    callbacks.clear();
    Check(!Steer(grant, 11, sample, 200, rebase, heading), "contact keeps native ground physics");
    Check(grant.phase == Phase::Active && callbacks.empty(), "contact adds no lift, speed or steering");
    Check(!grant.Prepare(11, sample, 399) && grant.phase == Phase::Active, "199 ms contact retains the grant");
    sample = Air(0); sample.speed = 7;
    Check(Steer(grant, 11, sample, 399, rebase, heading), "fall after brief contact resumes original glide");
    Check(grant.phase == Phase::Active && callbacks == std::vector<int>({1, 2}), "resume rebases before turning");
    sample.flags = kFeatherFall;
    Check(!grant.Prepare(11, sample, 400), "begin a real landing");
    Check(!grant.Prepare(11, sample, 600) && grant.phase == Phase::Ended, "200 ms continuous contact ends glide");
    for (uint32_t landingMs : {50u, 100u, 200u, 1000u})
    {
        Grant configured;
        Check(configured.Start(33, 11, 25, 180000, 7, 100, landingMs), "configured contact delay accepted");
        configured.ObserveAura(true, 100);
        Check(configured.Prepare(11, Air(), 101), "configured grant activates");
        configured.DidSteer();
        Check(!configured.Prepare(11, sample, 200), "configured contact begins");
        Check(!configured.Prepare(11, sample, 200 + landingMs - 1) && configured.phase == Phase::Active,
            "configuration controls actual landing threshold");
        Check(!configured.Prepare(11, sample, 200 + landingMs) && configured.phase == Phase::Ended,
            "configured threshold ends sustained contact");
    }
    for (uint32_t invalid : {0u, 49u, 1001u, 0xFFFFFFFFu})
    {
        Grant malformed;
        Check(!malformed.Start(34, 11, 25, 180000, 7, 100, invalid), "invalid contact delay refused");
    }
    grant = MakeGrant(); grant.ObserveAura(true, 100);
    Check(grant.Prepare(11, Air(500), 101), "start native-flight case"); grant.DidSteer();
    sample = Air(); sample.flags = kFeatherFall | kNativeFlight; sample.speed = 60;
    callbacks.clear();
    Check(!Steer(grant, 11, sample, 200, rebase, heading), "native flight keeps its own movement");
    Check(!grant.Prepare(11, sample, 900) && grant.phase == Phase::Active, "native flight is not a landing");
    Check(callbacks.empty(), "native flight receives no glider movement writes");
    sample = Air(0); sample.speed = 60;
    Check(grant.Prepare(11, sample, 901), "native flight can hand back to the original grant");
    Check(grant.HorizontalSpeed() == 25, "handoff does not inherit flight speed");
    grant.ObserveAura(true, 120100);
    Check(!grant.Prepare(11, Air(), 120100), "contact and flight do not extend grant lifetime");
    grant = MakeGrant(); grant.ObserveAura(true, 100); grant.DidSteer();
    Check(!grant.Prepare(11, Air(), 2101), "missing watcher fails closed");
    grant = MakeGrant(0xFFFFFFF0u); grant.ObserveAura(true, 0xFFFFFFF1u);
    Check(grant.Prepare(11, Air(), 16), "GetTickCount rollover does not end a fresh grant");
    grant.DidSteer(); grant.ObserveAura(true, 120016);
    Check(!grant.Prepare(11, Air(), 120016), "rollover cannot extend maximum lifetime");

    for (float bad : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -1.0f, 41.0f})
    {
        wxl::glider::Grant g;
        Check(!g.Start(1, 1, bad, 1000, 7, 0), "malformed speed rejected");
    }
    for (float bad : {std::numeric_limits<float>::quiet_NaN(), -3.0f, 0.0f, 2.99f, 15.01f})
    {
        wxl::glider::Grant g;
        Check(!g.Start(1, 1, 25, 1000, bad, 0), "malformed terminal descent rejected");
    }
    grant = MakeGrant(); grant.ObserveAura(true, 100); sample = Air();
    sample.facing = std::numeric_limits<float>::quiet_NaN();
    Check(!grant.Prepare(11, sample, 101), "nonfinite facing cannot enter native trig");

    Grant launch;
    Check(launch.Start(31, 11, 30, 120000, 7, 100), "faster ground launch grant is supported");
    launch.ObserveAura(true, 100);
    sample = Air(); sample.flags = kFeatherFall; sample.speed = 0;
    callbacks.clear();
    Check(!Steer(launch, 11, sample, 101, rebase, heading), "ground pending waits for native launch");
    Check(launch.phase == Phase::Pending && callbacks.empty(), "ground pending cannot synthesize falling");
    sample = Air(); sample.speed = 30; sample.verticalStartVelocity = -15;
    Check(Steer(launch, 11, sample, 102, rebase, heading), "native upward fifteen impulse activates steering");
    Check(launch.phase == Phase::Active && callbacks.size() == 2, "upward launch ACK follows native callbacks");
    sample.fallTime = 900;
    Check(launch.Prepare(11, sample, 103), "negative anchor remains valid after the upward apex");
    sample.verticalStartVelocity = -15.06f;
    Check(!launch.Prepare(11, sample, 104), "greater than authorized upward launch terminates grant");
    Check(launch.phase == Phase::Ended, "unexpected stronger upward motion cannot remain active");
    Grant redeploy;
    Check(redeploy.Start(32, 11, 30, 120000, 7, 100), "redeploy authorization is a fresh token");
    redeploy.ObserveAura(true, 100);
    sample = Air(); sample.speed = 30; sample.verticalStartVelocity = 7;
    Check(redeploy.Prepare(11, sample, 101), "preserved downward velocity is accepted without another lift");

    unsigned nativeRebases = 0, clockResets = 0;
    float suppliedVelocity = 0;
    const auto upwardVelocity = [](float) { return -8.0f; };
    const auto startFall = [&](float velocity)
    {
        suppliedVelocity = velocity;
        ++nativeRebases;
        return true;
    };
    const auto resetClock = [&] { ++clockResets; };
    Check(ApplyTerminalBoundary(7, 3, upwardVelocity, startFall, resetClock),
        "tuned sink can begin during upward launch");
    Check(suppliedVelocity == -8 && nativeRebases == 1 && clockResets == 1,
        "native boundary receives signed current velocity unchanged");
    for (unsigned frame = 0; frame != 100; ++frame)
        Check(ApplyTerminalBoundary(3, 3, upwardVelocity, startFall, resetClock), "steady mode needs no rebase");
    Check(nativeRebases == 1 && clockResets == 1, "ascending motion is not reimpulsed per frame");
    Check(ApplyTerminalBoundary(3, 7, upwardVelocity, startFall, resetClock),
        "ending tuned mode while ascending preserves native signed velocity");
    Check(nativeRebases == 2 && suppliedVelocity == -8, "mode exit has exactly one native rebase");
    Check(!ApplyTerminalBoundary(7, 3,
        [](float) { return std::numeric_limits<float>::quiet_NaN(); }, startFall, resetClock),
        "invalid velocity never reaches native StartFall");
    Check(nativeRebases == 2 && clockResets == 2, "invalid boundary cannot reset the fall clock");
    Check(!ApplyTerminalBoundary(7, 3, upwardVelocity, [](float) { return false; }, resetClock),
        "native StartFall rejection fails closed");
    Check(clockResets == 2, "failed native rebase does not excuse a fall clock reset");

    FallBoundary boundary;
    sample = Air(600);
    boundary.Capture(0x1000, 11, sample, 90.0f, 3.0f);
    Check(boundary.SameTrajectory(0x1000, 11, Air(700), 90), "steady custom terminal does not rebase each frame");
    Check(!boundary.SameTrajectory(0x2000, 11, Air(700), 90), "reused or other movement object cannot inherit sink");
    Check(!boundary.SameTrajectory(0x1000, 12, Air(700), 90), "new player cannot inherit sink");
    Check(!boundary.SameTrajectory(0x1000, 11, Air(0), 90), "new impulse keeps native reset");
    Check(!boundary.SameTrajectory(0x1000, 11, Air(700), 80), "teleport/new fall anchor invalidates sink");
    sample = Air(700); sample.flags = 0;
    Check(!boundary.SameTrajectory(0x1000, 11, sample, 90), "landing cannot restart falling");

    for (float sink : {3.0f, 5.0f, 7.0f, 10.0f, 15.0f})
    {
        constexpr float launchVelocity = -15.0f;
        constexpr float apex = 15.0f / 19.291105270385742f;
        Check(FallDistance(0.05f, 1, launchVelocity, sink, Native) < 0,
            "upward impulse gains height through native gravity");
        Check(Near(FallDistance(apex, 1, launchVelocity, sink, Native),
            Reference(apex, 7, launchVelocity)), "tuned sink preserves bounded native launch apex");
        Check(Near(FallDistance(10, 1, launchVelocity, sink, Native) -
            FallDistance(9, 1, launchVelocity, sink, Native), sink), "upward launch still reaches descent terminal");
        Check(Near(FallDistance(0.05f, 1, 0, sink, Native), Reference(0.05f, 7, 0)), "early gravity unchanged");
        Check(Near(FallDistance(10, 1, 0, sink, Native) - FallDistance(9, 1, 0, sink, Native), sink),
            "terminal descent is configured rate");
        Check(FallDistance(4, 0, 0, sink, Native) == Native(4, 0, 0), "ordinary gravity untouched");
        const float transition = sink / 19.291105270385742f;
        const float left = FallDistance(transition - 0.00001f, 1, 0, sink, Native);
        const float right = FallDistance(transition + 0.00001f, 1, 0, sink, Native);
        Check(right >= left && right - left < 0.001f, "native terminal transition stays continuous");
    }
    std::mt19937 random(12340);
    std::uniform_real_distribution<float> time(0, 120), descent(3, 15), initial(-15.0f, 6.9f);
    for (unsigned index = 0; index != 20000; ++index)
    {
        const float t = time(random), sink = descent(random), velocity = initial(random);
        const float actual = FallDistance(t, 1, velocity, sink, Native);
        Check(Near(actual, Reference(t, sink, velocity), 0.0008f), "transformed native curve matches physical contract");
        Check(FallDistance(t, 1, velocity, 7, Native) == Native(t, 1, velocity), "default path is bit exact");
        if (t >= std::max(0.0f, -velocity / 19.291105270385742f))
            Check(FallDistance(t + 0.01f, 1, velocity, sink, Native) >= actual,
                "descent after apex never gains altitude");
        else
            Check(actual >= -velocity * velocity / (2 * 19.291105270385742f) - 0.0001f,
                "ascent stays below native impulse energy height");
    }
    std::printf("PASS: %u production glider policy / native-curve contract checks\n", checks);
}

// ---- Native-port additions: the safe-fall velocity transform and the opening schedule ----
#include "Engine/Source/ModernAssets/WxlGoblinGlider/GliderVisualPolicy.hpp"

namespace {
// The client's CMovementShared::GetFeatherFallVelocity, safe-fall branch (terminal 7).
float NativeFeatherVelocity(uint32_t fallTimeMs, float start) {
    float velocity = start > 7.0f ? 7.0f : start;
    velocity = velocity + fallTimeMs * 0.001f * 19.291105f;
    return velocity > 7.0f ? 7.0f : velocity;
}
}  // namespace

struct PortChecks {
    PortChecks() {
        using namespace wxl::glider;
        // At the native terminal the transform is the native accessor.
        for (uint32_t t : {0u, 1u, 250u, 1000u, 60000u})
            for (float v : {-15.0f, -1.0f, 0.0f, 3.0f, 7.0f, 20.0f})
                Check(std::fabs(ScaledFeatherVelocity(t, v, 7.0f) - NativeFeatherVelocity(t, v)) < 1e-4f, "terminal 7 equals native velocity");
        // A tuned terminal is reached under gravity and never exceeded, in both directions.
        for (float terminal : {3.0f, 10.0f, 15.0f}) {
            Check(ScaledFeatherVelocity(0, 0.0f, terminal) == 0.0f, "starts at the initial velocity");
            Check(std::fabs(ScaledFeatherVelocity(600000, 0.0f, terminal) - terminal) < 1e-3f, "reaches the tuned terminal");
            float previous = -100.0f;
            for (uint32_t t = 0; t < 20000; t += 50) {
                const float v = ScaledFeatherVelocity(t, -15.0f, terminal);
                Check(v + 1e-3f >= previous && v <= terminal + 1e-3f, "monotonic and bounded");
                previous = v;
            }
        }
        // The velocity is the derivative of the distance curve at the tuned terminal.
        {
            const float terminal = 10.0f;
            const float t = 2.0f, dt = 0.001f;
            const float d1 = FallDistance(t, 1, 0.0f, terminal, Native), d2 = FallDistance(t + dt, 1, 0.0f, terminal, Native);
            const float velocity = ScaledFeatherVelocity(static_cast<uint32_t>(t * 1000.0f), 0.0f, terminal);
            Check(std::fabs((d2 - d1) / dt - velocity) < 0.05f, "velocity matches distance derivative");
        }
        // One opening transition: Open, then Opened after the opening duration, then nothing.
        OpeningSchedule schedule;
        Check(schedule.Tick(1000, 0) == VisualAction::None && schedule.Tick(1000, 6000) == VisualAction::None, "invalid durations ignored");
        Check(schedule.Tick(1000, 1000) == VisualAction::Open, "opens first");
        Check(schedule.Tick(1500, 1000) == VisualAction::None, "still opening");
        Check(schedule.Tick(2000, 1000) == VisualAction::Opened, "opened at the end of the opening");
        Check(schedule.Tick(9000, 1000) == VisualAction::None, "then idle");
    }
} g_portChecks;
