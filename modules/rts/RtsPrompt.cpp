// Prompts: script-driven choice menus (see RtsPrompt.h, docs/prompts.md), with the `prompt` Lua table.
#include "RtsPrompt.h"
#include "RtsInternal.h"
#include "RtsModule.h"
#include "RtsScriptInternal.h"

namespace aiverse::game {

PromptStore* Prompts::store(Simulation& sim) const { return active() ? sim.world().resource<PromptStore>() : nullptr; }

void Prompts::setup(Simulation& sim, RtsModule& m, const Json& rulesJson) {
    m_ = &m;
    enabled_ = rulesJson.getBool("prompts", false);
    if (!enabled_) return;
    sim.world().setResource<PromptStore>().players.assign(m.state(sim).players.size(), {});
}

const Prompt* Prompts::of(Simulation& sim, uint8_t player) const {
    auto* st = store(sim);
    return st && player < st->players.size() && st->players[player].open() ? &st->players[player] : nullptr;
}

bool Prompts::show(Simulation& sim, uint8_t player, Prompt p) {
    auto* st = store(sim);
    if (!st || player == 0 || player >= st->players.size() || p.id.empty() || p.id.find(':') != std::string::npos) return false;
    if (p.options.empty() || p.options.size() > 9 || p.lines.size() > 8) return false;
    st->players[player] = std::move(p);
    return true;
}

void Prompts::close(Simulation& sim, uint8_t player) {
    auto* st = store(sim);
    if (st && player < st->players.size()) st->players[player] = {};
}

bool Prompts::command(Simulation& sim, const Command& cmd) {
    if (cmd.type != kCmdPrompt) return false;
    auto* st = store(sim);
    if (!st || cmd.player == 0 || cmd.player >= st->players.size() || !m_->scripts()) return true;
    Prompt& p = st->players[cmd.player];
    if (cmd.param == kPromptInteract) {
        if (!p.open()) m_->scripts()->allowNamed(sim, "on_interact", cmd.player, "");
        return true;
    }
    if (!p.open() || cmd.param < 0 || cmd.param >= int64_t(p.options.size())) return true;
    const std::string arg = p.id + ":" + std::to_string(cmd.param + 1);
    p = {};   // closed before the script answers: it may open the next one
    m_->scripts()->allowNamed(sim, "on_prompt", cmd.player, arg);
    return true;
}

void Prompts::hash(Simulation& sim, Hasher& h) const {
    auto* st = store(sim);
    if (!st) return;
    for (auto& p : st->players) {
        h.str(p.id); h.str(p.title);
        h.u32(uint32_t(p.lines.size())); for (auto& s : p.lines) h.str(s);
        h.u32(uint32_t(p.options.size())); for (auto& s : p.options) h.str(s);
    }
}

void Prompts::io(Simulation& sim, Archive& a) {
    auto* st = store(sim);
    if (!st) return;
    uint32_t n = uint32_t(st->players.size());
    a(n);
    if (!a.writing() && n != st->players.size()) { a.fail(); return; }
    for (auto& p : st->players) a(p.id, p.title, p.lines, p.options);
}

// ================================================================= Lua

namespace script {
namespace {

Prompts* promptsOf(Vm& vm) {
    Prompts* p = vm.module ? vm.module->prompts() : nullptr;
    return p && p->active() && vm.sim && p->store(*vm.sim) ? p : nullptr;
}
bool strings(lua_State* L, int idx, std::vector<std::string>& out) {
    if (lua_isnoneornil(L, idx)) return true;
    if (!lua_istable(L, idx)) return false;
    for (lua_Integer i = 1;; ++i) {
        lua_rawgeti(L, idx, i);
        if (lua_isnil(L, -1)) { lua_pop(L, 1); return true; }
        size_t n = 0;
        const char* s = luaL_tolstring(L, -1, &n);
        out.emplace_back(s, std::min<size_t>(n, 200));
        lua_pop(L, 2);
    }
}
// prompt.show(player, id, title, {lines}, {options}) -> shown
int pShow(lua_State* L, Vm& vm) {
    Prompts* p = promptsOf(vm);
    int64_t s;
    if (!p) return fail(vm, "the game has no prompts (rules \"prompts\": true)");
    if (!argInt(L, vm, 1, s, "player")) return -1;
    Prompt pr;
    pr.id = lua_type(L, 2) == LUA_TSTRING ? lua_tostring(L, 2) : "";
    pr.title = lua_type(L, 3) == LUA_TSTRING ? lua_tostring(L, 3) : "";
    if (!strings(L, 4, pr.lines) || !strings(L, 5, pr.options)) return fail(vm, "prompt.show: lines and options are lists of strings");
    lua_pushboolean(L, p->show(*vm.sim, uint8_t(std::clamp<int64_t>(s, 0, 255)), std::move(pr)));
    return 1;
}
int pClose(lua_State* L, Vm& vm) {
    Prompts* p = promptsOf(vm);
    int64_t s;
    if (!argInt(L, vm, 1, s, "player")) return -1;
    if (p) p->close(*vm.sim, uint8_t(std::clamp<int64_t>(s, 0, 255)));
    return 0;
}
// prompt.get(player) -> nil | {id, title, lines, options}
int pGet(lua_State* L, Vm& vm) {
    Prompts* p = promptsOf(vm);
    int64_t s;
    if (!argInt(L, vm, 1, s, "player")) return -1;
    const Prompt* pr = p ? p->of(*vm.sim, uint8_t(std::clamp<int64_t>(s, 0, 255))) : nullptr;
    if (!pr) { lua_pushnil(L); return 1; }
    lua_newtable(L);
    lua_pushstring(L, pr->id.c_str()); lua_setfield(L, -2, "id");
    lua_pushstring(L, pr->title.c_str()); lua_setfield(L, -2, "title");
    for (int k = 0; k < 2; ++k) {
        const auto& v = k ? pr->options : pr->lines;
        lua_newtable(L);
        for (size_t i = 0; i < v.size(); ++i) { lua_pushstring(L, v[i].c_str()); lua_rawseti(L, -2, lua_Integer(i + 1)); }
        lua_setfield(L, -2, k ? "options" : "lines");
    }
    return 1;
}

} // namespace

void openPromptApi(Vm& vm) {
    lua_State* L = vm.L;
    static const luaL_Reg queries[] = {{"get", bindQ<pGet>}, {nullptr, nullptr}};
    static const luaL_Reg actions[] = {{"show", bind<pShow>}, {"close", bind<pClose>}, {nullptr, nullptr}};
    luaL_newlib(L, queries);
    if (vm.mode == Mode::Sim) luaL_setfuncs(L, actions, 0);
    lua_setglobal(L, "prompt");
}

} // namespace script
} // namespace aiverse::game
