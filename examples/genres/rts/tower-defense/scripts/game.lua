function on_start() state.kills = 0; state.choices = 0; state.boss = false end
on("unit_died", function(e)
  if e.unit_type == "boss" then state.boss = true end
  if e.owner == 2 then state.kills = state.kills + 1; game.set_var("kills", state.kills) end
end)
