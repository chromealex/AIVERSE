local labels = true
ui.set("label_action", "Hide order labels")
ui.open("courtyard")
ui.on("toggle_labels", function()
  labels = not labels
  ui.set("label_action", labels and "Hide order labels" or "Show order labels")
end)
view.on("order", function(order)
  if labels then view.notice("Order: " .. order.kind) end
end)
