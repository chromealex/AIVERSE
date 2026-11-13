function on_start() state.kills = 0; state.choices = 0; state.boss = false end
on("unit_died", function(e)
  if e.unit_type == "boss" then state.boss = true end
  if e.owner == 2 then state.kills = state.kills + 1; game.set_var("kills", state.kills) end
end)
function on_command(player, id, args)
  local heroes = game.units{owner=player,type="hero"}
  if #heroes == 0 then return false end
  if id == "move_hero" then game.order(heroes,"move",args.where.x,args.where.z); return true end
  return false
end
