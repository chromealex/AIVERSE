#include "ScriptSound.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

namespace aiverse::game {

namespace {

ScriptSound* host(lua_State* L) {
    auto* const* h = static_cast<ScriptSound* const*>(lua_touserdata(L, lua_upvalueindex(1)));
    return h ? *h : nullptr;
}
float field(lua_State* L, int t, const char* k, float def) {
    if (!lua_istable(L, t)) return def;
    lua_getfield(L, t, k);
    const float v = lua_isnumber(L, -1) ? float(lua_tonumber(L, -1)) : def;
    lua_pop(L, 1);
    return std::isfinite(v) ? v : def;
}
uint32_t handle(lua_State* L, int i) { return lua_isinteger(L, i) ? uint32_t(lua_tointeger(L, i)) : 0u; }

// sound.play(name, {gain, pan, pitch, variant (1-based), at, loop}) -> handle | nil
int sPlay(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    ScriptSound* h = host(L);
    if (!h) { lua_pushnil(L); return 1; }
    ScriptSoundPlay p;
    p.gain = std::max(0.0f, field(L, 2, "gain", 1));
    p.pan = field(L, 2, "pan", 0);
    p.pitch = field(L, 2, "pitch", 1);
    p.at = std::max(0.0f, field(L, 2, "at", 0));
    p.variant = int(field(L, 2, "variant", 0)) - 1;
    if (lua_istable(L, 2)) {
        lua_getfield(L, 2, "loop");
        if (lua_isboolean(L, -1)) p.loop = lua_toboolean(L, -1) ? 1 : 0;
        lua_pop(L, 1);
    }
    const uint32_t v = h->play(name, p);
    if (v) lua_pushinteger(L, lua_Integer(v)); else lua_pushnil(L);
    return 1;
}
// sound.set(handle, {gain, pan, pitch})
int sSet(lua_State* L) {
    ScriptSound* h = host(L);
    if (h && handle(L, 1)) h->set(handle(L, 1), field(L, 2, "gain", NAN), field(L, 2, "pan", NAN), field(L, 2, "pitch", NAN));
    return 0;
}
int sStop(lua_State* L) {
    ScriptSound* h = host(L);
    if (h && handle(L, 1)) h->stop(handle(L, 1), lua_isnumber(L, 2) ? float(lua_tonumber(L, 2)) : 0.02f);
    return 0;
}
int sPause(lua_State* L) {
    ScriptSound* h = host(L);
    if (h && handle(L, 1)) h->pause(handle(L, 1), lua_isnoneornil(L, 2) || lua_toboolean(L, 2));
    return 0;
}
int sSeek(lua_State* L) {
    ScriptSound* h = host(L);
    if (h && handle(L, 1) && lua_isnumber(L, 2)) h->seek(handle(L, 1), float(lua_tonumber(L, 2)));
    return 0;
}
// sound.state(handle) -> playing, time, paused
int sState(lua_State* L) {
    ScriptSound* h = host(L);
    float t = 0;
    bool paused = false;
    const bool playing = h && handle(L, 1) && h->state(handle(L, 1), t, paused);
    lua_pushboolean(L, playing);
    lua_pushnumber(L, playing ? t : 0);
    lua_pushboolean(L, playing && paused);
    return 3;
}
int sVariants(lua_State* L) {
    ScriptSound* h = host(L);
    lua_pushinteger(L, h ? h->variants(luaL_checkstring(L, 1)) : 0);
    return 1;
}
int sLength(lua_State* L) {
    ScriptSound* h = host(L);
    const char* name = luaL_checkstring(L, 1);
    lua_pushnumber(L, h ? h->length(name, int(luaL_optinteger(L, 2, 1)) - 1) : 0);
    return 1;
}
// sound.music(name | nil, fade = 1.5) -> started
int sMusic(lua_State* L) {
    ScriptSound* h = host(L);
    const char* name = lua_isstring(L, 1) ? lua_tostring(L, 1) : "";
    lua_pushboolean(L, h && h->music(name, lua_isnumber(L, 2) ? float(lua_tonumber(L, 2)) : 1.5f));
    return 1;
}
int sMusicGain(lua_State* L) {
    ScriptSound* h = host(L);
    if (h && lua_isnumber(L, 1)) h->musicGain(float(lua_tonumber(L, 1)));
    return 0;
}
int sCurrentMusic(lua_State* L) {
    ScriptSound* h = host(L);
    const std::string m = h ? h->currentMusic() : std::string();
    if (m.empty()) lua_pushnil(L); else lua_pushlstring(L, m.data(), m.size());
    return 1;
}
// sound.settings() -> {music, sfx, musicOn, sfxOn}
int sSettings(lua_State* L) {
    ScriptSound* h = host(L);
    const ScriptSoundSettings s = h ? h->settings() : ScriptSoundSettings{};
    lua_createtable(L, 0, 4);
    lua_pushnumber(L, s.music); lua_setfield(L, -2, "music");
    lua_pushnumber(L, s.sfx); lua_setfield(L, -2, "sfx");
    lua_pushboolean(L, s.musicOn); lua_setfield(L, -2, "musicOn");
    lua_pushboolean(L, s.sfxOn); lua_setfield(L, -2, "sfxOn");
    return 1;
}
int sAvailable(lua_State* L) {
    ScriptSound* h = host(L);
    lua_pushboolean(L, h && h->available());
    return 1;
}
// sound.trace(text): one line on stdout ("audio| text") when AIVERSE_AUDIO_LOG is set — for tools that
// compare what a game's sound logic decided; otherwise nothing.
int sTrace(lua_State* L) {
    static const bool on = [] { const char* e = std::getenv("AIVERSE_AUDIO_LOG"); return e && *e && *e != '0'; }();
    if (!on) return 0;
    size_t n = 0;
    const char* s = luaL_tolstring(L, 1, &n);
    std::fprintf(stdout, "audio| %.*s\n", int(n), s);
    std::fflush(stdout);
    lua_pop(L, 1);
    return 0;
}

} // namespace

void openScriptSound(lua_State* L, ScriptSound* const* h) {
    static const luaL_Reg fns[] = {{"play", sPlay}, {"set", sSet}, {"stop", sStop}, {"pause", sPause}, {"seek", sSeek},
                                   {"state", sState}, {"variants", sVariants}, {"length", sLength}, {"music", sMusic},
                                   {"music_gain", sMusicGain}, {"current_music", sCurrentMusic}, {"settings", sSettings},
                                   {"available", sAvailable}, {"trace", sTrace}, {nullptr, nullptr}};
    lua_createtable(L, 0, 14);
    for (const luaL_Reg* f = fns; f->name; ++f) {
        lua_pushlightuserdata(L, const_cast<ScriptSound**>(h));
        lua_pushcclosure(L, f->func, 1);
        lua_setfield(L, -2, f->name);
    }
    lua_setglobal(L, "sound");
}

} // namespace aiverse::game
