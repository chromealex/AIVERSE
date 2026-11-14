// Commerce: premises, businesses, the bank (see RtsCommerce.h, docs/commerce.md).
#include "RtsCommerce.h"
#include <algorithm>
#include <stdexcept>
#include "RtsInternal.h"
#include "RtsModule.h"
#include "RtsTopDown.h"
#include "RtsArsenal.h"

namespace aiverse::game {

using namespace detail;

namespace {
Fixed number(const Json& j, const char* key, Fixed def, const std::string& where, bool positive) {
    Fixed v = j.getFx(key, def);
    if (v.raw < 0 || (positive && v.raw == 0))
        throw std::runtime_error(where + "." + key + (positive ? ": must be positive" : ": must not be negative"));
    return v;
}
bool fits(const BusinessType& b, const std::string& kind) {
    return b.kinds.empty() || std::find(b.kinds.begin(), b.kinds.end(), kind) != b.kinds.end();
}
} // namespace

// ================================================================= data

CommerceRules Commerce::parse(const Json& j, const std::vector<std::string>& res, uint32_t tickRate, const std::vector<std::string>& types) {
    CommerceRules r;
    if (!j.isObject()) return r;
    r.enabled = true;
    const std::string cur = j.getStr("currency");
    r.currency = int(std::find(res.begin(), res.end(), cur) - res.begin());
    if (cur.empty() || r.currency >= int(res.size())) throw std::runtime_error("commerce.currency: name a resource of the rules (got '" + cur + "')");
    r.dayMinutes = number(j, "dayMinutes", r.dayMinutes, "commerce", true);
    const int64_t ticks = (r.dayMinutes * int64_t(60) * int64_t(tickRate) + Fixed::fromRatio(1, 2)).toInt();
    if (ticks < 1 || ticks > 100000000) throw std::runtime_error("commerce.dayMinutes: out of range");
    r.dayTicks = uint32_t(ticks);
    r.weekDays = int(j.getInt("weekDays", r.weekDays));
    if (r.weekDays < 1 || r.weekDays > 365) throw std::runtime_error("commerce.weekDays: 1..365");
    r.range = number(j, "range", r.range, "commerce", false);
    r.sellShare = number(j, "sellShare", r.sellShare, "commerce", false);
    if (r.sellShare > Fixed::fromInt(1)) throw std::runtime_error("commerce.sellShare: at most 1");
    if (const Json& b = j["bank"]; b.isObject()) {
        r.rateWeek = number(b, "rateWeek", r.rateWeek, "commerce.bank", false);
        r.unsecured = number(b, "unsecured", r.unsecured, "commerce.bank", false);
        r.collateralShare = number(b, "collateralShare", r.collateralShare, "commerce.bank", false);
        if (r.collateralShare > Fixed::fromInt(1)) throw std::runtime_error("commerce.bank.collateralShare: at most 1");
        r.termWeeks = int(b.getInt("termWeeks", r.termWeeks));
        r.graceWeeks = int(b.getInt("graceWeeks", r.graceWeeks));
        if (r.termWeeks < 1 || r.termWeeks > 10000) throw std::runtime_error("commerce.bank.termWeeks: 1..10000");
        if (r.graceWeeks < 0 || r.graceWeeks > 200) throw std::runtime_error("commerce.bank.graceWeeks: 0..200");
    } else if (!j["bank"].isNull()) throw std::runtime_error("commerce.bank: expected an object");
    if (const Json& bs = j["businesses"]; bs.isObject()) {
        for (auto& [id, bj] : bs.members()) {
            BusinessType b;
            b.id = id;
            const std::string where = "commerce.businesses." + id;
            if (!bj.isObject()) throw std::runtime_error(where + ": expected an object");
            b.name = bj.getStr("name", id);
            for (auto& k : bj["kinds"].items()) b.kinds.push_back(k.str());
            b.setup = number(bj, "setup", {}, where, false);
            b.incomeDay = number(bj, "incomeDay", {}, where, false);
            b.costDay = number(bj, "costDay", {}, where, false);
            b.staff = int(bj.getInt("staff", 0));
            if (b.staff < 0 || b.staff > 200) throw std::runtime_error(where + ".staff: 0..200");
            b.wageWeek = number(bj, "wageWeek", {}, where, false);
            b.ticket = number(bj, "ticket", b.incomeDay / 20, where, false);
            r.businesses.push_back(std::move(b));
        }
        std::sort(r.businesses.begin(), r.businesses.end(), [](const BusinessType& a, const BusinessType& b) { return a.id < b.id; });
        if (r.businesses.size() > 4096) throw std::runtime_error("commerce.businesses: too many");
    } else if (!j["businesses"].isNull()) throw std::runtime_error("commerce.businesses: expected an object");
    r.managerWageWeek = number(j, "managerWageWeek", r.managerWageWeek, "commerce", false);
    r.unmanagedShare = number(j, "unmanagedShare", r.unmanagedShare, "commerce", false);
    if (r.unmanagedShare > Fixed::fromInt(1)) throw std::runtime_error("commerce.unmanagedShare: at most 1");
    if (j.has("accounts")) {
        const std::string a = j.getStr("accounts");
        if (a != "slots" && a != "external") throw std::runtime_error("commerce.accounts: 'slots' or 'external'");
        r.externalAccounts = a == "external";
    }
    r.absentDays = int(j.getInt("absentDays", 0));
    if (r.absentDays < 0 || r.absentDays > 1000000) throw std::runtime_error("commerce.absentDays: out of range");
    if (j.has("storage")) { r.storage.clear(); for (auto& k : j["storage"].items()) r.storage.push_back(k.str()); }
    if (const Json& os = j["offers"]; os.isObject()) {
        for (auto& [id, oj] : os.members()) {
            CommerceOffer o;
            o.id = id;
            const std::string where = "commerce.offers." + id;
            if (!oj.isObject()) throw std::runtime_error(where + ": expected an object");
            o.name = oj.getStr("name", id);
            for (auto& k : oj["kinds"].items()) o.kinds.push_back(k.str());
            o.price = number(oj, "price", {}, where, false);
            o.weapon = oj.getStr("weapon");
            o.ammo = int(oj.getInt("ammo", 0));
            if (oj.has("vehicle")) {
                const std::string v = oj.getStr("vehicle");
                o.vehicle = int(std::find(types.begin(), types.end(), v) - types.begin());
                if (o.vehicle >= int(types.size())) throw std::runtime_error(where + ".vehicle: unknown type '" + v + "'");
            }
            if (o.weapon.empty() == (o.vehicle < 0)) throw std::runtime_error(where + ": give either \"weapon\" or \"vehicle\"");
            if (!o.weapon.empty() && (o.ammo < 0 || o.ammo > 100000)) throw std::runtime_error(where + ".ammo: out of range");
            r.offers.push_back(std::move(o));
        }
        std::sort(r.offers.begin(), r.offers.end(), [](const CommerceOffer& a, const CommerceOffer& b) { return a.id < b.id; });
    } else if (!j["offers"].isNull()) throw std::runtime_error("commerce.offers: expected an object");
    return r;
}

CommerceStore* Commerce::store(Simulation& sim) const { return active() ? sim.world().resource<CommerceStore>() : nullptr; }

void Commerce::setup(Simulation& sim, RtsModule& m, const Json& rulesJson, const Json& map) {
    m_ = &m;
    std::vector<std::string> res;
    for (auto& r : m.rules().resources) res.push_back(r.id);
    std::vector<std::string> types;
    for (auto& t : m.rules().types) types.push_back(t.id);
    rules_ = parse(rulesJson["commerce"], res, sim.tickRate(), types);
    if (!active()) return;
    auto& st = sim.world().setResource<CommerceStore>();
    const size_t slots = m.state(sim).players.size();
    st.slotRec.assign(slots, 0);
    st.accounts.assign(rules_.externalAccounts ? 1 : slots, {});
    for (size_t i = 1; i < st.accounts.size(); ++i) { st.accounts[i].id = i; st.accounts[i].slot = uint8_t(i); st.slotRec[i] = uint16_t(i); }
    const Json& list = map["premises"];
    if (list.isNull()) return;
    if (!list.isArray()) throw std::runtime_error("map premises: expected a list");
    if (list.size() > 60000) throw std::runtime_error("map premises: too many");
    for (auto& pj : list.items()) {
        Premise p;
        p.id = pj.getStr("id");
        if (p.id.empty()) throw std::runtime_error("map premises: every entry needs an id");
        const std::string where = "map premises '" + p.id + "'";
        for (auto& o : st.premises) if (o.id == p.id) throw std::runtime_error(where + ": the id is used twice");
        p.kind = pj.getStr("kind", "shop");
        const Json& at = pj["at"];
        if (!at.isArray() || at.size() != 2) throw std::runtime_error(where + ".at: expected [x, z]");
        p.at = {at[size_t(0)].fx(), Fixed{}, at[size_t(1)].fx()};
        p.price = number(pj, "price", {}, where, true);
        p.rentWeek = number(pj, "rentWeek", {}, where, false);
        p.traffic = number(pj, "traffic", p.traffic, where, false);
        const int64_t owner = pj.getInt("owner", 0);
        if (owner < 0 || owner >= int64_t(st.accounts.size())) throw std::runtime_error(where + ".owner: no such player");
        p.owner = uint16_t(owner);
        p.isPublic = pj.getBool("public", false);
        if (pj.has("business")) {
            p.business = int16_t(findBusiness(pj.getStr("business")));
            if (p.business < 0) throw std::runtime_error(where + ".business: unknown '" + pj.getStr("business") + "'");
            if (!fits(rules_.businesses[size_t(p.business)], p.kind)) throw std::runtime_error(where + ".business: does not fit a '" + p.kind + "'");
        }
        st.premises.push_back(std::move(p));
    }
}

int Commerce::findBusiness(const std::string& id) const {
    for (size_t i = 0; i < rules_.businesses.size(); ++i) if (rules_.businesses[i].id == id) return int(i);
    return -1;
}

int Commerce::findOffer(const std::string& id) const {
    for (size_t i = 0; i < rules_.offers.size(); ++i) if (rules_.offers[i].id == id) return int(i);
    return -1;
}

int Commerce::findPremise(Simulation& sim, const std::string& id) const {
    if (auto* st = store(sim))
        for (size_t i = 0; i < st->premises.size(); ++i) if (st->premises[i].id == id) return int(i);
    return -1;
}

uint32_t Commerce::day(const Simulation& sim) const { return rules_.dayTicks ? sim.tick() / rules_.dayTicks : 0; }

Fixed Commerce::cash(Simulation& sim, uint8_t player) const {
    auto& gs = m_->state(sim);
    return active() && player < gs.players.size() ? gs.players[player].stock[size_t(rules_.currency)] : Fixed{};
}

Fixed Commerce::cashOf(Simulation& sim, uint16_t rec) const {
    auto* st = store(sim);
    if (!st || rec == 0 || rec >= st->accounts.size()) return {};
    const CommerceAccount& a = st->accounts[rec];
    return a.slot ? cash(sim, a.slot) : a.wallet;
}

Fixed Commerce::limitOf(const CommerceStore& st, uint16_t rec) const {
    if (rec == 0 || rec >= st.accounts.size()) return {};
    Fixed owned{};
    for (auto& p : st.premises) if (p.owner == rec) owned += p.price;
    return rules_.unsecured + owned * rules_.collateralShare;
}

Fixed Commerce::creditLimit(Simulation& sim, uint8_t player) const {
    auto* st = store(sim);
    return st ? limitOf(*st, recOf(*st, player)) : Fixed{};
}

const CommerceAccount* Commerce::account(Simulation& sim, uint8_t player) const {
    auto* st = store(sim);
    const uint16_t rec = st ? recOf(*st, player) : uint16_t(0);
    return rec ? &st->accounts[rec] : nullptr;
}

const CommerceAccount* Commerce::accountById(Simulation& sim, uint64_t id) const {
    auto* st = store(sim);
    if (st && id)
        for (size_t i = 1; i < st->accounts.size(); ++i) if (st->accounts[i].id == id) return &st->accounts[i];
    return nullptr;
}

uint8_t Commerce::slotOf(Simulation& sim, uint16_t record) const {
    auto* st = store(sim);
    return st && record < st->accounts.size() ? st->accounts[record].slot : uint8_t(0);
}

bool Commerce::bind(Simulation& sim, uint8_t slot, uint64_t id) {
    auto* st = store(sim);
    if (!st || slot == 0 || slot >= st->slotRec.size() || id == 0) return false;
    uint16_t rec = 0;
    for (size_t i = 1; i < st->accounts.size(); ++i) if (st->accounts[i].id == id) rec = uint16_t(i);
    if (rec && st->accounts[rec].slot == slot) return true;
    if (rec && st->accounts[rec].slot != 0) return false;   // he sits in another slot
    if (!rec && st->accounts.size() >= 65000) return false;
    unbind(sim, slot);
    if (!rec) { st->accounts.emplace_back(); rec = uint16_t(st->accounts.size() - 1); st->accounts[rec].id = id; }
    CommerceAccount& a = st->accounts[rec];
    a.slot = slot;
    a.lastSeenDay = day(sim);
    st->slotRec[slot] = rec;
    m_->state(sim).players[slot].stock[size_t(rules_.currency)] = a.wallet;
    a.wallet = {};
    return true;
}

void Commerce::unbind(Simulation& sim, uint8_t slot) {
    auto* st = store(sim);
    const uint16_t rec = st ? recOf(*st, slot) : uint16_t(0);
    if (!rec) return;
    CommerceAccount& a = st->accounts[rec];
    Fixed& stock = m_->state(sim).players[slot].stock[size_t(rules_.currency)];
    a.wallet = stock;
    stock = {};
    a.slot = 0;
    a.lastSeenDay = day(sim);
    st->slotRec[slot] = 0;
}

Json Commerce::exportAccount(Simulation& sim, uint64_t id) const {
    auto* st = store(sim);
    Json out = Json::object();
    if (!st || id == 0) return out;
    uint16_t rec = 0;
    for (size_t i = 1; i < st->accounts.size(); ++i) if (st->accounts[i].id == id) rec = uint16_t(i);
    if (!rec) return out;
    const CommerceAccount& a = st->accounts[rec];
    auto raw = [](Fixed f) { return Json::number(int64_t(f.raw)); };
    out.set("wallet", raw(a.slot ? cash(sim, a.slot) : a.wallet));
    out.set("debt", raw(a.debt));
    out.set("missed", Json::number(int64_t(a.missed)));
    out.set("day", Json::number(int64_t(int64_t(day(sim)))));
    Json list = Json::array();
    for (const auto& p : st->premises) {
        if (p.owner != rec && p.tenant != rec) continue;
        Json e = Json::object();
        e.set("id", Json::string(p.id));
        e.set("owns", Json::fromBool(p.owner == rec));
        e.set("rents", Json::fromBool(p.tenant == rec));
        if (p.runBy() == rec) {
            if (p.business >= 0) e.set("business", Json::string(rules_.businesses[size_t(p.business)].id));
            e.set("staff", Json::number(int64_t(p.staff)));
            e.set("manager", Json::fromBool(p.manager));
            e.set("total", raw(p.total));
            e.set("stashCash", raw(p.stashCash));
            Json ammo = Json::array();
            for (int32_t n : p.stashAmmo) ammo.push(Json::number(int64_t(n)));
            e.set("stashAmmo", ammo);
        }
        list.push(e);
    }
    out.set("premises", list);
    return out;
}

bool Commerce::importAccount(Simulation& sim, uint64_t id, const Json& data) {
    auto* st = store(sim);
    if (!st || id == 0 || !data.isObject() || st->accounts.size() >= 65000) return false;
    for (size_t i = 1; i < st->accounts.size(); ++i) if (st->accounts[i].id == id) return false;
    st->accounts.emplace_back();
    const uint16_t rec = uint16_t(st->accounts.size() - 1);
    CommerceAccount& a = st->accounts[rec];
    a.id = id;
    a.wallet = Fixed::fromRaw(std::max<int64_t>(0, data.getInt("wallet")));
    a.debt = Fixed::fromRaw(std::max<int64_t>(0, data.getInt("debt")));
    a.missed = uint8_t(std::clamp<int64_t>(data.getInt("missed"), 0, 255));
    a.lastSeenDay = day(sim);
    if (const Json& list = data["premises"]; list.isArray())
        for (size_t i = 0; i < list.size(); ++i) {
            const Json& e = list[i];
            const std::string pid = e.getStr("id");
            for (auto& p : st->premises) {
                if (p.id != pid || p.isPublic) continue;
                const bool owns = e.getBool("owns") && p.owner == 0, rents = e.getBool("rents") && p.tenant == 0;
                if (owns) p.owner = rec;
                if (rents) p.tenant = rec;
                if ((owns || rents) && p.runBy() == rec) {   // what he ran there comes back with him
                    const int b = e.has("business") ? findBusiness(e.getStr("business")) : -1;
                    p.business = int16_t(b >= 0 && fits(rules_.businesses[size_t(b)], p.kind) ? b : -1);
                    p.staff = p.business >= 0 ? uint8_t(std::clamp<int64_t>(e.getInt("staff"), 0, rules_.businesses[size_t(p.business)].staff)) : uint8_t(0);
                    p.manager = p.business >= 0 && e.getBool("manager");
                    p.total = Fixed::fromRaw(e.getInt("total"));
                    p.stashCash = Fixed::fromRaw(std::max<int64_t>(0, e.getInt("stashCash")));
                    if (const Json& ammo = e["stashAmmo"]; ammo.isArray())
                        for (size_t k = 0; k < ammo.size() && k < p.stashAmmo.size(); ++k) p.stashAmmo[k] = int32_t(std::clamp<int64_t>(ammo[k].integer(0), 0, 1000000));
                }
                break;
            }
        }
    return true;
}

void Commerce::liquidate(Simulation& sim, CommerceStore& st, uint16_t rec) {
    CommerceAccount& a = st.accounts[rec];
    for (auto& p : st.premises) {
        if (p.tenant == rec) { p.tenant = 0; closeBusiness(p); vacate(sim, p, rec); }
        if (p.owner != rec) continue;
        p.owner = 0;
        if (p.tenant == 0) { closeBusiness(p); vacate(sim, p, rec); }
        pay(sim, rec, p.price * rules_.sellShare);
    }
    const Fixed back = fxMin(a.debt, fxMax(cashOf(sim, rec), Fixed{}));   // the bank first
    if (back.raw > 0) { a.debt -= back; pay(sim, rec, Fixed{} - back); }
    if (a.debt.raw <= 0) { a.debt = {}; a.missed = 0; }
    a.cars.clear();
}

bool Commerce::sellOut(Simulation& sim, uint64_t id) {
    auto* st = store(sim);
    if (!st || id == 0) return false;
    for (size_t i = 1; i < st->accounts.size(); ++i)
        if (st->accounts[i].id == id) { liquidate(sim, *st, uint16_t(i)); return true; }
    return false;
}

void Commerce::hash(Simulation& sim, Hasher& h) const {
    auto* st = store(sim);
    if (!st) return;
    h.u32(uint32_t(st->premises.size()));
    for (auto& p : st->premises) { h.u32(uint32_t(p.owner) | uint32_t(p.tenant) << 16); h.i32(p.business); h.fx(p.lastNet); h.fx(p.total);
        h.u32(uint32_t(p.staff) | uint32_t(p.manager) << 8 | uint32_t(p.visited) << 9); h.fx(p.served);
        h.fx(p.attendance); h.fx(p.stashCash); h.u32(uint32_t(p.stashAmmo.size())); for (int32_t n : p.stashAmmo) h.i32(n); }
    h.u32(uint32_t(st->accounts.size()));
    for (auto& a : st->accounts) {
        h.u64(a.id); h.u32(a.slot); h.fx(a.wallet); h.u32(a.lastSeenDay);
        h.fx(a.debt); h.u32(a.missed);
        for (Fixed f : {a.dayNet, a.weekNet, a.lastWeekNet, a.lastRent, a.lastLoanPayment, a.lastWages}) h.fx(f);
        h.u32(uint32_t(a.cars.size()));
        for (Entity e : a.cars) h.u64(e.packed());
    }
}

void Commerce::io(Simulation& sim, Archive& a) {
    auto* st = store(sim);
    if (!st) return;
    uint32_t n = uint32_t(st->premises.size()), na = uint32_t(st->accounts.size()), ns = uint32_t(st->slotRec.size());
    a(n, na, ns);
    if (!a.writing() && (n != st->premises.size() || ns != st->slotRec.size() || na == 0 || na > 65000)) { a.fail(); return; }   // another map
    if (!a.writing()) st->accounts.assign(na, {});
    for (auto& p : st->premises) {
        a(p.owner, p.tenant, p.business, p.lastNet, p.total, p.staff, p.manager, p.visited, p.served, p.stashCash, p.stashAmmo, p.attendance);
        if (!a.writing() && (p.owner >= na || p.tenant >= na || p.business < -1 || p.business >= int(rules_.businesses.size()))) { a.fail(); return; }
    }
    for (auto& c : st->accounts) {
        a(c.id, c.slot, c.wallet, c.lastSeenDay);
        a(c.debt, c.missed, c.dayNet, c.weekNet, c.lastWeekNet, c.lastRent, c.lastLoanPayment, c.lastWages, c.cars);
    }
    for (auto& r : st->slotRec) {
        a(r);
        if (!a.writing() && r >= na) { a.fail(); return; }
    }
}

// ================================================================= deals

void Commerce::pay(Simulation& sim, uint16_t rec, Fixed amount) {
    auto* st = store(sim);
    if (!st || rec == 0 || rec >= st->accounts.size() || amount.raw == 0) return;
    CommerceAccount& a = st->accounts[rec];
    if (amount.raw > 0) {
        if (a.slot) m_->addResource(sim, a.slot, uint16_t(rules_.currency), amount); else a.wallet += amount;
        return;
    }
    Fixed& have = a.slot ? m_->state(sim).players[a.slot].stock[size_t(rules_.currency)] : a.wallet;
    const Fixed need = Fixed{} - amount, taken = fxMin(need, fxMax(have, Fixed{}));
    have -= taken;
    a.debt += need - taken;   // what he could not cover is owed to the bank
}

void Commerce::fine(Simulation& sim, uint8_t player, Fixed amount) {
    auto* st = store(sim);
    if (st && amount.raw > 0) pay(sim, recOf(*st, player), Fixed{} - amount);
}

void Commerce::closeBusiness(Premise& p) { p.business = -1; p.lastNet = {}; p.total = {}; p.staff = 0; p.manager = false; p.served = {}; p.attendance = Fixed::fromInt(1); }

void Commerce::vacate(Simulation& sim, Premise& p, uint16_t who) {
    pay(sim, who, p.stashCash);
    p.stashCash = {};
    p.stashAmmo.clear();
}

Fixed Commerce::expectedToday(const Premise& p) const {
    if (p.business < 0) return {};
    const BusinessType& b = rules_.businesses[size_t(p.business)];
    Fixed income = b.incomeDay * p.traffic;
    if (b.staff > 0) income = income * int64_t(std::min<int>(p.staff, b.staff)) / int64_t(b.staff) * fxClamp(p.attendance, Fixed{}, Fixed::fromInt(1));
    if (!p.manager && !p.visited) income = income * rules_.unmanagedShare;
    return income;
}

bool Commerce::visit(Simulation& sim, int premise) {
    auto* st = store(sim);
    if (!st || premise < 0 || premise >= int(st->premises.size())) return false;
    Premise& p = st->premises[size_t(premise)];
    const uint16_t who = p.runBy();
    if (p.business < 0 || who == 0) return false;
    const Fixed ticket = rules_.businesses[size_t(p.business)].ticket;
    if (ticket.raw <= 0 || p.served + ticket > expectedToday(p) * 3 / 2) return false;
    p.served += ticket;
    pay(sim, who, ticket);
    return true;
}

const char* Commerce::refusal(Simulation& sim, uint8_t slot, CommerceOp op, int premise, int64_t value) const {
    auto* st = store(sim);
    if (!st) return "no commerce in this game";
    const uint16_t player = recOf(*st, slot);   // from here on "player" is his account record
    if (player == 0) return "no such player";
    const CommerceAccount& acc = st->accounts[player];
    const Fixed money = cashOf(sim, player);
    if (op == CommerceOp::Borrow || op == CommerceOp::Repay) {
        if (value <= 0 || value > 1000000000) return "bad amount";
        const Fixed amount = Fixed::fromInt(value);
        if (op == CommerceOp::Borrow) return acc.debt + amount > limitOf(*st, player) ? "over the credit limit" : nullptr;
        if (acc.debt.raw <= 0) return "nothing to repay";
        return money.raw <= 0 ? "not enough money" : nullptr;
    }
    if (premise < 0 || premise >= int(st->premises.size())) return "no such premises";
    const Premise& p = st->premises[size_t(premise)];
    if (rules_.range.raw > 0 && m_->topDown() && m_->topDown()->active()) {
        const Entity hero = m_->topDown()->heroOf(sim, slot);
        if (hero.valid() && sim.world().alive(hero) && flat(sim.world().get<Transform>(hero).pos - p.at).length() > rules_.range) return "too far";
    }
    if (p.isPublic && op != CommerceOp::Purchase) return "not for sale";
    switch (op) {
    case CommerceOp::Stash: case CommerceOp::Unstash: case CommerceOp::StashWeapons: case CommerceOp::TakeWeapons: {
        if (p.runBy() != player) return "not your home";
        if (std::find(rules_.storage.begin(), rules_.storage.end(), p.kind) == rules_.storage.end()) return "no stash here";
        if (op == CommerceOp::Stash) return value <= 0 ? "bad amount" : money.raw <= 0 ? "not enough money" : nullptr;
        if (op == CommerceOp::Unstash) return value <= 0 ? "bad amount" : p.stashCash.raw <= 0 ? "the stash is empty" : nullptr;
        Arsenal* ar = m_->arsenal();
        const ArsenalHand* hand = ar && ar->active() ? ar->hand(sim, slot) : nullptr;
        if (!hand || hand->ammo.empty()) return "no weapons in this game";
        bool any = false;
        for (size_t s = 0; s < hand->ammo.size(); ++s)
            any = any || (op == CommerceOp::StashWeapons ? hand->ammo[s] > 0 : s < p.stashAmmo.size() && p.stashAmmo[s] > 0);
        return any ? nullptr : op == CommerceOp::StashWeapons ? "nothing to put away" : "the stash is empty";
    }
    case CommerceOp::Hire: case CommerceOp::Fire: case CommerceOp::Manager: {
        if (p.runBy() != player) return "not yours";
        if (p.business < 0) return "no business here";
        const BusinessType& b = rules_.businesses[size_t(p.business)];
        if (op == CommerceOp::Hire) return p.staff >= b.staff ? "fully staffed" : nullptr;
        if (op == CommerceOp::Fire) return p.staff == 0 ? "nobody to fire" : nullptr;
        return nullptr;
    }
    case CommerceOp::Purchase: {
        if (value < 0 || value >= int64_t(rules_.offers.size())) return "no such offer";
        const CommerceOffer& o = rules_.offers[size_t(value)];
        if (!o.kinds.empty() && std::find(o.kinds.begin(), o.kinds.end(), p.kind) == o.kinds.end()) return "not sold here";
        if (!p.isPublic && p.business < 0) return "closed";
        if (money < o.price) return "not enough money";
        if (!o.weapon.empty()) {
            Arsenal* ar = m_->arsenal();
            const ArsenalHand* hand = ar && ar->active() ? ar->hand(sim, slot) : nullptr;
            if (!hand || hand->ammo.empty()) return "no weapons in this game";
            const auto& ws = ar->rules().weapons;
            size_t slot = 0;
            while (slot < ws.size() && ws[slot].weapon != o.weapon) ++slot;
            if (slot >= ws.size() || slot >= hand->ammo.size()) return "unknown weapon";
            if (hand->ammo[slot] < 0 || hand->ammo[slot] >= ws[slot].max) return "you carry all you can";
        }
        return nullptr;
    }
    case CommerceOp::Rent:
        if (p.owner == player) return "you own it";
        if (p.owner != 0) return "not for rent";
        if (p.tenant != 0) return p.tenant == player ? "already rented" : "taken";
        return money < p.rentWeek ? "not enough money" : nullptr;
    case CommerceOp::Leave:
        if (p.tenant != player) return "not your lease";
        return p.stashEmpty() ? nullptr : "empty the stash first";
    case CommerceOp::Buy:
        if (p.owner == player) return "you own it";
        if (p.owner != 0) return "not for sale";
        if (p.tenant != 0 && p.tenant != player) return "taken";
        return money < p.price ? "not enough money" : nullptr;
    case CommerceOp::Sell:
        if (p.owner != player) return "not yours";
        if (!p.stashEmpty()) return "empty the stash first";
        return acc.debt > limitOf(*st, player) - p.price * rules_.collateralShare ? "pledged to the bank" : nullptr;
    case CommerceOp::Open: {
        if (p.runBy() != player) return "not yours";
        if (p.business >= 0) return "already in business";
        if (value < 0 || value >= int64_t(rules_.businesses.size())) return "no such business";
        const BusinessType& b = rules_.businesses[size_t(value)];
        if (!fits(b, p.kind)) return "does not fit here";
        return money < b.setup ? "not enough money" : nullptr;
    }
    case CommerceOp::Close:
        if (p.runBy() != player) return "not yours";
        return p.business < 0 ? "nothing to close" : nullptr;
    default: return "unknown deal";
    }
}

bool Commerce::act(Simulation& sim, uint8_t slot, CommerceOp op, int premise, int64_t value) {
    if (refusal(sim, slot, op, premise, value)) return false;
    auto& st = *store(sim);
    const uint16_t player = recOf(st, slot);
    CommerceAccount& acc = st.accounts[player];
    if (op == CommerceOp::Borrow) { acc.debt += Fixed::fromInt(value); pay(sim, player, Fixed::fromInt(value)); return true; }
    if (op == CommerceOp::Repay) {
        const Fixed amount = fxMin(fxMin(Fixed::fromInt(value), acc.debt), cashOf(sim, player));
        acc.debt -= amount;                         // first, so that pay() has nothing to add back
        pay(sim, player, Fixed{} - amount);
        if (acc.debt.raw <= 0) { acc.debt = {}; acc.missed = 0; }
        return true;
    }
    Premise& p = st.premises[size_t(premise)];
    switch (op) {
    case CommerceOp::Rent: pay(sim, player, Fixed{} - p.rentWeek); p.tenant = player; break;   // the first week up front
    case CommerceOp::Leave: p.tenant = 0; closeBusiness(p); break;
    case CommerceOp::Buy: pay(sim, player, Fixed{} - p.price); p.owner = player; p.tenant = 0; break;   // a tenant who buys keeps his business
    case CommerceOp::Sell: p.owner = 0; closeBusiness(p); pay(sim, player, p.price * rules_.sellShare); break;
    case CommerceOp::Open: pay(sim, player, Fixed{} - rules_.businesses[size_t(value)].setup); p.business = int16_t(value); p.lastNet = {}; p.total = {}; break;
    case CommerceOp::Close: closeBusiness(p); break;
    case CommerceOp::Stash: {
        const Fixed amount = fxMin(Fixed::fromInt(value), cashOf(sim, player));
        pay(sim, player, Fixed{} - amount);
        p.stashCash += amount;
        break;
    }
    case CommerceOp::Unstash: {
        const Fixed amount = fxMin(Fixed::fromInt(value), p.stashCash);
        p.stashCash -= amount;
        pay(sim, player, amount);
        break;
    }
    case CommerceOp::StashWeapons: case CommerceOp::TakeWeapons: {
        Arsenal* ar = m_->arsenal();
        auto& ammo = ar->store(sim)->hands[slot].ammo;
        const auto& ws = ar->rules().weapons;
        p.stashAmmo.resize(ammo.size(), 0);
        for (size_t s = 0; s < ammo.size() && s < ws.size(); ++s) {
            if (ammo[s] < 0) continue;   // unlimited: nothing to store
            if (op == CommerceOp::StashWeapons) { p.stashAmmo[s] += ammo[s]; ammo[s] = 0; }
            else { const int32_t n = std::min(p.stashAmmo[s], ws[s].max - ammo[s]); ammo[s] += n; p.stashAmmo[s] -= n; }
        }
        break;
    }
    case CommerceOp::Hire: ++p.staff; break;
    case CommerceOp::Fire: --p.staff; break;
    case CommerceOp::Manager: p.manager = !p.manager; break;
    case CommerceOp::Purchase: {
        const CommerceOffer& o = rules_.offers[size_t(value)];
        pay(sim, player, Fixed{} - o.price);
        if (const uint16_t seller = p.runBy(); seller != 0 && seller != player) pay(sim, seller, o.price);
        if (!o.weapon.empty()) {
            Arsenal* ar = m_->arsenal();
            const auto& ws = ar->rules().weapons;
            size_t slot = 0;
            while (ws[slot].weapon != o.weapon) ++slot;
            int32_t& ammo = ar->store(sim)->hands[slot].ammo[slot];
            ammo = std::min<int32_t>(ws[slot].max, ammo + o.ammo);
        } else {
            // the car waits at the kerb next to the door: the first free spot of a small ring
            const MapGrid& g = m_->state(sim).grid;
            const UnitType& vt = m_->rules().types[size_t(o.vehicle)];
            Vec3 at = p.at;
            static const int ring[8][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {2, 0}, {-2, 0}, {0, 2}, {0, -2}};
            for (auto& d : ring) {
                const Vec3 c = p.at + Vec3{Fixed::fromInt(d[0]), Fixed{}, Fixed::fromInt(d[1])};
                if (g.passableAt(c, vt.pathLayer)) { at = c; break; }
            }
            const Entity car = m_->spawn(sim, uint16_t(o.vehicle), 0, at);
            if (car.valid()) acc.cars.push_back(car);
        }
        break;
    }
    default: return false;
    }
    return true;
}

bool Commerce::command(Simulation& sim, const Command& cmd) {
    if (cmd.type != kCmdCommerce) return false;
    if (!active() || cmd.param < 0) return true;
    act(sim, cmd.player, CommerceOp(cmd.param & 0xFF), int((cmd.param >> 8) & 0xFFFF), cmd.param >> 24);
    return true;
}

// ================================================================= the clock

void Commerce::tick(Simulation& sim) {
    auto* st = store(sim);
    if (!st) return;
    if (sim.tick() % 10 == 0 && m_->topDown() && m_->topDown()->active()) {   // who came by his business today
        for (size_t pl = 1; pl < st->slotRec.size(); ++pl) {
            const uint16_t rec = st->slotRec[pl];
            if (!rec) continue;
            const Entity hero = m_->topDown()->heroOf(sim, uint8_t(pl));
            if (!hero.valid() || !sim.world().alive(hero)) continue;
            const Vec3 hp = sim.world().get<Transform>(hero).pos;
            const Fixed reach = fxMax(rules_.range, Fixed::fromInt(2));
            for (auto& p : st->premises)
                if (p.business >= 0 && !p.visited && p.runBy() == rec && flat(hp - p.at).length() <= reach) p.visited = true;
        }
        for (auto& a : st->accounts)   // wrecked cars leave the books
            a.cars.erase(std::remove_if(a.cars.begin(), a.cars.end(), [&](Entity e) { return !sim.world().alive(e); }), a.cars.end());
    }
    if (sim.tick() == 0 || sim.tick() % rules_.dayTicks) return;
    endDay(sim, *st);
    if (day(sim) % uint32_t(rules_.weekDays) == 0) endWeek(sim, *st);
}

void Commerce::endDay(Simulation& sim, CommerceStore& st) {
    for (auto& a : st.accounts) a.dayNet = {};
    for (auto& p : st.premises) {
        const uint16_t who = p.runBy();
        if (p.business < 0 || who == 0) continue;
        const BusinessType& b = rules_.businesses[size_t(p.business)];
        // walk-in customers already paid `served`; the rest of the day's trade is booked now
        const Fixed rest = fxMax(expectedToday(p) - p.served, Fixed{});
        const Fixed net = p.served + rest - b.costDay;
        pay(sim, who, rest - b.costDay);
        p.served = {};
        p.visited = false;
        p.lastNet = net;
        p.total += net;
        st.accounts[who].dayNet += net;
        st.accounts[who].weekNet += net;
    }
    if (rules_.absentDays > 0)   // whoever has been away too long is sold out: the money waits in his wallet
        for (size_t i = 1; i < st.accounts.size(); ++i) {
            const CommerceAccount& a = st.accounts[i];
            if (a.slot != 0 || day(sim) < a.lastSeenDay + uint32_t(rules_.absentDays)) continue;
            bool holds = false;
            for (auto& p : st.premises) holds = holds || p.owner == i || p.tenant == i;
            if (holds) liquidate(sim, st, uint16_t(i));
        }
}

void Commerce::endWeek(Simulation& sim, CommerceStore& st) {
    for (size_t pl = 1; pl < st.accounts.size(); ++pl) {
        CommerceAccount& a = st.accounts[pl];
        const uint16_t who = uint16_t(pl);
        a.lastRent = {};
        a.lastLoanPayment = {};
        a.lastWages = {};
        for (auto& p : st.premises) {   // wages first: people before the landlord and the bank
            if (p.business < 0 || p.runBy() != who) continue;
            const Fixed wages = rules_.businesses[size_t(p.business)].wageWeek * int64_t(p.staff) + (p.manager ? rules_.managerWageWeek : Fixed{});
            pay(sim, who, Fixed{} - wages);
            a.lastWages += wages;
        }
        for (auto& p : st.premises) {
            if (p.tenant != who) continue;
            if (cashOf(sim, who) >= p.rentWeek) { pay(sim, who, Fixed{} - p.rentWeek); a.lastRent += p.rentWeek; }
            else { p.tenant = 0; closeBusiness(p); vacate(sim, p, who); }   // evicted
        }
        if (a.debt.raw > 0) {
            const Fixed interest = a.debt * rules_.rateWeek;
            const Fixed due = fxMin(a.debt, fxMax(a.debt / rules_.termWeeks, Fixed::fromInt(1))) + interest;
            a.debt += interest;
            const Fixed paid = fxMin(due, fxMax(cashOf(sim, who), Fixed{}));
            a.debt -= paid;
            pay(sim, who, Fixed{} - paid);
            a.lastLoanPayment = paid;
            a.missed = paid < due ? uint8_t(std::min(255, a.missed + 1)) : uint8_t(0);
            if (a.missed > rules_.graceWeeks) {   // the bank takes property, in map order
                for (auto& p : st.premises) {
                    if (a.debt.raw <= 0) break;
                    if (p.owner != who) continue;
                    p.owner = 0;
                    if (p.tenant == 0) { closeBusiness(p); vacate(sim, p, who); }
                    a.debt -= p.price * rules_.collateralShare;
                }
                if (a.debt.raw <= 0) { pay(sim, who, Fixed{} - a.debt); a.debt = {}; a.missed = 0; }
            }
        }
        a.lastWeekNet = a.weekNet;
        a.weekNet = {};
    }
}

} // namespace aiverse::game
