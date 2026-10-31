#include "Engine/Source/ModernAssets/WxlLoneWolf/LoneWolfIntent.hpp"
#include <cassert>

using namespace wxl::lonewolf::intent;

static Request Req(std::uintptr_t cast, std::uint64_t caster = 7, std::uint64_t target = 9, std::uint32_t spell = 3044, std::uint8_t count = 1) {
    Request r; r.cast = cast; r.caster = caster; r.target = target; r.spell = spell; r.count = count; return r;
}

int main() {
    State s;
    // No authority: nothing is recorded.
    assert(!s.Sent(Req(1)).serial);
    s.Authority(7);
    // Incomplete requests are refused.
    assert(!s.Sent(Req(0)).serial && !s.Sent(Req(1, 0)).serial && !s.Sent(Req(1, 8)).serial);
    assert(!s.Sent(Req(1, 7, 0)).serial && !s.Sent(Req(1, 7, 9, 0)).serial && !s.Sent(Req(1, 7, 9, 3044, 0)).serial);

    Stamp stamp = s.Sent(Req(0x1000));
    assert(stamp.serial && s.Current(stamp));
    // A different cast identity does not consume it; the exact one does, exactly once.
    assert(!s.Consume(Req(0x2000)).serial);
    assert(!s.Consume(Req(0x1000, 7, 9, 3044, 2)).serial);   // same node, other count: entry is dropped
    assert(!s.Consume(Req(0x1000)).serial);
    stamp = s.Sent(Req(0x1000));
    Stamp consumed = s.Consume(Req(0x1000));
    assert(consumed.serial == stamp.serial && !s.Consume(Req(0x1000)).serial);

    // Any newer decision invalidates an older stamp.
    stamp = s.Sent(Req(0x1000));
    s.Invalidate();
    assert(!s.Current(stamp) && !s.Consume(Req(0x1000)).serial);
    stamp = s.Sent(Req(0x1000));
    s.Authority(7);                 // unchanged authority keeps it
    assert(s.Current(stamp));
    s.Authority(8);                 // changed authority drops it
    assert(!s.Current(stamp));
    s.Authority(7);

    // Release drops a pending request for that node.
    stamp = s.Sent(Req(0x3000));
    s.Release(0x3000);
    assert(!s.Consume(Req(0x3000)).serial);

    // Ring capacity: more than 64 outstanding requests overwrite the oldest, never read out of range.
    for (std::uintptr_t i = 1; i <= 200; ++i) assert(s.Sent(Req(i)).serial);
    assert(!s.Consume(Req(1)).serial && s.Consume(Req(200)).serial);

    // Selection ownership: an explicit selection (even repeated) is a new decision.
    std::uint64_t token = s.Select();
    assert(s.OwnsSelection(token) && s.CurrentSelection(token));
    s.Invalidate();
    assert(s.OwnsSelection(token) && !s.CurrentSelection(token));   // still owned until rolled back
    assert(s.ReleaseSelection(token) && !s.ReleaseSelection(token) && !s.OwnsSelection(0));

    // Completion: accepted only for a normal, uncancelled, still-current, non-hidden cast.
    stamp = s.Sent(Req(0x4000));
    Completion ok(stamp, 0, true, 9);
    ok.Finish(true);
    assert(ok.Accepted(s));
    Completion hidden(stamp, 1, true, 9);
    hidden.Finish(true);
    assert(!hidden.Accepted(s));
    Completion early(stamp, 0, true, 9);
    early.Finish(false);
    early.Finish(true);
    assert(!early.Accepted(s));
    Completion released(stamp, 0, true, 9);
    released.Released();
    assert(!released.Accepted(s));
    Completion releasedAfterFinish(stamp, 0, true, 9);
    releasedAfterFinish.Finish(true);
    releasedAfterFinish.Released();
    assert(releasedAfterFinish.Accepted(s));
    Completion unparsed(stamp, 0, false, 0);
    unparsed.Finish(true);
    assert(!unparsed.Accepted(s));
    Completion none(Stamp{}, 0, true, 9);
    none.Finish(true);
    assert(!none.Accepted(s));
    s.Invalidate();
    assert(!ok.Accepted(s));

    // Reset clears everything.
    s.Reset();
    assert(!s.Sent(Req(1)).serial);
    return 0;
}
