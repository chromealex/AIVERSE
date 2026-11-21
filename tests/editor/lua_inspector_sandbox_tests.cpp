// Lua Custom Inspector sandbox regression (review fix 2026-11-21).
// Hostile inspector scripts must not abort, hang or exhaust memory of the editor:
// metatables on returned tables / _G, __gc finalizers, huge allocations, endless
// loops, self-referencing result tables, deep values.
// The inspector's helpers are private; the dependencies are included first so the
// access trick below only affects the inspector class itself.
#include "apps/editor/Editor.h"
#include "apps/editor/windows/StudioContextMenus.h"
#include "apps/editor/EditorUtil.h"
extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#define class struct
#include "apps/editor/windows/LuaCustomInspector.h"
#undef class

using namespace aiverse;
using namespace aiverse::editor;

#define CHECK(x) do { if (!(x)) { std::cerr << "lua_inspector_sandbox failed line " << __LINE__ << ": " #x "\n"; std::exit(1); } } while (false)

static std::filesystem::path dir;

static void load(LuaCustomInspector& ins, const std::string& source) {
    static int n = 0;
    const auto path = dir / ("s" + std::to_string(++n) + ".lua");
    { std::ofstream out(path, std::ios::binary); out << source; }
    ins.file_ = path.string();
    ins.reload();
}

int main() {
    dir = std::filesystem::temp_directory_path() / "aiverse_lua_inspector_sandbox";
    std::filesystem::create_directories(dir);
    DocEntity ent;
    ent.type = "unit";
    ent.extra = Json::object();
    const auto start = std::chrono::steady_clock::now();

    {   // __index metamethod on a returned table: raw reads, no hang, no error
        LuaCustomInspector ins;
        load(ins, "function inspector(ctx)\n"
                  "  local t = setmetatable({}, {__index=function() while true do end end})\n"
                  "  return {t}\nend\n");
        CHECK(ins.L_);
        bool found = false;
        CHECK(ins.invoke("inspector", ent, 1, nullptr, nullptr, found) == LUA_OK && found);
        lua_rawgeti(ins.L_, -1, 1);
        CHECK(LuaCustomInspector::field(ins.L_, -1, "kind", "label") == "label");
        CHECK(!LuaCustomInspector::flag(ins.L_, -1, "hidden"));
        CHECK(LuaCustomInspector::num(ins.L_, -1, "min", 7) == 7);
        lua_pop(ins.L_, 2);
        CHECK(lua_gettop(ins.L_) == 0);
    }
    {   // __gc finalizers are rejected (they would run without hooks, also at lua_close)
        LuaCustomInspector ins;
        load(ins, "setmetatable({}, {__gc=function() while true do end end})\n");
        CHECK(!ins.L_);
        CHECK(ins.message_.find("__gc") != std::string::npos);
    }
    {   // huge allocation: memory error inside pcall, state stays usable
        LuaCustomInspector ins;
        load(ins, "function inspector(ctx) local s = string.rep('x', 1 << 30) return {s} end\n"
                  "function ok(ctx) return {kind='label'} end\n");
        CHECK(ins.L_);
        bool found = false;
        CHECK(ins.invoke("inspector", ent, 1, nullptr, nullptr, found) != LUA_OK);
        CHECK(!ins.message_.empty());
        CHECK(ins.mem_->used < LuaCustomInspector::kMaxLuaMemory);
        CHECK(ins.invoke("ok", ent, 1, nullptr, nullptr, found) == LUA_OK && found);
        lua_pop(ins.L_, 1);
        CHECK(lua_gettop(ins.L_) == 0);
    }
    {   // endless loop: instruction budget
        LuaCustomInspector ins;
        load(ins, "function inspector(ctx) while true do end end\n");
        bool found = false;
        CHECK(ins.invoke("inspector", ent, 1, nullptr, nullptr, found) != LUA_OK);
        CHECK(ins.message_.find("budget") != std::string::npos);
        CHECK(lua_gettop(ins.L_) == 0);
    }
    {   // a metatable on _G cannot intercept the lookup of a missing callback
        LuaCustomInspector ins;
        load(ins, "setmetatable(_G, {__index=function() while true do end end})\n");
        CHECK(ins.L_);
        bool found = true;
        const std::string key = "k";
        const Json value = Json::number(int64_t(1));
        CHECK(ins.invoke("on_validate", ent, 1, &key, &value, found) == LUA_OK);
        CHECK(!found);
        CHECK(lua_gettop(ins.L_) == 0);
    }
    {   // self-referencing result: bounded conversion
        LuaCustomInspector ins;
        load(ins, "function on_action(ctx, id)\n"
                  "  local t = {} for i = 1, 4096 do t[i] = t end return t\nend\n");
        bool found = false;
        const std::string key = "a";
        CHECK(ins.invoke("on_action", ent, 1, &key, nullptr, found) == LUA_OK && found);
        const Json j = LuaCustomInspector::luaJson(ins.L_, -1);
        CHECK(j.isArray());
        lua_pop(ins.L_, 1);
    }
    {   // deep document values are cut at kMaxDepth instead of overflowing the stack
        LuaCustomInspector ins;
        load(ins, "function on_validate(ctx, key, v) return 42 end\n");
        Json deep = Json::number(int64_t(1));
        for (int i = 0; i < 64; ++i) { Json o = Json::object(); o.set("a", deep); deep = std::move(o); }
        DocEntity heavy = ent;
        heavy.extra.set("deep", deep);
        bool found = false;
        const std::string key = "deep";
        CHECK(ins.invoke("on_validate", heavy, 1, &key, &deep, found) == LUA_OK && found);
        CHECK(lua_type(ins.L_, -1) == LUA_TNUMBER);   // a number is not a validation message
        lua_pop(ins.L_, 1);
        CHECK(lua_gettop(ins.L_) == 0);
    }
    const auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    CHECK(seconds < 30);
    std::filesystem::remove_all(dir);
    std::cout << "Lua Custom Inspector sandbox passed\n";
    return 0;
}
