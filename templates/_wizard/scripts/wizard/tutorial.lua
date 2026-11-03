-- Step-by-step tutorial (game wizard kit, templates/_wizard): runs in the deterministic simulation of the
-- tutorial map. The steps are data: scripts/wizard/tutorial_steps.lua (written by the wizard from the game's
-- own rules; edit it freely). What a step can say:
--
--   {id = "train", text = "tut.train",            -- lang key (or plain text) shown in the tutorial panel
--    focus = {type = "barracks", owner = "me"},   -- what the arrow points at: type (or list), kind, owner me|enemy|neutral
--    allow = {"train", "smart"},                  -- hard mode: only these commands of the player go through
--    param = "footman",                           -- ... and only for this type (train / build / place / upgrade)
--    give = {gold = 200},                         -- the stock is topped up to this when the step starts (it can't get stuck)
--    done = {event = "trained", type = "footman", owner = "me", count = 2},   -- (captured: from = "enemy" too)
--           or {next = true} (the panel button) or {command = "move"} (the player gave that order)
--    auto = {type = "train", units = "barracks", param = "footman"},   -- test mode only (below)
--    weaken = 80, weaken_units = 50,              -- the enemy's structures / units lose that share of health at the start
--    free = true}                                 -- from this step on the enemy plays (its commands are dropped before;
--                                                 -- its units still defend themselves)
--
-- State for the view (scripts/view/wizard/tutorial_view.lua): state.wiz_tut = {i, n, need, focus, x, z, over}.
-- Test mode: map initialVars {wiz_tutorial_auto = 1} makes the script play every step itself, through the same gate as
-- a player (rts_headless / the wizard's check: the whole tutorial can be finished; `auto` in a step says how).
local cfg = require("wizard.tutorial_steps")
local P = cfg.player or 1
local hard = cfg.mode ~= "soft"
local always = {}
for _, c in ipairs(cfg.always or {"stop", "cancel", "hold"}) do always[c] = true end

local types = {}           -- type index (0-based, as in commands) -> id: rules data, the same on every machine
local function ensure_types()
  if types[0] == nil then for i, id in ipairs(game.types()) do types[i - 1] = id end end
end

local function T() return state.wiz_tut end
local function step() local t = T(); return t and cfg.steps[t.i] or nil end

local function enemy_of_me()
  for _, p in ipairs(game.players()) do
    if p ~= P and game.hostile(P, p) and game.player_alive(p) then return p end
  end
  return nil
end

local function owner_ok(want, owner)
  if want == nil or want == "me" then return owner == P end
  if want == "enemy" then return owner ~= P and owner ~= 0 and game.hostile(P, owner) end
  if want == "neutral" then return owner == 0 end
  if want == "any" then return true end
  return owner == want
end

local function type_ok(want, t)
  if want == nil then return true end
  if type(want) == "table" then
    for _, w in ipairs(want) do if w == t then return true end end
    return false
  end
  return want == t
end

local function owner_slot(want)
  if want == nil or want == "me" then return P end
  if want == "neutral" then return 0 end
  if want == "enemy" then return enemy_of_me() end
  return nil
end

-- the entity a step points at: nearest to the player's base (its first structure)
local function pick_focus(f)
  if not f then return nil end
  local base = game.units{owner = P, kind = "structure", limit = 1}[1] or game.units{owner = P, limit = 1}[1]
  local bx, bz = 0, 0
  if base then bx, bz = game.pos(base) end
  local list = type(f.type) == "table" and f.type or {f.type or false}
  local best, bestd = nil, nil
  for _, t in ipairs(list) do
    local filter = {kind = f.kind, x = bx, z = bz, radius = 100000, sort = "dist", limit = 4}
    if t then filter.type = t end
    local o = owner_slot(f.owner)
    if o then filter.owner = o end
    for _, h in ipairs(game.units(filter)) do
      if owner_ok(f.owner, game.owner(h)) and not (f.idle and not game.idle(h)) then
        local x, z = game.pos(h)
        local d = (x - bx) * (x - bx) + (z - bz) * (z - bz)
        if bestd == nil or d < bestd then best, bestd = h, d end
        break
      end
    end
  end
  return best
end

local function refresh_focus()
  local t, s = T(), step()
  if not t or not s then return end
  if t.focus and game.alive(t.focus) and not s.refocus then
    t.x, t.z = game.pos(t.focus)
    return
  end
  t.focus = pick_focus(s.focus)
  if not t.focus and s.focus and s.focus.kind == "structure" and s.focus.owner == "enemy" then   -- none of the target buildings
    t.focus = pick_focus({owner = "enemy", kind = "structure"}) or pick_focus({owner = "enemy"})
    t.no_target = true
  end
  if t.focus then t.x, t.z = game.pos(t.focus) else t.x, t.z = nil, nil end
end

local function enter(i)
  local t = T()
  t.i, t.n = i, 0
  t.focus = nil
  t.auto_at, t.auto_n, t.no_target = nil, nil, nil
  local s = cfg.steps[i]
  if not s then
    t.over = true
    game.set_var("wiz_tutorial_done", 1)
    if cfg.win_at_end ~= false then game.victory(P) end
    return
  end
  t.need = (s.done and s.done.count) or 1
  t.started = game.tick()
  for res, n in pairs(s.give or {}) do   -- top up to what the step costs (a stock above it stays; storage caps can clip gifts)
    local have = game.resource(P, res)
    if have == nil or have < n then game.give(P, res, n - (have or 0)) end
  end
  if s.free then t.free = true end
  local e = enemy_of_me()
  for kind, w in pairs({structure = s.weaken, unit = s.weaken_units}) do   -- the last fight stays short and winnable
    if e and w then
      for _, h in ipairs(game.units{owner = e, kind = kind}) do
        local hp = game.hp(h)
        if hp then game.set_hp(h, fx.max(fx(1), hp - hp * w // 100)) end
      end
    end
  end
  game.set_var("wiz_tutorial_step", i)
  refresh_focus()
  if t.no_target and s.weaken_units and e then   -- nothing to raze: the enemy's units are the target, keep it a short fight
    for _, h in ipairs(game.units{owner = e, kind = "unit"}) do
      local hp, max = game.hp(h)
      if max then game.set_hp(h, fx.max(fx(1), fx.min(hp, max * 15 // 100))) end
    end
  end
end

local function progress(n)
  local t = T()
  if not t or t.over then return end
  t.n = t.n + (n or 1)
  if t.n >= t.need then enter(t.i + 1) end
end

-- events: each step's `done` condition
local watched = {}
for _, s in ipairs(cfg.steps) do
  local d = s.done
  if d and d.event and not watched[d.event] then
    watched[d.event] = true
    on(d.event, function(e)
      local s2 = step()
      if not s2 or not s2.done or s2.done.event ~= e.event then return end
      local d2 = s2.done
      local owner = e.owner or e.player
      if d2.event == "captured" then owner = e.owner end
      if not owner_ok(d2.owner, owner) then return end
      if not type_ok(d2.type, e.unit_type) then
        -- anyIfNone: the step's target buildings don't exist (the enemy has none of them): any enemy loss counts
        if not (d2.anyIfNone and T().no_target) then return end
      end
      if d2.resource and e.resource ~= d2.resource then return end
      if d2.from and not owner_ok(d2.from, e.old_owner) then return end
      progress(1)
    end)
  end
end

-- the panel's "Next" button and "Skip" (rules commands wiz_tut_next / wiz_tut_skip, "card": false); wiz_tut_off from the
-- view script when the host has no game UI to show the panel in
function wiz_tutorial_command(player, id, args)
  if player ~= P or not T() or T().over then return false end
  if id == "wiz_tut_off" then   -- the host can't show the panel (the editor's Game view): a plain match, nothing locked
    local t = T()
    t.free, t.over, t.off = true, true, true
    return true
  end
  if id == "wiz_tut_skip" then
    if cfg.skippable == false then return false end
    T().free = true
    T().skipped = true
    enter(#cfg.steps + 1)
    return true
  end
  local s = step()
  if s and s.done and s.done.next then progress(s.done.count or 1) return true end
  return false
end

local function command_type_ok(s, c)
  for _, a in ipairs(s.allow or {}) do
    if a == c.type then
      if s.param and (c.type == "train" or c.type == "build" or c.type == "place" or c.type == "upgrade") then
        local p = c.param
        if type(p) == "number" then p = types[p] end
        return p == nil or type_ok(s.param, p)
      end
      return true
    end
  end
  return false
end

local prev_before = before_command
function before_command(player, c)
  ensure_types()
  local t = T()
  if t and not t.over then
    if player == P then
      local s = step()
      if s then
        local ok = always[c.type] or command_type_ok(s, c)
        local ap = t.auto_pass            -- test mode: the script's own command of this tick passes
        if ap and game.tick() - ap.tick <= 2 and (c.type == ap.type or c.type == "place") then ok = true end
        if hard and not ok then return false end
        if ok and s.done and s.done.command and type_ok(s.done.command, c.type) then progress(1) end
      end
    elseif not t.free and game.hostile(P, player) then
      return false                       -- the enemy waits until a step sets `free`
    end
  end
  if prev_before then return prev_before(player, c) end
end

-- ------------------------------------------------------------------ test mode: the script plays itself
local function nearest(owner_want, typ, kind, x, z)
  local o = owner_slot(owner_want)
  local best, bestd = nil, nil
  for _, ty in ipairs(type(typ) == "table" and typ or {typ or false}) do
    local f = {kind = kind, x = x, z = z, radius = 100000, sort = "dist", limit = 1}
    if ty then f.type = ty end
    if o then f.owner = o end
    local h = game.units(f)[1]
    if h then
      local hx, hz = game.pos(h)
      local d = (hx - x) * (hx - x) + (hz - z) * (hz - z)
      if bestd == nil or d < bestd then best, bestd = h, d end
    end
  end
  return best
end

local function first_type(t) if type(t) == "table" then return t[1] end return t end
local AUTO_SPOTS = {{6, 6}, {-6, 6}, {6, -6}, {-6, -6}, {9, 0}, {0, 9}, {-9, 0}, {0, -9}, {8, 8}, {-8, -8}}

local function auto_play()
  local t, s = T(), step()
  if not t or t.over or not s then return end
  if s.done and s.done.next then progress(s.done.count or 1)
  elseif s.auto and (t.auto_at == nil or game.tick() - t.auto_at >= (s.auto.near and 6 or 15) * game.tick_rate()) then
    t.auto_at = game.tick()            -- once per step (again after 15 s if it didn't finish)
    t.auto_n = (t.auto_n or 0) + 1
    local a = s.auto
    local units = {}
    if a.units then
      for _, ty in ipairs(type(a.units) == "table" and a.units or {a.units}) do
        for _, h in ipairs(game.units{owner = P, type = ty}) do units[#units + 1] = h end
      end
    elseif t.focus then units = {t.focus} end
    local busy = t.auto_n > 1                -- a retry only when the first try ended (long gather trips, building, training)
    for _, h in ipairs(units) do if game.idle(h) then busy = false end end
    if busy then return end
    if #units > 0 or a.bare then
      local c = {type = a.type, units = units, param = a.param}
      if a.target then
        local x, z = game.pos(units[1])
        c.target = nearest(a.target.owner, a.target.type, a.target.kind, x, z)
                   or (a.target.kind and nearest(a.target.owner, nil, nil, x, z))
        if c.target then c.x, c.z = game.pos(c.target) end
      elseif a.near then   -- a spot next to the player's base, another one on every retry
        local base = game.units{owner = P, kind = "structure", limit = 1}[1] or units[1]
        local x, z = game.pos(base)
        local o = AUTO_SPOTS[(t.auto_n or 0) % #AUTO_SPOTS + 1]
        c.x, c.z = x + o[1], z + o[2]
      end
      t.auto_pass = {tick = game.tick(), type = c.type}
      if c.target or c.x or a.bare then game.command(P, c) end
      if a.place and c.x then   -- sidebar building: put the finished structure down (fails quietly until it is ready)
        game.command(P, {type = "place", units = units, x = c.x, z = c.z})
      end
    end
  end
end

on("start", function()
  ensure_types()
  state.wiz_tut = {i = 0, n = 0, need = 1}
  enter(1)
end)

every(1, function()
  local t = T()
  if not t or t.over then return end
  ensure_types()
  refresh_focus()
  if game.var("wiz_tutorial_auto") > 0 then auto_play() end
end)
