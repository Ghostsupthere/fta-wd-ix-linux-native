// Compile the production queue against small native handle/model API stubs.
#include <cassert>
#include <cstring>
#include <map>
#include <memory>
#include <vector>
#include "WoW/Common/ComponentCore/WxlEquipmentOwner.h"
#include "WoW/Common/ComponentCore/WxlWaistModel.h"

using ITEM_SLOT = uint32_t;
constexpr uint32_t NUM_ITEM_SLOTS = 12;
using HOBJECT = void*;
struct CStatus {};
static uint32_t now = 0, created = 0, liveTextures = 0;
static bool enabled = true, fallback = false, asynchronous = false;
struct CTexture {
    std::string m_filename;
    void* m_asyncObject;
    uint32_t references = 1;
    explicit CTexture(std::string path) : m_filename(std::move(path)),
        m_asyncObject(asynchronous ? this : nullptr) { ++liveTextures; }
    ~CTexture() { --liveTextures; }
};
static void HandleClose(HOBJECT handle) {
    auto* texture = static_cast<CTexture*>(handle);
    assert(texture->references);
    if (!--texture->references) delete texture;
}
static CTexture* CreateTexture(const char* path, CStatus*) {
    ++created;
    std::string name = fallback ? "SolidTexture" : path;
    if (!fallback) name.resize(name.size() - 4); // Native cache omits .blp.
    return new CTexture(name);
}
static void* TextureGetGxTex(CTexture* texture, int, void*) { return texture; }
static uint32_t OsGetAsyncTimeMs() { return now; }
static bool WxlProfileEnabled(const char*, bool) { return enabled; }
static void WxlNativeDiagnostic(const char*, ...) {}

struct Shared { void* m_data; };
struct CM2Model {
    uint32_t m_refCount = 1, m_fFlags = 1;
    CM2Model* m_attachParent = nullptr;
    Shared* m_shared = nullptr;
    void* m_scene = nullptr;
    std::map<uint32_t, CTexture*> installed;
    ~CM2Model() { Clear(); }
    void Clear() { for (auto pair : installed) HandleClose(pair.second); installed.clear(); }
    void Release() { assert(m_refCount); if (!--m_refCount) Clear(); }
    void ReplaceTexture(uint32_t type, CTexture* texture) {
        if (installed.count(type)) HandleClose(installed[type]);
        ++texture->references;
        installed[type] = texture;
    }
};
struct WxlTexturePending;
struct CCharacterComponent {
    struct {
        CM2Model* m_model;
        struct { uint32_t m_raceID = 1, m_sexID = 0; } m_preferences;
    } m_data{};
    uint32_t m_itemDisplayID[NUM_ITEM_SLOTS]{};
    std::vector<std::unique_ptr<WxlTexturePending>> m_wxlTextures;
    void QueueWxlTexture(CM2Model*, ITEM_SLOT, uint32_t, uint32_t, const char*);
    void CancelWxlTextures(uint32_t = NUM_ITEM_SLOTS);
    void UpdateWxlTextures();
};
#include "WoW/Common/ComponentCore/WxlCollectionTexture.inl"

int main() {
    int skeleton = 0, scene = 0;
    Shared shared{&skeleton};
    CM2Model parent, child;
    parent.m_shared = &shared; parent.m_scene = &scene;
    child.m_attachParent = &parent;
    CCharacterComponent component;
    component.m_data.m_model = &parent;
    component.m_itemDisplayID[0] = 123;
    auto queue = [&](uint32_t type, const char* path = "Item\\armor.blp") {
        component.QueueWxlTexture(&child, 0, 123, type, path);
    };

    queue(2, "Item\\first.blp"); queue(2, "Item\\last.blp");
    assert(component.m_wxlTextures.size() == 1 && child.m_refCount == 2 && !created);
    asynchronous = true;
    component.UpdateWxlTextures();
    assert(created == 1 && child.installed.empty());
    component.m_wxlTextures[0]->texture->m_asyncObject = nullptr;
    component.UpdateWxlTextures();
    assert(component.m_wxlTextures.empty() && child.m_refCount == 1);
    assert(child.installed[2]->m_filename == "Item\\last");

    asynchronous = false; fallback = true;
    queue(2);
    component.UpdateWxlTextures();
    assert(child.installed[2]->m_filename == "Item\\last" && liveTextures == 1);
    const auto attempts = created;
    component.UpdateWxlTextures(); assert(created == attempts);
    now = 50; fallback = false;
    component.UpdateWxlTextures();
    assert(component.m_wxlTextures.empty() && child.installed[2]->m_filename == "Item\\armor");

    // Held child and texture references survive native detachment, then retire.
    asynchronous = true; queue(3); component.UpdateWxlTextures();
    child.m_attachParent = nullptr; child.Release();
    component.UpdateWxlTextures();
    assert(component.m_wxlTextures.empty() && child.m_refCount == 0 && liveTextures == 0);
    child.m_refCount = 1; child.m_attachParent = &parent;

    queue(2); ++component.m_itemDisplayID[0]; component.UpdateWxlTextures();
    assert(component.m_wxlTextures.empty()); component.m_itemDisplayID[0] = 123;
    queue(2); ++component.m_data.m_preferences.m_sexID; component.UpdateWxlTextures();
    assert(component.m_wxlTextures.empty());
    queue(2); WxlInvalidateWorldEquipment(); component.UpdateWxlTextures();
    assert(component.m_wxlTextures.empty());
    queue(2); enabled = false; component.UpdateWxlTextures(); enabled = true;
    assert(component.m_wxlTextures.empty() && child.m_refCount == 1);

    // Eight async requests per visit; rotation reaches every queued material.
    const auto before = created;
    for (uint32_t i = 0; i < 16; ++i) queue(i);
    component.UpdateWxlTextures(); assert(created == before + 8);
    component.UpdateWxlTextures(); assert(created == before + 16);
    now += 10000;
    component.UpdateWxlTextures(); component.UpdateWxlTextures();
    assert(component.m_wxlTextures.empty() && child.m_refCount == 1 && liveTextures == 0);

    for (uint32_t i = 0; i < 257; ++i) queue(i);
    assert(component.m_wxlTextures.size() == 256 && child.m_refCount == 257);
    component.CancelWxlTextures(1); assert(component.m_wxlTextures.size() == 256);
    component.CancelWxlTextures(0);
    assert(component.m_wxlTextures.empty() && child.m_refCount == 1 && liveTextures == 0);
}
