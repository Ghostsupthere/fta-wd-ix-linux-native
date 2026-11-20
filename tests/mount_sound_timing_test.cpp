#include "WoW/Source/SoundInterface/MountSoundTiming.h"

#include <cassert>
#include <limits>
#include <string>

#ifdef NDEBUG
#error Mount sound timing tests require enabled assertions
#endif

int main() {
    using namespace MountSoundTiming;
    constexpr auto header = "SoundID,DelayMin,DelayMax,CooldownMin,CooldownMax\n";
    Policy policy;
    Timing timing{};
    assert(!policy.Find(101, timing));
    assert(!policy.Parse("wrong header\n101,20,40,100,200"));
    assert(!policy.Parse(std::string(header) + "0,20,40,100,200"));
    assert(!policy.Parse(std::string(header) + "101,40,20,100,200"));
    assert(!policy.Parse(std::string(header) + "101,20,40,200,100"));
    assert(!policy.Parse(std::string(header) + "101,20,600001,100,200"));
    assert(!policy.Parse(std::string(header) + "101,20,40,100,600001"));
    assert(!policy.Parse(std::string(header) + "101,20,40,100,200,0"));
    assert(!policy.Parse(std::string(header) + "101,,40,100,200"));
    assert(!policy.Parse(std::string(header) + "101,20,40,100,200\n101,0,0,0,0"));
    assert(!policy.Parse(std::string(header) + std::string("101,20,40,100,200\0", 18)));
    assert(!policy.Parse(std::string(MaxFileBytes + 1, ' ')));

    assert(policy.Parse("SoundID,DelayMin,DelayMax,CooldownMin,CooldownMax\r\n"
                        "101,20,40,100,200\r\n102,0,0,0,0"));
    assert(policy.Find(101, timing));
    assert(timing.delayMin == 20 && timing.delayMax == 40 && timing.cooldownMin == 100 && timing.cooldownMax == 200);
    assert(!policy.Parse(std::string(header) + "101,20,40"));
    assert(policy.Find(101, timing));

    int first = 0;
    int second = 0;
    const auto nearWrap = std::numeric_limits<uint32_t>::max() - 5;
    policy.Played(&first, 101, nearWrap, 20);
    assert(policy.Remaining(&first, 101, nearWrap) == 20);
    assert(policy.Remaining(&first, 101, 3) == 11);
    assert(policy.Remaining(&first, 101, 14) == 0);
    assert(policy.Remaining(&second, 101, nearWrap) == 0);
    assert(!DeadlineReached(nearWrap, 14) && DeadlineReached(14, 14));

    auto max = [](uint32_t, uint32_t maximum) { return maximum; };
    int plays = 0;
    auto play = [&] { ++plays; return 0; };
    auto fail = [&] { ++plays; return 16; };
    assert(!HandleModelEvent(policy, &first, 999, 20, play, max) && plays == 0);
    assert(HandleModelEvent(policy, &first, 101, 20, fail, max) && plays == 1);
    assert(HandleModelEvent(policy, &first, 101, 20, play, max) && plays == 2);
    assert(HandleModelEvent(policy, &first, 101, 20, play, max) && plays == 2);
    assert(HandleModelEvent(policy, &first, 101, 219, play, max) && plays == 2);
    assert(HandleModelEvent(policy, &first, 101, 220, play, max) && plays == 3);
    assert(HandleModelEvent(policy, &second, 101, 20, play, max) && plays == 4);
    assert(HandleModelEvent(policy, &first, 102, 20, play, max) && plays == 5);
    assert(HandleModelEvent(policy, &first, 102, 20, play, max) && plays == 5);
    assert(HandleModelEvent(policy, &first, 102, 21, play, max) && plays == 6);

    policy.Forget(&first);
    assert(policy.Remaining(&first, 101, 220) == 0);
    assert(policy.Remaining(&second, 101, 20) == 200);
    policy.Reset();
    assert(!policy.Find(101, timing));
    assert(policy.Remaining(&second, 101, 20) == 0);
}
