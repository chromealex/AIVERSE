-- Bunker Wars battle HUD numbers (rules "scripts", installed by tools/importers/bunkerwars/client.py).
-- Every 5 ticks (4 per second) the players' army sizes, energy, morale and skill cards plus the match clocks are
-- written into state.bwhud; scripts/view/bw/hud_view.lua passes them to the HUD document (ui/hud/bw_hud.rml,
-- converted from BW's HUDScreen by the UI.Windows importer, logic in ui/logic/bw_hud.lua).
-- BW sources: PlayersBarComponent (units per player), HeroManaComponent (energy), PlayerMoraleItemComponent
-- (morale stars), HeroSkillsComponent (skill cards), TimeComponent / SuddenDeathPanel / BattleRegimePanel (clocks).
-- Presentation data only: nothing here changes the battle.

local ENERGY, MORALE = "energy", "morale"

local function has_resource(p, id) return game.resources(p)[id] ~= nil end

local function army(p)
  local n = game.count{kind = "unit", owner = p}
  for _, h in ipairs(game.units{kind = "structure", owner = p}) do
    local u = game.unit(h)
    if u and u.garrison ~= nil then n = n + u.garrison end
  end
  return n
end

local function publish()
  local hud = {t = fx.int(game.time()), players = {}}
  local modes = game.modes()
  if modes then
    hud.left = modes.time_left and fx.int(modes.time_left) or nil
    hud.sd = modes.sudden_death
    hud.sd_at = modes.sudden_death_at and fx.int(modes.sudden_death_at) or nil
    hud.sd_counter = modes.sd_counter
    hud.real = fx.int(modes.real_time)
    if modes.koh then
      hud.koh = {}
      for t = 0, 15 do if modes.koh[t] then hud.koh[#hud.koh + 1] = {team = t, score = fx.int(modes.koh[t])} end end
    end
  end
  for _, p in ipairs(game.players()) do
    if p ~= 0 then
      local row = {slot = p, team = game.team(p), alive = game.player_alive(p), units = army(p)}
      if has_resource(p, ENERGY) then row.energy = fx.int(game.resource(p, ENERGY)) end
      if has_resource(p, MORALE) then
        row.morale = fx.int(game.resource(p, MORALE))
        local lv = modes and modes.levels and modes.levels[MORALE]
        row.stars = lv and lv[p] or 0
      end
      local cards = {}
      for _, a in ipairs(game.player_abilities(p)) do
        if a.owned and not a.automatic then
          cards[#cards + 1] = {i = a.index, id = a.id, usable = a.usable, ready_in = fx.int(a.ready_in),
                               cooldown = fx.int(a.cooldown), energy = a.cost[ENERGY] and fx.int(a.cost[ENERGY]) or 0}
        end
      end
      row.cards = cards
      hud.players[#hud.players + 1] = row
    end
  end
  state.bwhud = hud
end

on("tick", function()
  if game.tick() % 5 == 0 then publish() end
end)
