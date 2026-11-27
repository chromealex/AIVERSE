ui.on("keydown", function(event)
 if event.key == "F7" then
  view.select(game.units{owner=1,type="tower"})
  return true
 end
end)
