#ifndef WOW_SOURCE_UI_LINUX_WXLNPCBOTENCHANT_H
#define WOW_SOURCE_UI_LINUX_WXLNPCBOTENCHANT_H

// Native port of the client half of WarcraftXL's wxl-npcbot-enchant: the NPCBot Paperdoll add-on
// asks which enchant is on the item-targeting cursor (WarcraftXL_GetPendingItemEnchant) and cancels
// exactly that cursor after it has used it (WarcraftXL_CancelPendingItemEnchant). The stock
// SpellStopTargeting Lua function rejects tainted add-on calls even outside combat, so the cancel
// goes straight to the native cancellation it performs.

#if defined(WOWDECOMP_LINUX)

void WxlRegisterNpcBotEnchantLua();

#endif  // WOWDECOMP_LINUX

#endif
