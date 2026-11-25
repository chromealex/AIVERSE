function on_start()
  game.message(1, "Select a scout to see its portrait. Use Possess to enter the outpost in first person.")
end

on("possessed", function(e)
  if e.unit then
    game.message(e.player, "Move with WASD, look with the mouse, crouch with Ctrl. Escape returns to the campaign.")
  end
end)

local parts = {"head", "torso", "leg"}
on("campaign_wound", function(e)
  game.message(e.player, "Scout wounded: " .. parts[e.value])
end)

on("campaign_fight_end", function(e)
  if e.value == 1 and e.other and game.alive(e.unit) then
    game.set_owner(e.unit, game.owner(e.other))
    game.message(e.player, "The watch captured the scout.")
  else
    game.message(e.player, "The scout retreated.")
  end
end)
