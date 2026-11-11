-- Complete graph authoring from Lua: nested state machine, motion search, rig and custom trait.
view.animation_trait("breathing", {
    stateSize = 1,
    post_update = function(params, state, dt, config)
        state[1] = (state[1] + dt) % 100
        return state
    end,
    post_evaluate = function(params, state, pose, config)
        -- Pose arrays use 1-based model node indices (golem node 3 = spine).
        local amount = 1 + math.sin(state[1] * config.frequency) * params.breath
        pose[3].s[1] = pose[3].s[1] * amount
        pose[3].s[2] = pose[3].s[2] * amount
        return pose
    end
})
view.animation_define("uaf_character", {
    parameters = { future = {0.3,0,0}, headTarget = {2,2,0}, breath = 0.03, dead = false },
    graphs = {
        locomotion = {
            parameters = { trajectory = {0.3,0,0}, dead = false },
            nodes = {
                { id="motion", kind="motionMatching", database={
                    root="root", clips={"Idle","Walk","Run"}, bones={"leg.L","leg.R"},
                    trajectory={{time=0.3,parameter="trajectory"}}, fps=20,
                    poseWeight=0.2, velocityWeight=0.02, trajectoryWeight=10,
                    searchInterval=0.1, maxCandidates=1024, blend=0.15, stripRoot=true
                }},
                { id="death", kind="clip", clip="Death", loop=false }
            },
            initial="alive",
            states={
                {id="alive",output="motion",transitions={{to="dead",fade=0.15,when={{parameter="dead",op="eq",value=1}}}}},
                {id="dead",output="death"}
            }
        }
    },
    nodes={
        {id="locomotion",kind="subgraph",graph="locomotion",bindings={trajectory="future",dead="dead"}},
        {id="rig",kind="rig",input="locomotion",units={
            {op="aim",bone="head",axis={0,0,1},target="headTarget",weight=0.3}
        }},
        {id="output",kind="stack",input="rig",traits={{trait="breathing",config={frequency=3.5}}}}
    },output="output"
})
local attached = {}
local time = 0
view.on_frame(function(dt)
    time = time + dt
    for _, actor in ipairs(game.units{player=1,type="footman"}) do
        if not attached[actor] then view.animation_use(actor,"uaf_character"); attached[actor]=true end
        -- Desired root-local displacement at +0.3 seconds, independent of root movement authority.
        local speed = (math.floor(time / 2) % 3 == 0) and 0 or ((math.floor(time / 2) % 3 == 1) and 1 or 3)
        view.animation(actor,{future={speed*0.3,0,0},headTarget={math.sin(time)*2,2,2}})
    end
end)
