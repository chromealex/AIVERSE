// The `commerce` table of the scripting API (RtsCommerce.h, docs/commerce.md). Queries in simulation and view
// scripts, commerce.act in simulation scripts only. The UI sends game.command{type = "commerce", param = commerce.param(...)}.
#include "RtsScriptInternal.h"
#include "RtsInternal.h"
#include "RtsCommerce.h"

namespace aiverse::game::script {

namespace {

const char* const kOps[] = {"", "rent", "leave", "buy", "sell", "open", "close", "borrow", "repay", "hire", "fire", "manager", "purchase", "stash", "unstash", "stash_weapons", "take_weapons"};

Commerce* commerceOf(Vm& vm) {
    Commerce* c = vm.module ? vm.module->commerce() : nullptr;
    return c && c->active() && vm.sim && c->store(*vm.sim) ? c : nullptr;
}
bool need(Vm& vm, Commerce*& c) {
    c = commerceOf(vm);
    if (!c) { vm.err = "the game has no commerce (rules \"commerce\")"; return false; }
    return true;
}
// op name, premises (index from 1 or id; nil for the bank), value (business id / amount)
bool dealArgs(lua_State* L, Vm& vm, Commerce* c, int idx, CommerceOp& op, int& premise, int64_t& value) {
    const char* name = lua_type(L, idx) == LUA_TSTRING ? lua_tostring(L, idx) : "";
    op = CommerceOp::None;
    for (int i = 1; i < int(std::size(kOps)); ++i) if (std::string(name) == kOps[i]) op = CommerceOp(i);
    if (op == CommerceOp::None) { vm.err = "commerce: unknown deal (rent, leave, buy, sell, open, close, borrow, repay, hire, fire, manager, purchase)"; return false; }
    premise = 0; value = 0;
    const bool bank = op == CommerceOp::Borrow || op == CommerceOp::Repay;
    const bool amount = bank || op == CommerceOp::Stash || op == CommerceOp::Unstash;
    int vi = idx + 1;
    if (!bank) {
        if (lua_type(L, vi) == LUA_TSTRING) premise = c->findPremise(*vm.sim, lua_tostring(L, vi));
        else { int64_t n; if (!argInt(L, vm, vi, n, "premises")) return false; premise = int(n) - 1; }
        ++vi;
    }
    if (op == CommerceOp::Open) {
        if (lua_type(L, vi) != LUA_TSTRING) { vm.err = "commerce: business id (string) expected"; return false; }
        value = c->findBusiness(lua_tostring(L, vi));
    } else if (op == CommerceOp::Purchase) {
        if (lua_type(L, vi) != LUA_TSTRING) { vm.err = "commerce: offer id (string) expected"; return false; }
        value = c->findOffer(lua_tostring(L, vi));
    } else if (amount && !argInt(L, vm, vi, value, "amount")) return false;
    return true;
}

int cActive(lua_State* L, Vm& vm) { lua_pushboolean(L, commerceOf(vm) != nullptr); return 1; }
int cClock(lua_State* L, Vm& vm) {
    Commerce* c = commerceOf(vm);
    if (!c) { lua_pushnil(L); return 1; }
    const uint32_t d = c->day(*vm.sim), wd = uint32_t(c->rules().weekDays);
    lua_newtable(L);
    lua_pushinteger(L, d + 1); lua_setfield(L, -2, "day");
    lua_pushinteger(L, d / wd + 1); lua_setfield(L, -2, "week");
    lua_pushinteger(L, d % wd + 1); lua_setfield(L, -2, "weekday");
    pushFx(L, Fixed::fromInt(int64_t(vm.sim->tick() % c->rules().dayTicks)) / int64_t(c->rules().dayTicks)); lua_setfield(L, -2, "time");
    return 1;
}
int cCount(lua_State* L, Vm& vm) {
    Commerce* c = commerceOf(vm);
    lua_pushinteger(L, c ? lua_Integer(c->store(*vm.sim)->premises.size()) : 0);
    return 1;
}
int cPremise(lua_State* L, Vm& vm) {
    Commerce* c = commerceOf(vm);
    int i = -1;
    if (c && lua_type(L, 1) == LUA_TSTRING) i = c->findPremise(*vm.sim, lua_tostring(L, 1));
    else if (c) { int64_t n; if (!argInt(L, vm, 1, n, "premises")) return -1; i = int(n) - 1; }
    if (!c || i < 0 || i >= int(c->store(*vm.sim)->premises.size())) { lua_pushnil(L); return 1; }
    const Premise& p = c->store(*vm.sim)->premises[size_t(i)];
    const bool mine = lua_isinteger(L, 2) && p.runBy() != 0 && lua_tointeger(L, 2) == p.runBy();
    lua_newtable(L);
    lua_pushinteger(L, i + 1); lua_setfield(L, -2, "index");
    lua_pushstring(L, p.id.c_str()); lua_setfield(L, -2, "id");
    lua_pushstring(L, p.kind.c_str()); lua_setfield(L, -2, "kind");
    pushFx(L, p.at.x); lua_setfield(L, -2, "x");
    pushFx(L, p.at.z); lua_setfield(L, -2, "z");
    pushFx(L, p.price); lua_setfield(L, -2, "price");
    pushFx(L, p.rentWeek); lua_setfield(L, -2, "rent");
    pushFx(L, p.traffic); lua_setfield(L, -2, "traffic");
    lua_pushinteger(L, p.owner); lua_setfield(L, -2, "owner");
    lua_pushinteger(L, p.tenant); lua_setfield(L, -2, "tenant");
    if (p.business >= 0) {
        const BusinessType& b = c->rules().businesses[size_t(p.business)];
        lua_pushstring(L, b.id.c_str()); lua_setfield(L, -2, "business");
        lua_pushinteger(L, b.staff); lua_setfield(L, -2, "staff_need");
        pushFx(L, c->expectedToday(p)); lua_setfield(L, -2, "expected");
    }
    lua_pushinteger(L, p.staff); lua_setfield(L, -2, "staff");
    lua_pushboolean(L, p.manager); lua_setfield(L, -2, "manager");
    lua_pushboolean(L, p.visited); lua_setfield(L, -2, "visited");
    lua_pushboolean(L, p.isPublic); lua_setfield(L, -2, "public");
    pushFx(L, p.served); lua_setfield(L, -2, "served");
    if (mine) {   // premise(id, player): the stash, to the one who lives there only
        pushFx(L, p.stashCash); lua_setfield(L, -2, "stash_cash");
        lua_newtable(L);
        for (size_t k = 0; k < p.stashAmmo.size(); ++k) { lua_pushinteger(L, p.stashAmmo[k]); lua_rawseti(L, -2, lua_Integer(k + 1)); }
        lua_setfield(L, -2, "stash_ammo");
    }
    pushFx(L, p.lastNet); lua_setfield(L, -2, "last_net");
    pushFx(L, p.total); lua_setfield(L, -2, "total");
    return 1;
}
int cAccount(lua_State* L, Vm& vm) {
    Commerce* c = commerceOf(vm);
    int64_t s;
    if (!argInt(L, vm, 1, s, "player")) return -1;
    if (!c || s <= 0 || s >= int64_t(c->store(*vm.sim)->accounts.size())) { lua_pushnil(L); return 1; }
    const CommerceAccount& a = c->store(*vm.sim)->accounts[size_t(s)];
    lua_newtable(L);
    pushFx(L, c->cash(*vm.sim, uint8_t(s))); lua_setfield(L, -2, "cash");
    pushFx(L, a.debt); lua_setfield(L, -2, "debt");
    pushFx(L, c->creditLimit(*vm.sim, uint8_t(s))); lua_setfield(L, -2, "limit");
    lua_pushinteger(L, a.missed); lua_setfield(L, -2, "missed");
    pushFx(L, a.dayNet); lua_setfield(L, -2, "day_net");
    pushFx(L, a.weekNet); lua_setfield(L, -2, "week_net");
    pushFx(L, a.lastWeekNet); lua_setfield(L, -2, "last_week_net");
    pushFx(L, a.lastRent); lua_setfield(L, -2, "last_rent");
    pushFx(L, a.lastLoanPayment); lua_setfield(L, -2, "last_loan_payment");
    pushFx(L, a.lastWages); lua_setfield(L, -2, "last_wages");
    lua_pushinteger(L, lua_Integer(a.cars.size())); lua_setfield(L, -2, "cars");
    return 1;
}
int cBusinesses(lua_State* L, Vm& vm) {
    Commerce* c = commerceOf(vm);
    lua_newtable(L);
    if (!c) return 1;
    int n = 0;
    for (auto& b : c->rules().businesses) {
        lua_newtable(L);
        lua_pushstring(L, b.id.c_str()); lua_setfield(L, -2, "id");
        lua_pushstring(L, b.name.c_str()); lua_setfield(L, -2, "name");
        pushFx(L, b.setup); lua_setfield(L, -2, "setup");
        pushFx(L, b.incomeDay); lua_setfield(L, -2, "income");
        pushFx(L, b.costDay); lua_setfield(L, -2, "cost");
        lua_pushinteger(L, b.staff); lua_setfield(L, -2, "staff");
        pushFx(L, b.wageWeek); lua_setfield(L, -2, "wage");
        pushFx(L, b.ticket); lua_setfield(L, -2, "ticket");
        lua_newtable(L);
        for (size_t k = 0; k < b.kinds.size(); ++k) { lua_pushstring(L, b.kinds[k].c_str()); lua_rawseti(L, -2, lua_Integer(k + 1)); }
        lua_setfield(L, -2, "kinds");
        lua_rawseti(L, -2, ++n);
    }
    return 1;
}
int cOffers(lua_State* L, Vm& vm) {
    Commerce* c = commerceOf(vm);
    lua_newtable(L);
    if (!c) return 1;
    int n = 0;
    for (auto& o : c->rules().offers) {
        lua_newtable(L);
        lua_pushstring(L, o.id.c_str()); lua_setfield(L, -2, "id");
        lua_pushstring(L, o.name.c_str()); lua_setfield(L, -2, "name");
        pushFx(L, o.price); lua_setfield(L, -2, "price");
        if (!o.weapon.empty()) { lua_pushstring(L, o.weapon.c_str()); lua_setfield(L, -2, "weapon"); lua_pushinteger(L, o.ammo); lua_setfield(L, -2, "ammo"); }
        if (o.vehicle >= 0) { lua_pushstring(L, vm.module->rules().types[size_t(o.vehicle)].id.c_str()); lua_setfield(L, -2, "vehicle"); }
        lua_newtable(L);
        for (size_t k = 0; k < o.kinds.size(); ++k) { lua_pushstring(L, o.kinds[k].c_str()); lua_rawseti(L, -2, lua_Integer(k + 1)); }
        lua_setfield(L, -2, "kinds");
        lua_rawseti(L, -2, ++n);
    }
    return 1;
}
// commerce.visit(premises) -> a walk-in customer paid (simulation scripts)
int cVisit(lua_State* L, Vm& vm) {
    Commerce* c; int64_t n = 0;
    if (!need(vm, c)) return -1;
    int i;
    if (lua_type(L, 1) == LUA_TSTRING) i = c->findPremise(*vm.sim, lua_tostring(L, 1));
    else { if (!argInt(L, vm, 1, n, "premises")) return -1; i = int(n) - 1; }
    lua_pushboolean(L, c->visit(*vm.sim, i));
    return 1;
}
// commerce.can(player, deal, ...) -> true | false, reason
int cCan(lua_State* L, Vm& vm) {
    Commerce* c; int64_t s; CommerceOp op; int premise; int64_t value;
    if (!need(vm, c) || !argInt(L, vm, 1, s, "player") || !dealArgs(L, vm, c, 2, op, premise, value)) return -1;
    const char* why = c->refusal(*vm.sim, uint8_t(std::clamp<int64_t>(s, 0, 255)), op, premise, value);
    lua_pushboolean(L, !why);
    if (!why) return 1;
    lua_pushstring(L, why);
    return 2;
}
// commerce.param(deal, ...) -> the param of game.command{type = "commerce"}
int cParam(lua_State* L, Vm& vm) {
    Commerce* c; CommerceOp op; int premise; int64_t value;
    if (!need(vm, c) || !dealArgs(L, vm, c, 1, op, premise, value)) return -1;
    lua_pushinteger(L, premise < 0 || value < 0 ? -1 : commercePack(op, premise, value));
    return 1;
}
int cAct(lua_State* L, Vm& vm) {
    Commerce* c; int64_t s; CommerceOp op; int premise; int64_t value;
    if (!need(vm, c) || !argInt(L, vm, 1, s, "player") || !dealArgs(L, vm, c, 2, op, premise, value)) return -1;
    lua_pushboolean(L, c->act(*vm.sim, uint8_t(std::clamp<int64_t>(s, 0, 255)), op, premise, value));
    return 1;
}

} // namespace

void openCommerceApi(Vm& vm) {
    lua_State* L = vm.L;
    static const luaL_Reg queries[] = {
        {"active", bindQ<cActive>}, {"clock", bindQ<cClock>}, {"count", bindQ<cCount>}, {"premise", bindQ<cPremise>},
        {"account", bindQ<cAccount>}, {"businesses", bindQ<cBusinesses>}, {"can", bindQ<cCan>}, {"param", bindQ<cParam>}, {"offers", bindQ<cOffers>}, {nullptr, nullptr}};
    static const luaL_Reg actions[] = {{"act", bind<cAct>}, {"visit", bind<cVisit>}, {nullptr, nullptr}};
    luaL_newlib(L, queries);
    if (vm.mode == Mode::Sim) luaL_setfuncs(L, actions, 0);
    lua_setglobal(L, "commerce");
}

} // namespace aiverse::game::script
