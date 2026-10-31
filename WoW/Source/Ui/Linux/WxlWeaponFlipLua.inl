// Native Lua surface matching the supplied Transmogrification weapon options.
int WeaponFlipAvailable(lua_State* L) { lua_pushboolean(L, NativeWeaponFeatureEnabled()); return 1; }
CM2Model* RemoteEquipmentRoot(uint64_t guid) {
    auto* player = static_cast<CGPlayer_C*>(ClntObjMgrObjectPtr(WOWGUID{guid}, TYPE_PLAYER, __FILE__, __LINE__));
    return player ? player->m_worldModel : nullptr;
}
template<uint32_t Mode> int SetFlip(lua_State* L) {
    const double value = lua_tonumber(L, 1);
    const bool accepted = NativeWeaponFeatureEnabled() && lua_isnumber(L, 1) && std::isfinite(value);
    if (accepted) SetNativeWeaponFlip(Mode, value != 0);
    lua_pushboolean(L, accepted); lua_pushboolean(L, GetNativeWeaponFlip(Mode)); return 2;
}
template<uint32_t Mode> int FlipEligible(lua_State* L) {
    lua_pushboolean(L, (NativeWeaponEligibility() & Mode) != 0); return 1;
}
int RefreshWeaponPlacement(lua_State* L) {
    // Every animation pass starts from a fresh attachment transform. Eligibility
    // is inspected live rather than retaining a callback/instance cache.
    lua_pushboolean(L, true); return 1;
}
uint64_t RemoteGuid(lua_State* L) {
    uint32_t halves[2]{};
    for (int i = 0; i < 2; ++i) {
        if (!lua_isnumber(L, i+1)) return 0;
        const double value = lua_tonumber(L, i+1);
        if (!std::isfinite(value) || value < 0 || value > UINT32_MAX || std::floor(value) != value) return 0;
        halves[i] = static_cast<uint32_t>(value);
    }
    const uint64_t guid = (uint64_t(halves[1]) << 32) | halves[0];
    return guid == ClntObjMgrGetActivePlayer().m_guid ? 0 : guid;
}
int SetRemoteBack(lua_State* L) {
    const auto guid = RemoteGuid(L);
    const double value = lua_tonumber(L, 3);
    const bool accepted = guid && lua_isnumber(L, 3) && std::isfinite(value) &&
        SetNativeRemoteWeaponFlags(guid, false, value != 0 ? 1 : 0);
    lua_pushboolean(L, accepted); return 1;
}
int SetRemoteHands(lua_State* L) {
    const auto guid = RemoteGuid(L);
    bool valid = guid != 0; uint32_t flags = 0;
    for (int index = 3; index <= 6; ++index) {
        if (index > 4 && lua_isnoneornil(L, index)) continue;
        const double value = lua_tonumber(L, index);
        valid = valid && lua_isnumber(L, index) && std::isfinite(value);
        if (value != 0) flags |= 1u << (index - 2);
    }
    lua_pushboolean(L, valid && SetNativeRemoteWeaponFlags(guid, true, flags)); return 1;
}
template<bool Hands> int BeginRemote(lua_State* L) { BeginNativeRemoteWeaponSync(Hands); lua_pushboolean(L,true); return 1; }
template<bool Hands> int EndRemote(lua_State* L) { EndNativeRemoteWeaponSync(Hands); lua_pushboolean(L,true); return 1; }
template<bool Hands> int ClearRemote(lua_State* L) { ClearNativeRemoteWeaponSync(Hands); lua_pushboolean(L,true); return 1; }
