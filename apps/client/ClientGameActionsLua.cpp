// Shared game UI actions; these are available with or without gamepad input.
#include "GameClient.h"
#include <algorithm>
extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

namespace aiverse::client {

// game.sendPercent([p]) -> p, game.useAbility(i) -> ok.
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
