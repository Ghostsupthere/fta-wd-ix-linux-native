local GetQuestsCompleted = GetQuestsCompleted
local QueryQuestsCompleted = QueryQuestsCompleted

local C_QuestLog = C_QuestLog or {}

function C_QuestLog.IsQuestFlaggedCompleted(QuestID)
	QueryQuestsCompleted()
	return GetQuestsCompleted()[QuestID] == true
end

if not C_QuestLog.GetMaxNumQuestsCanAccept then
	function C_QuestLog.GetMaxNumQuestsCanAccept()
		return GetQuestLogCapacity and GetQuestLogCapacity() or MAX_QUESTLOG_QUESTS
	end
end

-- Global
_G.C_QuestLog = C_QuestLog