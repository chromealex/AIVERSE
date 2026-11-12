// The `progression` table of the scripting API (in-match progression, RtsProgression.h). Queries in simulation and
// view scripts (HUD: xp bar, level, the level-up cards), actions in simulation scripts only. Events:
// on("progression_xp" | "progression_level" | "progression_offer" | "progression_perk" | "progression_recruit" |
// "progression_orb" | "progression_summon", fn); offer veto hook: progression_can_offer(player, perk_key | "recruit:<type>").
// View scripts choose cards with view.progression_choose(index) / view.progression_reroll() (RtsViewScript.cpp).
// Reference: docs/progression.md (keep scriptApiReference() in RtsScriptApi.cpp in sync).
#include "RtsScriptInternal.h"
#include "RtsInternal.h"
#include "libraries/include/aiverselib/genre/PerksData.h"

namespace aiverse::game::script {

namespace {
namespace lg = aiverse::lib::genre;

Progression* progOf(Vm& vm) {
    Progression* p = vm.module ? vm.module->progression() : nullptr;
    return p && p->active() && vm.sim && p->store(*vm.sim) ? p : nullptr;
}
bool needProg(Vm& vm, Progression*& p) {
    p = progOf(vm);
    if (!p) { vm.err = "the game has no progression (rules \"progression\")"; return false; }
    return true;
}
bool playerArg(lua_State* L, Vm& vm, int idx, Progression* pg, uint8_t& slot, const lg::PlayerProgress*& p) {
    int64_t s;
    if (!argInt(L, vm, idx, s, "player")) return false;
    p = s > 0 && s < 256 ? pg->progress(*vm.sim, uint8_t(s)) : nullptr;
    if (!p) { vm.err = "player " + std::to_string(s) + " has no progression"; return false; }
    slot = uint8_t(s);
    return true;
}
std::string perkKey(const ProgressionRules& R, int64_t id) {
    auto d = R.defs.perks.find(id);
    return d != R.defs.perks.end() && !d->second.key.empty() ? d->second.key : std::to_string(id);
}
// perk by key (string) or id (integer)
bool perkArg(lua_State* L, Vm& vm, int idx, const ProgressionRules& R, int64_t& id) {
    if (lua_type(L, idx) == LUA_TSTRING) {
        const std::string k = lua_tostring(L, idx);
        for (auto& [pid, d] : R.defs.perks) if (d.key == k) { id = pid; return true; }
        vm.err = "unknown perk '" + k + "'";
        return false;
    }
    if (!argInt(L, vm, idx, id, "perk")) return false;
    if (!R.defs.perks.contains(id)) { vm.err = "unknown perk " + std::to_string(id); return false; }
    return true;
}
const char* typeName(Vm& vm, const ProgressionRules& R, int64_t unit) {
    auto it = R.unitType.find(unit);
    return it == R.unitType.end() ? "" : vm.module->rules().types[it->second].id.c_str();
}

int pActive(lua_State* L, Vm& vm) { lua_pushboolean(L, progOf(vm) != nullptr); return 1; }
int pPlayers(lua_State* L, Vm& vm) {
    Progression* pg = progOf(vm);
    lua_newtable(L);
    if (!pg) return 1;
    int n = 0;
    for (auto& [id, p] : pg->store(*vm.sim)->perks.players) { (void)p; lua_pushinteger(L, id); lua_rawseti(L, -2, ++n); }
    return 1;
}
// progression.player(slot): {level, xp, xp_to_next (nil at the cap), progress (0..1), levels (banked), kills,
//   hero, perks = {{id, key, slot, initial}}, recruits = {[slot] = n}, offer_open}
int pPlayer(lua_State* L, Vm& vm) {
    Progression* pg; uint8_t slot; const lg::PlayerProgress* p;
    if (!needProg(vm, pg) || !playerArg(L, vm, 1, pg, slot, p)) return -1;
    const ProgressionRules& R = pg->rules();
    const ProgressionStore& st = *pg->store(*vm.sim);
    lua_createtable(L, 0, 10);
    lua_pushinteger(L, p->level); lua_setfield(L, -2, "level");
    lua_pushinteger(L, p->xp); lua_setfield(L, -2, "xp");
    if (auto need = lg::xpToNext(R.defs, R.perk, p->level)) { lua_pushinteger(L, *need); lua_setfield(L, -2, "xp_to_next"); }
    pushFx(L, Fixed::fromRatio(lg::levelProgressMilli(R.defs, R.perk, *p), 1000)); lua_setfield(L, -2, "progress");
    lua_pushinteger(L, lg::availableLevels(R.defs, R.perk, *p)); lua_setfield(L, -2, "levels");
    lua_pushinteger(L, slot < st.kills.size() ? st.kills[slot] : 0); lua_setfield(L, -2, "kills");
    lua_pushboolean(L, p->offer.open); lua_setfield(L, -2, "offer_open");
    if (Entity h = pg->heroOf(*vm.sim, slot); h.valid()) { pushEntity(L, h); lua_setfield(L, -2, "hero"); }
    lua_newtable(L);
    int n = 0;
    for (auto& o : p->mods) {
        lua_createtable(L, 0, 4);
        lua_pushinteger(L, o.perk); lua_setfield(L, -2, "id");
        lua_pushstring(L, perkKey(R, o.perk).c_str()); lua_setfield(L, -2, "key");
        lua_pushinteger(L, o.slot); lua_setfield(L, -2, "slot");
        lua_pushboolean(L, o.initial); lua_setfield(L, -2, "initial");
        lua_rawseti(L, -2, ++n);
    }
    lua_setfield(L, -2, "perks");
    lua_newtable(L);
    for (auto [s, c] : p->recruits) { lua_pushinteger(L, c); lua_rawseti(L, -2, s); }
    lua_setfield(L, -2, "recruits");
    return 1;
}
// progression.offer(slot): nil, or {revision, level, rerolls, cards = {{kind = "perk" | "recruit", perk, id, slot,
//   unit (type id), hook, scope}}} - what the level-up card UI shows
int pOffer(lua_State* L, Vm& vm) {
    Progression* pg; uint8_t slot; const lg::PlayerProgress* p;
    if (!needProg(vm, pg) || !playerArg(L, vm, 1, pg, slot, p)) return -1;
    if (!p->offer.open) { lua_pushnil(L); return 1; }
    const ProgressionRules& R = pg->rules();
    lua_createtable(L, 0, 4);
    lua_pushinteger(L, p->offer.revision); lua_setfield(L, -2, "revision");
    lua_pushinteger(L, p->offer.level); lua_setfield(L, -2, "level");
    lua_pushinteger(L, p->offer.rerollsLeft); lua_setfield(L, -2, "rerolls");
    lua_newtable(L);
    int n = 0;
    for (auto& it : p->offer.items) {
        lua_createtable(L, 0, 7);
        const bool recruit = it.type == lg::OfferVariant::Recruit;
        lua_pushstring(L, recruit ? "recruit" : "perk"); lua_setfield(L, -2, "kind");
        // Drone Fall card letter: R recruit, H hero perk (slot 0), P robot perk (other slots), G player-wide perk
        lua_pushstring(L, recruit ? "R" : it.slot < 0 ? "G" : it.slot == 0 ? "H" : "P"); lua_setfield(L, -2, "letter");
        lua_pushinteger(L, it.slot); lua_setfield(L, -2, "slot");
        if (const auto* ds = lg::deckSlot(*p, it.slot)) { lua_pushstring(L, typeName(vm, R, ds->unit)); lua_setfield(L, -2, "unit"); }
        if (!recruit) {
            const auto& d = R.defs.perks.at(it.perk);
            lua_pushinteger(L, it.perk); lua_setfield(L, -2, "id");
            lua_pushstring(L, perkKey(R, it.perk).c_str()); lua_setfield(L, -2, "perk");
            lua_pushstring(L, d.hook.c_str()); lua_setfield(L, -2, "hook");
            lua_pushstring(L, d.scope == lg::PerkScope::Slot ? "slot" : "player"); lua_setfield(L, -2, "scope");
        }
        lua_rawseti(L, -2, ++n);
    }
    lua_setfield(L, -2, "cards");
    return 1;
}
// progression.orbs(): {{id, x, z, value, owner, ready}}
int pOrbs(lua_State* L, Vm& vm) {
    Progression* pg = progOf(vm);
    lua_newtable(L);
    if (!pg) return 1;
    int n = 0;
    const uint32_t now = vm.sim->tick() + 1;
    for (auto& o : pg->store(*vm.sim)->pickups.orbs) {
        lua_createtable(L, 0, 6);
        lua_pushinteger(L, o.id); lua_setfield(L, -2, "id");
        pushFx(L, o.position.x); lua_setfield(L, -2, "x");
        pushFx(L, o.position.z); lua_setfield(L, -2, "z");
        lua_pushinteger(L, o.value); lua_setfield(L, -2, "value");
        lua_pushinteger(L, o.owner); lua_setfield(L, -2, "owner");
        lua_pushboolean(L, now >= o.readyAt); lua_setfield(L, -2, "ready");
        lua_rawseti(L, -2, ++n);
    }
    return 1;
}
// progression.points([slot]): summon circles {{id, x, z, radius, state (ready | progress | used | cooldown | squad_full | blocked | pending), progress}}
int pPoints(lua_State* L, Vm& vm) {
    Progression* pg = progOf(vm);
    lua_newtable(L);
    if (!pg) return 1;
    int64_t slot = 0;
    if (!lua_isnoneornil(L, 1) && !argInt(L, vm, 1, slot, "player")) return -1;
    static const char* const kView[] = {"ready", "progress", "used", "cooldown", "squad_full", "blocked", "pending"};
    const auto& st = *pg->store(*vm.sim);
    int n = 0;
    for (auto& d : pg->summonPoints()) {
        lua_createtable(L, 0, 6);
        lua_pushinteger(L, d.id); lua_setfield(L, -2, "id");
        pushFx(L, d.position.x); lua_setfield(L, -2, "x");
        pushFx(L, d.position.z); lua_setfield(L, -2, "z");
        pushFx(L, d.radius); lua_setfield(L, -2, "radius");
        int64_t milli = 0;
        auto v = lg::summonPointView(pg->summonConfig(), d, st.summons, slot, vm.sim->tick() + 1, &milli);
        lua_pushstring(L, kView[std::min<int>(6, int(v))]); lua_setfield(L, -2, "state");
        pushFx(L, Fixed::fromRatio(milli, 1000)); lua_setfield(L, -2, "progress");
        lua_rawseti(L, -2, ++n);
    }
    return 1;
}
// progression.stat(unit, stat [, base]): the perk pipeline of a unit (library stat names or ids; milli values)
int pStat(lua_State* L, Vm& vm) {
    Progression* pg;
    if (!needProg(vm, pg)) return -1;
    Entity e;
    if (!argEntity(L, 1, e) || !vm.sim->world().alive(e)) return fail(vm, "progression.stat: unit expected");
    int64_t stat = 0, base = 0;
    if (lua_type(L, 2) == LUA_TSTRING) {
        Json j = Json::string(lua_tostring(L, 2));
        try { stat = lg::detail::statId(j, "stat"); } catch (const std::exception& ex) { return fail(vm, ex.what()); }
    } else if (!argInt(L, vm, 2, stat, "stat")) return -1;
    if (!lua_isnoneornil(L, 3) && !argInt(L, vm, 3, base, "base")) return -1;
    lua_pushinteger(L, pg->statOf(*vm.sim, e, stat, base));
    return 1;
}
// progression.unit(unit): {player, slot, bearer = "hero" | "ally"} or nil
int pUnit(lua_State* L, Vm& vm) {
    Progression* pg = progOf(vm);
    Entity e;
    const ProgUnit* pu = pg && argEntity(L, 1, e) ? pg->store(*vm.sim)->find(e) : nullptr;
    if (!pu) { lua_pushnil(L); return 1; }
    lua_createtable(L, 0, 3);
    lua_pushinteger(L, pu->player); lua_setfield(L, -2, "player");
    lua_pushinteger(L, pu->slot); lua_setfield(L, -2, "slot");
    lua_pushstring(L, pu->bearer == kProgBearerHero ? "hero" : "ally"); lua_setfield(L, -2, "bearer");
    return 1;
}

// ---- actions (simulation scripts)
int pGrantXp(lua_State* L, Vm& vm) {
    Progression* pg; uint8_t slot; const lg::PlayerProgress* p; int64_t n;
    if (!needProg(vm, pg) || !playerArg(L, vm, 1, pg, slot, p) || !argInt(L, vm, 2, n, "xp")) return -1;
    lua_pushboolean(L, pg->grantXp(*vm.sim, slot, n, lua_toboolean(L, 3)));
    return 1;
}
int pGrantPerk(lua_State* L, Vm& vm) {
    Progression* pg; uint8_t slot; const lg::PlayerProgress* p; int64_t perk, s = -1;
    if (!needProg(vm, pg) || !playerArg(L, vm, 1, pg, slot, p) || !perkArg(L, vm, 2, pg->rules(), perk)) return -1;
    if (!lua_isnoneornil(L, 3) && !argInt(L, vm, 3, s, "slot")) return -1;
    lua_pushboolean(L, pg->grantPerk(*vm.sim, slot, perk, int(s)));
    return 1;
}
int pClearPerks(lua_State* L, Vm& vm) {
    Progression* pg; uint8_t slot; const lg::PlayerProgress* p; int64_t s = -2;
    if (!needProg(vm, pg) || !playerArg(L, vm, 1, pg, slot, p)) return -1;
    if (!lua_isnoneornil(L, 2) && !argInt(L, vm, 2, s, "slot")) return -1;
    lua_pushinteger(L, lg::clearPerks(pg->store(*vm.sim)->perks, slot, int32_t(s), !lua_isboolean(L, 3) || lua_toboolean(L, 3)));
    return 1;
}
int pChoose(lua_State* L, Vm& vm) {
    Progression* pg; uint8_t slot; const lg::PlayerProgress* p; int64_t index, rev;
    if (!needProg(vm, pg) || !playerArg(L, vm, 1, pg, slot, p) || !argInt(L, vm, 2, index, "index")) return -1;
    rev = p->offer.revision;
    if (!lua_isnoneornil(L, 3) && !argInt(L, vm, 3, rev, "revision")) return -1;
    lua_pushboolean(L, index >= 1 && pg->choose(*vm.sim, slot, uint32_t(rev), int(index - 1)));   // Lua cards are 1-based
    return 1;
}
int pReroll(lua_State* L, Vm& vm) {
    Progression* pg; uint8_t slot; const lg::PlayerProgress* p;
    if (!needProg(vm, pg) || !playerArg(L, vm, 1, pg, slot, p)) return -1;
    lua_pushboolean(L, pg->reroll(*vm.sim, slot, p->offer.revision));
    return 1;
}
int pOpen(lua_State* L, Vm& vm) {
    Progression* pg; uint8_t slot; const lg::PlayerProgress* p;
    if (!needProg(vm, pg) || !playerArg(L, vm, 1, pg, slot, p)) return -1;
    lua_pushboolean(L, pg->open(*vm.sim, slot));
    return 1;
}
int pClose(lua_State* L, Vm& vm) {
    Progression* pg; uint8_t slot; const lg::PlayerProgress* p;
    if (!needProg(vm, pg) || !playerArg(L, vm, 1, pg, slot, p)) return -1;
    lua_pushboolean(L, pg->close(*vm.sim, slot));
    return 1;
}
int pMagnet(lua_State* L, Vm& vm) {
    Progression* pg; uint8_t slot; const lg::PlayerProgress* p;
    if (!needProg(vm, pg) || !playerArg(L, vm, 1, pg, slot, p)) return -1;
    auto& m = pg->store(*vm.sim)->magnet;
    if (slot < m.size()) m[slot] = 1;
    return 0;
}
int pSpawnOrb(lua_State* L, Vm& vm) {
    Progression* pg; Fixed x, z; int64_t value, owner = 0;
    if (!needProg(vm, pg) || !argFx(L, vm, 1, x, "x") || !argFx(L, vm, 2, z, "z") || !argInt(L, vm, 3, value, "xp")) return -1;
    if (!lua_isnoneornil(L, 4) && !argInt(L, vm, 4, owner, "owner")) return -1;
    if (value < 1 || value > lg::kPickupMaxValue || owner < 0 || owner > 255) return fail(vm, "progression.spawn_orb: xp 1.., owner 0..255");
    auto& q = pg->store(*vm.sim)->orbQueue;
    if (q.size() < lg::kPickupMaxSpawns) q.push_back({owner, value, {x, Fixed{}, z}});
    return 0;
}
} // namespace

void openProgressionApi(Vm& vm) {
    lua_State* L = vm.L;
    static const luaL_Reg queries[] = {
        {"active", bindQ<pActive>}, {"players", bindQ<pPlayers>}, {"player", bindQ<pPlayer>}, {"offer", bindQ<pOffer>},
        {"orbs", bindQ<pOrbs>}, {"points", bindQ<pPoints>}, {"stat", bindQ<pStat>}, {"unit", bindQ<pUnit>}, {nullptr, nullptr}};
    static const luaL_Reg actions[] = {
        {"grant_xp", bind<pGrantXp>}, {"grant_perk", bind<pGrantPerk>}, {"clear_perks", bind<pClearPerks>}, {"choose", bind<pChoose>},
        {"reroll", bind<pReroll>}, {"open", bind<pOpen>}, {"close", bind<pClose>}, {"magnet", bind<pMagnet>},
        {"spawn_orb", bind<pSpawnOrb>}, {nullptr, nullptr}};
    luaL_newlib(L, queries);
    if (vm.mode == Mode::Sim) luaL_setfuncs(L, actions, 0);
    lua_setglobal(L, "progression");
}

} // namespace aiverse::game::script
