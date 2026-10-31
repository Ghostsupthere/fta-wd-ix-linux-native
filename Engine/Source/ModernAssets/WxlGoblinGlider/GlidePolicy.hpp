#pragma once
// Goblin Glider authorization and native-movement gate of WarcraftXL's wxl-goblin-glider
// (GPL-3.0-or-later, Copyright (C) 2026 WarcraftXL), unchanged apart from ScaledFeatherVelocity.
// Covered by tests/wxl_goblin_glider_test.cpp.

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace wxl::glider
{
    constexpr uint32_t kFalling = 0x00001000;
    constexpr uint32_t kFeatherFall = 0x20000000;
    constexpr uint32_t kForbiddenFlags = 0x0C200A00; // Transport, root, swimming and spline movement.
    constexpr uint32_t kNativeFlight = 0x02000400; // Native flight/gravity-off owns movement while present.
    constexpr uint32_t kDefaultLandingConfirmationMs = 200;
    constexpr uint32_t kMaximumDuration = 180000;
    constexpr uint32_t kPendingTimeout = 1500;
    constexpr uint32_t kAuraFreshness = 2000;
    constexpr float kMinimumInitialVelocity = -15.05f; // Native sign: the server's maximum upward launch is 15.

    struct NativeSample
    {
        uint32_t flags = 0;
        uint32_t fallTime = 0;
        float speed = 0.0f;
        float facing = 0.0f;
        float verticalStartVelocity = 0.0f;
        bool transported = false;
    };

    enum class Phase : uint32_t { None, Pending, Active, Ended };

    // Holds authorization only. Position, acceleration, gravity, collision and input stay native.
    class Grant
    {
    public:
        bool Start(uint32_t token, uint64_t guid, float speed, uint32_t duration, float sink, uint32_t now,
            uint32_t landingMs = kDefaultLandingConfirmationMs)
        {
            if (!token || !guid || !std::isfinite(speed) || speed < 7.0f || speed > 40.0f ||
                !duration || duration > kMaximumDuration || !std::isfinite(sink) || sink < 3.0f || sink > 15.0f ||
                landingMs < 50 || landingMs > 1000 || phase == Phase::Pending || phase == Phase::Active)
                return false;
            // A completed/revoked token cannot renew its own lifetime.
            if (token == id && guid == player)
                return false;
            id = token;
            player = guid;
            targetSpeed = speed;
            terminalSink = sink;
            lifetime = duration;
            landingConfirmationMs = landingMs;
            started = now;
            auraObserved = false;
            lastFallTime = 0;
            contactObserved = resumeAllowed = false;
            phase = Phase::Pending;
            return true;
        }

        void ObserveAura(bool allowed, uint32_t now)
        {
            auraObserved = allowed;
            auraAt = now;
            if (!allowed && phase == Phase::Active)
                End();
        }

        bool Prepare(uint64_t currentPlayer, const NativeSample& sample, uint32_t now)
        {
            if (phase != Phase::Pending && phase != Phase::Active)
                return false;
            if (currentPlayer != player || now - started >= lifetime ||
                (phase == Phase::Pending && now - started >= kPendingTimeout))
            {
                End();
                return false;
            }
            const bool numeric = std::isfinite(sample.speed) && std::isfinite(sample.facing) &&
                std::isfinite(sample.verticalStartVelocity);
            const bool observed = auraObserved && now - auraAt <= kAuraFreshness;
            if (!numeric || (sample.flags & kForbiddenFlags) || sample.transported ||
                (phase == Phase::Active && (!observed || !(sample.flags & kFeatherFall))))
            {
                End();
                return false;
            }
            if (sample.flags & kNativeFlight)
            {
                // Preserve the grant while authorized native flight owns motion. Server permission
                // is still required; no glider speed, gravity or flags are applied in this mode.
                contactObserved = false;
                resumeAllowed = phase == Phase::Active;
                return false;
            }
            if (!(sample.flags & kFalling))
            {
                if (phase == Phase::Active)
                {
                    if (!contactObserved)
                    {
                        contactObserved = true;
                        contactAt = now;
                    }
                    else if (now - contactAt >= landingConfirmationMs)
                        End();
                    resumeAllowed = true;
                }
                return false;
            }
            const bool impulse = sample.verticalStartVelocity >= kMinimumInitialVelocity &&
                (phase == Phase::Pending ? std::fabs(sample.speed - targetSpeed) <= 0.05f
                    : sample.speed >= 0.0f && (sample.speed <= targetSpeed + 0.05f || resumeAllowed));
            if (!observed || !(sample.flags & kFeatherFall) || !impulse)
            {
                // START/aura/knockback are separate packets; initial activation still requires
                // the exact authorized impulse. A contact never issues another upward launch.
                if (phase == Phase::Active)
                    End();
                return false;
            }
            if (phase == Phase::Active && sample.fallTime < lastFallTime && !resumeAllowed)
            {
                End(); // A new native jump/knockback owns the trajectory now.
                return false;
            }
            contactObserved = resumeAllowed = false;
            lastFallTime = sample.fallTime;
            return true;
        }

        void DidSteer() { if (phase == Phase::Pending) phase = Phase::Active; }
        void DidRebaseFall() { lastFallTime = 0; }
        void End() { if (phase != Phase::None) phase = Phase::Ended; auraObserved = false; }
        void Stop(uint32_t token) { if (token == id) End(); }
        void Reset() { *this = Grant{}; }
        float HorizontalSpeed() const { return targetSpeed; }

        uint32_t id = 0;
        uint64_t player = 0;
        Phase phase = Phase::None;
        float terminalSink = 7.0f;

    private:
        uint32_t started = 0;
        uint32_t lifetime = 0;
        uint32_t auraAt = 0;
        uint32_t lastFallTime = 0;
        uint32_t contactAt = 0;
        uint32_t landingConfirmationMs = kDefaultLandingConfirmationMs;
        float targetSpeed = 0.0f;
        bool auraObserved = false;
        bool contactObserved = false;
        bool resumeAllowed = false;
    };

    struct FallBoundary
    {
        bool active = false;
        uintptr_t movement = 0;
        uint64_t player = 0;
        uint32_t lastTime = 0;
        float startHeight = 0.0f;
        float initialVelocity = 0.0f;
        float terminal = 7.0f;

        bool SameTrajectory(uintptr_t current, uint64_t guid, const NativeSample& sample, float height) const
        {
            return active && movement == current && player == guid && sample.fallTime >= lastTime &&
                sample.verticalStartVelocity == initialVelocity && height == startHeight &&
                (sample.flags & (kFalling | kFeatherFall)) == (kFalling | kFeatherFall) &&
                !(sample.flags & (kForbiddenFlags | kNativeFlight)) && !sample.transported;
        }

        void Capture(uintptr_t current, uint64_t guid, const NativeSample& sample, float height, float sink)
        {
            active = true;
            movement = current;
            player = guid;
            lastTime = sample.fallTime;
            startHeight = height;
            initialVelocity = sample.verticalStartVelocity;
            terminal = sink;
        }

        void Clear() { *this = FallBoundary{}; }
    };

    template<class Velocity, class StartFall, class DidRebase>
    bool ApplyTerminalBoundary(float previous, float desired, Velocity&& velocity,
        StartFall&& startFall, DidRebase&& didRebase)
    {
        if (desired == previous)
            return true;
        const float currentVelocity = velocity(previous);
        // The native StartFall accepts signed velocity: negative preserves an ascending launch.
        if (!std::isfinite(currentVelocity) || !startFall(currentVelocity))
            return false;
        didRebase();
        return true;
    }

    template<class Rebase, class Heading>
    bool Steer(Grant& grant, uint64_t player, const NativeSample& sample, uint32_t now,
        Rebase&& rebase, Heading&& heading)
    {
        if (!grant.Prepare(player, sample, now))
            return false;
        // Rebase first: rotating a vector anchored at an earlier position would rotate past travel.
        rebase();
        heading();
        grant.DidSteer();
        return true;
    }

    template<class NativeDistance>
    float FallDistance(float time, int slowFall, float initialVelocity, float terminal, NativeDistance&& native)
    {
        if (!slowFall || terminal == 7.0f || !std::isfinite(terminal) || terminal < 3.0f || terminal > 15.0f)
            return native(time, slowFall, initialVelocity);
        // Reuse the client's exact fall curve. Scaling time by 1/a and distance by a² preserves
        // acceleration; scaling initial velocity by 1/a preserves its physical starting value.
        // Only terminal velocity changes from native 7 to 7*a. No shared client constant is patched.
        const float scale = terminal / 7.0f;
        return native(time / scale, slowFall, initialVelocity / scale) * scale * scale;
    }

    // The native feather-fall velocity (CMovementShared::GetFeatherFallVelocity) for a terminal
    // velocity of 7*factor: the same dimensional transform FallDistance applies, evaluated on the
    // transformed fall time and initial velocity and scaled back. The Windows module runs the native
    // accessor on a stack copy of the movement; this is that accessor's safe-fall branch.
    inline float ScaledFeatherVelocity(uint32_t fallTimeMs, float startVelocity, float terminal)
    {
        constexpr float kNativeTerminal = 7.0f;
        constexpr float kGravity = 19.291105f;
        const float factor = terminal / kNativeTerminal;
        const uint32_t scaledTime = static_cast<uint32_t>(static_cast<double>(fallTimeMs) / factor);
        float velocity = startVelocity / factor;
        if (velocity > kNativeTerminal)
            velocity = kNativeTerminal;
        velocity = velocity + scaledTime * 0.001f * kGravity;
        if (velocity > kNativeTerminal)
            velocity = kNativeTerminal;
        return velocity * factor;
    }
}
