-- Tutorial presentation (game wizard kit, templates/_wizard): the panel with the step text (ui/hud/wiz_tutorial.rml),
-- the arrow and the ring over what the step points at, and in hard mode a dimmed screen with a window around it.
-- Reads the simulation's state.wiz_tut (scripts/wizard/tutorial.lua) and the steps (scripts/wizard/tutorial_steps.lua).
local cfg = require("wizard.tutorial_steps")
local shown, last_step, pulse = false, -1, 0
local host_checked = false
local texts = {}
ui.set("tut", {text = "", title = "", step = 0, total = #cfg.steps, next = false, skip = false, need = 1, n = 0, hard = true})
ui.set("tut_n", 0)   -- every key the document binds is set before its first ui.open

local function tr(key)
  if type(key) ~= "string" then return "" end
  if texts[key] then return texts[key] end
  local ok, s = view.ui_call("game.tr", key)
  if not ok or s == nil or s == "" or s == key then s = key end
  texts[key] = s
  return s
end

local function ring(cx, cy, r, w, rgba, out)
  local n = 36
  for i = 0, n - 1 do
    local a0, a1 = i / n * math.pi * 2, (i + 1) / n * math.pi * 2
    local x0, y0 = cx + math.cos(a0) * r, cy + math.sin(a0) * r
    local x1, y1 = cx + math.cos(a1) * r, cy + math.sin(a1) * r
    local X0, Y0 = cx + math.cos(a0) * (r + w), cy + math.sin(a0) * (r + w)
    local X1, Y1 = cx + math.cos(a1) * (r + w), cy + math.sin(a1) * (r + w)
    for _, v in ipairs({x0, y0, rgba, x1, y1, rgba, X1, Y1, rgba, x0, y0, rgba, X1, Y1, rgba, X0, Y0, rgba}) do out[#out + 1] = v end
  end
end

local function quad(x0, y0, x1, y1, rgba, out)
  for _, v in ipairs({x0, y0, rgba, x1, y0, rgba, x1, y1, rgba, x0, y0, rgba, x1, y1, rgba, x0, y1, rgba}) do out[#out + 1] = v end
end

local function arrow(cx, top, rgba, out)   -- points down at (cx, top)
  local w, h, sw, sh = 22, 20, 9, 26
  for _, v in ipairs({cx - w, top - h, rgba, cx + w, top - h, rgba, cx, top, rgba}) do out[#out + 1] = v end
  quad(cx - sw, top - h - sh, cx + sw, top - h, rgba, out)
end

view.on_frame(function(dt)
  local t = (view.state() or {}).wiz_tut
  if view.player() == 0 or not t then return end
  if t.over then
    if shown then ui.close("wiz_tutorial"); shown = false end
    return
  end
  local s = cfg.steps[t.i]
  if not s then return end
  if not host_checked then   -- no game UI here (the editor's Game view): the panel can't be shown, so nothing gets locked
    host_checked = true
    if not view.ui_call("game.tr", "wiz.tut.title") then view.send("wiz_tut_off") return end
  end
  if t.i ~= last_step then
    last_step = t.i
    ui.set("tut", {text = tr(s.text), title = tr(s.title or "wiz.tut.title"), step = t.i, total = #cfg.steps,
                   next = s.done and s.done.next and true or false, skip = cfg.skippable ~= false,
                   need = t.need or 1, n = t.n or 0, hard = cfg.mode ~= "soft"})
    if not shown then ui.open("wiz_tutorial"); shown = true end
    if s.sound then view.sound(s.sound, 0, 0) end
  else
    ui.set("tut_n", t.n or 0)
  end
  pulse = pulse + dt
  if not t.x then return end
  local sx, sy, on = view.to_screen(t.x, t.z, 0.5)
  if not on then return end
  local verts = {}
  local r = 46 + math.sin(pulse * 4) * 6
  if cfg.mode ~= "soft" and cfg.dim ~= false then   -- dim everything but a window around the focus
    local d, R = 0x00000078, r + 18
    quad(-20000, -20000, 20000, sy - R, d, verts)
    quad(-20000, sy + R, 20000, 20000, d, verts)
    quad(-20000, sy - R, sx - R, sy + R, d, verts)
    quad(sx + R, sy - R, 20000, sy + R, d, verts)
  end
  local accent = cfg.color or 0xFFD24AFF
  ring(sx, sy, r, 5, accent, verts)
  arrow(sx, sy - r - 8 - math.abs(math.sin(pulse * 3)) * 14, accent, verts)
  view.draw(verts, {layer = 50})
end)
