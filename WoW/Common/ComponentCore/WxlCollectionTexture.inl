// Native collection texture requests. Included after the client types.
// The lifecycle fixture compiles this same implementation with native API stubs.
#ifndef WOW_COMMON_COMPONENTCORE_WXLCOLLECTIONTEXTURE_INL
#define WOW_COMMON_COMPONENTCORE_WXLCOLLECTIONTEXTURE_INL

struct WxlTexturePending {
    WxlEquipmentOwner owner;
    CM2Model* model = nullptr; // Strong reference until completion/cancellation.
    CTexture* texture = nullptr;
    uint32_t slot = 0;
    uint32_t display = 0;
    uint32_t type = 0;
    WxlWaistModel::RetryGate retry{};
    std::string path;

    ~WxlTexturePending() {
        if (texture) HandleClose(reinterpret_cast<HOBJECT>(texture));
        if (model) model->Release();
    }
};

static WxlEquipmentOwner SnapshotWxlEquipmentOwner(const CCharacterComponent* component) {
    CM2Model* parent = component->m_data.m_model;
    if (!parent) return {};
    return {parent, parent->m_shared, parent->m_shared ? parent->m_shared->m_data : nullptr, parent->m_scene,
        static_cast<uint32_t>(component->m_data.m_preferences.m_raceID),
        static_cast<uint32_t>(component->m_data.m_preferences.m_sexID), WxlProfileEnabled("WXL_COLLECTION_ARMOR", false),
        WxlEquipmentGeneration()};
}
void CCharacterComponent::QueueWxlTexture(CM2Model* model, ITEM_SLOT slot, uint32_t display,
                                         uint32_t type, const char* path) {
    if (!model || !path || !*path || std::strlen(path) >= 260 || slot >= NUM_ITEM_SLOTS) return;
    auto pending = std::make_unique<WxlTexturePending>();
    pending->path = path;
    pending->owner = SnapshotWxlEquipmentOwner(this);
    pending->slot = slot;
    pending->display = display;
    pending->type = type;
    const uint32_t now = OsGetAsyncTimeMs();
    pending->retry = {now, now, 0};
    // Hold before replacing any earlier request for this same model/type.
    ++model->m_refCount;
    pending->model = model;
    this->m_wxlTextures.erase(std::remove_if(this->m_wxlTextures.begin(), this->m_wxlTextures.end(),
        [model, type](const auto& old) { return old->model == model && old->type == type; }), this->m_wxlTextures.end());
    if (this->m_wxlTextures.size() >= 256) {
        WxlNativeDiagnostic("collection-armor: texture request limit reached for display=%u", display);
        return;
    }
    this->m_wxlTextures.push_back(std::move(pending));
}

void CCharacterComponent::CancelWxlTextures(uint32_t slot) {
    if (slot == NUM_ITEM_SLOTS) {
        this->m_wxlTextures.clear();
        return;
    }
    this->m_wxlTextures.erase(std::remove_if(this->m_wxlTextures.begin(), this->m_wxlTextures.end(),
        [slot](const auto& pending) { return pending->slot == slot; }), this->m_wxlTextures.end());
}

void CCharacterComponent::UpdateWxlTextures() {
    const auto owner = SnapshotWxlEquipmentOwner(this);
    if (!owner.model || !owner.collectionEnabled) {
        this->CancelWxlTextures();
        return;
    }
    const uint32_t now = OsGetAsyncTimeMs();
    size_t index = 0;
    uint32_t visited = 0;
    while (index < this->m_wxlTextures.size() && visited++ < 8) {
        auto& pending = *this->m_wxlTextures[index];
        bool finished = !(pending.owner == owner) || this->m_itemDisplayID[pending.slot] != pending.display ||
            pending.model->m_attachParent != this->m_data.m_model;
        if (!finished && pending.retry.Expired(now)) {
            WxlNativeDiagnostic("collection-armor: texture readiness timeout %s (display=%u type=%u)",
                                pending.path.c_str(), pending.display, pending.type);
            finished = true;
        }
        if (!finished) {
            if (!pending.texture && pending.retry.Due(now)) {
                CStatus status;
                pending.texture = CreateTexture(pending.path.c_str(), &status);
                if (!pending.texture) pending.retry.Failed(now);
            }
            if (pending.texture) {
                if (!WxlWaistModel::TextureMatches(pending.texture->m_filename, pending.path)) {
                    HandleClose(reinterpret_cast<HOBJECT>(pending.texture));
                    pending.texture = nullptr;
                    pending.retry.Failed(now);
                } else if ((pending.model->m_fFlags & 1) && TextureGetGxTex(pending.texture, 0, nullptr) &&
                           !pending.texture->m_asyncObject) {
                    pending.model->ReplaceTexture(pending.type, pending.texture);
                    WxlNativeDiagnostic("collection-armor: texture ready %s (display=%u type=%u)",
                                        pending.path.c_str(), pending.display, pending.type);
                    finished = true;
                }
            }
        }
        if (finished) this->m_wxlTextures.erase(this->m_wxlTextures.begin() + index);
        else ++index;
    }
    // Pending async requests must not starve later material types or slots.
    if (index && index < this->m_wxlTextures.size())
        std::rotate(this->m_wxlTextures.begin(), this->m_wxlTextures.begin() + index, this->m_wxlTextures.end());
}

#endif
