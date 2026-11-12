// Lua tables for the client platform services (agent-bw-client), in the UI Lua state next to
// store.* / ads.*:
//   social.available() social.provider() social.user(fn(u)) social.unlock(id[, fn(ok, err)])
//   social.progress(id, cur, max) social.achievements(fn(list, err)) social.stat(id[, fn(v)])
//   social.setStat(id, v) social.addStat(id, d) social.friends(fn(list, err)) social.presence(key, value)
//   social.clearPresence() social.overlay(page) social.invite(connect[, friendId])
//   push.available() push.permission(fn(granted)) push.token(fn(t)) push.schedule{id, title, body, delay,
//   category, tag, data, repeat} push.cancel(id) push.cancelCategory(c) push.cancelTag(t) push.cancelAll() push.badge(n)
//   links.available() links.initial(fn(link)) links.parse(url) -> {scheme, host, path, query = {..}}
// Events through game.on (ui/lib/meta.lua): "social" {event, data}, "push" {event, data}, "link" {url, source, deferred, data}.
extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

#include <cstdio>
#include <memory>

#include "ClientServices.h"
#include "engine/services/Links.h"
#include "engine/services/Push.h"
#include "engine/services/Social.h"
#include "engine/ui/UiLua.h"

namespace aiverse::client {
namespace {

struct Ctx {
    std::shared_ptr<bool> alive;
    std::function<void(const char*, const Json&)> emit;
};
Ctx& ctx(lua_State* L) { return *static_cast<Ctx*>(lua_touserdata(L, lua_upvalueindex(1))); }

struct Cb {
    std::shared_ptr<bool> alive;
    lua_State* L = nullptr;
    int ref = LUA_NOREF;
    void call(const Json& a, const Json* b = nullptr) {
        if (!*alive || ref == LUA_NOREF) return;
        lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
        luaL_unref(L, LUA_REGISTRYINDEX, ref);
        ref = LUA_NOREF;
        ui::luaPushJson(L, a);
        if (b) ui::luaPushJson(L, *b);
        if (lua_pcall(L, b ? 2 : 1, 0, 0) != LUA_OK) {
            std::fprintf(stderr, "[lua] %s\n", lua_tostring(L, -1));
            lua_pop(L, 1);
        }
    }
};
std::shared_ptr<Cb> keep(lua_State* L, int idx) {
    auto c = std::make_shared<Cb>();
    c->alive = ctx(L).alive;
    c->L = L;
    if (lua_isfunction(L, idx)) { lua_pushvalue(L, idx); c->ref = luaL_ref(L, LUA_REGISTRYINDEX); }
    return c;
}
Json err(bool ok, const std::string& e) { return ok ? Json() : Json::string(e); }

// ---------------------------------------------------------------- social
int sAvailable(lua_State* L) { lua_pushboolean(L, social::available()); return 1; }
int sProvider(lua_State* L) { lua_pushstring(L, social::provider().c_str()); return 1; }
int sUser(lua_State* L) {
    auto cb = keep(L, 1);
    social::user([cb](const social::User& u) {
        Json j = Json::object();
        j.set("id", Json::string(u.id)); j.set("name", Json::string(u.name));
        j.set("language", Json::string(u.language)); j.set("country", Json::string(u.country));
        Json e = err(u.ok, u.error);
        cb->call(u.ok ? j : Json(), &e);
    });
    return 0;
}
int sUnlock(lua_State* L) {
    auto cb = keep(L, 2);
    social::unlock(luaL_checkstring(L, 1), [cb](bool ok, const std::string& e) { Json x = err(ok, e); cb->call(Json::fromBool(ok), &x); });
    return 0;
}
int sProgress(lua_State* L) { social::progress(luaL_checkstring(L, 1), int(luaL_checkinteger(L, 2)), int(luaL_checkinteger(L, 3))); return 0; }
int sAchievements(lua_State* L) {
    auto cb = keep(L, 1);
    social::achievements([cb](bool ok, const std::vector<social::Achievement>& list, const std::string& e) {
        Json a = Json::array();
        for (auto& x : list) {
            Json j = Json::object();
            j.set("id", Json::string(x.id)); j.set("name", Json::string(x.name)); j.set("description", Json::string(x.description));
            j.set("unlocked", Json::fromBool(x.unlocked)); j.set("unlockTime", Json::number(x.unlockTime));
            a.push(j);
        }
        Json ee = err(ok, e);
        cb->call(a, &ee);
    });
    return 0;
}
int sStat(lua_State* L) {
    auto cb = keep(L, 2);
    social::stat(luaL_checkstring(L, 1), [cb](bool ok, double v) { cb->call(ok ? Json::number(v, 6) : Json()); });
    return 0;
}
int sSetStat(lua_State* L) { social::setStat(luaL_checkstring(L, 1), luaL_checknumber(L, 2)); return 0; }
int sAddStat(lua_State* L) { social::addStat(luaL_checkstring(L, 1), luaL_checknumber(L, 2)); return 0; }
int sFriends(lua_State* L) {
    auto cb = keep(L, 1);
    social::friends([cb](bool ok, const std::vector<social::Friend>& list, const std::string& e) {
        Json a = Json::array();
        for (auto& f : list) {
            Json j = Json::object();
            j.set("id", Json::string(f.id)); j.set("name", Json::string(f.name)); j.set("state", Json::string(f.state));
            j.set("playing", Json::fromBool(f.playingThisGame)); j.set("status", Json::string(f.status));
            a.push(j);
        }
        Json ee = err(ok, e);
        cb->call(a, &ee);
    });
    return 0;
}
int sPresence(lua_State* L) { social::setPresence(luaL_checkstring(L, 1), luaL_optstring(L, 2, "")); return 0; }
int sClearPresence(lua_State*) { social::clearPresence(); return 0; }
int sOverlay(lua_State* L) { social::overlay(luaL_optstring(L, 1, "friends")); return 0; }
int sInvite(lua_State* L) { social::invite(luaL_optstring(L, 1, ""), luaL_optstring(L, 2, "")); return 0; }

// ---------------------------------------------------------------- push
int pAvailable(lua_State* L) { lua_pushboolean(L, push::available()); return 1; }
int pPermission(lua_State* L) { auto cb = keep(L, 1); push::requestPermission([cb](bool g) { cb->call(Json::fromBool(g)); }); return 0; }
int pToken(lua_State* L) { auto cb = keep(L, 1); push::token([cb](const std::string& t) { cb->call(Json::string(t)); }); return 0; }
int pSchedule(lua_State* L) {
    Json o = ui::luaToJson(L, 1);
    push::Local n;
    n.id = o.getStr("id"); n.title = o.getStr("title"); n.body = o.getStr("body");
    n.delay = o["delay"].toDouble(0); n.category = o.getStr("category"); n.tag = o.getStr("tag");
    n.repeat = o.getInt("repeat", 0);
    if (o.has("data")) n.data = o["data"].dump(-1);
    auto cb = keep(L, 2);
    push::schedule(n, [cb](bool ok, const std::string& e) { Json x = err(ok, e); cb->call(Json::fromBool(ok), &x); });
    return 0;
}
int pCancel(lua_State* L) { push::cancel(luaL_checkstring(L, 1)); return 0; }
int pCancelCategory(lua_State* L) { push::cancelCategory(luaL_checkstring(L, 1)); return 0; }
int pCancelTag(lua_State* L) { push::cancelTag(luaL_checkstring(L, 1)); return 0; }
int pCancelAll(lua_State*) { push::cancelAll(); return 0; }
int pBadge(lua_State* L) { push::setBadge(int(luaL_checkinteger(L, 1))); return 0; }

// ---------------------------------------------------------------- links
Json linkJson(const links::Link& l) {
    Json j = Json::object();
    j.set("url", Json::string(l.url)); j.set("source", Json::string(l.source)); j.set("deferred", Json::fromBool(l.deferred));
    try { j.set("data", Json::parse(l.dataJson)); } catch (...) { j.set("data", Json::object()); }
    return j;
}
int lAvailable(lua_State* L) { lua_pushboolean(L, links::available()); return 1; }
int lInitial(lua_State* L) { auto cb = keep(L, 1); links::initial([cb](const links::Link& l) { cb->call(linkJson(l)); }); return 0; }
int lParse(lua_State* L) {
    links::Parsed p = links::parse(luaL_checkstring(L, 1));
    Json j = Json::object(), q = Json::object();
    j.set("scheme", Json::string(p.scheme)); j.set("host", Json::string(p.host)); j.set("path", Json::string(p.path));
    for (auto& [k, v] : p.query) q.set(k, Json::string(v));
    j.set("query", q);
    ui::luaPushJson(L, j);
    return 1;
}

void table(lua_State* L, Ctx* c, const char* name, std::initializer_list<luaL_Reg> fns) {
    lua_newtable(L);
    for (auto& f : fns) {
        lua_pushlightuserdata(L, c);
        lua_pushcclosure(L, f.func, 1);
        lua_setfield(L, -2, f.name);
    }
    lua_setglobal(L, name);
}

} // namespace

struct ClientServices::Impl {
    Ctx ctx;
    int socialId = 0, pushId = 0, linkId = 0;
};

ClientServices::ClientServices() = default;
ClientServices::~ClientServices() { detach(); }

void ClientServices::attach(lua_State* L, std::shared_ptr<bool> alive, std::function<void(const char*, const Json&)> emit) {
    detach();
    impl_ = std::make_unique<Impl>();
    impl_->ctx.alive = std::move(alive);
    impl_->ctx.emit = std::move(emit);
    Ctx* c = &impl_->ctx;
    table(L, c, "social", {{"available", sAvailable}, {"provider", sProvider}, {"user", sUser}, {"unlock", sUnlock}, {"progress", sProgress},
                           {"achievements", sAchievements}, {"stat", sStat}, {"setStat", sSetStat}, {"addStat", sAddStat},
                           {"friends", sFriends}, {"presence", sPresence}, {"clearPresence", sClearPresence}, {"overlay", sOverlay},
                           {"invite", sInvite}});
    table(L, c, "push", {{"available", pAvailable}, {"permission", pPermission}, {"token", pToken}, {"schedule", pSchedule},
                         {"cancel", pCancel}, {"cancelCategory", pCancelCategory}, {"cancelTag", pCancelTag}, {"cancelAll", pCancelAll},
                         {"badge", pBadge}});
    table(L, c, "links", {{"available", lAvailable}, {"initial", lInitial}, {"parse", lParse}});
    auto relay = [c](const char* kind) {
        return [c, kind](const std::string& ev, const std::string& data) {
            if (!*c->alive || !c->emit) return;
            Json j = Json::object();
            j.set("event", Json::string(ev));
            try { j.set("data", Json::parse(data)); } catch (...) { j.set("data", Json()); }
            c->emit(kind, j);
        };
    };
    impl_->socialId = social::onEvent(relay("social"));
    impl_->pushId = push::onEvent(relay("push"));
    impl_->linkId = links::onLink([c](const links::Link& l) { if (*c->alive && c->emit) c->emit("link", linkJson(l)); });
}

void ClientServices::detach() {
    if (!impl_) return;
    social::removeListener(impl_->socialId);
    push::removeListener(impl_->pushId);
    links::removeListener(impl_->linkId);
    impl_.reset();
}

} // namespace aiverse::client
