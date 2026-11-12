-- Bunker Wars battle HUD (rules "viewScripts", installed by tools/importers/bunkerwars/client.py): opens
-- ui/hud/bw_hud.rml (BW HUDScreen through the UI.Windows importer) and hands it the numbers scripts/bw/hud.lua
-- publishes (ui.set("bw", ...); ui/logic/bw_hud.lua writes them into the document). Pad emotions
-- (ClientGamepad "event:emotionN") show as a notice until the emotions port exists.

local opened = false

view.on_frame(function()
  local s = view.state()
  local h = s and s.bwhud
  if not h then return end
  local me = view.player()
  local mine
  for _, row in ipairs(h.players or {}) do if row.slot == me then mine = row end end
  ui.set("bw", {me = me, t = h.t, left = h.left, sd = h.sd, sd_at = h.sd_at, sd_counter = h.sd_counter, koh = h.koh,
                players = h.players, mine = mine})
  if not opened then ui.open("bw_hud"); opened = true end
end)

ui.on("pad", function(args)
  local a = args and args.action or ""
  if a:sub(1, 7) == "emotion" then view.notice("bw.emotion." .. a:sub(8)) end
end)
