-- Soft tutorial (game wizard kit, templates/_wizard): one-time hints during ordinary matches. What to say and when
-- is data: scripts/wizard/hints_config.lua (written by the wizard from the game's rules). A hint is shown once per
-- player profile (the UI Lua remembers it: ui/wizard_meta.lua, WizMeta.hint*), never twice in a match, and not at
-- all when the player turned hints off in the settings. Presentation only.
--
--   {id = "workers", text = "wiz.hint.workers", when = "idle", type = {"peasant"}, after = 12}
--   when: "start" (after `delay` s) | "event" (event = name, owner = me|enemy, type = ...) | "idle" (own units of
--         `type` idle for `after` s) | "supply" (supply reached the cap) | "resource" (resource below `below`)
local cfg = require("wizard.hints_config")
local queue, current, left, shown_now = {}, nil, 0, {}
local clock, idle_for = 0, {}
local texts = {}

local function call(fn, ...)
  local ok, r = view.ui_call(fn, ...)
  if ok then return r end
  return nil
end

local function tr(key)
  if texts[key] then return texts[key] end
  local s = call("game.tr", key)
  if s == nil or s == "" or s == key then s = key end
  texts[key] = s
  return s
end

local enabled = call("WizMeta.hintsOn")
if enabled == nil then enabled = true end

local function want(h)
  if not enabled or shown_now[h.id] then return false end
  if call("WizMeta.hintSeen", h.id) then shown_now[h.id] = true; return false end
  return true
end

local function show(h)
  if not want(h) then return end
  shown_now[h.id] = true
  queue[#queue + 1] = h
end

local function owner_ok(want_owner, owner)
  local me = view.player()
  if want_owner == nil or want_owner == "me" then return owner == me end
  if want_owner == "enemy" then return owner ~= me and owner ~= 0 end
  return true
end

local function type_ok(want_type, t)
  if want_type == nil then return true end
  if type(want_type) == "table" then
    for _, w in ipairs(want_type) do if w == t then return true end end
    return false
  end
  return want_type == t
end

for _, h in ipairs(cfg.hints or {}) do
  if h.when == "event" then
    view.on(h.event, function(e)
      if not owner_ok(h.owner, e.owner or e.player) or not type_ok(h.type, e.unit_type) then return end
      show(h)
    end)
  end
end

ui.set("hint", {text = "", title = ""})

view.on_frame(function(dt)
  local me = view.player()
  if me == 0 or view.screen() ~= "playing" or (view.state() or {}).wiz_tut then return end   -- not during the tutorial
  clock = clock + dt
  if current then
    left = left - dt
    if left <= 0 then ui.close("wiz_hint"); current = nil end
  elseif #queue > 0 then
    current = table.remove(queue, 1)
    call("WizMeta.hintShown", current.id)
    ui.set("hint", {text = tr(current.text), title = tr(current.title or "wiz.hint.title")})
    ui.open("wiz_hint")
    left = current.duration or cfg.duration or 7
  end
  if math.floor(clock) ~= math.floor(clock - dt) then   -- once a second
    for _, h in ipairs(cfg.hints or {}) do
      if h.when == "start" and clock >= (h.delay or 3) then show(h)
      elseif h.when == "idle" and want(h) then
        local n = 0
        local list = type(h.type) == "table" and h.type or {h.type}
        for _, t in ipairs(list) do
          for _, u in ipairs(game.units{owner = me, type = t}) do if game.idle(u) then n = n + 1 end end
        end
        idle_for[h.id] = n > 0 and (idle_for[h.id] or 0) + 1 or 0
        if idle_for[h.id] >= (h.after or 10) then show(h) end
      elseif h.when == "supply" then
        local p = game.player(me)
        if p and p.supply_cap and p.supply_cap > 0 and p.supply >= p.supply_cap then show(h) end
      elseif h.when == "resource" and h.resource then
        local v = game.resource(me, h.resource)
        if v and v < (h.below or 50) and clock > 60 then show(h) end
      end
    end
  end
end)

ui.on("wiz_hint_close", function() if current then left = 0 end end)
ui.on("wiz_hints_off", function()
  call("WizMeta.setHints", false)
  enabled = false
  queue = {}
  if current then left = 0 end
end)
