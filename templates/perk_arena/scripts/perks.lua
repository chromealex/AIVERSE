-- Simulation side of the Perk Arena progression (deterministic: runs on every peer).
-- A perk's "hook" names a global that runs when the perk is taken; the veto hook keeps cards out of offers.
state.burning = state.burning or 0

function on_burning_hits(e)
  state.burning = state.burning + 1
  game.log("player " .. e.player .. " took " .. e.args.perk .. " at level " .. e.args.level)
end

-- no second split shot card before the hero has some fire rate
function progression_can_offer(player, key)
  if key == "split_shot" then
    for _, p in ipairs(progression.player(player).perks) do
      if p.key == "rapid_fire" then return true end
    end
    return false
  end
  return true
end

on("progression_level", function(e)
  local p = progression.player(e.player)
  game.log("player " .. e.player .. " reached level " .. (p.level + p.levels) .. " (" .. p.kills .. " kills)")
end)
