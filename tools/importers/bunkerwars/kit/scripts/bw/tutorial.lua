-- Bunker Wars battle tutorials on engine primitives (tools/importers/bunkerwars/tutorial.py writes the data).
-- Usage (map script written by the importer):  require("bw.tutorial")(require("bw.tutorial_data.<map>"))
--
-- Semantics of BW's MapTutorialModule: "before" chapters run all their steps once at the start; the first
-- tutorial chapter starts active; every tick each active chapter runs its current step, and a step that
-- reports done moves the chapter on; GOTOChapter starts / dismisses chapters by name (several chapters run
-- in parallel, e.g. "MoveUnit" shows the finger while "WaitMoveUnits" waits for the move).
--
-- Points: BW point ids are resolved to garrison structures through game.data(h).bw_point (the BW map
-- importer sets it); without it, the garrison structures in entity order are ids 0, 1, 2, ...
-- Players: "player" = the human slot (var bw_human, default 1), "bot" = the first hostile player,
-- "neutral" = 0.
-- What the view shows (finger, windows, highlights, camera) is in state.bwt.ui for scripts/view/bw/tutorial_view.lua;
-- its clicks come back as the hidden commands bw_tut_click / bw_tut_window / bw_tut_menu / bw_tut_percent /
-- bw_tut_skip / bw_tut_off (rules "commands", fn bw_tutorial_command).
-- BW mechanics owned by other systems (morale, energy, skills, artifacts, time scale) go through the optional
-- global table bw_tutorial_hooks[action](step, player) when a game script defines it; otherwise they set
-- trigger variables (bw_energy, bw_morale, bw_skill_cooldown_<slot>, ...) and the step completes.
-- Test mode: initialVars {bw_tutorial_auto = 1} answers every UI wait by itself after a second and plays the
-- player's moves (WaitMoveUnits / WaitPointCaught / upgrades) through the same command path.

local BF = {noHeroes = true, noMorale = true, noSkills = true, noUpgrade = true, noConvertToHouse = true,
            noConvertToTower = true, noConvertToSmithy = true, noPercentChooser = true, noInput = true}

return function(data)
  local function T() return state.bwt end
  local function human() local v = game.var("bw_human"); local n = v and fx.int(v) or 0; return n > 0 and n or 1 end
  local function auto() local v = game.var("bw_tutorial_auto"); return v ~= nil and v > 0 end

  local function bot()
    local P = human()
    for _, p in ipairs(game.players()) do
      if p ~= P and game.hostile(P, p) and game.player_alive(p) then return p end
    end
    return nil
  end
  local function slot(kind)
    if kind == "bot" or kind == 1 then return bot() end
    if kind == "neutral" or kind == 2 then return 0 end
    return human()
  end

  -- ---------------------------------------------------------------- points
  -- garrison structures in entity order, with their BW id (game.data(h).bw_point, else the order 0, 1, 2 ...)
  local function points()
    local list, ids, tagged = {}, {}, false
    for _, h in ipairs(game.units{kind = "structure"}) do
      local u = game.unit(h)
      if u and u.garrison ~= nil then
        list[#list + 1] = h
        local d = game.has_data(h, "bw_point") and game.data(h).bw_point or nil
        ids[#ids + 1] = d
        if d ~= nil then tagged = true end
      end
    end
    if not tagged then for i = 1, #list do ids[i] = i - 1 end end
    return list, ids
  end
  local function point(id)
    if id == nil or id < 0 then return nil end
    local list, ids = points()
    for i = 1, #list do if ids[i] == id then return list[i] end end
    return nil
  end
  local function point_id(h)
    local list, ids = points()
    for i = 1, #list do if list[i] == h then return ids[i] or -1 end end
    return -1
  end

  -- ---------------------------------------------------------------- state
  local function new_state()
    return {active = {}, started = false, over = false, ai = true, flags = {}, pflags = {}, percent = 100, timescale = 1,
            invincible = {}, sends = {}, captures = {}, upgrades = {}, upgrade_orders = {}, converts = {}, skills = {}, fires = {},
            clicks = 0, windows_closed = {}, menus = 0, percent_set = 0,
            ui = {windows = {}, finger = nil, hidden = {}, point_hidden = {}, camera = nil, upgrade_tip = nil, seq = 0}}
  end

  local function chapter_index(name)
    for i, c in ipairs(data.chapters or {}) do if c.name == name then return i end end
    return nil
  end
  local function activate(i)
    local t = T()
    for _, a in ipairs(t.active) do if a.c == i then a.s = 1; a.st = {} return end end
    t.active[#t.active + 1] = {c = i, s = 1, st = {}}
  end
  local function dismiss(i)
    local t = T()
    for k, a in ipairs(t.active) do if a.c == i then table.remove(t.active, k) return end end
  end

  local function hook(s, ...)
    local h = bw_tutorial_hooks
    if type(h) == "table" and type(h[s.action]) == "function" then return h[s.action](s, ...) end
    return nil
  end

  local function flags_set(list, on)
    local t = T()
    for _, f in ipairs(list or {}) do t.flags[f] = on or nil end
  end

  local function point_flags(s)
    local t = T()
    local listed = {}
    for _, id in ipairs(s.points or {}) do listed[id] = true end
    local all = points()
    for _, h in ipairs(all) do
      local id = point_id(h)
      local hit = (s.selectionType == "onlyInclude") == (listed[id] == true)
      if hit then
        local pf = t.pflags[id] or {}
        for _, f in ipairs(s.modifyFlags or {}) do pf[f] = s.flagState == "disabled" and true or nil end
        t.pflags[id] = pf
      end
    end
  end
  local function point_blocked(h, flag)
    if not h then return false end
    local pf = T().pflags[point_id(h)]
    return pf ~= nil and pf[flag] == true
  end

  local function count_since(list, base, pred)
    local n = 0
    for i = base + 1, #list do if pred(list[i]) then n = n + 1 end end
    return n
  end

  -- test mode: the player's part of a wait, through the normal command path
  local function auto_send(from_id, to_id)
    local P = human()
    local to = point(to_id)
    if not to then
      for _, h in ipairs(points()) do if game.owner(h) ~= P then to = h; break end end
    end
    local from = point(from_id)
    if not from then
      for _, h in ipairs(points()) do if game.owner(h) == P and h ~= to then from = h; break end end
    end
    if from and to then game.command(P, {type = "send", units = {from}, target = to, param = 100}) end
  end

  -- ---------------------------------------------------------------- steps: return true when done
  local A = {}
  local function ui() return T().ui end
  local function bump() ui().seq = ui().seq + 1 end

  A.Wait = function(s, st)
    st.t0 = st.t0 or game.time()
    return game.time() - st.t0 >= (s.time or 0)
  end
  A.AiStop = function() T().ai = false; return true end
  A.AiGo = function() T().ai = true; return true end
  A.SetBattleFlags = function(s) flags_set(s.flags, s.method ~= "disable"); bump(); return true end
  A.SetPointFlags = function(s) point_flags(s); bump(); return true end
  A.SetMovePercent = function(s) T().percent = s.percent or 100; game.set_var("bw_percent", T().percent); bump(); return true end
  A.SetGameplayTimescale = function(s)
    hook(s)
    T().timescale = s.targetTimeScale or 1
    bump()
    return true
  end
  A.SetServiceTimescale = function(s) hook(s); return true end
  A.SetPlayerInvincible = function(s)
    T().invincible[slot(s.player or "player")] = s.invincible ~= 0 and s.invincible ~= false
    return true
  end
  A.SetPointOwner = function(s)
    for _, id in ipairs(s.points or {}) do
      local h = point(id)
      if h then game.set_owner(h, slot(s.player)) end
    end
    return true
  end
  A.PointSetUnits = function(s)
    for _, id in ipairs(s.pointIds or s.points or {}) do
      local h = point(id)
      if h then game.set_garrison(h, s.resultAmount or s.amount or s.count or 0) end
    end
    return true
  end
  A.PointAddUnits = function(s, st)
    -- amount over duration (0 = at once): integer units as the share of the time passes
    local dur = s.duration or 0
    st.t0 = st.t0 or game.time()
    st.added = st.added or 0
    local total = s.amount or 0
    local want = total
    if dur > 0 then
      local k = (game.time() - st.t0) / dur
      if k < 1 then want = fx.int(fx(total) * k) end
    end
    local delta = want - st.added
    if delta > 0 then
      for _, id in ipairs(s.pointIds or {}) do
        local h = point(id)
        local u = h and game.unit(h)
        if u and u.garrison then game.set_garrison(h, u.garrison + delta) end
      end
      st.added = want
    end
    return st.added >= total
  end
  A.MoveUnits = function(s)
    local to = point(s.toPoint)
    if not to then return true end
    for _, id in ipairs(s.fromPoints or {}) do
      local h = point(id)
      local u = h and game.unit(h)
      if u and u.garrison and u.garrison > 0 then
        local owner = game.owner(h)
        if owner == 0 then goto continue end   -- neutral garrisons don't send (no player to command them)
        local pct = 100
        if s.count and s.count > 0 then pct = math.max(1, math.min(100, (s.count * 100) // u.garrison)) end
        game.command(owner, {type = "send", units = {h}, target = to, param = pct})
      end
      ::continue::
    end
    return true
  end
  A.PlayerVictory = function() game.victory(human()); return true end
  A.PlayerDefeat = function() game.defeat(human()); return true end

  A.WaitMoveUnits = function(s, st)
    local t = T()
    if st.base == nil then st.base = #t.sends; st.asked = false end
    local from, to = point(s.pointFromId), point(s.pointToId)
    local n = count_since(t.sends, st.base, function(e)
      return (from == nil or e.from == from) and (to == nil or e.to == to)
    end)
    if auto() and not st.asked and game.time() > fx(1) then st.asked = true; auto_send(s.pointFromId, s.pointToId) end
    return n >= (s.moveTimes or 1)
  end
  A.MultiselectWaitMoveUnits = A.WaitMoveUnits
  A.WaitPointCaught = function(s, st)
    local t = T()
    if st.base == nil then st.base = #t.captures end
    local target = point(s.targetPointId)
    local who = (s.anyPlayer == 1 or s.anyPlayer == true) and nil or slot(s.player)
    if auto() and not st.asked and who == human() then st.asked = true; auto_send(-1, s.targetPointId) end
    return count_since(t.captures, st.base, function(e)
      return (target == nil or e.unit == target) and (who == nil or e.owner == who)
    end) > 0 or (target ~= nil and who ~= nil and game.owner(target) == who and st.base == 0 and false)
  end
  A.WaitPointUpgradeStarted = function(s, st)
    local t = T()
    if st.base == nil then st.base = #t.upgrade_orders end
    local target = point(s.targetPointId)
    if auto() and not st.asked then
      st.asked = true
      local h = target or points()[1]
      if h then game.command(human(), {type = "upgrade", units = {h}}) end
    end
    return count_since(t.upgrade_orders, st.base, function(e) return target == nil or e.unit == target end) > 0
  end
  A.WaitPointUpgradeFinished = function(s, st)
    local t = T()
    if st.base == nil then st.base = #t.upgrades end
    local target = point(s.targetPointId)
    if auto() and game.time() - (st.t0 or game.time()) > fx(20) then return true end
    st.t0 = st.t0 or game.time()
    return count_since(t.upgrades, st.base, function(e) return target == nil or e.unit == target end) > 0
  end
  A.WaitPointConvertStarted = function(s, st)
    local t = T()
    if st.base == nil then st.base = #t.converts end
    local target = point(s.targetPointId)
    if auto() then return true end
    return count_since(t.converts, st.base, function(e) return target == nil or e.unit == target end) > 0
  end
  A.WaitPointConvertFinished = A.WaitPointConvertStarted
  A.WaitTowerFire = function(s, st)
    local t = T()
    if st.base == nil then st.base = #t.fires end
    if auto() then return true end
    local target = point(s.towerPointId or s.targetPointId)
    return count_since(t.fires, st.base, function(e) return target == nil or e.unit == target end) > 0
  end
  A.WaitNoUnitsOnTheScreen = function(s)
    -- no units of that player walking between points (BW: the screen is clear of its moving units)
    if s.player then return game.count{kind = "unit", owner = slot(s.player)} == 0 end
    return game.count{kind = "unit"} == 0
  end
  A.PointConvert = function(s)
    -- BW converts a point to another building type (house / tower / smithy): the BW data port maps the types
    if hook(s, human()) == nil then game.set_var("bw_convert_" .. tostring(s.targetPointId or -1), s.targetCastType or 0) end
    return true
  end

  -- BW skills, energy, morale: the skills port (agent-bw-skills) can implement these through bw_tutorial_hooks
  A.SetEnergy = function(s) if hook(s, human()) == nil then game.set_var("bw_energy", s.energy or 0) end; return true end
  A.SetMorale = function(s) if hook(s, slot(s.player)) == nil then game.set_var("bw_morale_" .. tostring(slot(s.player) or 0), s.stars or s.morale or 0) end; return true end
  A.SetSkillCooldown = function(s)
    if hook(s, human()) == nil then game.set_var("bw_skill_cooldown_" .. tostring(s.skillSlot or 0), s.cooldown or s.time or 0) end
    return true
  end
  A.SetSkillForever = function(s) hook(s, human()); return true end
  A.ApplySkill = function(s) hook(s, slot(s.player)); return true end
  A.ApplySkillEnvironment = function(s) hook(s, slot(s.player)); return true end
  A.WaitSkillBecomeAvailable = function(s)
    local r = hook(s, human())
    return r == nil or r == true
  end
  A.SkillWaitUse = function(s, st)
    local t = T()
    if st.base == nil then st.base = #t.skills end
    local r = hook(s, human())
    if r ~= nil then return r end
    if auto() then return true end
    return #t.skills > st.base
  end

  -- UI: the view draws / opens, the player's clicks come back as commands
  A.ShowWindow = function(s) local w = ui().windows; w[#w + 1] = s.window or ""; bump(); return true end
  A.CloseWindow = function(s)
    local w = ui().windows
    for i = #w, 1, -1 do if w[i] == s.window then table.remove(w, i) end end
    bump()
    return true
  end
  A.CloseAllWindows = function() ui().windows = {}; bump(); return true end
  A.WaitWindowClose = function(s, st)
    local t = T()
    st.t0 = st.t0 or game.time()
    if auto() and game.time() - st.t0 >= 1 then return true end
    for _, w in ipairs(ui().windows) do if w == s.window then return t.windows_closed[s.window or ""] == true end end
    return true
  end
  A.WaitForClick = function(s, st)
    local t = T()
    if st.base == nil then st.base = t.clicks; st.t0 = game.time(); ui().waiting_click = true; bump() end
    if auto() and game.time() - st.t0 >= 1 then t.clicks = t.clicks + 1 end
    if t.clicks > st.base then ui().waiting_click = false; bump(); return true end
    return false
  end
  A.WaitBuildingMenuOpened = function(s, st)
    local t = T()
    if st.base == nil then st.base = t.menus; st.t0 = game.time() end
    if auto() and game.time() - st.t0 >= 1 then return true end
    return t.menus > st.base
  end
  A.WaitMovePercent = function(s, st)
    local t = T()
    if auto() then return true end
    return t.percent == (s.percent or t.percent) and t.percent_set > 0
  end
  A.FingerTip = function(s)
    local f = ui().finger or {}
    for _, op in ipairs(s.ops or {}) do
      if op == "hide" then f = nil break end
      if op == "show" then f.visible = true end
      if op == "animate" then f.anim = s.animationParam end
      if op == "move" or op == "show" then
        f.from = {kind = s.fromType, id = s.indexFrom, shift = s.shiftFrom}
        f.to = {kind = s.toType, id = s.indexTo, shift = s.shiftTo}
        f.duration = s.moveDuration or 0
        f.since = game.tick()
      end
    end
    if f then
      -- the view needs positions: resolve point ids now (BW ids -> world positions)
      for _, k in ipairs({"from", "to"}) do
        local e = f[k]
        if e and e.kind == "point" then
          local h = point(e.id)
          if h then local x, z = game.pos(h); e.x, e.z = x, z end
        end
      end
    end
    ui().finger = f
    bump()
    return true
  end
  A.FingerTipUpgrade = function(s) ui().upgrade_tip = {times = s.showTimes or 1, period = s.showPeriod or 1, since = game.tick()}; bump(); return true end
  A.ShowHideComponent = function(s) ui().hidden[s.tag or s.component or ""] = not (s.visible == 1 or s.visible == true); bump(); return true end
  A.ShowHideMenuButton = function(s) ui().hidden.menu = not (s.visible == 1 or s.visible == true or s.show == 1); bump(); return true end
  A.ShowHidePlayersBar = function(s) ui().hidden.playersBar = not (s.visible == 1 or s.visible == true or s.show == 1); bump(); return true end
  A.ShowHidePointComponent = function(s)
    ui().point_hidden[tostring(s.pointId or -1) .. ":" .. (s.tag or "")] = not (s.visible == 1 or s.visible == true)
    bump()
    return true
  end
  A.ScreenMove = function(s) ui().camera = {x = s.position and s.position[1] or 0, z = s.position and s.position[2] or 0, duration = s.duration or 0, since = game.tick()}; bump(); return true end
  A.CameraScale = function(s)
    local c = {zoom = s.targetScale or 1, duration = s.duration or 0, since = game.tick()}
    if s.targetPosition then c.x, c.z = s.targetPosition[1], s.targetPosition[2] end
    ui().camera = c
    bump()
    return true
  end
  A.AnimationTrigger = function(s) ui().animation = {name = s.triggerName or s.trigger or "", since = game.tick()}; bump(); return true end
  A.MultiselectChangeWait = function() return true end
  A.MultiselectIgnoreModeChange = function() return true end
  A.SetForceMultiselectMode = function() return true end
  A.Missing = function() return true end

  local function run_step(s, st)
    local f = A[s.action]
    if not f then
      if not T().warned then T().warned = {} end
      if not T().warned[s.action] then T().warned[s.action] = true; game.log("bw tutorial: unknown action " .. tostring(s.action)) end
      return true
    end
    -- one broken step must not stop the tutorial (an error would disable the tick handler for the match)
    local ok, done = pcall(f, s, st)
    if not ok then game.log("bw tutorial: " .. tostring(s.action) .. ": " .. tostring(done)); return true end
    return done
  end

  -- GOTO pass first (as BW: chapter changes take effect before the steps of this tick)
  local function goto_pass()
    local t = T()
    local guard = 0
    local again = true
    while again and guard < 64 do
      again, guard = false, guard + 1
      for k, a in ipairs(t.active) do
        local ch = data.chapters[a.c]
        local s = ch and ch.steps[a.s]
        if not s then table.remove(t.active, k); again = true; break end
        if s.action == "GOTOChapter" then
          for _, n in ipairs(s.chaptersToDismiss or {}) do local i = chapter_index(n); if i then dismiss(i) end end
          a.s, a.st = a.s + 1, {}
          for _, n in ipairs(s.gotoChapters or {}) do local i = chapter_index(n); if i then activate(i) end end
          again = true
          break
        end
      end
    end
  end

  on("start", function()
    state.bwt = new_state()
    for _, ch in ipairs(data.before or {}) do
      for _, s in ipairs(ch.steps or {}) do run_step(s, {}) end
    end
  end)

  on("tick", function()
    local t = T()
    if not t or t.over then return end
    if not t.started then t.started = true; if data.chapters and data.chapters[1] then activate(1) end end
    goto_pass()
    for _, a in ipairs(t.active) do
      local ch = data.chapters[a.c]
      local s = ch and ch.steps[a.s]
      if s and s.action ~= "GOTOChapter" and run_step(s, a.st) then
        if auto() then game.log("bw tutorial: " .. (ch.name ~= "" and ch.name or "#" .. a.c) .. " " .. a.s .. " " .. s.action .. " done") end
        a.s, a.st = a.s + 1, {}
      end
    end
    if #t.active == 0 then
      t.over = true; t.ai = true; ui().windows = {}; ui().finger = nil; bump()
      game.log("bw tutorial: finished")
    end
  end)

  -- events the waits count
  on("captured", function(e) local t = T(); if t then t.captures[#t.captures + 1] = {unit = e.unit, owner = e.owner} end end)
  on("upgraded", function(e) local t = T(); if t then t.upgrades[#t.upgrades + 1] = {unit = e.unit} end end)
  on("attack", function(e)
    local t = T()
    if t and e.unit and game.alive(e.unit) and game.unit(e.unit).structure then t.fires[#t.fires + 1] = {unit = e.unit} end
  end)
  on("ability", function(e) local t = T(); if t and e.player == human() then t.skills[#t.skills + 1] = {ability = e.ability} end end)
  on("player_ability", function(e) local t = T(); if t and e.player == human() then t.skills[#t.skills + 1] = {ability = e.value} end end)

  -- commands: AI pause, battle / point flags, the player's moves counted for the waits
  local prev_before = before_command
  function before_command(player, c)
    local t = T()
    if t and not t.over then
      local P = human()
      if player ~= P and player ~= 0 and not t.ai then return false end
      if player == P then
        if t.flags.noInput then return false end
        local h = c.units and c.units[1]
        if c.type == "send" then
          if point_blocked(h, "moveOut") or point_blocked(c.target, "moveIn") then return false end
          t.sends[#t.sends + 1] = {from = h, to = c.target}
        elseif c.type == "upgrade" then
          if t.flags.noUpgrade or point_blocked(h, "upgrade") then return false end
          t.upgrade_orders[#t.upgrade_orders + 1] = {unit = h}
        elseif c.type == "transform" or c.type == "convert" then
          if point_blocked(h, "convert") then return false end
          t.converts[#t.converts + 1] = {unit = h}
        elseif (c.type == "cast" or c.type == "player_ability") and (t.flags.noSkills or point_blocked(h, "skills")) then
          return false
        end
      end
    end
    if prev_before then return prev_before(player, c) end
  end

  local prev_damage = before_damage
  function before_damage(e)
    local t = T()
    if t and not t.over and e.unit and t.invincible[game.owner(e.unit)] then return 0 end
    if prev_damage then return prev_damage(e) end
  end

  -- hidden commands from the view (rules "commands", "card": false)
  function bw_tutorial_command(player, id, args)
    local t = T()
    if not t or player ~= human() then return false end
    if id == "bw_tut_click" then t.clicks = t.clicks + 1
    elseif id == "bw_tut_window" then t.windows_closed[(args and args.window) or ""] = true
    elseif id == "bw_tut_menu" then t.menus = t.menus + 1
    elseif id == "bw_tut_percent" then t.percent = (args and args.percent) or t.percent; t.percent_set = t.percent_set + 1
    elseif id == "bw_tut_skip" or id == "bw_tut_off" then
      t.over, t.ai, t.active, t.skipped = true, true, {}, id == "bw_tut_skip"
      ui().windows, ui().finger = {}, nil
      bump()
    else return false end
    return true
  end
end
