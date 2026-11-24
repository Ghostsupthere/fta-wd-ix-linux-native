#!/usr/bin/env python3
"""Exercise production hardening guards and local processing trials without a client session."""
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile

here = Path(__file__).resolve().parent
root = here.parents[1]


def portion(file, start, stop, include_stop=False):
    text = (root / file).read_text()
    begin = text.index(start)
    end = text.index(stop, begin + len(start))
    if include_stop:
        end += len(stop)
    return text[begin:end] + "\n"


def main():
    area = "WoW/Source/WorldClient/MapArea.cpp"
    spells = "WoW/Source/Magic/MagicClient/SpellVisuals.cpp"
    network = "WoW/Source/Net/NetClient/NetClient.cpp"
    with tempfile.TemporaryDirectory(prefix="port-readiness-") as tmp:
        work = Path(tmp)
        (work / "hardening.inc").write_text(
            portion("WoW/Source/Ui/GameUI.cpp", "void CGGameUI::SetUIScale(", "\n}", True)
            + portion("Engine/Source/Model2/M2Model.cpp", "M2SequenceFallback* CM2Model::SequenceFallbackById(", "\n}", True)
            + portion("WoW/Common/ComponentCore/CharacterComponent.cpp", "int32_t CCharacterComponent::UpdateTextureSlot(", "\n}", True)
        )
        (work / "terrain_prepare.inc").write_text(
            portion(area, "CMapChunk* CMapArea::PrepareChunk", "\n    int32_t index").split("{", 1)[1]
        )
        (work / "terrain_teardown.inc").write_text(
            portion(area, "        const auto x = static_cast<uint32_t>(chunk->m_aIndex.x);", "        CMap::FreeBaseObjLink(link);")
        )
        (work / "lightning_guard.inc").write_text(
            portion(spells, "        Bolt& bolt = this->m_bolts[i];", "        CGObject_C* source")
                .replace("this->m_bolts[i]", "this->bolt")
        )
        (work / "network.inc").write_text(
            portion(network, "void NETEVENTQUEUE::PollBatch()", "// VERIFY:")
            + portion(network, "void NETEVENTQUEUE::ReleaseNode(", "// VERIFY: 75864678")
                .replace("void* connection, NetClient* client", "void*, NetClient* client")
        )
        (work / "batch2.inc").write_text(
            portion("WoW/Source/Ui/ReputationInfo_Scripts.cpp", "static uint32_t GetFactionListIndex(", "\n}", True)
            + portion("WoW/Source/Ui/RuneInfo.cpp", "bool CGRuneInfo::IsRuneIndexValid(", "// VERIFY: c02488c7")
            + portion("WoW/Source/Ui/BattlefieldInfo.cpp", "int32_t Packet_MSG_BATTLEGROUND_PLAYER_POSITIONS(", "\n}", True)
                .replace("void* param, NETMESSAGE msgId, uint32_t time,", "void*, NETMESSAGE, uint32_t,")
            + portion("Engine/Source/BLPFile/blp.cpp", "int32_t CBLPFile::Open(", "\n}", True)
        )
        (work / "preview_guard.inc").write_text(
            portion("WoW/Source/Glue/CharacterSelection.cpp", "        auto* scene = CCharacterSelection::m_modelFrame->GetScene();", "        data.m_flags |= 0x2;")
        )
        (work / "pet_guard.inc").write_text(
            portion("WoW/Source/Ui/PetInfo.cpp", "        CVehicle_C* vehicle = nullptr;\n        if (s_pets.m_count", "            CGActionBar::UpdateTemporaryActionBar")
        )
        (work / "name_guard.inc").write_text(
            portion("WoW/Source/Game/GameClient/PlayerName.cpp", "    if (!(this->m_flags & 0x4) && (!object->m_worldModel", "\n    for ", False)
                .replace("this->m_flags", "flags").replace("return;", "return false;")
        )
        ui = "WoW/Source/Ui/"
        objects = "WoW/Source/Object/ObjectClient/"
        (work / "corpse-init.inc").write_text(
            portion(objects + "Corpse_C.cpp", "    this->CGObject_C::PostInit(time, create, a3);", "    int32_t useComponent")
                .replace("    this->CGObject_C::PostInit(time, create, a3);\n\n", "")
        )
        (work / "corpse-loot.inc").write_text(
            portion(objects + "Corpse_C.cpp", "void CGCorpse_C::ShowLootEffect(", "\n}", True)
        )
        (work / "corpse-replace.inc").write_text(
            portion(objects + "Corpse_C.cpp", "void CGCorpse_C::OnFlagChanged(", "\n}", True)
                .replace("uint32_t oldFlags", "uint32_t")
        )
        (work / "vehicle-remove.inc").write_text(
            portion(objects + "Vehicle_C.cpp", "void CVehicle_C::RemoveAllPassengersLocally(", "\n}", True)
        )
        (work / "combat-death.inc").write_text(
            portion(objects + "UnitCombatLog_C.cpp", "void UnitCombatLogUnitDead(", "\n}", True)
        )
        (work / "poi-objective.inc").write_text(
            portion(ui + "QuestLog.h", "inline bool QuestPOIObjectiveMatches(", "\n}", True)
        )
        (work / "commentator-roster.inc").write_text(
            portion(ui + "CommentatorFrame.cpp", "int32_t CGCommentator::OnReceivePlayerInfo(", "\n}", True)
                .replace("void* param, NETMESSAGE msgId, uint32_t time,", "void*, NETMESSAGE, uint32_t,")
        )
        (work / "commentator-faction.inc").write_text(
            portion(ui + "CommentatorFrame_Scripts.cpp", "    if (faction >= std::size(instance->m_numPlayers))",
                "    WDataStore msg;")
        )
        (work / "gossip.inc").write_text(
            portion(ui + "GossipInfo.cpp", "int32_t Packet_SMSG_GOSSIP_MESSAGE(", "\n}", True)
                .replace("void* param, NETMESSAGE msgId, uint32_t time,", "void*, NETMESSAGE, uint32_t,")
        )
        trade = ui + "TradeSkillFrame.cpp"
        (work / "tradeskill-callback.inc").write_text(
            portion(trade, "static std::unordered_set<int32_t> s_deniedRecipeItems;", "\n}", True)
                .replace("const WOWGUID& guid, void* param,", "const WOWGUID&, void*,")
        )
        (work / "tradeskill-metadata.inc").write_text(
            portion(trade, "                const int32_t itemId = spell.m_effectItemType[0];",
                "            } else {\n                info->m_itemClass = -1;")
        )
        (work / "tradeskill-link.inc").write_text(
            portion(trade, "static const char s_base64Encode[]", "\n", True)
            + portion(trade, "static bool IsTradeSkillLinkPayload(", "\n}", True)
        )
        for name in ("hardening", "processing", "network", "network_profile", "batch2", "batch3"):
            executable = work / name
            subprocess.run([*shlex.split(os.environ.get("CXX", "g++")), "-std=c++17", "-O2", "-UNDEBUG",
                "-Wall", "-Wextra", "-Werror", "-pthread", "-I", str(root), "-I", str(work),
                str(here / (name + ".cpp")), "-o", str(executable)], check=True)
            subprocess.run([executable, *(sys.argv[1:] if name == "batch2" else [])], check=True)


if __name__ == "__main__":
    main()
