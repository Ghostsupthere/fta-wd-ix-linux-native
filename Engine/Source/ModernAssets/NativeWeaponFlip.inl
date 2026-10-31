// Native adaptation of WarcraftXL BackSheathFlip.cpp (GPL-3.0-or-later).
// Settings/staging are UI-thread owned. Workers only read published snapshots.
namespace {
constexpr uint32_t backMask = 1, handMask = 30, allFlipMask = 31;
std::atomic<uint32_t> localFlips{0};
std::atomic<bool> weaponFeatureEnabled{true};
CM2Model* (*remoteResolver)(uint64_t) = nullptr;
struct RemoteFlipState {
    uint64_t guid = 0;
    uint32_t flags = 0, pending = 0;
    bool seenBack = false, seenHand = false;
};
std::array<RemoteFlipState, 80> remoteFlips{};
bool backSync = false, handSync = false;
struct ResolvedFlip { CM2Model* root = nullptr; uint32_t flags = 0; };
struct FlipSnapshot { std::array<ResolvedFlip, 80> entries{}; uint32_t count = 0; };
std::shared_ptr<const FlipSnapshot> remoteFlipSnapshot;
void PruneRemoteFlips() {
    for (auto& state : remoteFlips)
        if (!state.flags && !state.pending) state = {};
}
void RefreshNativeRemoteWeapons() {
    FlipSnapshot next;
    for (const auto& state : remoteFlips) {
        if (!state.guid || !state.flags || !remoteResolver) continue;
        auto* root = remoteResolver(state.guid);
        if (!root || root == localModel.load(std::memory_order_relaxed)) continue;
        bool duplicate = false;
        for (uint32_t i = 0; i < next.count; ++i)
            if (next.entries[i].root == root) {
                // Ambiguous recycled/shared roots must retain native placement.
                next.entries[i].flags = 0; duplicate = true; break;
            }
        if (!duplicate) next.entries[next.count++] = {root, state.flags};
    }
    auto old = std::atomic_load(&remoteFlipSnapshot);
    bool same = old && old->count == next.count;
    for (uint32_t i = 0; same && i < next.count; ++i)
        same = old->entries[i].root == next.entries[i].root && old->entries[i].flags == next.entries[i].flags;
    if (!same) {
        std::shared_ptr<const FlipSnapshot> published = std::make_shared<FlipSnapshot>(next);
        std::atomic_store(&remoteFlipSnapshot, std::move(published));
    }
}
}
bool NativeWeaponFeatureEnabled() { return weaponFeatureEnabled.load(std::memory_order_relaxed); }
void SetNativeWeaponFeatureEnabled(bool enabled) {
    weaponFeatureEnabled.store(enabled, std::memory_order_relaxed);
    if (!enabled) ResetNativeWeaponPresentation();
}
void SetNativeWeaponFlip(uint32_t mode, bool enabled) {
    if (enabled) localFlips.fetch_or(mode & allFlipMask, std::memory_order_relaxed);
    else localFlips.fetch_and(~(mode & allFlipMask), std::memory_order_relaxed);
}
bool GetNativeWeaponFlip(uint32_t mode) { return (localFlips.load(std::memory_order_relaxed) & mode) != 0; }
uint32_t NativeWeaponEligibility() {
    if (!NativeWeaponFeatureEnabled()) return 0;
    auto* root = localResolver ? localResolver() : nullptr;
    uint32_t result = 0, visited = 0;
    for (auto* child = root ? root->m_attachList : nullptr; child && visited++ < 1024; child = child->m_attachNext) {
        if (child->m_attachParent != root || !child->m_shared) continue;
        const auto family = wxl::weaponflip::ClassifyWeaponPath(child->m_shared->m_fileName, sizeof(child->m_shared->m_fileName));
        const auto eligibility = wxl::weaponflip::EvaluateAttachment(WxlAttachmentPoint(child->m_attachmentId), family);
        if (eligibility.back || eligibility.drawnBack) result |= 1;
        if (eligibility.held == wxl::weaponflip::Hand::Main || eligibility.sheathed == wxl::weaponflip::Hand::Main) result |= 2;
        if (eligibility.held == wxl::weaponflip::Hand::Off || eligibility.sheathed == wxl::weaponflip::Hand::Off) result |= 4;
    }
    return result;
}
void SetNativeRemoteWeaponResolver(CM2Model* (*resolver)(uint64_t)) { remoteResolver = resolver; }
bool SetNativeRemoteWeaponFlags(uint64_t guid, bool hands, uint32_t flags) {
    if (!guid || !NativeWeaponFeatureEnabled()) return false;
    const uint32_t mask = hands ? handMask : backMask;
    flags &= mask;
    RemoteFlipState* found = nullptr;
    for (auto& state : remoteFlips) if (state.guid == guid) { found = &state; break; }
    if (!found && !flags) return true;
    if (!found) for (auto& state : remoteFlips) if (!state.guid) { found = &state; break; }
    if (!found) return false;
    found->guid = guid;
    auto& destination = (hands ? handSync : backSync) ? found->pending : found->flags;
    destination = (destination & ~mask) | flags;
    (hands ? found->seenHand : found->seenBack) = true;
    PruneRemoteFlips();
    return true;
}
void BeginNativeRemoteWeaponSync(bool hands) {
    (hands ? handSync : backSync) = true;
    const uint32_t mask = hands ? handMask : backMask;
    for (auto& state : remoteFlips) {
        state.pending &= ~mask;
        (hands ? state.seenHand : state.seenBack) = false;
    }
}
void EndNativeRemoteWeaponSync(bool hands) {
    if (!(hands ? handSync : backSync)) return;
    const uint32_t mask = hands ? handMask : backMask;
    for (auto& state : remoteFlips) {
        const bool seen = hands ? state.seenHand : state.seenBack;
        state.flags = (state.flags & ~mask) | (seen ? state.pending & mask : 0);
        state.pending &= ~mask;
    }
    (hands ? handSync : backSync) = false;
    PruneRemoteFlips();
}
void ClearNativeRemoteWeaponSync(bool hands) {
    const uint32_t mask = hands ? handMask : backMask;
    for (auto& state : remoteFlips) { state.flags &= ~mask; state.pending &= ~mask; }
    (hands ? handSync : backSync) = false;
    PruneRemoteFlips();
}
void InvalidateNativeWeaponWorld() {
    localModel.store(nullptr, std::memory_order_relaxed);
    remoteFlips = {}; backSync = handSync = false;
    std::atomic_store(&remoteFlipSnapshot, std::shared_ptr<const FlipSnapshot>{});
}
void ResetNativeWeaponPresentation() {
    InvalidateNativeWeaponWorld();
    localFlips.store(0, std::memory_order_relaxed);
}
void ApplyNativeWeaponOrientation(CM2Model* model, bool local) {
    if (!NativeWeaponFeatureEnabled()) return;
    uint32_t flags = local ? localFlips.load(std::memory_order_relaxed) : 0;
    if (!local) {
        auto snapshot = std::atomic_load(&remoteFlipSnapshot);
        if (snapshot) for (uint32_t i = 0; i < snapshot->count; ++i)
            if (snapshot->entries[i].root == model->m_attachParent) { flags = snapshot->entries[i].flags; break; }
    }
    if (!flags) return;
    const auto family = wxl::weaponflip::ClassifyWeaponPath(model->m_shared->m_fileName, sizeof(model->m_shared->m_fileName));
    const auto eligibility = wxl::weaponflip::EvaluateAttachment(WxlAttachmentPoint(model->m_attachmentId), family);
    const bool main = eligibility.held == wxl::weaponflip::Hand::Main;
    const bool held = eligibility.held != wxl::weaponflip::Hand::None;
    const bool roll = eligibility.back ? (flags & 1) : (held && (flags & (main ? 2 : 4)));
    const bool reverse = held && (flags & (main ? 8 : 16));
    auto& m = model->m_matrixF4;
    // Source row-vector rotations: X preserves weapon length; Y reverses grip.
    // Neither transform changes the attachment translation row.
    if (roll) { m.b0=-m.b0; m.b1=-m.b1; m.b2=-m.b2; m.b3=-m.b3; m.c0=-m.c0; m.c1=-m.c1; m.c2=-m.c2; m.c3=-m.c3; }
    if (reverse) { m.a0=-m.a0; m.a1=-m.a1; m.a2=-m.a2; m.a3=-m.a3; m.c0=-m.c0; m.c1=-m.c1; m.c2=-m.c2; m.c3=-m.c3; }
}
