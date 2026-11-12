#include "ClientSpine.h"
extern "C" {
#include <lauxlib.h>
#include <lua.h>
}
#include <algorithm>
#include <cstdio>
#include "engine/ui/UiLayer.h"
#include "engine/ui/UiLua.h"

namespace aiverse::client {

void ClientSpine::init(gfx::Device& dev, ui::UiLayer& ui, const Json& manifest) {
    dev_ = &dev;
    ui_ = &ui;
    textures_.init(dev);
    ready_ = false;
    const Json& decl = manifest["spine"];
    for (const auto& [name, opts] : decl.members()) {
        std::string err;
        if (!create(name, opts, err)) std::fprintf(stderr, "[spine] %s: %s\n", name.c_str(), err.c_str());
    }
}

void ClientSpine::shutdown() {
    if (!dev_) return;
    for (auto& [n, v] : views_)
        if (v.target.valid()) dev_->destroyTexture(v.target);
    views_.clear();
    renderer_.shutdown();
    textures_.shutdown();
    dev_ = nullptr;
}

bool ClientSpine::create(const std::string& name, const Json& o, std::string& error) {
    if (!dev_) { error = "no device"; return false; }
    if (name.empty() || name.size() > 64) { error = "bad view name"; return false; }
    if (views_.size() >= 32 && !views_.count(name)) { error = "at most 32 spine views"; return false; }
    if (!ready_) {
        if (!renderer_.init(*dev_)) { error = "renderer: " + dev_->lastError(); return false; }
        ready_ = true;
    }
    View v;
    try {
        auto asset = spine::loadSpineAsset(o.getStr("skeleton"), o.getStr("atlas"), float(o["scale"].toDouble(1)));
        for (const std::string& w : asset.data->warnings) std::fprintf(stderr, "[spine] %s: %s\n", name.c_str(), w.c_str());
        v.player = std::make_unique<spine::SpinePlayer>(asset);
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
    v.width = std::clamp(int(o.getInt("width", 512)), 16, 2048);
    v.height = std::clamp(int(o.getInt("height", 512)), 16, 2048);
    v.pad = float(o["pad"].toDouble(8));
    for (const Json& a : o["idle"].items()) v.idle.push_back(a.str());
    auto& st = v.player->state;
    st.defaultMix = float(o["mix"].toDouble(0.2));
    if (o.has("skin")) v.player->skeleton.setSkin(o.getStr("skin"));
    std::string anim = o.getStr("animation", v.idle.empty() ? std::string() : v.idle[0]);
    if (anim.empty() && !v.player->skeleton.data().animations.empty()) anim = v.player->skeleton.data().animations[0].name;
    if (!anim.empty()) st.setAnimation(0, anim, o.getBool("loop", true));
    gfx::TextureDesc td;
    td.width = v.width;
    td.height = v.height;
    td.renderTarget = true;
    td.label = "spine view";
    v.target = dev_->createTexture(td);
    if (!v.target.valid()) { error = "can't create the view texture"; return false; }
    auto old = views_.find(name);
    if (old != views_.end() && old->second.target.valid()) dev_->destroyTexture(old->second.target);
    View& stored = views_[name] = std::move(v);
    // events -> UI scripts; a finished loop picks the next idle animation
    std::string viewName = name;
    stored.player->state.listener = [this, viewName](const spine::TrackEvent& e) {
        auto it = views_.find(viewName);
        if (it == views_.end()) return;
        View& vw = it->second;
        if (e.type == spine::TrackEvent::Complete && e.track == 0 && !vw.idle.empty() &&
            std::find(vw.idle.begin(), vw.idle.end(), e.animation) != vw.idle.end()) {
            vw.nextIdle = true;   // switched after update(): the state is iterating its tracks now
        }
        if (!onEvent || (e.type != spine::TrackEvent::Event && e.type != spine::TrackEvent::Complete)) return;
        Json j = Json::object();
        j.set("view", Json::string(viewName));
        j.set("type", Json::string(e.type == spine::TrackEvent::Event ? "event" : "complete"));
        j.set("animation", Json::string(e.animation));
        if (e.data) {
            j.set("name", Json::string(e.data->name));
            j.set("int", Json::number(int64_t(e.key->intValue)));
            j.set("float", Json::number(double(e.key->floatValue)));
            j.set("string", Json::string(e.key->string));
        }
        onEvent(j);
    };
    if (ui_) ui_->setDynamicTexture("spine:" + name, stored.target, stored.width, stored.height);
    return true;
}

bool ClientSpine::remove(const std::string& name) {
    auto it = views_.find(name);
    if (it == views_.end()) return false;
    if (dev_ && it->second.target.valid()) dev_->destroyTexture(it->second.target);
    views_.erase(it);
    return true;
}

bool ClientSpine::play(const std::string& name, const std::string& anim, bool loop, int track, bool add, float delay) {
    auto it = views_.find(name);
    if (it == views_.end()) return false;
    auto& st = it->second.player->state;
    return add ? st.addAnimation(track, anim, loop, delay) : st.setAnimation(track, anim, loop);
}

bool ClientSpine::skin(const std::string& name, const std::string& s) {
    auto it = views_.find(name);
    return it != views_.end() && it->second.player->skeleton.setSkin(s);
}

std::vector<std::string> ClientSpine::animations(const std::string& name) const {
    std::vector<std::string> out;
    auto it = views_.find(name);
    if (it != views_.end())
        for (const auto& a : it->second.player->skeleton.data().animations) out.push_back(a.name);
    return out;
}

void ClientSpine::render(float dt) {
    if (!dev_ || views_.empty()) return;
    // RmlUi samples images with v = 0 at the top: GL render targets have v = 0 at the bottom
    bool flip = !dev_->info().renderTargetFlipY;
    for (auto& [name, v] : views_) {
        v.player->update(dt);
        if (v.nextIdle) {
            v.nextIdle = false;
            v.rnd = v.rnd * 1103515245u + 12345u;   // presentation only
            v.player->state.setAnimation(0, v.idle[(v.rnd >> 16) % v.idle.size()], true);
        }
        if (!v.fitted) {   // frame the first posed frame once: the view doesn't zoom with the animation
            spine::poseBounds(v.player->skeleton, v.bounds);
            v.fitted = true;
        }
        float mvp[16];
        spine::fitMatrix(v.bounds, v.width, v.height, v.pad, flip, mvp);
        gfx::PassDesc pass;
        pass.colorTarget = v.target;
        pass.clearColor[0] = pass.clearColor[1] = pass.clearColor[2] = pass.clearColor[3] = 0;
        pass.clearDepthEnabled = false;
        dev_->beginPass(pass);
        dev_->setViewport({0, 0, v.width, v.height});
        dev_->setScissor({});
        v.player->draw(*dev_, renderer_, textures_, mvp, false);
        dev_->endPass();
    }
}

// ---------------------------------------------------------------- Lua

namespace {

ClientSpine& self(lua_State* L) { return *static_cast<ClientSpine*>(lua_touserdata(L, lua_upvalueindex(1))); }

int lCreate(lua_State* L) {
    std::string name = luaL_checkstring(L, 1);
    Json opts = ui::luaToJson(L, 2);
    std::string err;
    bool ok = self(L).create(name, opts, err);
    lua_pushboolean(L, ok);
    if (ok) return 1;
    lua_pushstring(L, err.c_str());
    return 2;
}
int lPlay(lua_State* L) {
    lua_pushboolean(L, self(L).play(luaL_checkstring(L, 1), luaL_checkstring(L, 2), lua_toboolean(L, 3), int(luaL_optinteger(L, 4, 0)), false, 0));
    return 1;
}
int lAdd(lua_State* L) {
    lua_pushboolean(L, self(L).play(luaL_checkstring(L, 1), luaL_checkstring(L, 2), lua_toboolean(L, 3), int(luaL_optinteger(L, 5, 0)), true,
                                    float(luaL_optnumber(L, 4, 0))));
    return 1;
}
int lSkin(lua_State* L) { lua_pushboolean(L, self(L).skin(luaL_checkstring(L, 1), luaL_checkstring(L, 2))); return 1; }
int lRemove(lua_State* L) { lua_pushboolean(L, self(L).remove(luaL_checkstring(L, 1))); return 1; }
int lAnimations(lua_State* L) {
    auto list = self(L).animations(luaL_checkstring(L, 1));
    lua_createtable(L, int(list.size()), 0);
    for (size_t i = 0; i < list.size(); i++) {
        lua_pushstring(L, list[i].c_str());
        lua_rawseti(L, -2, lua_Integer(i + 1));
    }
    return 1;
}

} // namespace

void openSpineLua(lua_State* L, ClientSpine* spine) {
    static const luaL_Reg fns[] = {{"create", lCreate}, {"play", lPlay}, {"add", lAdd}, {"skin", lSkin},
                                   {"remove", lRemove}, {"animations", lAnimations}, {nullptr, nullptr}};
    lua_newtable(L);
    for (const luaL_Reg* f = fns; f->name; f++) {
        lua_pushlightuserdata(L, spine);
        lua_pushcclosure(L, f->func, 1);
        lua_setfield(L, -2, f->name);
    }
    lua_setglobal(L, "spine");
}

} // namespace aiverse::client
