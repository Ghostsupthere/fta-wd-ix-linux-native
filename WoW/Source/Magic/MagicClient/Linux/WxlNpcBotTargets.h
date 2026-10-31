#ifndef WOW_SOURCE_MAGIC_MAGICCLIENT_LINUX_WXLNPCBOTTARGETS_H
#define WOW_SOURCE_MAGIC_MAGICCLIENT_LINUX_WXLNPCBOTTARGETS_H

// Native port of WarcraftXL's wxl-npcbot-targets (party/raid targeting of server-grouped NPCBots,
// e.g. Prayer of Mending) and its wxl-npcbot-resurrection half (dead grouped bots as targets of the
// class resurrection spells). The Windows module detours five client functions; here the
// reconstructed functions call these helpers directly:
//   CGUnit_C::IsInMyPartyOrRaid (0x718CA0) / IsInMyParty (0x718D70)  -> WxlNpcBotGroupedMember
//   Spell_C_RangeCheckSelected (0x809610)                            -> WxlNpcBotResurrectionRangeView
//   Spell_C_HandleSpriteClick (0x80BC80) / Spell_C::HandleSpriteRay (0x803A50)
//                                                                    -> WxlNpcBotResurrectionMaskFor
// The original result is always produced first; a creature is added only when its GUID is in the
// current native roster and the other side is the active player.

#if defined(WOWDECOMP_LINUX)

#include <cstdint>

class CGObject_C;
class CGUnit_C;
class PendingSpellCast;
class SpellRec;
struct WOWGUID;

// `raid` selects the party-or-raid roster (the spell's raid-member check) over the party roster.
bool WxlNpcBotGroupedMember(const CGUnit_C* self, const CGUnit_C* other, bool raid);

// A call-local spell record whose friendly-corpse requirement is a dead-ally one when `caster`
// is the local player resurrecting a dead grouped bot with `spell`; otherwise `spell` itself.
const SpellRec* WxlNpcBotResurrectionRangeView(CGUnit_C* caster, const SpellRec* spell, WOWGUID target, SpellRec& storage);

// The target-type mask to present while the native click/hover validators run for `target` and the
// pending cast, or `original` when this is not a dead grouped bot being resurrected.
uint32_t WxlNpcBotResurrectionMaskFor(uint32_t original, const CGObject_C* target, const PendingSpellCast* pending);

#endif  // WOWDECOMP_LINUX

#endif
