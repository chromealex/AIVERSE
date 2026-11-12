-- Lua hooks of the Extraction Raid sample (docs/raid.md). Simulation script: deterministic, state in `state`.

function on_start()
  state.freed = 0
  state.extracted = {}
end

-- interact "fn" of the robot: freeing it also drops a medkit next to it
function on_robot_freed(ctx)
  state.freed = state.freed + 1
  raid.give(ctx.player, "medkit", 1)
end

-- veto hook: the landing zone refuses raiders without loot (the helicopter takes anyone)
function raid_can_extract(player, point)
  if point ~= "lz" then return true end
  local p = raid.player(player)
  for _, n in pairs(p.carried) do if n > 0 then return true end end
  return false, "Bring something back!"
end

on("raid_extracted", function(e)
  state.extracted[#state.extracted + 1] = e.player
end)
