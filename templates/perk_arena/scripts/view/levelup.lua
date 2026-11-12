-- View side: hands the level-up cards to the game's UI Lua and sends the card the player clicked.
-- The UI (RmlUi + UI Lua) defines LevelUp.show(cards, revision, rerolls), LevelUp.hide(), LevelUp.take() -> card
-- index, "reroll" or nil, and LevelUp.xp(progress 0..1, level) for the xp bar. Without a UI Lua (headless, tools)
-- view.ui_call returns false: the cards go to a notice, and the bot / simulation Lua picks.
local shown = nil

local function playing()
  if not progression.active() then return false end
  for _, s in ipairs(progression.players()) do
    if s == view.player() then return true end
  end
  return false
end

local function show()
  if not playing() then return end
  local offer = progression.offer(view.player())
  if not offer then
    if shown then view.ui_call("LevelUp.hide") end
    shown = nil
    return
  end
  if shown == offer.revision then return end
  shown = offer.revision
  local ok = view.ui_call("LevelUp.show", offer.cards, offer.revision, offer.rerolls)
  if not ok then
    local names = {}
    for i, c in ipairs(offer.cards) do names[#names + 1] = i .. ": " .. (c.perk or ("recruit " .. (c.unit or "?"))) end
    view.notice("Level " .. offer.level .. " - " .. table.concat(names, "  "))
  end
end

view.on("progression_offer", function(e) if e.player == view.player() then show() end end)

-- xp orbs and summon circles on the ground (the engine view draws nothing progression-specific by itself)
local function num(v) if math.type(v) == "fx" then return fx.int(v * 1000) / 1000 end return v end
local function quad(t, x, z, r, rgba)
  local y = 0.06
  local a = {x - r, y, z, rgba, x, y, z - r, rgba, x + r, y, z, rgba}
  local b = {x - r, y, z, rgba, x + r, y, z, rgba, x, y, z + r, rgba}
  for _, v in ipairs(a) do t[#t + 1] = v end
  for _, v in ipairs(b) do t[#t + 1] = v end
end
local function ring(t, x, z, r, w, rgba, fill)
  local n = 32
  for i = 0, n - 1 do
    local a0, a1 = i / n * 2 * math.pi, (i + 1) / n * 2 * math.pi
    local c0, s0, c1, s1 = math.cos(a0), math.sin(a0), math.cos(a1), math.sin(a1)
    local ri = r - w
    if fill and fill > 0 and i < n * fill then ri = 0 end
    local y = 0.05
    local q = {x + c0 * ri, y, z + s0 * ri, rgba, x + c0 * r, y, z + s0 * r, rgba, x + c1 * r, y, z + s1 * r, rgba,
               x + c0 * ri, y, z + s0 * ri, rgba, x + c1 * r, y, z + s1 * r, rgba, x + c1 * ri, y, z + s1 * ri, rgba}
    for _, v in ipairs(q) do t[#t + 1] = v end
  end
end

view.on_frame(function()
  if not progression.active() then return end
  local verts = {}
  for _, o in ipairs(progression.orbs()) do quad(verts, num(o.x), num(o.z), o.ready and 0.22 or 0.14, 0x55d8ffdd) end
  for _, c in ipairs(progression.points(view.player())) do
    local col = (c.state == "ready" or c.state == "progress") and 0x7dff9acc or 0x88888888
    ring(verts, num(c.x), num(c.z), num(c.radius), 0.12, col, num(c.progress))
  end
  if #verts > 0 then view.draw_ground(verts, {layer = 1}) end
end)

view.on_frame(function()
  if not playing() then return end
  if shown then
    local ok, card = view.ui_call("LevelUp.take")
    if ok and card == "reroll" then view.progression_reroll()
    elseif ok and card then view.progression_choose(card) end
  end
  local p = progression.player(view.player())
  view.ui_call("LevelUp.xp", p.progress, p.level)
end)
