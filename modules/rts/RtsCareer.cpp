// Careers: ladders of ranks with perks (see RtsCareer.h, docs/careers.md).
#include "RtsCareer.h"
#include <algorithm>
#include <stdexcept>
#include "RtsInternal.h"
#include "RtsModule.h"

namespace aiverse::game {

CareerRules Career::parse(const Json& j) {
    CareerRules r;
    if (!j.isObject()) return r;
    r.enabled = true;
    for (auto& [id, cj] : j.members()) {
        CareerType c;
        c.id = id;
        const std::string where = "careers." + id;
        if (!cj.isObject()) throw std::runtime_error(where + ": expected an object");
        c.name = cj.getStr("name", id);
        c.open = cj.getBool("open", false);
        c.office = cj.getStr("office");
        for (auto& rj : cj["ranks"].items()) {
            CareerRank rk;
            rk.name = rj.getStr("name");
            if (rk.name.empty()) throw std::runtime_error(where + ".ranks: every rank needs a name");
            rk.points = rj.getFx("points");
            if (rk.points.raw < 0) throw std::runtime_error(where + ".ranks: points must not be negative");
            if (!c.ranks.empty() && rk.points <= c.ranks.back().points) throw std::runtime_error(where + ".ranks: points must grow from rank to rank");
            for (auto& p : rj["perks"].items()) rk.perks.push_back(p.str());
            c.ranks.push_back(std::move(rk));
        }
        if (c.ranks.empty() || c.ranks[0].points.raw != 0) throw std::runtime_error(where + ".ranks: the first rank starts at 0 points");
        r.careers.push_back(std::move(c));
    }
    std::sort(r.careers.begin(), r.careers.end(), [](const CareerType& a, const CareerType& b) { return a.id < b.id; });
    if (r.careers.empty() || r.careers.size() > 250) throw std::runtime_error("careers: list between 1 and 250");
    return r;
}

CareerStore* Career::store(Simulation& sim) const { return active() ? sim.world().resource<CareerStore>() : nullptr; }

void Career::setup(Simulation& sim, RtsModule& m, const Json& rulesJson) {
    m_ = &m;
    rules_ = parse(rulesJson["careers"]);
    if (!active()) return;
    auto& st = sim.world().setResource<CareerStore>();
    st.players.assign(m.state(sim).players.size(), std::vector<CareerProgress>(rules_.careers.size()));
}

int Career::find(const std::string& id) const {
    for (size_t i = 0; i < rules_.careers.size(); ++i) if (rules_.careers[i].id == id) return int(i);
    return -1;
}

const CareerProgress* Career::progress(Simulation& sim, uint8_t player, int career) const {
    auto* st = store(sim);
    if (!st || player == 0 || player >= st->players.size() || career < 0 || career >= int(rules_.careers.size())) return nullptr;
    return &st->players[player][size_t(career)];
}

int Career::rankOf(const CareerType& c, Fixed points) const {
    int r = 0;
    for (size_t i = 1; i < c.ranks.size(); ++i) if (points >= c.ranks[i].points) r = int(i);
    return r;
}

int Career::rank(Simulation& sim, uint8_t player, int career) const {
    const CareerProgress* p = progress(sim, player, career);
    return p && p->joined ? rankOf(rules_.careers[size_t(career)], p->points) : -1;
}

bool Career::hasPerk(Simulation& sim, uint8_t player, const std::string& perk) const {
    for (size_t c = 0; c < rules_.careers.size(); ++c) {
        const int r = rank(sim, player, int(c));
        for (int i = 0; i <= r; ++i) {
            const auto& perks = rules_.careers[c].ranks[size_t(i)].perks;
            if (std::find(perks.begin(), perks.end(), perk) != perks.end()) return true;
        }
    }
    return false;
}

bool Career::join(Simulation& sim, uint8_t player, int career) {
    auto* p = const_cast<CareerProgress*>(progress(sim, player, career));
    if (!p || p->joined) return false;
    p->joined = true;
    p->points = {};
    return true;
}

bool Career::leave(Simulation& sim, uint8_t player, int career) {
    auto* p = const_cast<CareerProgress*>(progress(sim, player, career));
    if (!p || !p->joined) return false;
    *p = {};
    return true;
}

int Career::add(Simulation& sim, uint8_t player, int career, Fixed points) {
    auto* p = const_cast<CareerProgress*>(progress(sim, player, career));
    if (!p || !p->joined) return 0;
    const CareerType& c = rules_.careers[size_t(career)];
    const int before = rankOf(c, p->points);
    p->points = fxMax(p->points + points, Fixed{});
    return rankOf(c, p->points) - before;
}

bool Career::command(Simulation& sim, const Command& cmd) {
    if (cmd.type != kCmdCareer) return false;
    if (!active() || cmd.param < 0) return true;
    const int career = int(cmd.param >> 8);
    if (career >= int(rules_.careers.size()) || !rules_.careers[size_t(career)].open) return true;   // by invitation only
    const CareerOp op = CareerOp(cmd.param & 0xFF);
    if (op == CareerOp::Join) join(sim, cmd.player, career);
    else if (op == CareerOp::Leave) leave(sim, cmd.player, career);
    return true;
}

void Career::hash(Simulation& sim, Hasher& h) const {
    auto* st = store(sim);
    if (!st) return;
    for (auto& pl : st->players)
        for (auto& p : pl) { h.u32(p.joined); h.fx(p.points); }
}

void Career::io(Simulation& sim, Archive& a) {
    auto* st = store(sim);
    if (!st) return;
    uint32_t np = uint32_t(st->players.size()), nc = uint32_t(rules_.careers.size());
    a(np, nc);
    if (!a.writing() && (np != st->players.size() || nc != rules_.careers.size())) { a.fail(); return; }   // other rules
    for (auto& pl : st->players)
        for (auto& p : pl) a(p.joined, p.points);
}

} // namespace aiverse::game
