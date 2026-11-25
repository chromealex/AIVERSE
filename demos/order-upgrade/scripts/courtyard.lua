function on_start()
  game.message(1, "Select the scout and right-click the ground to issue orders. The guard house upgrades in two seconds.")
  game.after(2, "start_upgrade", 0)
end

function start_upgrade()
  local houses = game.units{type = "outpost", owner = 1}
  if houses[1] then
    game.command(1, {type = "upgrade", units = {houses[1]}, param = "watchtower"})
  end
end
