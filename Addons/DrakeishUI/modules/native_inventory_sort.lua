-- Bounded whole-stack sorting for ordinary carried bags only.
-- Each swap completes with an empty cursor; the next swap waits for server state.
local addon=select(2,...)
local I=addon.Inventory
local driver=CreateFrame("Frame")
local autoDriver=CreateFrame("Frame")
local nextAutoTry,autoDeadline
local interactionFrames={"BankFrame","MerchantFrame","TradeFrame","MailFrame","AuctionFrame","GuildBankFrame"}
local function read(bag,slot)
    local texture,count,locked,_,_,_,link=GetContainerItemInfo(bag,slot)
    if not texture then return false,locked end
    local name,_,quality,_,_,class,subclass=GetItemInfo(link or 0)
    if not link or not name then return nil,locked end
    local id=tonumber(string.match(link,"item:(%d+)"))
    if not id then return nil,locked end
    return {id=id,link=link,count=count or 1,name=name,class=class or "",subclass=subclass or "",quality=quality or 0},locked
end
local function same(a,b)
    if a==false or b==false then return a==b end
    return a and b and a.link==b.link and a.count==b.count
end
local function sameSortTarget(a,b)
    return a==b or (a and b and a.id==b.id)
end
local function inventoryInteractionOpen()
    for _,name in ipairs(interactionFrames) do
        local panel=_G[name]
        if panel and panel:IsShown() then return true end
    end
    return false
end
function I:StopSort(text)
    local state=self.sorting
    if state and state.ownedCursor and CursorHasItem() then ClearCursor() end
    self.sorting=nil;driver:SetScript("OnUpdate",nil)
    -- The swap acknowledgement can settle after the stock bag's last paint.
    if state then self:RefreshAfterSort(state.touchedBags,state.pending) end
    self:UpdateSortButtons()
    if text then self:SetStatus(text) end
end
local function advance()
    local state=I.sorting
    if not state then return end
    if InCombatLockdown() then I:StopSort("Sort stopped for combat.");return end
    if GetCursorInfo() then I:StopSort("Sort stopped: the cursor is in use.");return end
    if state.automatic and inventoryInteractionOpen() then I:StopSort();return end
    for bag,size in pairs(state.capacities) do
        if GetContainerNumSlots(bag)~=size then I:StopSort("Sort stopped: a bag was changed.");return end
    end
    if state.pending then
        local p=state.pending
        local a,lockedA=read(state.slots[p.a].bag,state.slots[p.a].slot)
        local b,lockedB=read(state.slots[p.b].bag,state.slots[p.b].slot)
        if lockedA or lockedB or not same(a,state.items[p.a]) or not same(b,state.items[p.b]) then
            if GetTime()>p.deadline then I:StopSort("Sort stopped while waiting for the server. Your items are kept; try again when unlocked.") end
            return
        end
        state.pending=nil
    end
    while state.index<=#state.slots do
        local index=state.index
        local goal=state.goal[index]
        local current=state.items[index]
        if sameSortTarget(goal,current) then state.index=index+1
        else
            local other
            for n=index+1,#state.slots do
                local item=state.items[n]
                if sameSortTarget(goal,item) then other=n;break end
            end
            if not other then I:StopSort("Sort stopped: inventory changed.");return end
            local a,b=state.slots[index],state.slots[other]
            local nowA,lockA=read(a.bag,a.slot)
            local nowB,lockB=read(b.bag,b.slot)
            if lockA or lockB then I:StopSort("Sort stopped: an item is locked. Try again shortly.");return end
            if not same(nowA,current) or not same(nowB,state.items[other]) then I:StopSort("Sort stopped: inventory changed.");return end
            -- Both nonempty stacks have different IDs. This cannot accidentally
            -- merge same-item stacks and leave a remainder attached to the cursor.
            local from,to=other,index
            if state.items[from]==false then from,to=to,from end
            local src,dst=state.slots[from],state.slots[to]
            local sourceItem=state.items[from]
            state.touchedBags[src.bag]=true
            state.touchedBags[dst.bag]=true
            local ok=pcall(function()
                PickupContainerItem(src.bag,src.slot)
                state.ownedCursor=true
                local kind,id=GetCursorInfo()
                if kind~="item" or id~=sourceItem.id then error("pickup refused") end
                PickupContainerItem(dst.bag,dst.slot)
                if CursorHasItem() then PickupContainerItem(src.bag,src.slot) end
                if CursorHasItem() then error("swap incomplete") end
                state.ownedCursor=false
            end)
            if not ok then I:StopSort("Sort stopped: an item could not move. Any held item was returned.");return end
            state.items[index],state.items[other]=state.items[other],state.items[index]
            state.pending={a=index,b=other,deadline=GetTime()+3}
            state.index=index+1
            I:SetStatus("Sorting whole stacks... click Sort again to cancel.")
            return
        end
    end
    I:StopSort("Sorting complete. Excluded and specialty bags were left in place.")
end
function I:StartSort(automatic)
    if not automatic then self:CancelAutoSort() end
    if self.sorting then
        if not automatic then self:StopSort("Sort cancelled.") end
        return
    end
    if InCombatLockdown() then
        if not automatic then self:SetStatus("Leave combat before sorting.") end
        return
    end
    if GetCursorInfo() or (StackSplitFrame and StackSplitFrame.IsShown and StackSplitFrame:IsShown()) then
        if automatic then return "retry" end
        self:SetStatus("Put down what you are moving before sorting.");return
    end
    if not automatic and not self.carriedOpen then
        OpenAllBags(true)
        self:CancelAutoSort()
    end
    local db=addon.Layout:DB()
    local state={slots={},items={},goal={},capacities={},touchedBags={},index=1,automatic=automatic}
    for bag=0,4 do
        local _,family=GetContainerNumFreeSlots(bag)
        local size=GetContainerNumSlots(bag)
        if (family or 0)==0 and not db.excludeSort[bag] then
            state.capacities[bag]=size
            for slot=1,size do
                local item,locked=read(bag,slot)
                if locked or item==nil then
                    if automatic then return "retry" end
                    self:SetStatus(locked and "An item is locked. Try sorting again shortly."
                        or "Waiting for item information. Try sorting again shortly.")
                    return
                end
                state.slots[#state.slots+1]={bag=bag,slot=slot}
                state.items[#state.items+1]=item
                state.goal[#state.goal+1]=item
            end
        end
    end
    local mode=db.sortMode
    table.sort(state.goal,function(a,b)
        if a==false then return false end
        if b==false then return true end
        if mode=="Name" and a.name~=b.name then return a.name<b.name end
        if mode=="Quality" and a.quality~=b.quality then return a.quality>b.quality end
        if a.class~=b.class then return a.class<b.class end
        if a.quality~=b.quality then return a.quality>b.quality end
        if a.subclass~=b.subclass then return a.subclass<b.subclass end
        if a.name~=b.name then return a.name<b.name end
        return a.id<b.id
    end)
    if automatic then
        local needsSort
        for index,item in ipairs(state.items) do
            if not sameSortTarget(item,state.goal[index]) then needsSort=true;break end
        end
        if not needsSort then return end
    end
    self.sorting=state
    self:UpdateSortButtons()
    local elapsed=0
    driver:SetScript("OnUpdate",function(_,delta)
        elapsed=elapsed+delta
        if elapsed<.03 then return end
        elapsed=0;advance()
    end)
    self:SetStatus("Sorting whole stacks... click Sort again to cancel.")
end
function I:CancelAutoSort()
    autoDriver:SetScript("OnUpdate",nil)
end
local function tryAutoSort()
    local now=GetTime()
    if now<nextAutoTry then return end
    if now>=autoDeadline or not I.carriedOpen or not addon.Layout:DB().bagAutoSort or inventoryInteractionOpen()
            or InCombatLockdown() or I:StartSort(true)~="retry" then
        I:CancelAutoSort()
    else
        nextAutoTry=now+.25
    end
end
function I:QueueAutoSort()
    if not addon.Layout:DB().bagAutoSort or inventoryInteractionOpen() or InCombatLockdown() then return end
    local now=GetTime()
    nextAutoTry,autoDeadline=now+.35,now+3
    autoDriver:SetScript("OnUpdate",tryAutoSort)
end
driver:RegisterEvent("PLAYER_REGEN_DISABLED")
driver:RegisterEvent("PLAYER_LEAVING_WORLD")
driver:SetScript("OnEvent",function()
    I:CancelAutoSort()
    if I.sorting then I:StopSort("Sort stopped.") end
end)
