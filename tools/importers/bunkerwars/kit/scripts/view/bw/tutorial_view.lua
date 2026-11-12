-- Bunker Wars tutorial presentation (tools/importers/bunkerwars/kit): reads state.bwt.ui of scripts/bw/tutorial.lua and
-- draws BW's finger tip (a hand moving from one point to another, a ring on the point to tap), the upgrade hint,
-- and the tutorial windows. A window "X" opens its document from view.bw.tutorial_windows (client.py converts
-- BW's window prefabs with the UI.Windows importer into ui/hud/X.rml), else the generic panel ui/hud/bw_tutorial.rml
-- with the text of lang key "bw.tut.X". A click on the panel / the window / the screen while a WaitForClick runs sends bw_tut_click.
local okw, own = pcall(function() return require("view.bw.tutorial_windows") end)   -- window -> its ui/hud document (client.py)
if not okw or type(own) ~= "table" then own = {} end
local seq, open, waiting = -1, {}, false
local pulse = 0
local texts = {}
ui.set("bwt", {text = "", title = "", click = false})

local function tr(key)
  if texts[key] then return texts[key] end
  local ok, s = view.ui_call("game.tr", key)
  if not ok or s == nil or s == "" or s == key then s = "" end
  texts[key] = s
  return s
end

local function push(out, ...) for _, v in ipairs({...}) do out[#out + 1] = v end end
local function ring(cx, cy, r, w, rgba, out)
  local n = 32
  for i = 0, n - 1 do
    local a0, a1 = i / n * math.pi * 2, (i + 1) / n * math.pi * 2
    local c0, s0, c1, s1 = math.cos(a0), math.sin(a0), math.cos(a1), math.sin(a1)
    push(out, cx + c0 * r, cy + s0 * r, rgba, cx + c1 * r, cy + s1 * r, rgba, cx + c1 * (r + w), cy + s1 * (r + w), rgba,
         cx + c0 * r, cy + s0 * r, rgba, cx + c1 * (r + w), cy + s1 * (r + w), rgba, cx + c0 * (r + w), cy + s0 * (r + w), rgba)
  end
end
local function disc(cx, cy, r, rgba, out)
  local n = 20
  for i = 0, n - 1 do
    local a0, a1 = i / n * math.pi * 2, (i + 1) / n * math.pi * 2
    push(out, cx, cy, rgba, cx + math.cos(a0) * r, cy + math.sin(a0) * r, rgba, cx + math.cos(a1) * r, cy + math.sin(a1) * r, rgba)
  end
end
-- a simple pointing hand: palm disc + finger capsule pointing at (x, y)
local function hand(x, y, pressed, out)
  local col, edge = 0xFFFFFFFF, 0x202020E0
  local d = pressed and 4 or 12
  disc(x + 18 + d, y + 34 + d, 17, edge, out)
  disc(x + 18 + d, y + 34 + d, 15, col, out)
  push(out, x - 4 + d, y + 2 + d, edge, x + 8 + d, y - 4 + d, edge, x + 26 + d, y + 26 + d, edge,
       x - 4 + d, y + 2 + d, edge, x + 26 + d, y + 26 + d, edge, x + 12 + d, y + 32 + d, edge)
  disc(x + 2 + d, y - 1 + d, 6, col, out)
end

local function world(e)
  if not e or e.x == nil then return nil end
  local sx, sy, on = view.to_screen(e.x, e.z, 0.5)
  if not on then return nil end
  local sh = e.shift or {0, 0}
  return sx + (sh[1] or 0), sy - (sh[2] or 0)
end

view.on_frame(function(dt)
  local s = view.state() or {}
  local t = s.bwt
  if view.player() == 0 or not t then return end
  local u = t.ui or {}
  pulse = pulse + dt
  if u.seq ~= seq then
    seq = u.seq
    -- windows: open the new ones, close the gone ones
    local want = {}
    for _, w in ipairs(u.windows or {}) do want[w] = true end
    for w, how in pairs(open) do
      if not want[w] then
        if how ~= "bw_hud" then ui.close(how) end
        open[w] = nil
      end
    end
    for _, w in ipairs(u.windows or {}) do
      if not open[w] then
        if own[w] then
          local doc = own[w] == true and w or own[w]
          if doc ~= "bw_hud" then ui.open(doc) end   -- HUDScreen: the battle HUD is open already
          open[w] = doc
        else
          ui.set("bwt", {text = tr("bw.tut." .. w) ~= "" and tr("bw.tut." .. w) or w, title = tr("bw.tut.title"), click = u.waiting_click == true})
          ui.open("bw_tutorial")
          open[w] = "bw_tutorial"
        end
      end
    end
    if u.waiting_click ~= waiting then
      waiting = u.waiting_click == true
      local cur = s.bwt.ui.windows and s.bwt.ui.windows[#s.bwt.ui.windows]
      if cur and open[cur] == "bw_tutorial" then
        ui.set("bwt", {text = tr("bw.tut." .. cur) ~= "" and tr("bw.tut." .. cur) or cur, title = tr("bw.tut.title"), click = waiting})
      end
    end
  end
  local verts = {}
  local f = u.finger
  if f and f.visible ~= false then
    local fx0, fy0 = world(f.from)
    local tx, ty = world(f.to)
    if fx0 and tx then
      local dur = (f.duration or 0)
      local k = 1
      if dur > 0 then k = ((view.tick and view.tick() or 0) - (f.since or 0)) / (dur * 20) end
      if k ~= k or k < 0 then k = 0 end
      local cyc = (pulse % 1.6) / 1.6   -- the hand repeats the drag
      local p = dur > 0 and cyc or 1
      local x, y = fx0 + (tx - fx0) * p, fy0 + (ty - fy0) * p
      ring(tx, ty, 30 + math.sin(pulse * 5) * 4, 4, 0xFFD24AFF, verts)
      if fx0 ~= tx or fy0 ~= ty then ring(fx0, fy0, 26, 3, 0xFFFFFFB0, verts) end
      hand(x, y, f.anim == "FingerDown" or p < 0.15, verts)
    end
  end
  if #verts > 0 then view.draw(verts, {layer = 60}) end
end)

ui.on("bwt_click", function() view.send("bw_tut_click") end)
