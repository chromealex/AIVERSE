#include "ServicesLua.h"

#include "Analytics.h"
#include "Privacy.h"
#include "engine/platform/PluginJson.h"

extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

#include <cstdio>
#include <new>
#include <string>

namespace aiverse::services {
namespace {
namespace json = plugins::json;

struct Ctx { std::shared_ptr<bool> alive; };
Ctx& ctx(lua_State* L) { return *static_cast<Ctx*>(lua_touserdata(L, lua_upvalueindex(1))); }

json::Value number(lua_State* L, int idx) {
    if (lua_isinteger(L, idx)) return json::Value::number(int64_t(lua_tointeger(L, idx)));
    char buf[40];
    std::snprintf(buf, sizeof buf, "%.15g", double(lua_tonumber(L, idx)));
    return json::Value::number(std::string(buf));
}

// A scalar Lua value as JSON (nested tables only one level deep: receipts); nil for anything else.
json::Value scalar(lua_State* L, int idx, int depth = 0);

json::Value table(lua_State* L, int idx, int depth) {
    json::Value o = json::Value::object();
    if (!lua_istable(L, idx) || depth > 1) return o;
    idx = lua_absindex(L, idx);
    lua_pushnil(L);
    while (lua_next(L, idx)) {
        if (lua_type(L, -2) == LUA_TSTRING) {
            json::Value v = scalar(L, -1, depth + 1);
            if (!v.isNull()) o.set(lua_tostring(L, -2), std::move(v));
        }
        lua_pop(L, 1);
    }
    return o;
}

json::Value scalar(lua_State* L, int idx, int depth) {
    switch (lua_type(L, idx)) {
    case LUA_TSTRING: return json::Value::string(lua_tostring(L, idx));
    case LUA_TNUMBER: return number(L, idx);
    case LUA_TBOOLEAN: return json::Value::boolean_(lua_toboolean(L, idx) != 0);
    case LUA_TTABLE: return depth <= 1 ? table(L, idx, depth) : json::Value();
    default: return json::Value();
    }
}

std::string fields(lua_State* L, int idx) { return lua_istable(L, idx) ? json::dump(table(L, idx, 1)) : "{}"; }
std::string str(lua_State* L, int idx) { return luaL_optstring(L, idx, ""); }

// A Lua function kept across an async call; dropped if the state is gone.
struct Callback {
    std::shared_ptr<bool> alive;
    lua_State* L = nullptr;
    int ref = LUA_NOREF;
    void call(const std::string& value) {
        if (!*alive || ref == LUA_NOREF) return;
        lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
        luaL_unref(L, LUA_REGISTRYINDEX, ref);
        ref = LUA_NOREF;
        lua_pushstring(L, value.c_str());
        if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
            std::fprintf(stderr, "[lua] %s\n", lua_tostring(L, -1));
            lua_pop(L, 1);
        }
    }
};
std::shared_ptr<Callback> keep(lua_State* L, int idx) {
    auto c = std::make_shared<Callback>();
    c->alive = ctx(L).alive;
    c->L = L;
    if (lua_isfunction(L, idx)) { lua_pushvalue(L, idx); c->ref = luaL_ref(L, LUA_REGISTRYINDEX); }
    return c;
}

// ------------------------------------------------------------------ analytics.*
int aAvailable(lua_State* L) { lua_pushboolean(L, analytics::available()); return 1; }
int aProviders(lua_State* L) {
    lua_newtable(L);
    int i = 1;
    for (auto& p : analytics::providers()) { lua_pushstring(L, p.c_str()); lua_rawseti(L, -2, i++); }
    return 1;
}
int aEvent(lua_State* L) { analytics::event(luaL_checkstring(L, 1), fields(L, 2)); return 0; }
int aDesign(lua_State* L) {
    const std::string id = luaL_checkstring(L, 1);
    if (lua_type(L, 2) == LUA_TNUMBER) analytics::design(id, fields(L, 3), double(lua_tonumber(L, 2)));
    else analytics::design(id, fields(L, 2));
    return 0;
}
int aProgression(lua_State* L) {
    const std::string s = luaL_checkstring(L, 1);
    const analytics::Progression st = s == "complete" ? analytics::Progression::Complete
                                    : s == "fail" ? analytics::Progression::Fail : analytics::Progression::Start;
    const int scoreAt = lua_type(L, 3) == LUA_TNUMBER ? 3 : 4;
    std::optional<int64_t> score;
    if (lua_type(L, scoreAt) == LUA_TNUMBER) score = int64_t(lua_tonumber(L, scoreAt));
    analytics::progression(st, luaL_checkstring(L, 2), fields(L, 3), score);
    return 0;
}
int aResource(lua_State* L) {
    const analytics::Flow flow = str(L, 1) == "sink" ? analytics::Flow::Sink : analytics::Flow::Source;
    analytics::resource(flow, luaL_checkstring(L, 2), double(luaL_checknumber(L, 3)), str(L, 4), str(L, 5), fields(L, 6));
    return 0;
}
int aPurchase(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    const json::Value t = table(L, 1, 0);
    analytics::purchase(t["currency"].str(), t["priceMicros"].i64(), t["itemType"].str(), t["itemId"].str(), t["cartType"].str(),
                        t["receipt"].isObject() ? json::dump(t["receipt"]) : "{}", t["fields"].isObject() ? json::dump(t["fields"]) : "{}");
    return 0;
}
int aUser(lua_State* L) { analytics::setUserId(str(L, 1)); return 0; }
int aProperty(lua_State* L) {
    std::string v = lua_type(L, 2) == LUA_TNUMBER ? json::dump(number(L, 2)) : str(L, 2);
    analytics::setUserProperty(luaL_checkstring(L, 1), v);
    return 0;
}
int aFlush(lua_State*) { analytics::flush(); return 0; }
int aDeferredLink(lua_State* L) {
    auto cb = keep(L, 1);
    analytics::deferredLink([cb](const std::string& url) { cb->call(url); });
    return 0;
}

// ------------------------------------------------------------------ privacy.*
int pProvider(lua_State* L) { lua_pushstring(L, privacy::provider().c_str()); return 1; }
int pTracking(lua_State* L) { lua_pushstring(L, privacy::name(privacy::tracking())); return 1; }
int pTrackingAllowed(lua_State* L) { lua_pushboolean(L, privacy::trackingAllowed()); return 1; }
int pRequest(lua_State* L) {
    auto cb = keep(L, 1);
    privacy::requestTracking([cb](privacy::Tracking t) { cb->call(privacy::name(t)); });
    return 0;
}
int pRefresh(lua_State* L) {
    auto cb = keep(L, 1);
    privacy::refreshTracking([cb](privacy::Tracking t) { cb->call(privacy::name(t)); });
    return 0;
}
int pConsent(lua_State* L) {
    if (lua_istable(L, 1)) {
        privacy::Consent c = privacy::consent();
        auto flag = [&](const char* key, bool& out) {
            lua_getfield(L, 1, key);
            if (!lua_isnil(L, -1)) out = lua_toboolean(L, -1) != 0;
            lua_pop(L, 1);
        };
        flag("analytics", c.analytics);
        flag("personalizedAds", c.personalizedAds);
        flag("ageRestricted", c.ageRestricted);
        privacy::setConsent(c);
    }
    const privacy::Consent c = privacy::consent();
    lua_newtable(L);
    lua_pushboolean(L, c.analytics); lua_setfield(L, -2, "analytics");
    lua_pushboolean(L, c.personalizedAds); lua_setfield(L, -2, "personalizedAds");
    lua_pushboolean(L, c.ageRestricted); lua_setfield(L, -2, "ageRestricted");
    lua_pushboolean(L, privacy::consentSet()); lua_setfield(L, -2, "set");
    return 1;
}

int ctxGc(lua_State* L) { static_cast<Ctx*>(lua_touserdata(L, 1))->~Ctx(); return 0; }

void registerTable(lua_State* L, int ctxIndex, const char* global, std::initializer_list<luaL_Reg> fns) {
    lua_newtable(L);
    for (auto& f : fns) {
        lua_pushvalue(L, ctxIndex);
        lua_pushcclosure(L, f.func, 1);
        lua_setfield(L, -2, f.name);
    }
    lua_setglobal(L, global);
}
} // namespace

void openServicesLua(lua_State* L, std::shared_ptr<bool> alive) {
    if (!L) return;
    void* mem = lua_newuserdatauv(L, sizeof(Ctx), 0);
    new (mem) Ctx{alive ? std::move(alive) : std::make_shared<bool>(true)};
    lua_newtable(L);
    lua_pushcfunction(L, ctxGc);
    lua_setfield(L, -2, "__gc");
    lua_setmetatable(L, -2);
    const int c = lua_gettop(L);
    registerTable(L, c, "analytics", {{"available", aAvailable}, {"providers", aProviders}, {"event", aEvent}, {"design", aDesign},
                                      {"progression", aProgression}, {"resource", aResource}, {"purchase", aPurchase},
                                      {"user", aUser}, {"property", aProperty}, {"flush", aFlush}, {"deferredLink", aDeferredLink}});
    registerTable(L, c, "privacy", {{"provider", pProvider}, {"tracking", pTracking}, {"trackingAllowed", pTrackingAllowed},
                                    {"requestTracking", pRequest}, {"refreshTracking", pRefresh}, {"consent", pConsent}});
    lua_pop(L, 1);
}

} // namespace aiverse::services
