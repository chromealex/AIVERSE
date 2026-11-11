-- Save fullbody.json as <project>/animations/fullbody.json, using the golem model.
-- Enable view.programmableAnimation on the type before assigning a dynamic graph.
view.animation_load("body", "fullbody")
local attached = {}
local time = 0
view.on_frame(function(dt)
    time = time + dt
    for _, actor in ipairs(game.units{player=1,type="footman"}) do
        if not attached[actor] then view.animation_use(actor,"body");attached[actor]=true end
        view.animation(actor, {left={0.5,1.65,math.sin(time)*0.2}, strength=1})
    end
end)
