#pragma once
// Pure rules of WarcraftXL's wxl-npcbot-targets (GPL-3.0-or-later, Copyright (C) 2026 WarcraftXL):
// which creatures may stand in for a party/raid member, and which spells and target-type masks
// the dead-NPCBot resurrection path touches. Covered by tests/wxl_npcbot_targets_test.cpp.
#include <cstdint>

namespace wxl::npcbot {

// SpellRec target-type bits the resurrection path rewrites.
inline constexpr uint32_t kFriendlyCorpse = 0x8000;
inline constexpr uint32_t kDeadAlly = 0x100 | 0x400;

// NPCBots are server-grouped creatures with HighGuid::Unit. Pets and vehicles (0xF140 / 0xF150)
// and players keep their native rules; entry ranges and names are never identities.
inline bool IsCreatureGuid(uint64_t guid) { return (guid >> 48) == 0xF130; }

// Player class resurrection families (every rank) plus this server's Intercession (300331).
inline bool IsResurrectionSpell(uint32_t spell) {
    switch (spell) {
        case 2006: case 2010: case 10880: case 10881: case 20770: case 25435: case 48171:  // Resurrection
        case 2008: case 20609: case 20610: case 20776: case 20777: case 25590: case 49277:  // Ancestral Spirit
        case 7328: case 10322: case 10324: case 20772: case 20773: case 48949: case 48950:  // Redemption
        case 50769: case 50768: case 50767: case 50766: case 50765: case 50764: case 50763:  // Revive
        case 20484: case 20739: case 20742: case 20747: case 20748: case 26994: case 48477:  // Rebirth
        case 300331:                                                                          // Intercession
            return true;
        default:
            return false;
    }
}

// The friendly-corpse requirement becomes a dead-ally requirement for this one target.
inline uint32_t DeadBotTargetMask(uint32_t mask) { return (mask & ~kFriendlyCorpse) | kDeadAlly; }

}  // namespace wxl::npcbot
