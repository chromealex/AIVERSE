-- Logic of the Bunker Wars battle HUD: ui/hud/bw_hud.rml is BW's HUDScreen (+ HUDLayout and its components:
-- SkillsPanel, PlayersBar, PercentChooser, Timer, OpenMenuButton, BattleRegimePanel, SuddenDeathPanel, ...)
-- converted by the UI.Windows importer; scripts/view/bw/hud_view.lua sends the battle numbers as ui.set("bw", v).
-- Element ids are the importer's (<slot>-<object>), see ui/ugui/screens/hud.json. Installed by
-- tools/importers/bunkerwars/client.py; BW behaviour per component:
--   HeroManaComponent      energy value (Mana/Value), shown while the player has energy
--   HeroSkillsComponent    one card per owned skill (Content/Content/Skill, 5 pooled copies in the card row): Ready /
--                          NotReady / NoEnergy states, cooldown seconds in the header; click = use the skill
--   PlayersBarComponent    one row per player (Values/Player, 8 pooled copies): army size, its share of all units
--   PercentChooser         25 / 50 / 75 / 100: game.sendPercent, the chosen one highlighted (Enabled)
--   HeroSkillTimer / Time  match clock: time limit left, else time played (mm:ss); extra time in sudden death
--   SuddenDeathPanel       shown from the sudden death preview on
--   OpenMenuButton         the pause menu (game.pause)
local rt = require('ugui.runtime')
local M = {on = {}}

local doc
local made = {players = {}, cards = {}}

local function el(id) return doc and doc:GetElementById(id) or nil end
local function child(root, src)
    if not root then return nil end
    return root:QuerySelector('[u-src="' .. src .. '"]')
end
local function setText(e, s)
    if not e then return end
    local t = e:QuerySelector('.u-text') or e
    t.inner_rml = s
end
local function show(e, on) if e then rt.show(e, on) end end
local function clock(sec)
    sec = math.max(0, math.floor(sec or 0))
    return string.format('%02d:%02d', sec // 60, sec % 60)
end

-- pooled rows: client.py writes 8 PlayersBar rows (playersbar-player-1..8) and 5 skill cards
-- (skillspanel1player-skill-1..5, in the card row) next to BW's template elements
local function pool(prefix, n)
    local list = {}
    for k = 1, n do list[k] = el(prefix .. '-' .. k) end
    return list
end

local PERCENT = {[100] = 'percentslider-enabled', [75] = 'percentslider-enabled-2', [50] = 'percentslider-enabled-3',
                 [25] = 'percentslider-enabled-4'}
local function percents()
    local p = game.sendPercent()
    for v, id in pairs(PERCENT) do show(el(id), p == v) end
end
M.on['percentslider-button100'] = function() game.sendPercent(100); percents() end
M.on['percentslider-button75'] = function() game.sendPercent(75); percents() end
M.on['percentslider-button50'] = function() game.sendPercent(50); percents() end
M.on['percentslider-button25'] = function() game.sendPercent(25); percents() end
M.on['openmenu-openmenubutton'] = function() game.pause() end

local function players(v)
    local rows = v.players or {}
    made.players = made.players[1] and made.players or pool('playersbar-player', 8)
    local total = 0
    for _, r in ipairs(rows) do total = total + (r.units or 0) end
    for i, row in ipairs(made.players) do
        local r = rows[i]
        show(row, r ~= nil)
        if r then
            setText(child(row, 'Text'), tostring(r.units or 0))
            local bar = child(row, 'Bar (1)')
            if bar then bar.style.width = string.format('%.1f%%', total > 0 and 100 * (r.units or 0) / total or 0) end
            show(child(row, 'Dead'), r.alive == false)
        end
    end
end

local function cards(mine)
    local list = mine and mine.cards or {}
    if not made.cards[1] then
        made.cards = pool('skillspanel1player-skill', 5)
        show(el('skillspanel1player-skill'), false)
        for k, e in ipairs(made.cards) do
            local btn = child(e, 'Container')
            if btn then btn:AddEventListener('click', function() local m = made.cardInfo[k]; if m then game.useAbility(m.i) end end) end
        end
    end
    made.cardInfo = list
    local energy = mine and mine.energy or 0
    for i, e in ipairs(made.cards) do
        local a = list[i]
        show(e, a ~= nil)
        if a then
            local ready = a.usable and (a.ready_in or 0) <= 0
            show(child(e, 'Ready'), ready)
            show(child(e, 'ReadyToUse'), ready)
            show(child(e, 'NotReady'), not ready)
            show(child(e, 'NoEnergy'), (a.energy or 0) > energy)
            local h = child(e, 'Header')
            if h then
                show(h, (a.ready_in or 0) > 0)
                setText(child(h, 'Description'), tostring(a.ready_in or 0))
            end
        end
    end
end

function M.update(v)
    if not doc or type(v) ~= 'table' then return end
    local mine = v.mine
    show(el('skillspanel1player-mana'), mine ~= nil and mine.energy ~= nil)
    if mine and mine.energy then setText(el('skillspanel1player-value-3'), tostring(mine.energy)) end
    -- HeroSkillTimerComponent: the match clock under the skills (time limit left, else time played); "extra time"
    -- (BackOvertime) from the sudden death on. TimeBar: only for time-limit matches.
    setText(el('skillspanel1player-value-2'), clock(v.left or v.t))
    show(el('skillspanel1player-backovertime'), (v.sd or 0) >= 2)
    show(el('timebar'), v.left ~= nil)
    if v.left then setText(el('timebar-value'), clock(v.left)) end
    show(el('suddendeathpanel'), (v.sd or 0) >= 1)
    players(v)
    cards(mine)
    percents()
end

function M.mount(document)
    doc = document
    made = {players = {}, cards = {}, cardInfo = {}}
    -- slots the battle does not use yet (replay / spectator / network / multiselect / 2-player panels)
    for _, id in ipairs({'replaycontrols', 'spectatorcontrols', 'replayspeed', 'networkconnection', 'skillspanel2players-1',
                         'skillspanel2players-2', 'hero1-rightbottom', 'hero2-leftbottom', 'hero3-lefttop', 'hero4-righttop',
                         'openmenuhint', 'tips', 'moraletips', 'attackparameterspanel', 'suddendeathbutton', 'multiselect',
                         'buildingmenu', 'skillspanel1player-drop', 'counter', 'skillspanel1player-manualmorale',
                         'skillspanel1player-variants', 'skillspanel1player-border-3', 'timebar-icon'}) do
        show(el(id), false)
    end
    -- tooltips (u-src Hint) open on demand in BW (ButtonWithTipComponent), as do the manual morale rows
    for _, sel in ipairs({'[u-src="Hint"]', '[u-src="ManualMorale"]'}) do
        for _, e in ipairs(doc:QuerySelectorAll(sel)) do show(e, false) end
    end
    if not M.hooked then   -- scriptui._set: the HUD values arrive under the key "bw"
        M.hooked = true
        local set = scriptui._set
        scriptui._set = function(key, value)
            set(key, value)
            if key == 'bw' then M.update(value) end
        end
    end
    percents()
end

return M
