local function frame(zoom)
    view.set_camera({x = 10, z = 10, zoom = zoom, pitch = 35, yaw = 25})
end

ui.open("distance")
ui.on("near", function() frame(9) end)
ui.on("far", function() frame(34) end)
ui.on("reset", function() frame(16) end)

local first = true
view.on_frame(function()
    if first then frame(16); first = false end
    local camera = view.camera()
    if camera then ui.set("distance", math.floor(camera.zoom + 0.5)) end
end)
