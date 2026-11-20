#ifndef WOW_SOURCE_UI_MACROCONDITIONS_H
#define WOW_SOURCE_UI_MACROCONDITIONS_H

#include <cstddef>
#include <cstdint>
#include <string_view>
#include "Engine/Source/Storm/H/stpl.h"
#include "WoW/Source/Object/WGUID.h"
#include "WoW/Source/Ui/MacroCondition.h"
#include "WoW/Source/Ui/MacroSubstring.h"

typedef int32_t (*MACROCONDITIONFUNC)(const WOWGUID* unit, const TSGrowableArray<MacroSubstring>* args);

class MacroConditions : public TSHashObject<MacroConditions, HASHKEY_STR> {
    public:
    /* 0018 */ MacroSubstring m_target{};
    /* 0020 */ TSGrowableArray<MacroCondition> m_conditions;

    void Parse();
    int32_t Evaluate(MacroSubstring* target);
};

void ClearMacroConditionCache();
int32_t GetMacroConditionType(std::string_view name);
int32_t EvaluateKnown(const WOWGUID*, const TSGrowableArray<MacroSubstring>* args);
int32_t EvaluateWrathTalent(const WOWGUID*, const TSGrowableArray<MacroSubstring>* args);
int32_t EvaluateTalentTree(const WOWGUID*, const TSGrowableArray<MacroSubstring>* args);
int32_t EvaluateGlyph(const WOWGUID*, const TSGrowableArray<MacroSubstring>* args);
int32_t EvaluateResting(const WOWGUID*, const TSGrowableArray<MacroSubstring>* args);
int32_t EvaluateOverrideBar(const WOWGUID*, const TSGrowableArray<MacroSubstring>* args);
int32_t EvaluatePossessBar(const WOWGUID*, const TSGrowableArray<MacroSubstring>* args);
int32_t EvaluateTimewalking(const WOWGUID*, const TSGrowableArray<MacroSubstring>* args);

int32_t EvaluateCombat(const WOWGUID* unit, const TSGrowableArray<MacroSubstring>* args);
int32_t EvaluateExists(const WOWGUID* unit, const TSGrowableArray<MacroSubstring>* args);
int32_t EvaluateHelp(const WOWGUID* unit, const TSGrowableArray<MacroSubstring>* args);
int32_t EvaluateHarm(const WOWGUID* unit, const TSGrowableArray<MacroSubstring>* args);
int32_t EvaluateParty(const WOWGUID* unit, const TSGrowableArray<MacroSubstring>* args);
int32_t EvaluateRaid(const WOWGUID* unit, const TSGrowableArray<MacroSubstring>* args);
int32_t EvaluateDead(const WOWGUID* unit, const TSGrowableArray<MacroSubstring>* args);
int32_t EvaluateFlyable(const WOWGUID* unit, const TSGrowableArray<MacroSubstring>* args);
int32_t EvaluateIndoors(const WOWGUID* unit, const TSGrowableArray<MacroSubstring>* args);
int32_t EvaluateOutdoors(const WOWGUID* unit, const TSGrowableArray<MacroSubstring>* args);
int32_t EvaluateSwimming(const WOWGUID* unit, const TSGrowableArray<MacroSubstring>* args);
int32_t EvaluateFlying(const WOWGUID* unit, const TSGrowableArray<MacroSubstring>* args);
int32_t EvaluateMounted(const WOWGUID* unit, const TSGrowableArray<MacroSubstring>* args);
int32_t EvaluateStealth(const WOWGUID* unit, const TSGrowableArray<MacroSubstring>* args);
int32_t EvaluateGroup(const WOWGUID* unit, const TSGrowableArray<MacroSubstring>* args);
int32_t EvaluateStance(const WOWGUID* unit, const TSGrowableArray<MacroSubstring>* args);
int32_t EvaluatePet(const WOWGUID* unit, const TSGrowableArray<MacroSubstring>* args);
int32_t EvaluateModifier(const WOWGUID* unit, const TSGrowableArray<MacroSubstring>* args);
int32_t EvaluateButton(const WOWGUID* unit, const TSGrowableArray<MacroSubstring>* args);
int32_t EvaluateActionBar(const WOWGUID* unit, const TSGrowableArray<MacroSubstring>* args);
int32_t EvaluateBonusBar(const WOWGUID* unit, const TSGrowableArray<MacroSubstring>* args);
int32_t EvaluateEquipped(const WOWGUID* unit, const TSGrowableArray<MacroSubstring>* args);
int32_t EvaluateChanneling(const WOWGUID* unit, const TSGrowableArray<MacroSubstring>* args);
int32_t EvaluateSpec(const WOWGUID* unit, const TSGrowableArray<MacroSubstring>* args);
int32_t EvaluateVehicleUI(const WOWGUID* unit, const TSGrowableArray<MacroSubstring>* args);
int32_t EvaluateUnitHasVehicleUI(const WOWGUID* unit, const TSGrowableArray<MacroSubstring>* args);
int32_t EvaluateCursor(const WOWGUID* unit, const TSGrowableArray<MacroSubstring>* args);

#endif
