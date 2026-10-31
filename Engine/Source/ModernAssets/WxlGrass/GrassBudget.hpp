#pragma once
// The two Environment-page limits of the supplied wxl-living-azeroth grass instancing, as pure policy:
//  - grass.memoryMB: how much instance data (64 bytes per plant) the cached batches may hold; the
//    least recently used batches make room for a new one, and one that cannot fit at all is refused.
//  - grass.buildMs: a soft per-frame budget for building batches. A build may start while the frame's
//    spent time is under the budget ("one already-started grass group can exceed the soft budget");
//    later batches that frame draw the stock way and are built on a later frame.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace wxl::grass::budget
{
    struct CachedItem
    {
        const void* key = nullptr;
        uint64_t bytes = 0;
        uint64_t lastUse = 0;
    };

    struct Plan
    {
        bool fits = false;                 // false: the new batch alone exceeds the limit
        std::vector<const void*> evict;    // least recently used first
    };

    inline uint64_t LimitBytes(double megabytes)
    {
        if (!(megabytes > 0.0)) return 0;
        return static_cast<uint64_t>(megabytes * 1024.0 * 1024.0);
    }

    // Which cached batches to drop so `incoming` more bytes fit under `limit`.
    inline Plan PlanEvictions(std::vector<CachedItem> items, uint64_t incoming, uint64_t limit)
    {
        Plan plan;
        if (incoming > limit) return plan;
        uint64_t total = incoming;
        for (const CachedItem& item : items) total += item.bytes;
        plan.fits = true;
        if (total <= limit) return plan;
        std::sort(items.begin(), items.end(), [](const CachedItem& a, const CachedItem& b) {
            return a.lastUse != b.lastUse ? a.lastUse < b.lastUse : a.key < b.key;
        });
        for (const CachedItem& item : items)
        {
            if (total <= limit) break;
            plan.evict.push_back(item.key);
            total -= item.bytes;
        }
        return plan;
    }

    class BuildBudget
    {
    public:
        void SetMilliseconds(double ms) { budgetMs_ = ms > 0.0 ? ms : 0.0; }
        double Milliseconds() const { return budgetMs_; }
        void NewFrame() { spentMs_ = 0.0; }
        // A build may start while the time already spent this frame is under the budget.
        bool MayStart() const { return spentMs_ < budgetMs_; }
        void Spend(double ms) { if (ms > 0.0) spentMs_ += ms; }
        double Spent() const { return spentMs_; }

    private:
        double budgetMs_ = 2.0;
        double spentMs_ = 0.0;
    };
}
