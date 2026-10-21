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
    ["F17"] = "RStickClick",
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
-- Building blocks. Button icons are textures next to their labels, not |T escapes inside text, and labels use
-- Blizzard's font objects (the ones the default UI draws with) rather than fonts set by hand: on the Switch, text set up
-- either other way didn't show. Frames are scaled up as a whole for the handheld screen.

local FONTS = {
    title = "GameFontNormalHuge",      -- gold, large
    heading = "GameFontNormalLarge",   -- gold
    text = "GameFontHighlightLarge",   -- white
    hint = "GameFontDisableLarge",     -- grey
}

-- Every label, for the diagnostic below.
local labels = {}

local function Label(parent, style)
    local label = parent:CreateFontString(nil, "OVERLAY", FONTS[style])
    table.insert(labels, label)
    return label
end

-- Logs/Console.txt on the Switch: each label's font and measured width, so text that doesn't show can be told apart
-- from a font that didn't load (width 0) or text that isn't drawn.
local function LogLabels()
    if not SwitchControls_Log then
        return
    end

    local shown = 0

    for _, label in ipairs(labels) do
        local font, size, flags = label:GetFont()
        local r, g, b, a = label:GetTextColor()

        if shown < 12 then
            SwitchControls_Log(string.format("label %q font %s %s %s width %.1f height %.1f color %.2f %.2f %.2f %.2f alpha %.2f visible %s", label:GetText() or "", tostring(font), tostring(size), tostring(flags), label:GetStringWidth() or -1, label:GetHeight() or -1, r or -1, g or -1, b or -1, a or -1, label:GetEffectiveAlpha() or -1, tostring((label:IsVisible()))))
            shown = shown + 1
        end
    end

    local bar = SwitchControlsHintBar
    SwitchControls_Log(string.format("%d labels; hint bar %.0fx%.0f scale %.2f shown %s", #labels, bar:GetWidth() or -1, bar:GetHeight() or -1, bar:GetEffectiveScale() or -1, tostring((bar:IsVisible()))))
end

-- Icons for the given glyph names in a row from the left of the anchor; returns the row's width.
local function Icons(parent, glyphs, size, anchor, x, y)
    for i, name in ipairs(glyphs) do
        local icon = parent:CreateTexture(nil, "OVERLAY")
        icon:SetTexture(MEDIA .. name)
        icon:SetWidth(size)
        icon:SetHeight(size)
        icon:SetPoint("LEFT", anchor, "LEFT", x + (i - 1) * (size - 2), y)
    end

    return #glyphs * (size - 2) + 2
end

-- A row of text and icons from the left of the anchor, at x: items are strings or { glyph }. Returns the new x.
local function Sequence(parent, items, size, style, anchor, x)
    for _, item in ipairs(items) do
        if type(item) == "table" then
            x = x + Icons(parent, item, size, anchor, x, 0) + 4
        else
            local label = Label(parent, style)
            label:SetText(item)
            label:SetPoint("LEFT", anchor, "LEFT", x, 0)
            x = x + label:GetStringWidth() + 6
        end
    end

    return x
end

local function Split(entry)
    local glyphs = {}

    for i = 1, #entry - 1 do
        glyphs[i] = entry[i]
    end

    return glyphs, entry[#entry]
end

---------------------------------------------------------------------------------------------------------------------
-- Hint bar: the mode and what the buttons do in it, along the top of the screen.

local HINTS = {
    play = {
        title = "PLAY",
        { "LStick", "Move" },
        { "RStick", "Look" },
        { "L", "RStick", "Zoom" },
        { "B", "Jump" },
        { "R", "Target" },
        { "RStickClick", "Interact" },
        { "Minus", "Cursor" },
    },
    cursor = {
        title = "CURSOR",
        { "RStick", "Pointer" },
        { "A", "Click" },
        { "B", "Right-click" },
        { "Y", "Chat" },
        { "Minus", "Play" },
    },
    bind = {
        title = "KEY BINDING",
        { "A", "Any button to bind it" },
        { "ZL", "Shift" },
        { "ZR", "Ctrl" },
        { "Plus", "Cancel" },
    },
    text = {
        title = "TYPING",
        { "A", "Enter" },
        { "B", "Cancel" },
        { "X", "Next field" },
        { "Y", "Keyboard" },
    },
}

-- Narrow enough to sit between the target frame and the minimap at the top of a 720p screen.
local HINT_ICON = 22
local HINT_SCALE = 1.0

-- The bar dims after a few seconds in the same mode and comes back when the mode changes.
local HINT_DIM_AFTER = 4
local HINT_DIM_ALPHA = 0.35

local hintBar = CreateFrame("Frame", "SwitchControlsHintBar", UIParent)
hintBar:SetFrameStrata("HIGH")
hintBar:SetScale(HINT_SCALE)
hintBar:SetHeight(HINT_ICON + 14)
hintBar:SetPoint("TOP", UIParent, "TOP", 0, -2)
hintBar:SetBackdrop({
    bgFile = "Interface\\Tooltips\\UI-Tooltip-Background",
    edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border",
    tile = true,
    tileSize = 16,
    edgeSize = 14,
    insets = { left = 4, right = 4, top = 4, bottom = 4 },
})
hintBar:SetBackdropColor(0.05, 0.05, 0.08, 0.85)
hintBar:SetBackdropBorderColor(0.6, 0.6, 0.6, 0.9)

-- Tapping the bar (or clicking it in cursor mode) opens the controls list.
hintBar:EnableMouse(true)

-- One row per state, built once; the bar shows one at a time and takes its width.
local hintRows = {}

for state, hints in pairs(HINTS) do
    local row = CreateFrame("Frame", nil, hintBar)
    row:SetAllPoints(hintBar)
    row:Hide()

    local x = 14
    local title = Label(row, "heading")
    title:SetText(hints.title)
    title:SetPoint("LEFT", row, "LEFT", x, 0)
    x = x + title:GetStringWidth() + 20

    for _, hint in ipairs(hints) do
        local glyphs, text = Split(hint)
        x = x + Icons(row, glyphs, HINT_ICON, row, x, 0) + 4

        local label = Label(row, "text")
        label:SetText(text)
        label:SetPoint("LEFT", row, "LEFT", x, 0)
        x = x + label:GetStringWidth() + 18
    end

    -- How to get the full list: hold Minus, or tap the bar. (The action bars show their own buttons.)
    x = Sequence(row, { "Hold", { "Minus" }, "help" }, HINT_ICON, "hint", row, x + 6)

    row.width = x + 8
    hintRows[state] = row
end

local shownHints
local shownSince = 0

local function ShowHints(state)
    if state == shownHints then
        local age = GetTime() - shownSince
        local alpha = 1

        if age > HINT_DIM_AFTER then
            alpha = math.max(HINT_DIM_ALPHA, 1 - (age - HINT_DIM_AFTER) * 2 * (1 - HINT_DIM_ALPHA))
        end

        hintBar:SetAlpha(alpha)
        return
    end

    if shownHints then
        hintRows[shownHints]:Hide()
    end

    shownHints = state
    shownSince = GetTime()
    hintRows[state]:Show()
    hintBar:SetWidth(hintRows[state].width)
    hintBar:SetAlpha(1)
end

---------------------------------------------------------------------------------------------------------------------
-- Controls list: every control, in two columns. Opens the first time in the world, and on holding Minus, tapping the
-- hint bar or X in cursor mode.

local HELP_COLUMNS = {
    {
        { title = "Play mode" },
        { "LStick", "Move" },
        { "RStick", "Turn camera and character" },
        { "B", "Jump" },
        { "A", "X", "Y", "Action buttons 1, 2, 3" },
        { "Dpad", "Action buttons 4 to 7" },
        { "L", "Tap: action button 8" },
        { "L", "RStick", "Hold L: zoom camera" },
        { "R", "Target nearest enemy" },
        { "ZR", "R", "Target nearest friend" },
        { "RStickClick", "Interact: loot, talk, use" },
        { "ZL", "ZR", "Hold: Shift / Ctrl bindings" },
        { "LStickClick", "Autorun" },
    },
    {
        { title = "Cursor mode" },
        { "RStick", "Move the pointer" },
        { "A", "ZR", "Click (hold to drag)" },
        { "B", "ZL", "Right-click" },
        { "DpadUp", "DpadDown", "Scroll" },
        { "Y", "Open chat" },
        { title = "Switching" },
        { "Minus", "Play / cursor mode" },
        { "Minus", "Hold: this list" },
        { "Plus", "Menu, close, clear target" },
        { title = "Typing" },
        { "A", "Send", },
        { "Y", "Keyboard again" },
    },
}

local HELP_ICON = 28
local HELP_ROW = 32
local HELP_COLUMN = 330
local HELP_SCALE = 1.25

local help = CreateFrame("Frame", "SwitchControlsHelp", UIParent)
help:SetFrameStrata("DIALOG")
help:SetScale(HELP_SCALE)
help:SetToplevel(true)
help:SetPoint("CENTER", UIParent, "CENTER", 0, 0)
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

local helpTitle = Label(help, "title")
helpTitle:SetText("Controller")
helpTitle:SetPoint("TOP", help, "TOP", 0, -22)

do
    local rows = 0

    for column, entries in ipairs(HELP_COLUMNS) do
        local x = 30 + (column - 1) * HELP_COLUMN
        local y = -70

        for _, entry in ipairs(entries) do
            if entry.title then
                local heading = Label(help, "heading")
                heading:SetText(entry.title)
                heading:SetPoint("TOPLEFT", help, "TOPLEFT", x, y - 6)
                y = y - HELP_ROW
            else
                local glyphs, text = Split(entry)
                local anchor = CreateFrame("Frame", nil, help)
                anchor:SetWidth(1)
                anchor:SetHeight(HELP_ICON)
                anchor:SetPoint("TOPLEFT", help, "TOPLEFT", x, y)
                Icons(help, glyphs, HELP_ICON, anchor, 0, 0)

                local label = Label(help, "text")
                label:SetText(text)
                label:SetPoint("LEFT", anchor, "LEFT", 3 * (HELP_ICON - 2) + 12, 0)
                y = y - HELP_ROW
            end
        end

        rows = math.max(rows, -y)
    end

    help:SetWidth(2 * HELP_COLUMN + 20)

    -- Closing, by controller or touch.
    local closeAnchor = CreateFrame("Frame", nil, help)
    closeAnchor:SetWidth(1)
    closeAnchor:SetHeight(HELP_ICON)
    closeAnchor:SetPoint("BOTTOMLEFT", help, "BOTTOMLEFT", 30, 26)
    Sequence(help, { "Hold", { "Minus" }, "or press", { "Plus" }, "to close" }, HELP_ICON - 4, "hint", closeAnchor, 0)

    -- Options, big enough to tap.
    local function Toggle(key, text, x)
        local button = CreateFrame("Button", nil, help, "UIPanelButtonTemplate")
        button:SetWidth(200)
        button:SetHeight(40)
        button:SetPoint("BOTTOMLEFT", help, "BOTTOMLEFT", x, 70)

        local function Refresh()
            button:SetText(text .. ": " .. (SwitchControlsDB and SwitchControlsDB[key] and "On" or "Off"))
        end

        button:SetScript("OnClick", function()
            SwitchControlsDB[key] = not SwitchControlsDB[key]
            SwitchControls_SetOption(key, SwitchControlsDB[key])
            Refresh()
        end)

        button:SetScript("OnShow", Refresh)
        Refresh()
    end

    Toggle("hints", "Hint bar", 30)
    Toggle("invertLook", "Invert camera", 245)
    Toggle("returnToPlay", "Back to play", 460)

    local close = CreateFrame("Button", nil, help, "UIPanelButtonTemplate")
    close:SetWidth(180)
    close:SetHeight(40)
    close:SetPoint("BOTTOMRIGHT", help, "BOTTOMRIGHT", -30, 22)
    close:SetText("Close")
    close:SetScript("OnClick", function()
        help:Hide()
    end)

    help:SetHeight(rows + 130)
end

local function ToggleHelp()
    if help:IsShown() then
        help:Hide()
    else
        help:Show()
    end
end

-- The Interact key binding (Bindings.xml, on F17 by default: the right stick click, Os/Switch/Input.cpp) clicks this
-- button: the nearest lootable body, NPC or usable object, as a right-click on it would. A normal binding, so it can
-- be moved to another button in Key Bindings and the right stick click given something else.
BINDING_HEADER_SWITCHCONTROLS = "Switch Controls"
BINDING_NAME_SWITCHCONTROLS_INTERACT = "Interact (loot, talk, use)"
local interact = CreateFrame("Button", "SwitchControlsInteract", UIParent)
interact:SetScript("OnClick", function()
    if not SwitchControls_Interact() then
        UIErrorsFrame:AddMessage("Nothing nearby to interact with", 1.0, 0.1, 0.1, 1.0)
    end
end)

-- Holding Minus, and X in cursor mode, send F19 (Os/Switch/Input.cpp), which clicks this button.
local helpToggle = CreateFrame("Button", "SwitchControlsHelpToggle", UIParent)
helpToggle:SetScript("OnClick", ToggleHelp)
hintBar:SetScript("OnMouseUp", ToggleHelp)

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
    "GossipFrame", "MerchantFrame", "QuestFrame", "QuestLogFrame", "TaxiFrame", "ClassTrainerFrame", "BankFrame",
    "MailFrame", "OpenMailFrame", "AuctionFrame", "PetStableFrame", "TabardFrame", "GuildRegistrarFrame",
    "PetitionFrame", "TradeSkillFrame", "SpellBookFrame", "CharacterFrame", "FriendsFrame", "PVPFrame",
    -- The Lua error dialog sits in the middle of the screen, where the right stick's look clicks: it needs the cursor
    -- to be closed, or the camera can't turn.
    "BasicScriptErrors", "ScriptErrors",
}

-- The windows that want the pointer that are open, as a set, and how many.
local function OpenWindows()
    local open, count = {}, 0

    local function Add(window)
        if not open[window] then
            open[window] = true
            count = count + 1
        end
    end

    if GetUIPanel then
        for _, side in ipairs(PANEL_SIDES) do
            local panel = GetUIPanel(side)

            if panel then
                Add(panel)
            end
        end
    end

    for _, name in ipairs(WINDOWS) do
        local window = _G[name]

        if window and window:IsShown() then
            Add(window)
        end
    end

    for i = 1, NUM_CONTAINER_FRAMES or 13 do
        local container = _G["ContainerFrame" .. i]

        if container and container:IsShown() then
            Add(container)
        end
    end

    return open, count
end

local function NeedsCursor()
    local _, count = OpenWindows()
    return count > 0
end

local function WindowName(window)
    return window.GetName and window:GetName() or "a window"
end

-- In play mode, a window opening switches to cursor mode ("auto"). The windows that brought the cursor, and any opened
-- while it is up (gossip leading to a vendor, a confirmation popup), are its triggers: when the last of them closes,
-- the "returnToPlay" option goes back to play mode, even with other windows (bags kept open, say) still up. Windows
-- already open when play mode came back don't bring the cursor again; only a newly opened one does. Cursor mode the
-- player chose is left alone.
local auto = false
local triggers = {}
local lastOpen = {}

local function Log(text)
    if SwitchControls_Log then
        SwitchControls_Log(text)
    end
end

local function UpdateCursor(mode)
    local open = OpenWindows()
    local opened = {}

    for window in pairs(open) do
        if not lastOpen[window] then
            opened[window] = true
        end
    end

    lastOpen = open

    if mode == "play" then
        -- The player went back to play mode with the cursor up for a window.
        auto = false
        triggers = {}

        local window = next(opened)

        if window then
            Log(WindowName(window) .. " opened: cursor mode")
            SwitchControls_SetMode("cursor")
            auto = true
            triggers = opened
            return "cursor"
        end

        return mode
    end

    if not auto then
        return mode
    end

    for window in pairs(opened) do
        triggers[window] = true
    end

    for window in pairs(triggers) do
        if not open[window] then
            triggers[window] = nil
        end
    end

    if next(triggers) then
        return mode
    end

    auto = false

    if SwitchControlsDB and SwitchControlsDB.returnToPlay == false then
        return mode
    end

    Log("windows closed: play mode")
    SwitchControls_SetMode("play")
    return "play"
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

-- Whether the Key Bindings window (Blizzard_BindingUI) has a row waiting for a key; the controller then sends keys.
local keyCapture = false

local function UpdateKeyCapture()
    local capture = KeyBindingFrame and KeyBindingFrame:IsShown() and KeyBindingFrame.selected ~= nil or false

    if capture ~= keyCapture then
        keyCapture = capture
        SwitchControls_SetKeyCapture(capture)
    end
end

local elapsedSinceUpdate = 0
local staleTextFocus = 0

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

    -- Text mode with no text box focused: one was destroyed while it had the focus, so the controller never heard it
    -- lost it, and the sticks would stay dead. Two looks in a row, so a focus change in between doesn't count.
    if textFocus and not GetCurrentKeyBoardFocus() then
        staleTextFocus = staleTextFocus + 1

        if staleTextFocus >= 2 then
            staleTextFocus = 0
            textFocus = false
            SwitchControls_ClearTextFocus()
        end
    else
        staleTextFocus = 0
    end

    -- The same for the Key Bindings window's capture, whose hooks could miss a way of closing it.
    UpdateKeyCapture()

    mode = UpdateCursor(mode)

    if SwitchControlsDB.hints then
        local capture = KeyBindingFrame and KeyBindingFrame:IsShown() and KeyBindingFrame.selected ~= nil
        ShowHints(textFocus and "text" or capture and "bind" or mode)
        hintBar:Show()
    else
        hintBar:Hide()
    end
end)

---------------------------------------------------------------------------------------------------------------------
-- Options: kept by the Switch build in Registry.txt as soon as they change (SwitchControlsDB is written only on a clean
-- logout or quit, which closing the game from the HOME menu skips). An option the Switch build has never stored comes
-- from SwitchControlsDB, as earlier versions kept it, or the default.

local OPTION_DEFAULTS = {
    hints = true,
    invertLook = false,
    returnToPlay = true,
}

local function LoadOptions()
    for key, default in pairs(OPTION_DEFAULTS) do
        local value = SwitchControls_GetOption(key)

        if value == nil then
            value = SwitchControlsDB[key]

            if value == nil then
                value = default
            end

            SwitchControls_SetOption(key, value)
        elseif key == "invertLook" then
            SwitchControls_SetOption(key, value)
        end

        SwitchControlsDB[key] = value
    end
end

-- Interact starts on the right stick click (F17), unless that key or the action is bound already. The binding set is 0
-- (the defaults) until the saved bindings load, and saving then would replace them with the defaults: before
-- VARIABLES_LOADED (bindingsLoaded) that waits, after it the defaults are what's in use, saved as account bindings.
local function BindInteract(bindingsLoaded)
    if InCombatLockdown() or GetBindingKey("SWITCHCONTROLS_INTERACT") or GetBindingAction("F17") ~= "" then
        return
    end

    local set = GetCurrentBindingSet()

    if set ~= 1 and set ~= 2 then
        if not bindingsLoaded then
            return
        end

        set = 1
    end

    if SetBinding("F17", "SWITCHCONTROLS_INTERACT") then
        SaveBindings(set)
    end
end

---------------------------------------------------------------------------------------------------------------------
-- Key Bindings window (Blizzard_BindingUI, loaded on demand): while a row waits for a key, the controller sends keys
-- rather than clicks, so every button can be bound; Plus cancels instead of binding Escape; keys a controller button
-- sends show as that button.

local keyBindingsSetUp = false

local function ShowBindingGlyphs()
    for i = 1, KEY_BINDINGS_DISPLAYED or 17 do
        for keyID = 1, 2 do
            local button = _G["KeyBindingFrameBinding" .. i .. "Key" .. keyID .. "Button"]

            if button and button:IsShown() and button.commandName then
                local key = select(keyID, GetBindingKey(button.commandName, KeyBindingFrame.mode))
                local glyphs = KeyGlyphs(key, 16)

                if glyphs then
                    button:SetText(glyphs)
                end
            end
        end
    end
end

local function SetUpKeyBindings()
    if keyBindingsSetUp or not KeyBindingFrame then
        return
    end

    keyBindingsSetUp = true

    hooksecurefunc("KeyBindingFrame_SetSelected", UpdateKeyCapture)
    hooksecurefunc("KeyBindingFrame_Update", ShowBindingGlyphs)
    KeyBindingFrame:HookScript("OnShow", UpdateKeyCapture)
    KeyBindingFrame:HookScript("OnHide", UpdateKeyCapture)

    -- Plus sends Escape, which would be bound to the waiting row; it cancels the row instead. Holding Minus sends F19
    -- (the controls list), which isn't a key to bind either.
    local onKeyDown = KeyBindingFrame:GetScript("OnKeyDown")

    KeyBindingFrame:SetScript("OnKeyDown", function(self, key)
        if self.selected and (key == "ESCAPE" or key == "F19") then
            if self.buttonPressed then
                self.buttonPressed:UnlockHighlight()
            end

            KeyBindingFrameOutputText:SetText("")
            KeyBindingFrame_SetSelected(nil)
            KeyBindingFrame_Update()
            KeyBindingFrame_UpdateUnbindKey()
            return
        end

        onKeyDown(self, key)
    end)
end

---------------------------------------------------------------------------------------------------------------------
-- Video options (FrameXML's VideoOptionsPanels): the controls the Switch has no use for are greyed out, since the game
-- always fills the screen, the swap chain is the system's, the game draws the cursor and the display gamma can't be
-- changed. The first time in the world on the Switch, the effects start at Blizzard's "Fair" preset (applied through
-- the panel's own code, so each slider maps to its setting as it does in the window), which suits the Tegra X1 at
-- 720p; changes made after that are kept.

local UNUSED_VIDEO_CHECKBOXES = {
    "TripleBuffer", "HardwareCursor", "FixInputLag", "Windowed", "Maximized", "DisableResize", "DesktopGamma",
}

local SWITCH_VIDEO_QUALITY = 2

local unusedVideoControls = {}

local function NoOp()
end

local function DisableGamma()
    if VideoOptionsResolutionPanelGammaSlider then
        BlizzardOptionsPanel_Slider_Disable(VideoOptionsResolutionPanelGammaSlider)
    end
end

local function SetUpVideoOptions()
    if not VideoOptionsResolutionPanel or not BlizzardOptionsPanel_CheckButton_Disable then
        return
    end

    for _, name in ipairs(UNUSED_VIDEO_CHECKBOXES) do
        local box = _G["VideoOptionsResolutionPanel" .. name]

        if box then
            unusedVideoControls[box] = true
            BlizzardOptionsPanel_CheckButton_Disable(box)
            box.Enable = NoOp
        end
    end

    -- The panel enables some of them again as others change (Maximized with Windowed, the gamma ones together).
    hooksecurefunc("BlizzardOptionsPanel_CheckButton_Enable", function(box)
        if unusedVideoControls[box] then
            BlizzardOptionsPanel_CheckButton_Disable(box)
        end
    end)

    hooksecurefunc("VideoOptionsResolutionPanel_RefreshGammaControls", DisableGamma)
    DisableGamma()
end

local function ApplySwitchVideoPreset()
    if SwitchControls_GetOption("videoPreset") ~= nil or not VideoOptionsEffectsPanel_SetVideoQuality then
        return
    end

    local applied = pcall(function()
        VideoOptionsEffectsPanel_SetVideoQuality(SWITCH_VIDEO_QUALITY)
        VideoOptionsPanel_Okay(VideoOptionsEffectsPanel)
    end)

    if applied then
        SwitchControls_SetOption("videoPreset", true)
        Log("video effects set to the Fair preset")
    end
end

local function Step(name, step, ...)
    local ok, message = pcall(step, ...)

    if not ok then
        Log(name .. " failed: " .. tostring(message))
    end
end

local entered = false

frame:RegisterEvent("ADDON_LOADED")
frame:RegisterEvent("PLAYER_ENTERING_WORLD")
frame:RegisterEvent("VARIABLES_LOADED")
frame:RegisterEvent("PLAYER_LOGOUT")
frame:RegisterEvent("UPDATE_BINDINGS")

frame:SetScript("OnEvent", function(self, event, arg1)
    if event == "ADDON_LOADED" and arg1 == "SwitchControls" then
        SwitchControlsDB = SwitchControlsDB or {}
        LoadOptions()

        -- Lua errors also go to Logs/Console.txt, with where they happened, for diagnosing on the console.
        local handler = geterrorhandler()

        seterrorhandler(function(message, ...)
            Log("Lua error: " .. tostring(message) .. "\n" .. (debugstack and debugstack(2, 12, 0) or ""))
            return handler(message, ...)
        end)

        if IsAddOnLoaded("Blizzard_BindingUI") then
            SetUpKeyBindings()
        end
    elseif event == "PLAYER_ENTERING_WORLD" then
        -- Only the first time: it also fires after every loading screen.
        if not entered then
            entered = true
            Log("in the world")

            -- Each step on its own, so one failing (an error is logged) doesn't keep the others, play mode above all,
            -- from happening.
            Step("help binding", SetOverrideBindingClick, frame, false, "F19", "SwitchControlsHelpToggle")
            Step("interact binding", BindInteract, false)
            Step("video options", SetUpVideoOptions)
            Step("video preset", ApplySwitchVideoPreset)

            if not NeedsCursor() then
                SwitchControls_SetMode("play")
            end

            Step("labels", LogLabels)

            -- Kept natively like the options: the saved variables aren't written when the game is closed from HOME.
            if not SwitchControls_GetOption("helpSeen") and not SwitchControlsDB.helpSeen then
                SwitchControls_SetOption("helpSeen", true)
                help:Show()
            end
        end

        Step("hotkeys", UpdateAllHotkeys)
    elseif event == "UPDATE_BINDINGS" then
        UpdateAllHotkeys()
    elseif event == "VARIABLES_LOADED" then
        -- Bindings can arrive after PLAYER_ENTERING_WORLD, replacing the ones set there.
        BindInteract(true)
    elseif event == "ADDON_LOADED" and arg1 == "Blizzard_BindingUI" then
        SetUpKeyBindings()
    elseif event == "PLAYER_LOGOUT" then
        -- Back to the character screens, which need the pointer.
        keyCapture = false
        SwitchControls_SetKeyCapture(false)
        SwitchControls_SetMode("cursor")
    end
end)

SLASH_SWITCHCONTROLS1 = "/switch"
SlashCmdList["SWITCHCONTROLS"] = function(arguments)
    if arguments and arguments:lower():match("hints") then
        SwitchControlsDB.hints = not SwitchControlsDB.hints
        SwitchControls_SetOption("hints", SwitchControlsDB.hints)
        DEFAULT_CHAT_FRAME:AddMessage("Switch Controls: hint bar " .. (SwitchControlsDB.hints and "on" or "off") .. ".")
    elseif help:IsShown() then
        help:Hide()
    else
        help:Show()
    end
end
