function on_command(player, id, args)
  local source, target
  if id == "enable_oaks" then
    source, target = "oak", "markedOak"
  elseif id == "disable_oaks" then
    source, target = "markedOak", "oak"
  else
    return false
  end
  for _, oak in ipairs(game.units{owner = player, type = source}) do
    game.transform(oak, target)
  end
end
