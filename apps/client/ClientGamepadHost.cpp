// GameClient side of the gamepad (ClientGamepad.h): polling, snap targets, event injection, the cursor.
#include "GameClient.h"
extern "C" {
#include <lauxlib.h>
#include <lua.h>
}
#include "imgui.h"
#include "modules/rts/view/ScriptHud.h"

namespace aiverse::client {

void GameClient::updateGamepad(float dt) {
    padPanX_ = padPanY_ = 0;
    static bool configured = false;   // game.json is loaded once per process
    if (!configured) { gamepad_.configure(manifest_); configured = true; }
    int n = platform::pollGamepads();
    if (n == 0) return;
    int idx = 0;
    while (idx < platform::kMaxGamepads && !platform::gamepad(idx).connected) idx++;
    std::vector<ClientGamepad::Target> targets;
    const std::string& want = gamepad_.snapTargets();
    if (sim_ && screen_ == Screen::Playing && want != "none")
        for (const auto& o : scene_.objects) {
            if (o.type >= rules_.types.size()) continue;
            bool structure = rules_.types[o.type].structure;
            if ((want == "structures" && !structure) || (want == "units" && structure)) continue;
            float2 sp;
            if (objectScreen(o, 0.5f, sp)) targets.push_back({sp.x, sp.y});
        }
    auto f = gamepad_.update(platform::gamepad(idx), dt, float(platform::windowWidth()), float(platform::windowHeight()), targets);
    padInjecting_ = true;
    for (const auto& e : f.events) event(e);
    padInjecting_ = false;
    if (sim_ && screen_ == Screen::Playing)
        for (const auto& a : f.actions) runPadAction(a);
    padAiming_ = f.aiming;
    padOriginX_ = f.originX;
    padOriginY_ = f.originY;
    padPanX_ = f.panX;
    padPanY_ = f.panY;
}

// Game actions of the pad layout (ClientGamepad.h): the Bunker Wars chords and any game's "buttons" / "chords".
void GameClient::runPadAction(const ClientGamepad::Action& a) {
    World& w = sim_->world();
    auto mine = [&](Entity e) { return e.valid() && w.alive(e) && w.has<Owner>(e) && w.get<Owner>(e).player == human_; };
    auto arg = [&](size_t colon) { return a.what.size() > colon ? std::atoi(a.what.c_str() + colon) : 0; };
    if (a.what == "cancel") {
        platform::Event e; e.type = platform::EventType::KeyDown; e.key = platform::Key::Escape; e.x = a.x; e.y = a.y;
        padInjecting_ = true; event(e); e.type = platform::EventType::KeyUp; event(e); padInjecting_ = false;
    } else if (a.what.rfind("send:", 0) == 0) {
        // BW JoystickInputStrategy1.MoveUnits: SetUnitsPercent + OnMoveUnits(origin, target)
        const int pct = std::clamp(arg(5), 1, 100);
        sendPercent_ = pct;
        Entity target = pick(a.x, a.y);
        if (!target.valid() || !w.has<Garrison>(target)) return;
        std::vector<Entity> src;
        Entity origin = pick(a.originX, a.originY);
        if (mine(origin) && origin != target && w.has<Garrison>(origin)) src.push_back(origin);
        else {
            cleanSelection();
            for (Entity e : selection_) if (e != target && mine(e) && w.has<Garrison>(e)) src.push_back(e);
        }
        if (src.empty()) return;
        Command c;
        c.type = kCmdSend; c.param = pct; c.units = src; c.target = target;
        issue(c);
    } else if (a.what.rfind("ability:", 0) == 0) {
        // BW SelectSlotN: the N-th skill card of the panel (owned, active, not automatic)
        const GameState& gs = mod().state(*sim_);
        if (human_ >= gs.playerAbilities.size()) return;
        int left = arg(8);
        const auto& st = gs.playerAbilities[human_];
        for (size_t i = 0; i < rules_.playerAbilities.size() && i < st.size(); ++i) {
            if (game::scriptHudAbility(manifest_, rules_.playerAbilities[i].id)) continue;
            const PlayerAbilityDef& d = rules_.advancedEffects ? mod().playerAbilityFor(*sim_, human_, int(i)) : rules_.playerAbilities[i];
            if (!st[i].owned || d.passive || d.autoEvery.raw > 0 || --left > 0) continue;
            if (!mod().canUsePlayerAbility(*sim_, human_, int(i))) return;
            if (d.target == TargetKind::None || d.target == TargetKind::Self || d.target == TargetKind::Player) {
                Command c; c.type = kCmdPlayerAbility; c.param = int64_t(i); issue(c);
            } else { mode_ = Mode::Skill; abilityIndex_ = int(i); pairFirst_ = false; }
            return;
        }
    } else if (a.what == "upgrade" || a.what.rfind("convert:", 0) == 0) {
        // BW UpgradeToNextLevel / ConvertToHouse / Tower / Smithy on the point under the selector
        Entity e = pick(a.x, a.y);
        if (!mine(e)) { cleanSelection(); e = selection_.empty() ? Entity{} : selection_.front(); }
        if (!mine(e) || !w.has<Unit>(e)) return;
        const UnitType& t = rules_.types[w.get<Unit>(e).type];
        const uint64_t want = a.what == "upgrade" ? t.tags : rules_.tagMask(a.what.substr(8));
        if (want == 0 || (a.what != "upgrade" && (t.tags & want) != 0)) return;   // converting into its own kind: nothing
        for (uint16_t up : t.upgrades) {
            const uint64_t tags = rules_.types[up].tags;
            if (a.what == "upgrade" ? tags != want : (tags & want) == 0) continue;
            Command c; c.type = kCmdUpgrade; c.units = {e}; c.param = up; issue(c);
            return;
        }
    } else if (a.what.rfind("event:", 0) == 0) {
        Json args = Json::object();
        args.set("action", Json::string(a.what.substr(6)));
        viewScripts_.uiEvent(*sim_, "pad", args);
    }
}

void GameClient::drawGamepadCursor() {
    if (!gamepad_.active()) return;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImVec2 c(gamepad_.cursorX(), gamepad_.cursorY());
    dl->AddCircle(c, 14.f, IM_COL32(0, 0, 0, 160), 24, 5.f);
    dl->AddCircle(c, 14.f, IM_COL32(255, 230, 120, 255), 24, 2.5f);
    dl->AddCircleFilled(c, 3.f, IM_COL32(255, 230, 120, 255));
    if (padAiming_) {   // BW ShowArrow(origin, target) while the send modifier is held
        ImVec2 o(padOriginX_, padOriginY_);
        dl->AddLine(o, c, IM_COL32(0, 0, 0, 140), 7.f);
        dl->AddLine(o, c, IM_COL32(255, 230, 120, 230), 3.f);
        dl->AddCircle(o, 18.f, IM_COL32(255, 230, 120, 230), 24, 2.5f);
    }
}

// UI Lua: game.sendPercent([p]) -> p, game.useAbility(i) -> ok. sendPercent: the share of a garrison a send takes (25 / 50 / 75 / 100 in the engine HUD,
// any 1..100 from a game's own percent chooser, e.g. Bunker Wars PercentChooser); pads and the HUD share it.
void GameClient::openPadLua(lua_State* L) {
    lua_getglobal(L, "game");
    if (!lua_istable(L, -1)) { lua_pop(L, 1); return; }
    lua_pushlightuserdata(L, this);
    lua_pushcclosure(L, [](lua_State* S) -> int {
        auto* c = static_cast<GameClient*>(lua_touserdata(S, lua_upvalueindex(1)));
        if (lua_isnumber(S, 1)) c->sendPercent_ = std::clamp(int(lua_tointeger(S, 1)), 1, 100);
        lua_pushinteger(S, c->sendPercent_);
        return 1;
    }, 1);
    lua_setfield(L, -2, "sendPercent");
    // game.useAbility(i): the player ability with that rules index (0-based) as its HUD card would: instant
    // targets cast at once, others enter the targeting mode (a game's own skill panel, e.g. BW HeroSkillsComponent)
    lua_pushlightuserdata(L, this);
    lua_pushcclosure(L, [](lua_State* S) -> int {
        auto* c = static_cast<GameClient*>(lua_touserdata(S, lua_upvalueindex(1)));
        const int i = int(luaL_checkinteger(S, 1));
        bool ok = false;
        if (c->sim_ && c->screen_ == Screen::Playing && i >= 0 && size_t(i) < c->rules_.playerAbilities.size() &&
            c->mod().canUsePlayerAbility(*c->sim_, c->human_, i)) {
            const PlayerAbilityDef& d = c->rules_.advancedEffects ? c->mod().playerAbilityFor(*c->sim_, c->human_, i) : c->rules_.playerAbilities[size_t(i)];
            if (d.target == TargetKind::None || d.target == TargetKind::Self || d.target == TargetKind::Player) {
                Command cmd; cmd.type = kCmdPlayerAbility; cmd.param = i; c->issue(cmd);
            } else { c->mode_ = Mode::Skill; c->abilityIndex_ = i; c->pairFirst_ = false; }
            ok = true;
        }
        lua_pushboolean(S, ok);
        return 1;
    }, 1);
    lua_setfield(L, -2, "useAbility");
    lua_pop(L, 1);
}

} // namespace aiverse::client
