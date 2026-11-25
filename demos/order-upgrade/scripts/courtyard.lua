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

function on_command(player, id)
  if player ~= 1 or id ~= "restart_upgrade" then return false end
  local houses = game.units{type = "outpost", owner = player}
  local towers = game.units{type = "watchtower", owner = player}
  if houses[1] then
    game.command(player, {type = "cancel", units = {houses[1]}, param = 0})
  elseif towers[1] then
    game.transform(towers[1], "outpost")
  end
  start_upgrade()
end
