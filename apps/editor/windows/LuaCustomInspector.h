// AIVERSE Studio: sandboxed Lua 5.4 Custom Inspector / Property Drawer.
// Lua produces a declarative widget tree. C++ owns data, editing, validation and Undo.
// Scripts have no direct access to the filesystem, OS, gameplay world, or raw ImGui.
#pragma once
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <cstdint>
#include <cstdlib>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>
#include "../Editor.h"
#include "StudioContextMenus.h"
#include "../EditorUtil.h"
extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

namespace aiverse::editor {

class LuaCustomInspector {
    // Sandbox contract (review fix 2026-11-21):
    // - every call into script code runs inside lua_pcall of a C trampoline with the
    //   instruction-budget hook and the memory cap active, so a script error never
    //   reaches the panic handler;
    // - tables returned by scripts are read only with raw access (lua_rawget / lua_next),
    //   so __index / __len metamethods can neither error outside pcall nor loop;
    // - globals are looked up raw (a metatable on _G cannot intercept the lookup);
    // - setmetatable rejects __gc (finalizers run without hooks, also at lua_close);
    // - the allocator caps script allocations at kMaxLuaMemory (string.rep etc.);
    //   host-side pushes of document data are not capped (they mirror existing data).
    struct LuaMem { size_t used = 0; bool strict = false; };
    lua_State* L_ = nullptr;
    std::unique_ptr<LuaMem> mem_;
    std::string file_, type_, source_, draft_, message_;
    std::filesystem::file_time_type stamp_{};
    std::map<std::string, std::string> textFields_;
    std::set<std::string> initialized_;
    bool editorOpen_ = false;
    bool applyAll_ = true;
    int lastEntity_ = -1;
    uint64_t seenRevision_ = 0;      // doc revision the text field caches belong to
    std::string liveKey_;            // slider / drag being edited: one undo snapshot per drag
    uint64_t liveRevision_ = 0;      // doc revision right after the last live write
    static constexpr int kMaxNodes = 384;
    static constexpr int kMaxDepth = 12;
    static constexpr int kMaxEdits = 128;
    static constexpr int kMaxJsonNodes = 65536;   // values converted from one script result
    static constexpr size_t kMaxSource = 128 * 1024;
    static constexpr size_t kMaxLuaMemory = size_t(64) << 20;

    static void* luaAlloc(void* ud, void* ptr, size_t osize, size_t nsize) {
        auto* mem = static_cast<LuaMem*>(ud);
        const size_t old = ptr ? osize : 0;
        if (nsize == 0) { std::free(ptr); mem->used -= old; return nullptr; }
        if (mem->strict && nsize > old && mem->used - old + nsize > kMaxLuaMemory) return nullptr;
        void* p = std::realloc(ptr, nsize);
        if (p) mem->used = mem->used - old + nsize;
        return p;
    }
    static int panic(lua_State* L) {
        const char* m = lua_type(L, -1) == LUA_TSTRING ? lua_tostring(L, -1) : "?";
        std::fprintf(stderr, "Lua Inspector: unprotected Lua error: %s\n", m);
        return 0;   // Lua aborts; by construction no host call raises outside pcall
    }
    static int safeSetMetatable(lua_State* L) {
        lua_settop(L, 2);
        if (lua_type(L, 2) == LUA_TTABLE) {
            lua_pushstring(L, "__gc");
            const bool gc = lua_rawget(L, 2) != LUA_TNIL;
            lua_pop(L, 1);
            if (gc) return luaL_error(L, "setmetatable: __gc is not allowed in inspector scripts");
        }
        lua_pushvalue(L, lua_upvalueindex(1));
        lua_insert(L, 1);
        lua_call(L, 2, 1);
        return 1;
    }
    static void hook(lua_State* L, lua_Debug*) { luaL_error(L, "Lua Inspector exceeded instruction budget"); }
    static void budget(lua_State* L) { lua_sethook(L, hook, LUA_MASKCOUNT, 100000); }
    static void unbudget(lua_State* L) { lua_sethook(L, nullptr, 0, 0); }
    static std::string safeName(const std::string& type) {
        std::string out;
        for (unsigned char c : type) out += std::isalnum(c) || c == '_' || c == '-' ? char(c) : '_';
        return out.empty() ? "unnamed" : out;
    }
    // Raw t[key] onto the stack (never calls metamethods). Returns its type.
    static int rawField(lua_State* L, int index, const char* key) {
        index = lua_absindex(L, index);
        if (!lua_istable(L, index)) { lua_pushnil(L); return LUA_TNIL; }
        lua_pushstring(L, key);
        return lua_rawget(L, index);
    }
    static int rawGlobal(lua_State* L, const char* name) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, LUA_RIDX_GLOBALS);
        lua_pushstring(L, name);
        lua_rawget(L, -2);
        lua_remove(L, -2);
        return lua_type(L, -1);
    }
    static std::string field(lua_State* L, int index, const char* key, const char* fallback = "") {
        const int t = rawField(L, index, key);
        const char* v = t == LUA_TSTRING || t == LUA_TNUMBER ? lua_tostring(L, -1) : nullptr;
        std::string out = v ? v : fallback;
        lua_pop(L, 1);
        return out;
    }
    static bool flag(lua_State* L, int index, const char* key, bool fallback = false) {
        rawField(L, index, key);
        bool v = lua_isboolean(L, -1) ? lua_toboolean(L, -1) != 0 : fallback;
        lua_pop(L, 1);
        return v;
    }
    static double num(lua_State* L, int index, const char* key, double fallback = 0) {
        rawField(L, index, key);
        double v = lua_type(L, -1) == LUA_TNUMBER ? lua_tonumber(L, -1) : fallback;
        lua_pop(L, 1);
        return std::isfinite(v) ? v : fallback;
    }
    static bool safePath(const std::string& path) {
        if (path.empty() || path.size() > 192) return false;
        for (unsigned char c : path) if (!std::isalnum(c) && c != '_' && c != '-' && c != '.' && c != '[' && c != ']') return false;
        if (path.find("..") != std::string::npos || path.front() == '.' || path.back() == '.') return false;
        return true;
    }
    struct Token { std::string key; int arrayIndex = -1; }; // Lua arrays: 1-based
    static bool tokenize(const std::string& path, std::vector<Token>& tokens) {
        if (!safePath(path)) return false;
        size_t i = 0;
        while (i < path.size()) {
            size_t start = i;
            while (i < path.size() && path[i] != '.' && path[i] != '[') ++i;
            if (start == i) return false;
            tokens.push_back({path.substr(start, i - start), -1});
            while (i < path.size() && path[i] == '[') {
                ++i;
                const size_t from = i;
                while (i < path.size() && std::isdigit(static_cast<unsigned char>(path[i]))) ++i;
                if (from == i || i >= path.size() || path[i] != ']') return false;
                int index = 0;
                for (size_t j = from; j < i; ++j) {
                    index = index * 10 + (path[j] - '0');
                    if (index > 4096) return false;
                }
                if (index < 1) return false;
                tokens.push_back({{}, index});
                ++i;
            }
            if (i < path.size()) {
                if (path[i++] != '.' || i == path.size()) return false;
            }
            if (tokens.size() > 32) return false;
        }
        return !tokens.empty();
    }
    static const Json& getPath(const Json& root, const std::string& path) {
        static const Json nil;
        std::vector<Token> tokens;
        if (!tokenize(path, tokens)) return nil;
        const Json* p = &root;
        for (auto& t : tokens) {
            if (t.arrayIndex > 0) {
                if (!p->isArray() || p->size() < size_t(t.arrayIndex)) return nil;
                p = &(*p)[size_t(t.arrayIndex - 1)];
            } else {
                if (!p->isObject()) return nil;
                p = &(*p)[t.key];
            }
        }
        return *p;
    }
    static bool setPath(Json& root, const std::string& path, const Json& value, std::string& error) {
        std::vector<Token> ts;
        if (!tokenize(path, ts)) { error = "Invalid property path: " + path; return false; }
        Json* dst = &root;
        for (size_t i = 0; i < ts.size(); ++i) {
            const bool last = i + 1 == ts.size();
            auto& t = ts[i];
            if (t.arrayIndex > 0) {
                if (!dst->isArray()) { error = "Property is not an array: " + path; return false; }
                if (size_t(t.arrayIndex) > dst->size()) { error = "Array index out of range: " + path; return false; }
                if (last) dst->at(size_t(t.arrayIndex - 1)) = value;
                else dst = &dst->at(size_t(t.arrayIndex - 1));
            } else {
                if (!dst->isObject()) { error = "Property is not an object: " + path; return false; }
                if (last) dst->set(t.key, value);
                else {
                    if (const Json* found = &(*dst)[t.key]; found->isNull()) {
                        dst->set(t.key, ts[i + 1].arrayIndex > 0 ? Json::array() : Json::object());
                    }
                    dst = &dst->member(t.key);
                }
            }
        }
        return true;
    }
    // Raw, allocation-free (on the Lua side) conversion; `nodes` bounds the total work
    // so shared / self-referencing tables cannot blow up (t = {t, t, ...}).
    static Json luaJson(lua_State* L, int index) { int nodes = kMaxJsonNodes; return luaJson(L, index, 0, nodes); }
    static Json luaJson(lua_State* L, int index, int depth, int& nodes) {
        index = lua_absindex(L, index);
        if (depth > kMaxDepth || --nodes < 0 || !lua_checkstack(L, 3)) return {};
        const int type = lua_type(L, index);
        if (type == LUA_TBOOLEAN) return Json::fromBool(lua_toboolean(L, index) != 0);
        if (type == LUA_TNUMBER) {
            if (lua_isinteger(L, index)) return Json::number(int64_t(lua_tointeger(L, index)));
            double v = lua_tonumber(L, index);
            return std::isfinite(v) ? Json::number(v, 4) : Json{};
        }
        if (type == LUA_TSTRING) {
            size_t len = 0;
            const char* s = lua_tolstring(L, index, &len);
            return Json::string(std::string(s, len));
        }
        if (type != LUA_TTABLE) return {};
        const size_t n = lua_rawlen(L, index);
        bool isArray = n > 0;
        lua_pushnil(L);
        while (lua_next(L, index)) {
            if (!lua_isinteger(L, -2) || lua_tointeger(L, -2) < 1 || size_t(lua_tointeger(L, -2)) > n) isArray = false;
            lua_pop(L, 1);
            if (--nodes < 0) { lua_pop(L, 1); return {}; }
        }
        if (isArray) {
            Json a = Json::array();
            for (size_t i = 1; i <= std::min(n, size_t(4096)) && nodes > 0; ++i) {
                lua_rawgeti(L, index, lua_Integer(i)); a.push(luaJson(L, -1, depth + 1, nodes)); lua_pop(L, 1);
            }
            return a;
        }
        Json object = Json::object();
        lua_pushnil(L);
        while (lua_next(L, index)) {
            if (lua_type(L, -2) == LUA_TSTRING && nodes > 0) {
                size_t len = 0;
                const char* k = lua_tolstring(L, -2, &len);
                if (k && len <= 128 && std::strlen(k) == len) object.set(k, luaJson(L, -1, depth + 1, nodes));
            }
            lua_pop(L, 1);
        }
        return object;
    }
    // Only called inside the protected trampoline (callProtected).
    static void pushJson(lua_State* L, const Json& value, int depth = 0) {
        if (!lua_checkstack(L, 3)) luaL_error(L, "Lua Inspector: value too deep");
        if (depth > kMaxDepth) { lua_pushnil(L); return; }
        if (value.isString()) { const auto s = value.str(); lua_pushlstring(L, s.data(), s.size()); }
        else if (value.isNumber()) lua_pushnumber(L, value.toDouble());
        else if (value.type() == Json::Type::Bool) lua_pushboolean(L, value.boolean());
        else if (value.isObject()) {
            lua_newtable(L);
            for (const auto& [k, v] : value.members()) { pushJson(L, v, depth + 1); lua_setfield(L, -2, k.c_str()); }
        } else if (value.isArray()) {
            lua_newtable(L);
            for (size_t i = 0; i < value.size() && i < 4096; ++i) { pushJson(L, value[i], depth + 1); lua_rawseti(L, -2, lua_Integer(i + 1)); }
        } else lua_pushnil(L);
    }
    static void context(lua_State* L, const DocEntity& ent, size_t count) {
        lua_newtable(L);
        lua_pushstring(L, ent.type.c_str()); lua_setfield(L, -2, "type");
        lua_pushstring(L, ent.id.c_str()); lua_setfield(L, -2, "id");
        lua_pushinteger(L, ent.owner); lua_setfield(L, -2, "owner");
        lua_pushnumber(L, ent.x); lua_setfield(L, -2, "x");
        lua_pushnumber(L, ent.z); lua_setfield(L, -2, "z");
        lua_pushinteger(L, lua_Integer(count)); lua_setfield(L, -2, "selection_count");
        pushJson(L, ent.extra); lua_setfield(L, -2, "values");
    }
    void reset() {
        if (L_) lua_close(L_);   // no finalizers: setmetatable rejects __gc
        L_ = nullptr;
        mem_.reset();
        textFields_.clear(); initialized_.clear(); liveKey_.clear();
    }
    std::string errorText(const char* fallback) const {
        return lua_type(L_, -1) == LUA_TSTRING ? std::string(lua_tostring(L_, -1)) : std::string(fallback);
    }
    struct CallArgs {
        const char* function; const DocEntity* ent; size_t count;
        const std::string* key; const Json* value; LuaMem* mem;
    };
    // Runs entirely under lua_pcall: global lookup, argument construction and the
    // script call itself. Returns (found, result).
    static int callProtected(lua_State* L) {
        auto* a = static_cast<CallArgs*>(lua_touserdata(L, 1));
        lua_settop(L, 0);
        if (!lua_checkstack(L, 2 * kMaxDepth + 16)) return luaL_error(L, "Lua Inspector: stack exhausted");
        if (rawGlobal(L, a->function) != LUA_TFUNCTION) { lua_pushboolean(L, 0); lua_pushnil(L); return 2; }
        context(L, *a->ent, a->count);
        int nargs = 1;
        if (a->key) { lua_pushlstring(L, a->key->data(), a->key->size()); ++nargs; }
        if (a->value) { pushJson(L, *a->value); ++nargs; }
        a->mem->strict = true;
        budget(L);
        lua_call(L, nargs, 1);
        unbudget(L);
        a->mem->strict = false;
        lua_pushboolean(L, 1);
        lua_insert(L, -2);
        return 2;
    }
    // On LUA_OK with found == true the script's single result is left on the stack.
    int invoke(const char* function, const DocEntity& ent, size_t count, const std::string* key,
               const Json* value, bool& found) {
        found = false;
        if (!L_ || !lua_checkstack(L_, 8)) return LUA_ERRMEM;
        CallArgs args{function, &ent, count, key, value, mem_.get()};
        lua_pushcfunction(L_, callProtected);
        lua_pushlightuserdata(L_, &args);
        const int rc = lua_pcall(L_, 1, 2, 0);
        unbudget(L_);
        mem_->strict = false;
        if (rc != LUA_OK) { message_ = errorText("Lua callback failed"); lua_pop(L_, 1); return rc; }
        found = lua_toboolean(L_, -2) != 0;
        lua_remove(L_, -2);
        if (!found) lua_pop(L_, 1);
        return LUA_OK;
    }
    void reload() {
        reset(); source_.clear(); message_.clear();
        std::ifstream in(file_, std::ios::binary);
        if (!in) return;
        std::ostringstream contents; contents << in.rdbuf(); source_ = contents.str();
        if (source_.size() > kMaxSource) { message_ = "Lua script exceeds 128 KiB"; return; }
        mem_ = std::make_unique<LuaMem>();
        L_ = lua_newstate(luaAlloc, mem_.get());
        if (!L_) { mem_.reset(); message_ = "Could not allocate Lua state"; return; }
        lua_atpanic(L_, panic);
        const luaL_Reg libs[] = {{LUA_GNAME, luaopen_base}, {LUA_STRLIBNAME, luaopen_string},
            {LUA_TABLIBNAME, luaopen_table}, {LUA_MATHLIBNAME, luaopen_math}, {LUA_UTF8LIBNAME, luaopen_utf8}};
        for (const auto& lib : libs) { luaL_requiref(L_, lib.name, lib.func, 1); lua_pop(L_, 1); }
        for (const char* k : {"dofile", "loadfile", "load", "collectgarbage"}) { lua_pushnil(L_); lua_setglobal(L_, k); }
        lua_getglobal(L_, "string"); lua_pushnil(L_); lua_setfield(L_, -2, "dump"); lua_pop(L_, 1);
        lua_getglobal(L_, "setmetatable"); lua_pushcclosure(L_, safeSetMetatable, 1); lua_setglobal(L_, "setmetatable");
        mem_->strict = true;
        budget(L_);
        int err = luaL_loadbufferx(L_, source_.data(), source_.size(), file_.c_str(), "t");
        if (err == LUA_OK) err = lua_pcall(L_, 0, 0, 0);
        unbudget(L_);
        mem_->strict = false;
        if (err != LUA_OK) { message_ = errorText("Lua error"); reset(); }
    }
    std::vector<int> targets(Editor& ed) const {
        std::vector<int> out;
        const auto& d = ed.doc().data;
        if (applyAll_) for (int i : ed.selectedEntities())
            if (i >= 0 && size_t(i) < d.entities.size() && d.entities[size_t(i)].type == type_) out.push_back(i);
        if (out.empty() && ed.selection.kind == Selection::Entity && ed.selection.index >= 0 && size_t(ed.selection.index) < d.entities.size())
            out.push_back(ed.selection.index);
        return out;
    }
    struct Edit { std::string path; Json value; std::string op = "set"; };
    static bool editsFrom(lua_State* L, int index, std::vector<Edit>& result) {
        index = lua_absindex(L, index);
        if (!lua_istable(L, index)) return false;
        auto one = [&](int at) {
            const std::string path = field(L, at, "key", field(L, at, "path").c_str());
            if (!safePath(path)) return false;
            rawField(L, at, "value");
            Edit e{path, luaJson(L, -1), field(L, at, "op", "set")};
            lua_pop(L, 1);
            if (e.op != "set" && e.op != "remove" && e.op != "append") return false;
            result.push_back(std::move(e));
            return true;
        };
        rawField(L, index, "edits");
        if (lua_istable(L, -1)) {
            const size_t n = lua_rawlen(L, -1);
            if (n > kMaxEdits) { lua_pop(L, 1); return false; }
            for (size_t i = 1; i <= n; ++i) {
                lua_rawgeti(L, -1, lua_Integer(i));
                const bool ok = lua_istable(L, -1) && one(lua_gettop(L));
                lua_pop(L, 1);
                if (!ok) { lua_pop(L, 1); return false; }
            }
            lua_pop(L, 1);
            return !result.empty();
        }
        lua_pop(L, 1);
        return one(index);
    }
    bool runCallback(Editor& ed, int index, const char* function, const std::string& key,
                     const Json* newValue, std::vector<Edit>& edits) {
        if (!L_) return false;
        bool found = false;
        const int rc = invoke(function, ed.doc().data.entities[size_t(index)], targets(ed).size(), &key, newValue, found);
        if (rc != LUA_OK) { edits.clear(); return true; }
        if (!found) return false;
        if (lua_type(L_, -1) == LUA_TSTRING && std::string(lua_tostring(L_, -1)) == "cancel") {
            edits.clear();
        } else if (lua_istable(L_, -1)) {
            std::vector<Edit> replacement;
            if (editsFrom(L_, -1, replacement)) edits = std::move(replacement);
            else if (newValue) edits = {{key, luaJson(L_, -1), "set"}};
        } else if (newValue && !lua_isnil(L_, -1)) edits = {{key, luaJson(L_, -1), "set"}};
        lua_pop(L_, 1);
        return true;
    }
    static bool applyEdit(Json& object, const Edit& e, std::string& error) {
        if (e.op == "set") return setPath(object, e.path, e.value, error);
        std::vector<Token> tokens;
        if (!tokenize(e.path, tokens)) { error = "Invalid path: " + e.path; return false; }
        // Immutable lookup + mutable traversal: no partial changes to document (transaction uses a copy).
        Json* p = &object;
        for (size_t i = 0; i < tokens.size(); ++i) {
            const auto& t = tokens[i];
            const bool final = i + 1 == tokens.size();
            if (t.arrayIndex > 0) {
                if (!p->isArray() || p->size() < size_t(t.arrayIndex)) { error = "Array index out of range: " + e.path; return false; }
                if (final) { if (e.op != "remove") return false; p->eraseAt(size_t(t.arrayIndex - 1)); return true; }
                p = &p->at(size_t(t.arrayIndex - 1));
            } else {
                if (!p->isObject()) { error = "Expected object: " + e.path; return false; }
                if (final) {
                    if (e.op == "remove") { p->erase(t.key); return true; }
                    Json array = (*p)[t.key];
                    if (!array.isArray()) { if (!array.isNull()) { error = "Expected array: " + e.path; return false; } array = Json::array(); }
                    if (array.size() >= 4096) { error = "Array exceeds 4096 entries"; return false; }
                    array.push(e.value); p->set(t.key, std::move(array)); return true;
                }
                Json* found = p->find(t.key);
                if (!found) { error = "Missing property: " + e.path; return false; }
                p = found;
            }
        }
        return false;
    }
    // liveKey: a continuous widget (slider / drag / color) writing every frame. The
    // first write of a drag takes the undo snapshot, later writes of the same drag
    // (same key, no other document change in between) update in place.
    void transact(Editor& ed, std::vector<Edit> edits, const std::string* liveKey = nullptr) {
        if (ed.playing() || edits.empty()) return;
        auto ids = targets(ed);
        if (ids.empty() || edits.size() > kMaxEdits) return;
        auto& doc = ed.doc();
        std::vector<Json> next;
        next.reserve(ids.size());
        std::string error;
        for (int id : ids) {
            Json copy = doc.data.entities[size_t(id)].extra;
            if (!copy.isObject()) copy = Json::object();
            for (const auto& e : edits) if (!applyEdit(copy, e, error)) { message_ = error.empty() ? "Invalid edit" : error; return; }
            next.push_back(std::move(copy));
        }
        bool different = false;
        for (size_t i = 0; i < ids.size(); ++i) if (!(next[i] == doc.data.entities[size_t(ids[i])].extra)) different = true;
        if (!different) return;
        const bool merge = liveKey && !liveKey_.empty() && liveKey_ == *liveKey && liveRevision_ == doc.revision;
        if (!merge) doc.beginEdit();
        for (size_t i = 0; i < ids.size(); ++i) doc.data.entities[size_t(ids[i])].extra = std::move(next[i]);
        doc.touched();
        if (liveKey) { liveKey_ = *liveKey; liveRevision_ = doc.revision; } else liveKey_.clear();
        textFields_.clear(); initialized_.clear();
        seenRevision_ = doc.revision;
        message_ = "Applied to " + std::to_string(ids.size()) + " object(s) · Undo available";
    }
    bool validate(Editor& ed, int index, const std::string& key, const Json& v) {
        if (!L_) return true;
        bool found = false;
        const int rc = invoke("on_validate", ed.doc().data.entities[size_t(index)], targets(ed).size(), &key, &v, found);
        if (rc != LUA_OK) return false;
        if (!found) return true;
        bool valid = !lua_isboolean(L_, -1) || lua_toboolean(L_, -1);
        // Only a string is a message (lua_isstring is also true for numbers).
        if (lua_type(L_, -1) == LUA_TSTRING) { message_ = lua_tostring(L_, -1); valid = false; }
        if (!valid && message_.empty()) message_ = "Lua validation rejected the edit";
        lua_pop(L_, 1);
        return valid;
    }
    void change(Editor& ed, int index, const std::string& key, const Json& v, bool live = false) {
        if (!validate(ed, index, key, v)) return;
        std::vector<Edit> edits = {{key, v, "set"}};
        runCallback(ed, index, "on_change", key, &v, edits);
        transact(ed, std::move(edits), live ? &key : nullptr);
    }
    void action(Editor& ed, int index, const std::string& name) {
        std::vector<Edit> edits;
        runCallback(ed, index, "on_action", name, nullptr, edits);
        transact(ed, std::move(edits));
    }
    static bool assetInProject(const std::filesystem::path& root, const std::filesystem::path& path) {
        if (path.empty() || path.is_absolute()) return false;
        for (const auto& p : path) if (p == "..") return false;
        std::error_code ec;
        return std::filesystem::is_regular_file(root / path, ec) && !ec;
    }
    void assetWidget(Editor& ed, int index, const std::string& key, const std::string& label, const Json& current) {
        auto base = std::filesystem::path(ed.ws().project.dir);
        if (!initialized_.count(key)) { textFields_[key] = current.str(); initialized_.insert(key); }
        auto& value = textFields_[key];
        ui::inputString((label + "##asset").c_str(), value);
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            if (value.empty() || assetInProject(base, std::filesystem::path(value))) change(ed, index, key, Json::string(value));
            else message_ = "Asset must be an existing project-relative file";
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("...##pick")) ImGui::OpenPopup("##asset_pick");
        if (ImGui::BeginPopup("##asset_pick")) {
            ImGui::TextDisabled("Select project asset");
            std::error_code ec;
            auto iter = std::filesystem::recursive_directory_iterator(base, std::filesystem::directory_options::skip_permission_denied, ec);
            const auto end = std::filesystem::recursive_directory_iterator();
            int visited = 0, shown = 0;
            for (; !ec && iter != end && visited < 1200 && shown < 140; iter.increment(ec), ++visited) {
                if (iter.depth() > 6) { iter.disable_recursion_pending(); continue; }
                if (!iter->is_regular_file(ec) || iter->is_symlink(ec)) continue;
                const auto relative = std::filesystem::relative(iter->path(), base, ec);
                if (ec) break;
                const std::string option = relative.generic_string();
                if (option.size() > 220) continue;
                if (ImGui::Selectable(option.c_str(), option == value)) {
                    change(ed, index, key, Json::string(option));
                    ImGui::CloseCurrentPopup();
                }
                ++shown;
            }
            if (!shown) ImGui::TextDisabled("No assets found");
            ImGui::EndPopup();
        }
    }
    bool widget(Editor& ed, int index, int at, int& budgetNodes, int depth, const std::string& prefix = "") {
        if (++budgetNodes > kMaxNodes || depth > kMaxDepth || !lua_checkstack(L_, 6)) return false;
        at = lua_absindex(L_, at);
        const std::string kind = field(L_, at, "kind", "label");
        const std::string rawKey = field(L_, at, "key");
        const std::string key = prefix.empty() ? rawKey : rawKey.empty() ? prefix : prefix + "." + rawKey;
        const std::string label = field(L_, at, "label", rawKey.c_str());
        if (flag(L_, at, "hidden")) return true;
        ImGui::PushID(budgetNodes);
        if (kind == "label" || kind == "help") {
            if (kind == "help") ImGui::TextDisabled("%s", label.c_str());
            else ImGui::TextWrapped("%s", label.c_str());
        } else if (kind == "spacer") ImGui::Spacing();
        else if (kind == "separator") ImGui::SeparatorText(label.c_str());
        else if (kind == "group" || kind == "foldout" || kind == "row") {
            bool show = kind == "row" || ImGui::CollapsingHeader(label.c_str(), ImGuiTreeNodeFlags_DefaultOpen);
            if (show) {
                if (kind == "row") ImGui::BeginGroup();
                rawField(L_, at, "children");
                if (lua_istable(L_, -1)) {
                    const size_t n = std::min(static_cast<size_t>(lua_rawlen(L_, -1)), size_t(kMaxNodes));
                    for (size_t i = 1; i <= n && budgetNodes < kMaxNodes; ++i) {
                        lua_rawgeti(L_, -1, lua_Integer(i));
                        if (lua_istable(L_, -1)) widget(ed, index, -1, budgetNodes, depth + 1, key);
                        lua_pop(L_, 1);
                        if (kind == "row" && i < n) ImGui::SameLine();
                    }
                }
                lua_pop(L_, 1);
                if (kind == "row") ImGui::EndGroup();
            }
        } else if (kind == "button") {
            if (ImGui::Button(label.c_str())) action(ed, index, rawKey.empty() ? label : rawKey);
        } else if (!safePath(key)) {
            ImGui::TextDisabled("Invalid property path: %s", key.c_str());
        } else {
            const Json& current = getPath(ed.doc().data.entities[size_t(index)].extra, key);
            bool readOnly = flag(L_, at, "readonly", false) || ed.playing();
            ImGui::BeginDisabled(readOnly);
            if (kind == "checkbox" || kind == "bool") {
                bool v = current.boolean(flag(L_, at, "default"));
                if (ImGui::Checkbox(label.c_str(), &v)) change(ed, index, key, Json::fromBool(v));
            } else if (kind == "slider" || kind == "number" || kind == "int") {
                float v = float(current.toDouble(num(L_, at, "default")));
                const float lo = float(num(L_, at, "min")), hi = float(num(L_, at, "max", 100));
                bool edited = kind == "slider" ? ImGui::SliderFloat(label.c_str(), &v, lo, std::max(lo, hi)) : ImGui::DragFloat(label.c_str(), &v, 0.1f);
                if (ImGui::IsItemActivated()) liveKey_.clear();   // a new drag gets its own undo step
                if (edited && std::isfinite(v)) change(ed, index, key, kind == "int" ? Json::number(int64_t(std::llround(v))) : Json::number(double(v), 4), true);
            } else if (kind == "text" || kind == "multiline" || kind == "asset") {
                if (kind == "asset") assetWidget(ed, index, key, label, current);
                else {
                    if (!initialized_.count(key)) { textFields_[key] = current.str(); initialized_.insert(key); }
                    auto& v = textFields_[key];
                    bool edited = kind == "multiline" ? ui::inputMultiline((label + "##lua").c_str(), v, ImVec2(-1, 90)) : ui::inputString((label + "##lua").c_str(), v);
                    (void)edited;
                    if (ImGui::IsItemDeactivatedAfterEdit()) change(ed, index, key, Json::string(v));
                }
            } else if (kind == "enum" || kind == "dropdown") {
                std::string value = current.str();
                if (ImGui::BeginCombo(label.c_str(), value.c_str())) {
                    rawField(L_, at, "options");
                    if (lua_istable(L_, -1)) for (size_t i = 1; i <= std::min(static_cast<size_t>(lua_rawlen(L_, -1)), size_t(128)); ++i) {
                        lua_rawgeti(L_, -1, lua_Integer(i));
                        const int ot = lua_type(L_, -1);
                        const std::string option = ot == LUA_TSTRING || ot == LUA_TNUMBER ? lua_tostring(L_, -1) : ot == LUA_TTABLE ? field(L_, -1, "value") : std::string();
                        if (ImGui::Selectable(option.c_str(), value == option)) change(ed, index, key, Json::string(option));
                        lua_pop(L_, 1);
                    }
                    lua_pop(L_, 1); ImGui::EndCombo();
                }
            } else if (kind == "vector2" || kind == "vector3" || kind == "vector4" || kind == "color") {
                const int n = kind == "vector2" ? 2 : kind == "vector3" ? 3 : 4;
                float vec[4] = {0, 0, 0, 1};
                for (int i = 0; i < n; ++i) if (current.isArray() && size_t(i) < current.size()) vec[i] = float(current[size_t(i)].toDouble());
                bool changed = kind == "color" ? ImGui::ColorEdit4(label.c_str(), vec) : n == 2 ? ImGui::DragFloat2(label.c_str(), vec, 0.1f) : n == 3 ? ImGui::DragFloat3(label.c_str(), vec, 0.1f) : ImGui::DragFloat4(label.c_str(), vec, 0.1f);
                if (ImGui::IsItemActivated()) liveKey_.clear();
                if (changed) {
                    Json arr = Json::array();
                    for (int i = 0; i < n; ++i) arr.push(Json::number(double(vec[i]), 4));
                    change(ed, index, key, arr, true);
                }
            } else if (kind == "array") {
                // A copy: transact() below replaces entities[index].extra, which
                // `current` points into (use-after-free while iterating).
                const Json array = current;
                ImGui::Text("%s (%zu)", label.c_str(), array.isArray() ? array.size() : 0u);
                ImGui::SameLine();
                if (ImGui::SmallButton("+")) {
                    rawField(L_, at, "default"); Json d = luaJson(L_, -1); lua_pop(L_, 1);
                    transact(ed, {{key, d, "append"}});
                }
                if (array.isArray()) for (size_t i = 0; i < array.size() && i < 128; ++i) {
                    ImGui::PushID(int(i));
                    const std::string childPath = key + "[" + std::to_string(i + 1) + "]";
                    const bool open = ImGui::TreeNodeEx("##element", ImGuiTreeNodeFlags_SpanAvailWidth, "[%zu]", i + 1);
                    ImGui::SameLine();
                    if (ImGui::SmallButton("-")) {
                        transact(ed, {{childPath, {}, "remove"}});
                        if (open) ImGui::TreePop();
                        ImGui::PopID();
                        break;   // indices shifted: redraw from the document next frame
                    }
                    if (open) {
                        rawField(L_, at, "element");
                        if (lua_istable(L_, -1)) {
                            // Child widgets inherit element's path, e.g. inventory[1].count.
                            widget(ed, index, -1, budgetNodes, depth + 1, childPath);
                        } else {
                            ImGui::TextDisabled("%s", array[i].dump(-1).c_str());
                        }
                        lua_pop(L_, 1);
                        ImGui::TreePop();
                    }
                    ImGui::PopID();
                }
            } else {
                ImGui::TextDisabled("Unsupported inspector widget: %s", kind.c_str());
            }
            ImGui::EndDisabled();
        }
        const std::string tooltip = field(L_, at, "tooltip");
        if (!tooltip.empty() && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tooltip.c_str());
        if (StudioContextMenus::get().available(ed) &&
            ImGui::BeginPopupContextItem("##inspector_property_context")) {
            StudioContextMenus::get().draw(ed, "inspector.property", key.c_str());
            ImGui::EndPopup();
        }
        ImGui::PopID();
        return true;
    }

public:
    ~LuaCustomInspector() { reset(); }
    void draw(Editor& ed) {
        if (ed.ws().project.dir.empty() || ed.selection.kind != Selection::Entity ||
            ed.selection.index < 0 || size_t(ed.selection.index) >= ed.doc().data.entities.size()) return;
        const auto& ent = ed.doc().data.entities[size_t(ed.selection.index)];
        const std::string type = safeName(ent.type);
        const auto path = (std::filesystem::path(ed.ws().project.dir) / "editor" / "inspectors" / (type + ".lua")).string();
        if (file_ != path) { file_ = path; type_ = ent.type; draft_.clear(); source_.clear(); stamp_ = {}; editorOpen_ = false; reset(); }
        if (lastEntity_ != ed.selection.index) { lastEntity_ = ed.selection.index; textFields_.clear(); initialized_.clear(); }
        // Undo / redo / other windows change the document: drop cached text field
        // values (not while a field is being typed into; that edit commits first).
        if (seenRevision_ != ed.doc().revision && !ImGui::IsAnyItemActive()) {
            seenRevision_ = ed.doc().revision; textFields_.clear(); initialized_.clear();
        }
        if (!ImGui::IsAnyItemActive()) liveKey_.clear();   // a released drag ends its undo step
        std::error_code ec;
        auto now = std::filesystem::last_write_time(file_, ec);
        if (ec) now = {};
        if (now != stamp_) { stamp_ = now; reload(); draft_ = source_; }
        ImGui::Separator();
        ui::section(ICON_BRACES, "Lua Custom Inspector");
        ImGui::TextDisabled("%s", ("editor/inspectors/" + type + ".lua").c_str());
        if (ed.selectedEntities().size() > 1) {
            ImGui::Checkbox("Редактировать все выбранные объекты этого типа", &applyAll_);
            ImGui::TextDisabled("%zu выбранных", targets(ed).size());
        }
        if (ui::button(ICON_PENCIL, editorOpen_ ? "Скрыть код" : "Редактировать Lua")) editorOpen_ = !editorOpen_;
        ImGui::SameLine();
        if (ui::toolButton(ICON_REFRESH, "Перезагрузить Lua")) { reload(); draft_ = source_; }
        if (!message_.empty()) ui::callout(ui::Tone::Warn, message_);
        if (L_) {
            bool found = false;
            if (invoke("inspector", ent, targets(ed).size(), nullptr, nullptr, found) == LUA_OK && found) {
                if (lua_istable(L_, -1)) {
                    int count = 0;
                    size_t n = std::min(static_cast<size_t>(lua_rawlen(L_, -1)), size_t(kMaxNodes));
                    for (size_t i = 1; i <= n && count < kMaxNodes && L_; ++i) {
                        lua_rawgeti(L_, -1, lua_Integer(i));
                        if (lua_istable(L_, -1)) widget(ed, ed.selection.index, -1, count, 0);
                        lua_pop(L_, 1);
                    }
                }
                lua_pop(L_, 1);
            }
        } else if (!editorOpen_) ui::hint("Для этого типа ещё нет Lua-инспектора. Нажмите «Редактировать Lua».");
        if (editorOpen_) {
            if (draft_.empty() && source_.empty()) draft_ =
                "function inspector(ctx)\n  return {\n"
                "    {kind='group', label='Gameplay', children={\n"
                "      {kind='slider', key='stats.health', label='Health', min=0, max=100},\n"
                "      {kind='checkbox', key='enabled', label='Enabled'},\n"
                "      {kind='enum', key='ai.mode', label='AI Mode', options={'Idle','Patrol','Attack'}},\n"
                "      {kind='asset', key='model', label='Model path'},\n"
                "      {kind='array', key='inventory', label='Inventory', default={name='New', count=1},\n"
                "        element={kind='group', label='Item', children={\n"
                "          {kind='text', key='name', label='Name'},\n"
                "          {kind='int', key='count', label='Count'}\n"
                "        }}}\n"
                "    }},\n    {kind='button', key='reset', label='Reset Health'}\n  }\nend\n\n"
                "function on_action(ctx, id)\n  if id == 'reset' then\n"
                "    return {edits={{key='stats.health',value=100},{key='enabled',value=true}}}\n"
                "  end\nend\n";
            ui::inputMultiline("##studio_lua_source", draft_, ImVec2(-1, 280));
            if (ui::button(ICON_SAVE, "Сохранить Lua")) {
                if (draft_.size() > kMaxSource) { message_ = "Lua script exceeds 128 KiB"; return; }
                std::filesystem::create_directories(std::filesystem::path(file_).parent_path(), ec);
                if (ec) { message_ = ec.message(); return; }
                std::ofstream out(file_, std::ios::binary | std::ios::trunc);
                if (!out) { message_ = "Could not open script for writing"; return; }
                out << draft_; out.close();
                if (!out) { message_ = "Lua file save failed"; return; }
                reload(); draft_ = source_;
                stamp_ = std::filesystem::last_write_time(file_, ec);
            }
        }
    }
};

} // namespace aiverse::editor
