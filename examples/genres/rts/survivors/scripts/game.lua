function on_start() state.kills = 0; state.choices = 0; state.boss = false end
on("unit_died", function(e)
  if e.unit_type == "boss" then state.boss = true end
  if e.owner == 2 then state.kills = state.kills + 1; game.set_var("kills", state.kills) end
end)
function on_command(player, id, args)
  local heroes = game.units{owner=player,type="hero"}
  if #heroes == 0 then return false end
  if id == "move_hero" then game.order(heroes,"move",args.where.x,args.where.z); return true end
  if (id == "power" or id == "speed") and state.choices > 0 then
    game.buff(heroes[1],id == "power" and "damage" or "speed",1,1)
    state.choices = state.choices - 1; game.set_var("choices",state.choices); return true
  end
  return false
end
every(1,function()
  if state.kills >= 12 and state.boss then game.victory(1) end
end)
on("level_up",function(e)
  if e.player == 1 then state.choices=state.choices+1; game.set_var("choices",state.choices) end
end)
