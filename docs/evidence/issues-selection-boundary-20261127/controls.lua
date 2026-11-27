local checked = false
view.on_frame(function()
 local units = game.units{owner=1,type="scout"}
 assert(#units == 257)
 if not checked then
  local selected = {}
  for i=1,256 do selected[i]=units[i] end
  view.select(selected)
  assert(#view.selection() == 256)
  assert(not pcall(function() view.select(units) end))
  assert(#view.selection() == 256)
  assert(not pcall(function() view.select({"invalid"}) end))
  assert(#view.selection() == 256)
  assert(not pcall(function() view.select_add(units[257]) end))
  assert(#view.selection() == 256)
  view.select({units[1],units[1]})
  assert(#view.selection() == 1)
  view.select({})
  assert(#view.selection() == 0)
  game.log("SELECTION_BOUNDARY_PASS")
  checked = true
 end
 local selected = {}
 for i=1,256 do selected[i]=units[i] end
 view.select(selected)
end)
