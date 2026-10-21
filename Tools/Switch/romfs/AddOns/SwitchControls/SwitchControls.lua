-- Switch Controls: shows which Switch button does what, switches the controller to cursor mode while menus and windows
-- are open, and labels action buttons with the controller buttons that press them.
--
-- The Switch build (WoW.nro) installs this add-on and provides SwitchControls_GetMode / SetMode / IsDocked. Anywhere
-- else it does nothing. The button layout below has to match Engine/Source/Os/Switch/Input.cpp.

if not SwitchControls_GetMode then
    return
end

local MEDIA = "Interface\\AddOns\\SwitchControls\\Media\\"

local function Glyph(name, size)
    size = size or 18
    return "|T" .. MEDIA .. name .. ":" .. size .. ":" .. size .. "|t"
end

-- The keys controller buttons send in play mode, and the buttons that send them.
local KEY_GLYPHS = {
    ["1"] = "A",
    ["2"] = "X",
    ["3"] = "Y",
    ["4"] = "DpadRight",
    ["5"] = "DpadUp",
    ["6"] = "DpadLeft",
    ["7"] = "DpadDown",
    ["8"] = "L",
    ["TAB"] = "R",
    ["SPACE"] = "B",
    ["ESCAPE"] = "Plus",
    ["NUMLOCK"] = "LStickClick",
}

-- Held ZL is Shift and held ZR is Ctrl.
local MODIFIER_GLYPHS = {
    SHIFT = "ZL",
    CTRL = "ZR",
}

-- "CTRL-SHIFT-2" -> the glyphs of ZR, ZL and X; nil for a key no controller button sends.
local function KeyGlyphs(key, size)
    if not key then
        return nil
    end

    local modifiers = ""
    local base = key

    while true do
        local modifier, rest = base:match("^(%u+)%-(.+)$")

        if not modifier then
            break
        end

        local glyph = MODIFIER_GLYPHS[modifier]

        if not glyph then
            return nil
        end

        modifiers = modifiers .. Glyph(glyph, size)
        base = rest
    end

    local glyph = KEY_GLYPHS[base]

    if not glyph then
        return nil
    end

    return modifiers .. Glyph(glyph, size)
end

local frame = CreateFrame("Frame", "SwitchControlsFrame", UIParent)

---------------------------------------------------------------------------------------------------------------------
-- Hint bar: what the buttons do right now, along the top of the screen.

local HINTS = {
    play = {
        { "LStick", "Move" },
        { "RStick", "Look" },
        { "B", "Jump" },
        { "A", "X", "Y", "1 2 3" },
        { "Dpad", "4-7" },
        { "L", "8" },
        { "R", "Target" },
        { "ZL", "ZR", "Shift / Ctrl" },
        { "Plus", "Menu" },
        { "Minus", "Cursor" },
    },
    cursor = {
        { "RStick", "Pointer" },
        { "A", "Click" },
        { "B", "Right-click" },
        { "DpadUp", "DpadDown", "Scroll" },
        { "Y", "Chat" },
        { "X", "Controls" },
        { "Plus", "Close / Menu" },
        { "Minus", "Play" },
    },
    text = {
        { "A", "Enter" },
        { "B", "Cancel" },
        { "X", "Next field" },
        { "Y", "Keyboard" },
        { "Dpad", "Arrows" },
    },
}

local function HintText(hints)
    local parts = {}

    for _, hint in ipairs(hints) do
        local glyphs = ""

        for i = 1, #hint - 1 do
            glyphs = glyphs .. Glyph(hint[i])
        end

        table.insert(parts, glyphs .. " " .. hint[#hint])
    end

    return table.concat(parts, "     ")
end

local HINT_TEXT = {}

for state, hints in pairs(HINTS) do
    HINT_TEXT[state] = HintText(hints)
end

local hintBar = CreateFrame("Frame", "SwitchControlsHintBar", UIParent)
hintBar:SetFrameStrata("HIGH")
hintBar:SetHeight(28)
hintBar:SetPoint("TOP", UIParent, "TOP", 0, -2)
hintBar:SetBackdrop({
    bgFile = "Interface\\Tooltips\\UI-Tooltip-Background",
    edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border",
    tile = true,
    tileSize = 16,
    edgeSize = 12,
    insets = { left = 3, right = 3, top = 3, bottom = 3 },
})
hintBar:SetBackdropColor(0, 0, 0, 0.6)
hintBar:SetBackdropBorderColor(0.5, 0.5, 0.5, 0.8)
hintBar:EnableMouse(false)

local hintText = hintBar:CreateFontString(nil, "OVERLAY", "GameFontHighlight")
hintText:SetPoint("CENTER", hintBar, "CENTER", 0, 0)

local shownHints

local function ShowHints(state)
    if state == shownHints then
        return
    end

    shownHints = state
    hintText:SetText(HINT_TEXT[state])
    hintBar:SetWidth(hintText:GetStringWidth() + 24)
end

---------------------------------------------------------------------------------------------------------------------
-- Help panel: every control, shown the first time in the world, with X in cursor mode, or /switch.

local HELP = {
    { title = "Play mode" },
    { "LStick", "Move (forward, back, strafe)" },
    { "RStick", "Turn the camera and your character" },
    { "B", "Jump" },
    { "A", "X", "Y", "Action buttons 1, 2, 3" },
    { "DpadRight", "DpadUp", "DpadLeft", "DpadDown", "Action buttons 4, 5, 6, 7" },
    { "L", "Action button 8" },
    { "R", "Target the nearest enemy" },
    { "ZL", "ZR", "Hold for Shift / Ctrl: more bindings" },
    { "LStickClick", "Autorun" },
    { title = "Cursor mode" },
    { "RStick", "Move the pointer" },
    { "A", "ZR", "Left click (hold to drag)" },
    { "B", "ZL", "Right click" },
    { "DpadUp", "DpadDown", "Scroll" },
    { "Y", "Enter: open chat" },
    { "X", "This list" },
    { title = "Anywhere" },
    { "Minus", "RStickClick", "Switch between play and cursor mode" },
    { "Plus", "Menu, close windows, clear target" },
    { title = "Typing" },
    { "A", "Enter (send)" },
    { "B", "Cancel" },
    { "X", "Next field" },
    { "Y", "Open the keyboard again" },
}

local help = CreateFrame("Frame", "SwitchControlsHelp", UIParent)
help:SetFrameStrata("DIALOG")
help:SetWidth(470)
help:SetPoint("CENTER", UIParent, "CENTER", 0, 20)
help:SetBackdrop({
    bgFile = "Interface\\DialogFrame\\UI-DialogBox-Background",
    edgeFile = "Interface\\DialogFrame\\UI-DialogBox-Border",
    tile = true,
    tileSize = 32,
    edgeSize = 32,
    insets = { left = 11, right = 12, top = 12, bottom = 11 },
})
help:EnableMouse(true)
help:Hide()
table.insert(UISpecialFrames, "SwitchControlsHelp")

local helpTitle = help:CreateFontString(nil, "OVERLAY", "GameFontNormalLarge")
helpTitle:SetPoint("TOP", help, "TOP", 0, -20)
helpTitle:SetText("Switch Controls")

local helpClose = CreateFrame("Button", nil, help, "UIPanelCloseButton")
helpClose:SetPoint("TOPRIGHT", help, "TOPRIGHT", -6, -6)

do
    local y = -50

    for _, entry in ipairs(HELP) do
        if entry.title then
            y = y - 6
            local title = help:CreateFontString(nil, "OVERLAY", "GameFontNormal")
            title:SetPoint("TOPLEFT", help, "TOPLEFT", 26, y)
            title:SetText(entry.title)
            y = y - 20
        else
            local glyphs = ""

            for i = 1, #entry - 1 do
                glyphs = glyphs .. Glyph(entry[i], 20)
            end

            local keys = help:CreateFontString(nil, "OVERLAY", "GameFontHighlight")
            keys:SetPoint("TOPLEFT", help, "TOPLEFT", 36, y)
            keys:SetText(glyphs)

            local text = help:CreateFontString(nil, "OVERLAY", "GameFontHighlight")
            text:SetPoint("TOPLEFT", help, "TOPLEFT", 140, y - 3)
            text:SetText(entry[#entry])
            y = y - 22
        end
    end

    local footer = help:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall")
    footer:SetPoint("TOPLEFT", help, "TOPLEFT", 26, y - 8)
    footer:SetText("/switch shows this again. /switch hints turns the hint bar on or off.")

    help:SetHeight(-y + 44)
end

-- X in cursor mode sends F19 (Os/Switch/Input.cpp), which clicks this button.
local helpToggle = CreateFrame("Button", "SwitchControlsHelpToggle", UIParent)
helpToggle:SetScript("OnClick", function()
    if help:IsShown() then
        help:Hide()
    else
        help:Show()
    end
end)

---------------------------------------------------------------------------------------------------------------------
-- Cursor mode while menus and windows are open.

local PANEL_SIDES = { "left", "center", "right", "doublewide", "fullscreen" }

-- Windows that need the pointer but aren't UI panels (or load on demand, so may not exist yet).
local WINDOWS = {
    "GameMenuFrame", "InterfaceOptionsFrame", "VideoOptionsFrame", "AudioOptionsFrame", "KeyBindingFrame",
    "MacroFrame", "WorldMapFrame", "LootFrame", "ItemTextFrame", "TradeFrame", "HelpFrame", "ChatConfigFrame",
    "ColorPickerFrame", "StackSplitFrame", "DropDownList1", "SwitchControlsHelp", "StaticPopup1", "StaticPopup2",
    "StaticPopup3", "StaticPopup4", "GroupLootFrame1", "GroupLootFrame2", "GroupLootFrame3", "GroupLootFrame4",
    "BattlefieldFrame", "InspectFrame", "CalendarFrame", "AchievementFrame", "GuildBankFrame", "BarberShopFrame",
}

local function NeedsCursor()
    if GetUIPanel then
        for _, side in ipairs(PANEL_SIDES) do
            if GetUIPanel(side) then
                return true
            end
        end
    end

    for _, name in ipairs(WINDOWS) do
        local window = _G[name]

        if window and window:IsShown() then
            return true
        end
    end

    for i = 1, NUM_CONTAINER_FRAMES or 13 do
        local container = _G["ContainerFrame" .. i]

        if container and container:IsShown() then
            return true
        end
    end

    return false
end

-- auto: cursor mode is on because a window wanted it, and goes when the windows do. suppressed: the player went back to
-- play mode with a window open, so leave it until the windows close.
local auto = false
local suppressed = false

local function UpdateCursor(mode)
    local needs = NeedsCursor()

    if not needs then
        suppressed = false

        if auto then
            auto = false

            if mode == "cursor" then
                SwitchControls_SetMode("play")
                return "play"
            end
        end

        return mode
    end

    if mode == "play" then
        if auto then
            auto = false
            suppressed = true
        elseif not suppressed then
            SwitchControls_SetMode("cursor")
            auto = true
            return "cursor"
        end
    end

    return mode
end

---------------------------------------------------------------------------------------------------------------------
-- Action button labels: the controller buttons in place of the key names.

local function UpdateHotkey(button, actionButtonType)
    local name = button:GetName()
    local hotkey = name and _G[name .. "HotKey"]

    if not hotkey then
        return
    end

    local id = actionButtonType == "MULTICASTACTIONBUTTON" and button.buttonIndex or button:GetID()
    local key = GetBindingKey((actionButtonType or "ACTIONBUTTON") .. id) or GetBindingKey("CLICK " .. name .. ":LeftButton")
    local glyphs = KeyGlyphs(key, 14)

    if glyphs then
        hotkey:SetText(glyphs)
        hotkey:Show()
    end
end

hooksecurefunc("ActionButton_UpdateHotkeys", UpdateHotkey)

local BARS = {
    { "ActionButton", "ACTIONBUTTON" },
    { "MultiBarBottomLeftButton", "MULTIACTIONBAR1BUTTON" },
    { "MultiBarBottomRightButton", "MULTIACTIONBAR2BUTTON" },
    { "MultiBarRightButton", "MULTIACTIONBAR3BUTTON" },
    { "MultiBarLeftButton", "MULTIACTIONBAR4BUTTON" },
}

local function UpdateAllHotkeys()
    for _, bar in ipairs(BARS) do
        for i = 1, NUM_ACTIONBAR_BUTTONS or 12 do
            local button = _G[bar[1] .. i]

            if button then
                ActionButton_UpdateHotkeys(button, bar[2] ~= "ACTIONBUTTON" and bar[2] or nil)
            end
        end
    end
end

---------------------------------------------------------------------------------------------------------------------

local elapsedSinceUpdate = 0

frame:SetScript("OnUpdate", function(self, elapsed)
    elapsedSinceUpdate = elapsedSinceUpdate + elapsed

    if elapsedSinceUpdate < 0.1 then
        return
    end

    elapsedSinceUpdate = 0

    if not SwitchControlsDB then
        return
    end

    local mode, textFocus = SwitchControls_GetMode()
    mode = UpdateCursor(mode)

    if SwitchControlsDB.hints then
        ShowHints(textFocus and "text" or mode)
        hintBar:Show()
    else
        hintBar:Hide()
    end
end)

local entered = false

frame:RegisterEvent("ADDON_LOADED")
frame:RegisterEvent("PLAYER_ENTERING_WORLD")
frame:RegisterEvent("PLAYER_LOGOUT")
frame:RegisterEvent("UPDATE_BINDINGS")

frame:SetScript("OnEvent", function(self, event, arg1)
    if event == "ADDON_LOADED" and arg1 == "SwitchControls" then
        SwitchControlsDB = SwitchControlsDB or {}

        if SwitchControlsDB.hints == nil then
            SwitchControlsDB.hints = true
        end
    elseif event == "PLAYER_ENTERING_WORLD" then
        -- Only the first time: it also fires after every loading screen.
        if not entered then
            entered = true
            SetOverrideBindingClick(frame, false, "F19", "SwitchControlsHelpToggle")

            if not NeedsCursor() then
                SwitchControls_SetMode("play")
            end

            if not SwitchControlsDB.helpSeen then
                SwitchControlsDB.helpSeen = true
                help:Show()
            end
        end

        UpdateAllHotkeys()
    elseif event == "UPDATE_BINDINGS" then
        UpdateAllHotkeys()
    elseif event == "PLAYER_LOGOUT" then
        -- Back to the character screens, which need the pointer.
        SwitchControls_SetMode("cursor")
    end
end)

SLASH_SWITCHCONTROLS1 = "/switch"
SlashCmdList["SWITCHCONTROLS"] = function(arguments)
    if arguments and arguments:lower():match("hints") then
        SwitchControlsDB.hints = not SwitchControlsDB.hints
        DEFAULT_CHAT_FRAME:AddMessage("Switch Controls: hint bar " .. (SwitchControlsDB.hints and "on" or "off") .. ".")
    elseif help:IsShown() then
        help:Hide()
    else
        help:Show()
    end
end
