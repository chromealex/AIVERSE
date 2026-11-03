-- Meta game of a wizard-made game (game wizard kit, templates/_wizard): the tutorial first, then levels with stars
-- and rewards, upgrades bought with a soft currency (they reach the match as initialVars, scripts/wizard/meta_bonus.lua
-- applies them), a daily reward, interstitials every N matches, a rewarded "double reward", one-time hints.
-- Everything it does is set in ui/wizard.json (the "Make a game" window writes it; edit freely). Every screen
-- requires this file first, so WizMeta and the data model keys exist before a document binds them.
if WizMeta then return WizMeta end
WizMeta = {}
local W = WizMeta
local cfg = game.json("ui/wizard.json") or {}
W.cfg = cfg

local function today() return os.date("%Y-%m-%d") end

-- ------------------------------------------------------------------ rows for the screens
function W.currencyRows()
    local rows = {}
    for _, c in ipairs(cfg.currencies or {}) do
        rows[#rows + 1] = {id = c.id, value = profile.get(c.id, 0), name = game.hasText("currency." .. c.id) and game.tr("currency." .. c.id) or c.id}
    end
    return rows
end

local function rewardText(r)
    local parts = {}
    local keys = {}
    for k in pairs(r or {}) do keys[#keys + 1] = k end
    table.sort(keys)
    for _, k in ipairs(keys) do
        local name = game.hasText("currency." .. k) and game.tr("currency." .. k) or k
        parts[#parts + 1] = "+" .. tostring(r[k]) .. " " .. name
    end
    return table.concat(parts, "  ")
end
W.rewardText = rewardText

local function text(s)
    if type(s) == "string" and s:sub(1, 1) == "@" then return game.tr(s:sub(2)) end
    return s or ""
end

function W.levelRows()
    local rows, stars = {}, profile.get("stars", {})
    local maps = game.maps()
    for i, lv in ipairs(cfg.levels or {}) do
        local s = stars[tostring(lv.map)] or 0
        local prevMap = i > 1 and cfg.levels[i - 1].map or nil
        local locked = prevMap ~= nil and (stars[tostring(prevMap)] or 0) == 0 and not cfg.allOpen
        rows[#rows + 1] = {index = i, map = lv.map, title = text(lv.title) ~= "" and text(lv.title) or (maps[lv.map] or ("#" .. i)),
                           stars = s, locked = locked, reward = rewardText(lv.reward), boss = lv.boss and true or false}
    end
    return rows
end

function W.upgradeLevel(id) return profile.get("upgrades." .. id, 0) end

function W.upgradeCost(u)
    local lvl = W.upgradeLevel(u.id)
    local c = {}
    for k, v in pairs(u.cost or {}) do c[k] = v + (u.growth or 0) * lvl end
    return c
end

function W.upgradeRows()
    local rows = {}
    for _, u in ipairs(cfg.upgrades or {}) do
        local lvl, max = W.upgradeLevel(u.id), u.max or 5
        local cost = W.upgradeCost(u)
        local parts = {}
        for k, v in pairs(cost) do parts[#parts + 1] = tostring(v) .. " " .. (game.hasText("currency." .. k) and game.tr("currency." .. k) or k) end
        rows[#rows + 1] = {id = u.id, name = text(u.name), description = text(u.description), level = lvl, max = max,
                           maxed = lvl >= max, cost = table.concat(parts, "  "), affordable = profile.can(cost) and lvl < max}
    end
    return rows
end

function W.buyUpgrade(id)
    for _, u in ipairs(cfg.upgrades or {}) do
        if u.id == id and W.upgradeLevel(id) < (u.max or 5) and profile.spend(W.upgradeCost(u)) then
            profile.set("upgrades." .. id, W.upgradeLevel(id) + 1)
            if sound and sound.play then sound.play("wiz_buy") end
        end
    end
    W.refresh()
end

-- initialVars for a match: upgrade levels (wiz_up_<id>) and the level number
local function matchVars(levelIndex)
    local v = {}
    for _, u in ipairs(cfg.upgrades or {}) do v["wiz_up_" .. u.id] = W.upgradeLevel(u.id) end
    if levelIndex then v.wiz_level = levelIndex end
    return v
end

-- ------------------------------------------------------------------ starting matches
function W.tutorialDone()
    local t = cfg.tutorial
    return not t or not t.map or profile.get("tutorial.done", false)
end

function W.startTutorial()
    if not cfg.tutorial or not cfg.tutorial.map then return end
    W.pending = {kind = "tutorial"}
    game.start(cfg.tutorial.map, {difficulty = 0, initialVars = matchVars(nil)})
end

function W.startLevel(i)
    local lv = (cfg.levels or {})[i]
    if not lv then return end
    local row = W.levelRows()[i]
    if row and row.locked then return end
    W.pending = {kind = "level", index = i}
    game.start(lv.map, {difficulty = lv.difficulty or profile.get("difficulty", 1), initialVars = matchVars(i)})
end

-- "Play": the tutorial while it isn't done (when it is forced), else the first level without stars, else the level list
function W.play()
    if cfg.tutorial and cfg.tutorial.forced ~= false and not W.tutorialDone() then return W.startTutorial() end
    local rows = W.levelRows()
    for i, r in ipairs(rows) do
        if not r.locked and r.stars == 0 then return W.startLevel(i) end
    end
    ui.open("levels")
end

-- ------------------------------------------------------------------ after a match
local function starsFor(lv, seconds)
    local t = lv.stars or {300, 600}
    if seconds <= (t[1] or 300) then return 3 end
    if seconds <= (t[2] or 600) then return 2 end
    return 1
end

-- called by the result screen once per match: {won, seconds, map, vars, ...} -> what was given
function W.settle(r)
    local out = {won = r.won, reward = {}, stars = 0, tutorial = false, first = false}
    if r.spectator then return out end
    profile.add("stats.matches", 1)
    if r.won then profile.add("stats.wins", 1) end
    local t = cfg.tutorial
    if t and t.map == r.map then
        out.tutorial = true
        local finished = r.won or ((r.vars or {}).wiz_tutorial_done or 0) > 0
        if finished and not profile.get("tutorial.done", false) then
            profile.set("tutorial.done", true)
            out.reward = t.reward or {}
            out.first = true
        end
        out.won = finished
    else
        for _, lv in ipairs(cfg.levels or {}) do
            if lv.map == r.map then
                if r.won then
                    local s = starsFor(lv, r.seconds or 0)
                    local stars = profile.get("stars", {})
                    out.first = (stars[tostring(lv.map)] or 0) == 0
                    stars[tostring(lv.map)] = math.max(stars[tostring(lv.map)] or 0, s)
                    profile.set("stars", stars)
                    out.stars = s
                    out.reward = (out.first and lv.firstReward) or lv.reward or cfg.winReward or {}
                else
                    out.reward = cfg.lossReward or {}
                end
            end
        end
    end
    local xp = r.won and (cfg.xpWin or 50) or (cfg.xpLoss or 15)
    profile.give(out.reward)
    profile.add("xp", xp)
    while profile.get("xp", 0) >= profile.get("level", 1) * (cfg.xpPerLevel or 100) do
        profile.add("xp", -profile.get("level", 1) * (cfg.xpPerLevel or 100))
        profile.add("level", 1)
    end
    out.xp = xp
    out.rewardText = rewardText(out.reward)
    W.last = out
    W.matches = (W.matches or 0) + 1
    return out
end

function W.doubleReward(done)
    local last = W.last
    if not last or last.doubled or not next(last.reward or {}) then if done then done(false) end return end
    if not ads.available() or game.preview() then if done then done(false) end return end
    ads.show("rewarded", function(r)
        if r.rewarded then last.doubled = true; profile.give(last.reward) end
        W.refresh()
        if done then done(r.rewarded) end
    end, "double_reward")
end

-- an interstitial between matches, never during the tutorial, never with "no ads" bought
function W.maybeInterstitial()
    local a = cfg.ads or {}
    local every = a.interstitialEvery or 0
    if every <= 0 or profile.get("no_ads", false) or not W.tutorialDone() then return end
    if (W.matches or 0) > 0 and (W.matches % every) == 0 and ads.available() then ads.show("interstitial", function() end, "between_matches") end
end

-- ------------------------------------------------------------------ daily reward
function W.dailyRow()
    local d = cfg.daily
    if not d or d.enabled == false then return {available = false, enabled = false} end
    local last, streak = profile.get("daily.last", ""), profile.get("daily.streak", 0)
    local rewards = d.rewards or {{gold = 50}}
    local i = (streak % #rewards) + 1
    return {enabled = true, available = last ~= today(), day = i, days = #rewards, reward = rewardText(rewards[i])}
end

function W.claimDaily()
    local row = W.dailyRow()
    if not row.available then return end
    local rewards = cfg.daily.rewards or {{gold = 50}}
    profile.give(rewards[row.day])
    profile.set("daily.streak", profile.get("daily.streak", 0) + 1)
    profile.set("daily.last", today())
    W.refresh()
    ui.closePopup("daily")
end

-- ------------------------------------------------------------------ hints (the soft tutorial; view scripts call these)
function W.hintsOn() return profile.get("settings.hints", cfg.hints ~= false) and true or false end
function W.setHints(on) profile.set("settings.hints", on and true or false); W.refresh() end
function W.toggleHints() W.setHints(not W.hintsOn()) end
function W.hintSeen(id) return profile.get("hints." .. tostring(id), false) and true or false end
function W.hintShown(id) profile.set("hints." .. tostring(id), true) end
function W.resetTutorial()
    profile.set("tutorial.done", false)
    profile.set("hints", {})
    W.refresh()
end

-- ------------------------------------------------------------------ data model
function W.refresh()
    ui.set("wizCur", W.currencyRows())
    ui.set("wizLevels", W.levelRows())
    ui.set("wizUpgrades", W.upgradeRows())
    ui.set("wizDaily", W.dailyRow())
    ui.set("wiz", {tutorial = cfg.tutorial ~= nil and cfg.tutorial.map ~= nil, tutorialDone = W.tutorialDone(),
                   upgrades = #(cfg.upgrades or {}) > 0, shop = cfg.shop ~= false, daily = (cfg.daily or {}).enabled ~= false and cfg.daily ~= nil,
                   levels = #(cfg.levels or {}) > 0, hints = W.hintsOn(), double = (cfg.ads or {}).rewardedDouble and true or false,
                   level = profile.get("level", 1), xp = profile.get("xp", 0), xpNext = profile.get("level", 1) * (cfg.xpPerLevel or 100),
                   xpFill = "width: " .. math.floor(math.min(1, profile.get("xp", 0) / (profile.get("level", 1) * (cfg.xpPerLevel or 100))) * 100) .. "%;"})
end

game.on("profile", function() W.refresh() end)
game.on("language", function() W.refresh() end)
game.on("matchEnd", function() W.maybeInterstitial() end)
W.refresh()
ui.set("wizResult", {won = false, rewardText = "", xp = 0, stars = 0, tutorial = false, first = false, doubled = false})
return W
