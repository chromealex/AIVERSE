-- Copy contacts.json into <project>/animations/contacts.json.
-- The actor type must use models/golem.glb and view.programmableAnimation=true.
view.animation_load("contacts", "contacts")
local actor = game.units{player=1}[1]
if actor then
    view.animation_use(actor, "contacts")
    view.animation(actor, {plantLeft=1, plantRight=1, obstacle={0.8,1.2,0}})
    -- For a swing phase: view.animation(actor, {plantLeft=0}).
    -- After a rendered frame: view.animation_status(actor) reports supported,
    -- stable, supports, collisions, penetration, balanceError and supportError.
end
