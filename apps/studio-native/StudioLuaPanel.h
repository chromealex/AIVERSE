// Extensions panel: the project's editor/extensions.lua (the legacy editor's declarative Lua Editor API)
// for the HTML Studio. window(ctx) returns widgets, on_action / on_change return commands, scene_gizmos(ctx)
// returns Scene overlay shapes. Lua is sandboxed (no io/os/load, instruction budget) and never gets engine
// pointers: everything crosses as plain JSON.
#pragma once
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include "engine/data/Json.h"
extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

namespace aiverse::studio {

struct StudioLuaContext { int selectionCount = 0, selectionIndex = -1, objectCount = 0, mapWidth = 0, mapHeight = 0; double time = 0; bool playing = false; };

class StudioLuaPanel {
public:
    ~StudioLuaPanel() { reset(); }
    StudioLuaPanel() = default;
    StudioLuaPanel(const StudioLuaPanel&) = delete;
    StudioLuaPanel& operator=(const StudioLuaPanel&) = delete;

    // {exists, error?, widgets?: [...]} for the panel; reloads the file when it changed.
    Json window(const std::string& projectDir, const StudioLuaContext& ctx) {
        Json out = Json::object();
        load(projectDir);
        out.set("path", Json::string("editor/extensions.lua"));
        out.set("exists", Json::fromBool(L_ != nullptr || !error_.empty()));
        if (!error_.empty()) { out.set("error", Json::string(error_)); return out; }
        if (!L_) return out;
        lua_getglobal(L_, "window");
        if (!lua_isfunction(L_, -1)) { lua_pop(L_, 1); out.set("error", Json::string("Define function window(ctx) ... end")); return out; }
        pushCtx(ctx);
        if (!invoke(1, 1)) { out.set("error", Json::string(error_)); return out; }
        out.set("widgets", toJson(-1, 0));
        lua_pop(L_, 1);
        return out;
    }
    // on_action(ctx, id) / on_change(ctx, id, value): the command the script returns (or null).
    Json action(const std::string& projectDir, const StudioLuaContext& ctx, const std::string& id, const Json* value) {
        load(projectDir);
        if (!L_) throw std::runtime_error(error_.empty() ? "No editor/extensions.lua" : error_);
        const char* fn = value ? "on_change" : "on_action";
        lua_getglobal(L_, fn);
        if (!lua_isfunction(L_, -1)) { lua_pop(L_, 1); return Json(); }
        pushCtx(ctx);
        lua_pushlstring(L_, id.data(), id.size());
        int argc = 2;
        if (value) {
            ++argc;
            if (value->isString()) { const std::string s = value->str(); lua_pushlstring(L_, s.data(), s.size()); }
            else if (value->isNumber()) lua_pushnumber(L_, value->toDouble());
            else lua_pushboolean(L_, value->getBool("", false) || (value->type() == Json::Type::Bool && value->dump(-1) == "true"));
        }
        if (!invoke(argc, 1)) throw std::runtime_error(error_);
        Json out = lua_istable(L_, -1) ? toJson(-1, 0) : Json();
        lua_pop(L_, 1);
        return out;
    }
    Json contextMenu(const std::string& projectDir, const StudioLuaContext& ctx,
                     const std::string& window, const std::string& element, const std::string* actionId) {
        load(projectDir, "context_menus.lua");
        if (!error_.empty()) throw std::runtime_error(error_);
        const Json empty = actionId ? Json() : Json::array();
        if (!L_) return empty;
        lua_getglobal(L_, actionId ? "on_context_action" : "context_menu");
        if (!lua_isfunction(L_, -1)) { lua_pop(L_, 1); return empty; }
        pushCtx(ctx);
        lua_pushlstring(L_, window.data(), window.size()); lua_setfield(L_, -2, "window");
        lua_pushlstring(L_, element.data(), element.size()); lua_setfield(L_, -2, "element");
        if (actionId) lua_pushlstring(L_, actionId->data(), actionId->size());
        if (!invoke(actionId ? 2 : 1, 1)) throw std::runtime_error(error_);
        Json result = toJson(-1, 0);
        lua_pop(L_, 1);
        return actionId || result.isArray() ? result : empty;
    }
    // scene_gizmos(ctx): up to 256 shapes {kind: line|circle|point|label, x,y,z, x2,y2,z2, radius, r,g,b, thickness, text}.
    Json gizmos(const std::string& projectDir, const StudioLuaContext& ctx) {
        load(projectDir);
        if (!L_) return Json::array();
        lua_getglobal(L_, "scene_gizmos");
        if (!lua_isfunction(L_, -1)) { lua_pop(L_, 1); return Json::array(); }
        pushCtx(ctx);
        if (!invoke(1, 1)) return Json::array();
        Json out = lua_istable(L_, -1) ? toJson(-1, 0) : Json::array();
        lua_pop(L_, 1);
        if (!out.isArray()) return Json::array();
        return out;
    }
    // "Create example": writes editor/extensions.lua when it does not exist.
    static bool createExample(const std::string& projectDir, std::string* err) {
        namespace fs = std::filesystem;
        const fs::path target = fs::path(projectDir) / "editor" / "extensions.lua";
        std::error_code ec;
        if (fs::exists(target, ec)) { if (err) *err = "editor/extensions.lua already exists"; return false; }
        fs::create_directories(target.parent_path(), ec);
        if (ec) { if (err) *err = ec.message(); return false; }
        std::ofstream f(target);
        f << "-- AIVERSE project-local editor extension (Studio Extensions panel)\n"
             "function window(ctx)\n"
             "  return {\n"
             "    {kind='label', label='Objects: '..ctx.object_count..' · selected: '..ctx.selection_count},\n"
             "    {kind='button', id='focus', label='Focus selected object'},\n"
             "    {kind='slider', id='radius', label='Gizmo radius', min=2, max=40, value=10},\n"
             "    {kind='button', id='inspector', label='Open Inspector'}\n"
             "  }\n"
             "end\n"
             "local radius = 10\n"
             "function on_action(ctx, id)\n"
             "  if id == 'focus' then return {command='focus_selection'} end\n"
             "  if id == 'inspector' then return {command='open_window', id='inspector'} end\n"
             "end\n"
             "function on_change(ctx, id, value)\n"
             "  if id == 'radius' then radius = value; return {command='status', text='Radius '..value} end\n"
             "end\n"
             "function scene_gizmos(ctx)\n"
             "  local x, z = ctx.map_width / 2, ctx.map_height / 2   -- the middle of the map\n"
             "  return {\n"
             "    {kind='circle', x=x, y=0, z=z, radius=radius, r=0.3, g=0.8, b=1.0},\n"
             "    {kind='label', x=x, y=0, z=z, text='Map centre', r=1, g=1, b=1}\n"
             "  }\n"
             "end\n";
        return bool(f);
    }

private:
    lua_State* L_ = nullptr;
    std::string path_, error_;
    std::filesystem::file_time_type stamp_{};
    bool missing_ = true;

    void reset() { if (L_) lua_close(L_); L_ = nullptr; error_.clear(); }
    static void hook(lua_State* L, lua_Debug*) { luaL_error(L, "Lua editor extension exceeded instruction budget"); }
    bool invoke(int nargs, int nret) {
        lua_sethook(L_, hook, LUA_MASKCOUNT, 100000);
        const int result = lua_pcall(L_, nargs, nret, 0);
        lua_sethook(L_, nullptr, 0, 0);
        if (result == LUA_OK) return true;
        error_ = lua_isstring(L_, -1) ? lua_tostring(L_, -1) : "Lua runtime error";
        lua_pop(L_, 1);
        return false;
    }
    void load(const std::string& projectDir, const char* file = "extensions.lua") {
        namespace fs = std::filesystem;
        const fs::path script = fs::path(projectDir) / "editor" / file;
        std::error_code ec;
        const auto stamp = fs::last_write_time(script, ec);
        const bool missing = bool(ec);
        if (path_ == script.string() && missing == missing_ && (missing || stamp == stamp_)) {
            // Runtime errors from the last call are cleared on the next successful reload only.
            return;
        }
        reset();
        path_ = script.string(); missing_ = missing; stamp_ = stamp;
        if (missing) return;
        if (fs::file_size(script, ec) > 128 * 1024 || ec) { error_ = "Lua extension exceeds 128 KiB"; return; }
        std::ifstream f(script, std::ios::binary);
        const std::string src((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        L_ = luaL_newstate();
        if (!L_) { error_ = "Cannot create Lua state"; return; }
        const luaL_Reg libs[] = {{"_G", luaopen_base}, {LUA_STRLIBNAME, luaopen_string}, {LUA_TABLIBNAME, luaopen_table},
                                 {LUA_MATHLIBNAME, luaopen_math}, {LUA_UTF8LIBNAME, luaopen_utf8}};
        for (const auto& lib : libs) { luaL_requiref(L_, lib.name, lib.func, 1); lua_pop(L_, 1); }
        for (const char* global : {"dofile", "loadfile", "load", "collectgarbage", "require"}) { lua_pushnil(L_); lua_setglobal(L_, global); }
        if (luaL_loadbufferx(L_, src.data(), src.size(), file, "t") != LUA_OK) {
            error_ = lua_tostring(L_, -1); lua_close(L_); L_ = nullptr; return;
        }
        if (!invoke(0, 0)) { lua_close(L_); L_ = nullptr; }
    }
    void pushCtx(const StudioLuaContext& c) {
        lua_newtable(L_);
        lua_pushinteger(L_, c.selectionCount > 0 ? 1 : 0); lua_setfield(L_, -2, "selection_kind");
        lua_pushinteger(L_, c.selectionIndex); lua_setfield(L_, -2, "selection_index");
        lua_pushinteger(L_, c.selectionCount); lua_setfield(L_, -2, "selection_count");
        lua_pushinteger(L_, c.objectCount); lua_setfield(L_, -2, "object_count");
        lua_pushinteger(L_, c.mapWidth); lua_setfield(L_, -2, "map_width");
        lua_pushinteger(L_, c.mapHeight); lua_setfield(L_, -2, "map_height");
        lua_pushnumber(L_, c.time); lua_setfield(L_, -2, "time");
        lua_pushboolean(L_, c.playing); lua_setfield(L_, -2, "playing");
    }
    // Lua value -> JSON: tables with 1..n keys become arrays (max 256), others objects with string keys.
    Json toJson(int index, int depth) {
        index = lua_absindex(L_, index);
        switch (lua_type(L_, index)) {
        case LUA_TNUMBER: { const double v = lua_tonumber(L_, index); return std::isfinite(v) ? Json::number(v, 6) : Json(); }
        case LUA_TBOOLEAN: return Json::fromBool(lua_toboolean(L_, index) != 0);
        case LUA_TSTRING: { size_t n = 0; const char* s = lua_tolstring(L_, index, &n); return Json::string(std::string(s, std::min<size_t>(n, 4096))); }
        case LUA_TTABLE: {
            if (depth > 8) return Json();
            const lua_Integer len = lua_Integer(lua_rawlen(L_, index));
            if (len > 0) {
                Json arr = Json::array();
                for (lua_Integer i = 1; i <= std::min<lua_Integer>(len, 256); ++i) {
                    lua_rawgeti(L_, index, i); arr.push(toJson(-1, depth + 1)); lua_pop(L_, 1);
                }
                return arr;
            }
            Json obj = Json::object();
            int keys = 0;
            lua_pushnil(L_);
            while (lua_next(L_, index) != 0) {
                if (lua_type(L_, -2) == LUA_TSTRING && ++keys <= 128) obj.set(lua_tostring(L_, -2), toJson(-1, depth + 1));
                lua_pop(L_, 1);
            }
            return obj;
        }
        default: return Json();
        }
    }
};

} // namespace aiverse::studio
