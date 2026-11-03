-- Meta upgrades in the match (game wizard kit, templates/_wizard): the menu passes the levels bought in the upgrades
-- screen as initialVars wiz_up_<id> (ui/wizard_meta.lua); this script turns them into stat bonuses for the player's
-- units and extra start resources. Data: scripts/wizard/meta_config.lua. A match without the vars (online, the editor,
-- replays of old versions) plays with no bonus. Deterministic: integers and fx only.
local cfg = require("wizard.meta_config")
local P = cfg.player or 1

local function level(id) return fx.int(game.var("wiz_up_" .. id)) end

local function apply(h)
  if not state.wiz_meta or game.owner(h) ~= P or game.has_data(h, "wiz_b") then return end   -- once per unit (spawned + trained both fire)
  game.data(h).wiz_b = true
  local hp, max = game.hp(h)
  for _, u in ipairs(cfg.upgrades or {}) do
    if u.stat then
      local l = level(u.id)
      if l > 0 then
        if u.percent then game.buff(h, u.stat, 0, fx(100 + u.percent * l, 100)) end
        if u.add then game.buff(h, u.stat, u.add * l, 1) end
      end
    end
  end
  local _, newMax = game.hp(h)   -- a tougher unit starts as healthy as it was (same share of its health)
  if hp and max and newMax and max > 0 and newMax > max then game.set_hp(h, hp * newMax / max) end
end

on("start", function()
  for _, u in ipairs(cfg.upgrades or {}) do
    local l = level(u.id)
    if u.stat and l > 0 then state.wiz_meta = true end
    if u.resource and l > 0 then game.give(P, u.resource, (u.amount or 50) * l) end
  end
  for _, h in ipairs(game.units{owner = P}) do apply(h) end
end)

on("spawned", function(e) apply(e.unit) end)
on("trained", function(e) apply(e.unit) end)
on("built", function(e) apply(e.unit) end)
