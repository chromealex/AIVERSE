local function angle(yaw, zoom)
    view.set_camera({x=10, z=10, pitch=38, yaw=yaw, zoom=zoom})
end
ui.open("gallery")
ui.on("front", function() angle(25,11) end)
ui.on("side", function() angle(115,11) end)
ui.on("wide", function() angle(25,19) end)
local initial=true
view.on_frame(function() if initial then angle(25,11);initial=false end end)
