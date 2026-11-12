-- Golem Pit: the boss fight's story beats come from encounter events (docs/encounters.md).
-- Simulation script: deterministic; logs show in aiverse_headless ([script] lines) and the editor console.
local PHASES = { "the golem wakes", "the golem calls its brood", "the golem charges!" }

on("boss_engaged", function(e)
  game.log("boss engaged at tick " .. game.tick())
end)

on("boss_phase", function(e)
  local name = PHASES[e.value + 1] or ("phase " .. e.value)
  game.message(0, name)
  game.log("boss phase " .. e.value .. ": " .. name)
end)

on("boss_enraged", function(e) game.message(0, "the golem is enraged!") game.log("boss enraged") end)
on("boss_adds", function(e) game.log("adds: " .. (e.value >> 16)) end)
on("boss_defeated", function(e) game.message(0, "the golem falls") game.log("boss defeated") end)

-- telegraphs and statuses: counted, summarised once the fight ends
on("telegraph_start", function(e) state.telegraphs = (state.telegraphs or 0) + 1 end)
on("telegraph_strike", function(e) state.strikes = (state.strikes or 0) + 1 end)
on("status_start", function(e) state.statuses = (state.statuses or 0) + 1 end)
on("unit_died", function(e)
  if state.reported then return end
  local alive = encounters.bosses()
  if #alive == 0 and (state.telegraphs or 0) > 0 then
    state.reported = true
    game.log("telegraphs=" .. (state.telegraphs or 0) .. " strikes=" .. (state.strikes or 0) .. " statuses=" .. (state.statuses or 0))
  end
end)
