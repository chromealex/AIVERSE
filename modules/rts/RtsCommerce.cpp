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
    st.accounts.assign(m.state(sim).players.size(), {});
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
        p.owner = uint8_t(owner);
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

Fixed Commerce::creditLimit(Simulation& sim, uint8_t player) const {
    auto* st = store(sim);
    if (!st || player == 0 || player >= st->accounts.size()) return {};
    Fixed owned{};
    for (auto& p : st->premises) if (p.owner == player) owned += p.price;
    return rules_.unsecured + owned * rules_.collateralShare;
}

void Commerce::hash(Simulation& sim, Hasher& h) const {
    auto* st = store(sim);
    if (!st) return;
    h.u32(uint32_t(st->premises.size()));
    for (auto& p : st->premises) { h.u32(uint32_t(p.owner) | uint32_t(p.tenant) << 8); h.i32(p.business); h.fx(p.lastNet); h.fx(p.total);
        h.u32(uint32_t(p.staff) | uint32_t(p.manager) << 8 | uint32_t(p.visited) << 9); h.fx(p.served);
        h.fx(p.stashCash); h.u32(uint32_t(p.stashAmmo.size())); for (int32_t n : p.stashAmmo) h.i32(n); }
    for (auto& a : st->accounts) {
        h.fx(a.debt); h.u32(a.missed);
        for (Fixed f : {a.dayNet, a.weekNet, a.lastWeekNet, a.lastRent, a.lastLoanPayment, a.lastWages}) h.fx(f);
        h.u32(uint32_t(a.cars.size()));
        for (Entity e : a.cars) h.u64(e.packed());
    }
}

void Commerce::io(Simulation& sim, Archive& a) {
    auto* st = store(sim);
    if (!st) return;
    uint32_t n = uint32_t(st->premises.size()), na = uint32_t(st->accounts.size());
    a(n, na);
    if (!a.writing() && (n != st->premises.size() || na != st->accounts.size())) { a.fail(); return; }   // another map
    for (auto& p : st->premises) {
        a(p.owner, p.tenant, p.business, p.lastNet, p.total, p.staff, p.manager, p.visited, p.served, p.stashCash, p.stashAmmo);
        if (!a.writing() && (p.owner >= na || p.tenant >= na || p.business < -1 || p.business >= int(rules_.businesses.size()))) { a.fail(); return; }
    }
    for (auto& c : st->accounts) a(c.debt, c.missed, c.dayNet, c.weekNet, c.lastWeekNet, c.lastRent, c.lastLoanPayment, c.lastWages, c.cars);
}

// ================================================================= deals

void Commerce::pay(Simulation& sim, uint8_t player, Fixed amount) {
    auto* st = store(sim);
    if (!st || player == 0 || player >= st->accounts.size() || amount.raw == 0) return;
    if (amount.raw > 0) { m_->addResource(sim, player, uint16_t(rules_.currency), amount); return; }
    Fixed& have = m_->state(sim).players[player].stock[size_t(rules_.currency)];
    const Fixed need = Fixed{} - amount, taken = fxMin(need, fxMax(have, Fixed{}));
    have -= taken;
    st->accounts[player].debt += need - taken;   // what he could not cover is owed to the bank
}

void Commerce::closeBusiness(Premise& p) { p.business = -1; p.lastNet = {}; p.total = {}; p.staff = 0; p.manager = false; p.served = {}; }

void Commerce::vacate(Simulation& sim, Premise& p, uint8_t who) {
    pay(sim, who, p.stashCash);
    p.stashCash = {};
    p.stashAmmo.clear();
}

Fixed Commerce::expectedToday(const Premise& p) const {
    if (p.business < 0) return {};
    const BusinessType& b = rules_.businesses[size_t(p.business)];
    Fixed income = b.incomeDay * p.traffic;
    if (b.staff > 0) income = income * int64_t(std::min<int>(p.staff, b.staff)) / int64_t(b.staff);
    if (!p.manager && !p.visited) income = income * rules_.unmanagedShare;
    return income;
}

bool Commerce::visit(Simulation& sim, int premise) {
    auto* st = store(sim);
    if (!st || premise < 0 || premise >= int(st->premises.size())) return false;
    Premise& p = st->premises[size_t(premise)];
    const uint8_t who = p.runBy();
    if (p.business < 0 || who == 0) return false;
    const Fixed ticket = rules_.businesses[size_t(p.business)].ticket;
    if (ticket.raw <= 0 || p.served + ticket > expectedToday(p) * 3 / 2) return false;
    p.served += ticket;
    pay(sim, who, ticket);
    return true;
}

const char* Commerce::refusal(Simulation& sim, uint8_t player, CommerceOp op, int premise, int64_t value) const {
    auto* st = store(sim);
    if (!st) return "no commerce in this game";
    if (player == 0 || player >= st->accounts.size()) return "no such player";
    const CommerceAccount& acc = st->accounts[player];
    const Fixed money = cash(sim, player);
    if (op == CommerceOp::Borrow || op == CommerceOp::Repay) {
        if (value <= 0 || value > 1000000000) return "bad amount";
        const Fixed amount = Fixed::fromInt(value);
        if (op == CommerceOp::Borrow) return acc.debt + amount > creditLimit(sim, player) ? "over the credit limit" : nullptr;
        if (acc.debt.raw <= 0) return "nothing to repay";
        return money.raw <= 0 ? "not enough money" : nullptr;
    }
    if (premise < 0 || premise >= int(st->premises.size())) return "no such premises";
    const Premise& p = st->premises[size_t(premise)];
    if (rules_.range.raw > 0 && m_->topDown() && m_->topDown()->active()) {
        const Entity hero = m_->topDown()->heroOf(sim, player);
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
        const ArsenalHand* hand = ar && ar->active() ? ar->hand(sim, player) : nullptr;
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
            const ArsenalHand* hand = ar && ar->active() ? ar->hand(sim, player) : nullptr;
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
        return acc.debt > creditLimit(sim, player) - p.price * rules_.collateralShare ? "pledged to the bank" : nullptr;
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

bool Commerce::act(Simulation& sim, uint8_t player, CommerceOp op, int premise, int64_t value) {
    if (refusal(sim, player, op, premise, value)) return false;
    auto& st = *store(sim);
    CommerceAccount& acc = st.accounts[player];
    if (op == CommerceOp::Borrow) { acc.debt += Fixed::fromInt(value); pay(sim, player, Fixed::fromInt(value)); return true; }
    if (op == CommerceOp::Repay) {
        const Fixed amount = fxMin(fxMin(Fixed::fromInt(value), acc.debt), cash(sim, player));
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
        const Fixed amount = fxMin(Fixed::fromInt(value), cash(sim, player));
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
        auto& ammo = ar->store(sim)->hands[player].ammo;
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
        if (const uint8_t seller = p.runBy(); seller != 0 && seller != player) pay(sim, seller, o.price);
        if (!o.weapon.empty()) {
            Arsenal* ar = m_->arsenal();
            const auto& ws = ar->rules().weapons;
            size_t slot = 0;
            while (ws[slot].weapon != o.weapon) ++slot;
            int32_t& ammo = ar->store(sim)->hands[player].ammo[slot];
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
        for (size_t pl = 1; pl < st->accounts.size(); ++pl) {
            const Entity hero = m_->topDown()->heroOf(sim, uint8_t(pl));
            if (!hero.valid() || !sim.world().alive(hero)) continue;
            const Vec3 hp = sim.world().get<Transform>(hero).pos;
            const Fixed reach = fxMax(rules_.range, Fixed::fromInt(2));
            for (auto& p : st->premises)
                if (p.business >= 0 && !p.visited && p.runBy() == pl && flat(hp - p.at).length() <= reach) p.visited = true;
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
        const uint8_t who = p.runBy();
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
}

void Commerce::endWeek(Simulation& sim, CommerceStore& st) {
    for (size_t pl = 1; pl < st.accounts.size(); ++pl) {
        CommerceAccount& a = st.accounts[pl];
        const uint8_t who = uint8_t(pl);
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
            if (cash(sim, who) >= p.rentWeek) { pay(sim, who, Fixed{} - p.rentWeek); a.lastRent += p.rentWeek; }
            else { p.tenant = 0; closeBusiness(p); vacate(sim, p, who); }   // evicted
        }
        if (a.debt.raw > 0) {
            const Fixed interest = a.debt * rules_.rateWeek;
            const Fixed due = fxMin(a.debt, fxMax(a.debt / rules_.termWeeks, Fixed::fromInt(1))) + interest;
            a.debt += interest;
            const Fixed paid = fxMin(due, fxMax(cash(sim, who), Fixed{}));
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
