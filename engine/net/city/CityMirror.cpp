#include "CityMirror.h"
#include <cmath>
#include "engine/net/HeroInput.h"
#include "modules/rts/RtsCommerce.h"
#include "modules/rts/RtsModule.h"
#include "modules/rts/RtsTopDown.h"

namespace aiverse::net::city {
namespace {
game::RtsModule& mod(Simulation& sim) { return static_cast<game::RtsModule&>(sim.module()); }
Fixed cell(uint16_t q) { return Fixed::fromRaw(int64_t(q) << (Fixed::kFracBits - 6)); }
}

Mirror::Mirror(Simulation& local, Client& client) : sim_(local), client_(client) {
    // Both worlds were set up from the same map: at the start a unit has the same index here and there.
    const auto& types = mod(sim_).rules().types;
    sim_.world().each<game::Unit, Transform>([&](Entity e, game::Unit& u, Transform&) {
        if (u.type < types.size() && !types[u.type].structure) map_[e.index] = e;
    });
    sim_.setCommandSink([this](Command&& c) {
        if (c.type == kCmdHeroInput) {   // the stick: remembered, to draw the hero ahead of the server's word
            const topdown::Input in = topdown::unpack(c.param);
            double speed = 0;
            if (hero_.valid() && sim_.world().alive(hero_)) speed = mod(sim_).rules().types[sim_.world().get<game::Unit>(hero_).type].speed.toDouble();
            const Vec3 dir = game::TopDown::direction(in.moveAngle);
            const double k = speed * double(in.moveMag) / 1000.0;
            sticks_.push_back({time_, dir.x.toDouble() * k, dir.z.toDouble() * k});
        }
        client_.submit(c);
        return sim_.tick();
    });
}
Mirror::~Mirror() { sim_.setCommandSink({}); }

Entity Mirror::localOf(uint32_t serverId) const {
    const auto it = map_.find(serverId);
    return it == map_.end() ? kNullEntity : it->second;
}

double Mirror::latency() const {
    return std::clamp(double(client_.rttMs()) / 1000.0 + double(interval_) + 1.5 / double(std::max<uint32_t>(1, client_.tickRate())), 0.05, 0.5);
}

void Mirror::update(double seconds) {
    time_ += seconds;
    if (client_.ready() && client_.tick() != tick_) apply();
    sinceSnapshot_ += seconds;
    alpha_ = float(std::min(1.0, sinceSnapshot_ / double(interval_)));
    placeHero(seconds);
    showEvents();
    if (client_.premisesVersion() != premisesVersion_) showPremises();
}

// The hero: between the two told positions, plus what the stick did while the answer was on its way.
void Mirror::placeHero(double seconds) {
    World& w = sim_.world();
    if (!heroKnown_ || !hero_.valid() || !w.alive(hero_)) return;
    const double L = latency();
    while (sticks_.size() > 1 && sticks_[1].at < time_ - L) sticks_.pop_front();   // the first one may still cover the window's start
    double want[2] = {0, 0}, now = 0;
    if (predict_ && !heroInside_) {
        // The way the server will go in that time: the velocity follows the stick as the hero's does (accel / decel).
        double accel = 0, decel = 0;
        if (game::TopDown* td = mod(sim_).topDown()) { accel = td->rules().accel.toDouble(); decel = td->rules().decel.toDouble(); }
        size_t i = 0;
        double vx = 0, vz = 0;   // at the window's start: what the stick asked before it, taken as reached
        if (!sticks_.empty() && sticks_[0].at <= time_ - L) { vx = sticks_[0].vx; vz = sticks_[0].vz; }
        const double dt = 1.0 / 60.0;
        for (double t = time_ - L; t < time_; t += dt) {
            while (i + 1 < sticks_.size() && sticks_[i + 1].at <= t) ++i;
            double wx = 0, wz = 0;
            if (!sticks_.empty() && sticks_[i].at <= t) { wx = sticks_[i].vx; wz = sticks_[i].vz; }
            const double dx = wx - vx, dz = wz - vz, d = std::hypot(dx, dz);
            const bool up = wx * wx + wz * wz >= vx * vx + vz * vz && wx * vx + wz * vz >= 0;
            const double rate = (up ? accel : decel) * dt;
            if (rate <= 0 || d <= rate) { vx = wx; vz = wz; } else { vx += dx * rate / d; vz += dz * rate / d; }
            const double h = std::min(dt, time_ - t);
            want[0] += vx * h; want[1] += vz * h;
        }
        if (!sticks_.empty()) now = std::hypot(sticks_.back().vx, sticks_.back().vz);
        // He pushes and the server does not move him (a wall, a car in the way): the lead melts.
        double grip = 1;
        if (!sticks_.empty() && now > 0.1 && time_ - sticks_.back().at > L + 0.2) grip = std::clamp(serverSpeed_ / now, 0.0, 1.0);
        grip_ += (grip - grip_) * std::min(1.0, seconds * 8);
        want[0] *= grip_; want[1] *= grip_;
    }
    const double k = std::min(1.0, seconds * 15);
    lead_[0] += (want[0] - lead_[0]) * k; lead_[1] += (want[1] - lead_[1]) * k;
    const double a = double(alpha_);
    const double x = heroFrom_[0] + (heroTo_[0] - heroFrom_[0]) * a + lead_[0], z = heroFrom_[1] + (heroTo_[1] - heroFrom_[1]) * a + lead_[1];
    Transform& t = w.get<Transform>(hero_);
    t.pos = Vec3{Fixed::fromRaw(int64_t(x * Fixed::kOne)), t.pos.y, Fixed::fromRaw(int64_t(z * Fixed::kOne))};
    t.prevPos = t.pos;   // already interpolated: the scene's own alpha must not move it again
}

// The events of the server's simulation go to the local collectors (sound, effects, view scripts, notices).
void Mirror::showEvents() {
    auto remote = client_.takeSimEvents();
    if (remote.empty() && !hadEvents_) return;
    std::vector<SimEvent> events;
    events.reserve(remote.size());
    for (const RemoteEvent& r : remote) {
        SimEvent e;
        e.type = r.type; e.player = r.player; e.value = r.value;
        e.a = r.a == UINT32_MAX ? kNullEntity : localOf(r.a);
        e.b = r.b == UINT32_MAX ? kNullEntity : localOf(r.b);
        e.pos = Vec3{cell(r.x), Fixed{}, cell(r.z)};
        events.push_back(e);
    }
    hadEvents_ = !events.empty();
    sim_.present(std::move(events));   // an empty batch clears the last one
}

// Who owns what, as the door menu reads it: this player, somebody else, nobody.
void Mirror::showPremises() {
    premisesVersion_ = client_.premisesVersion();
    game::Commerce* c = mod(sim_).commerce();
    auto* st = c && c->active() ? c->store(sim_) : nullptr;
    if (!st || !bound_) return;
    const uint16_t me = bound_ < st->slotRec.size() ? st->slotRec[bound_] : uint16_t(0);
    if (!others_) {
        c->importAccount(sim_, 2, Json::object());
        for (size_t i = 1; i < st->accounts.size(); ++i) if (st->accounts[i].id == 2) others_ = uint16_t(i);
    }
    const auto& told = client_.premises();
    for (size_t i = 0; i < told.size() && i < st->premises.size(); ++i) {
        const PremiseState& s = told[i];
        game::Premise& p = st->premises[i];
        p.owner = s.owner == 1 ? me : s.owner == 2 ? others_ : uint16_t(0);
        p.tenant = s.tenant == 1 ? me : s.tenant == 2 ? others_ : uint16_t(0);
        p.business = s.business >= 0 && size_t(s.business) < c->rules().businesses.size() ? s.business : int16_t(-1);
        p.staff = s.staff; p.manager = (s.flags & 1) != 0; p.visited = (s.flags & 2) != 0;
        p.attendance = Fixed::fromRaw(int64_t(s.attendance) * Fixed::kOne / 255);
        p.lastNet = Fixed::fromRaw(s.lastNet); p.total = Fixed::fromRaw(s.total); p.served = Fixed::fromRaw(s.served); p.stashCash = Fixed::fromRaw(s.stashCash);
    }
}

void Mirror::apply() {
    game::RtsModule& m = mod(sim_);
    World& w = sim_.world();
    const auto& types = m.rules().types;
    if (tick_ && client_.tick() > tick_)   // the time between two snapshots, smoothed: what the interpolation runs over
        interval_ = std::clamp(0.7f * interval_ + 0.3f * float(client_.tick() - tick_) / float(client_.tickRate()), 0.02f, 0.5f);
    tick_ = client_.tick();
    ++snapshots_;
    {   // the hero's stretch starts where its told position (without the lead) is drawn now
        const double a = double(alpha_);
        heroFrom_[0] += (heroTo_[0] - heroFrom_[0]) * a; heroFrom_[1] += (heroTo_[1] - heroFrom_[1]) * a;
    }
    // What is drawn now becomes the start of the next stretch.
    for (auto& [id, e] : map_)
        if (w.alive(e)) { Transform& t = w.get<Transform>(e); t.prevPos = t.prevPos + (t.pos - t.prevPos) * Fixed::fromRaw(int64_t(double(alpha_) * Fixed::kOne)); }
    sinceSnapshot_ = 0;
    alpha_ = 0;

    const auto& seen = client_.entities();
    for (auto& [id, s] : seen) {
        const EntityState& st = s.state;
        if (st.type >= types.size()) continue;
        auto it = map_.find(id);
        if (it != map_.end() && (!w.alive(it->second) || w.get<game::Unit>(it->second).type != st.type)) {   // another unit took the id
            if (w.alive(it->second)) w.destroy(it->second);
            map_.erase(it);
            it = map_.end();
        }
        const Vec3 pos{cell(st.x), Fixed{}, cell(st.z)};
        Entity e;
        if (it == map_.end()) {
            e = m.spawn(sim_, st.type, st.owner, pos);
            map_[id] = e;
            w.get<Transform>(e).prevPos = w.get<Transform>(e).pos;
        } else e = it->second;
        Transform& t = w.get<Transform>(e);
        const Fixed y = t.pos.y;   // the ground height stays the local one
        if ((t.pos - pos).lengthSq() > Fixed::fromInt(36)) t.prevPos = Vec3{pos.x, y, pos.z};   // a jump (respawn, teleport): no slide
        t.pos = Vec3{pos.x, y, pos.z};
        game::Unit& u = w.get<game::Unit>(e);
        const double a = double(st.yaw) * (3.14159265358979323846 / 128.0);
        u.facing = Vec3{Fixed::fromRaw(int64_t(std::cos(a) * Fixed::kOne)), Fixed{}, Fixed::fromRaw(int64_t(std::sin(a) * Fixed::kOne))};
        u.hp = Fixed::fromRaw(types[st.type].maxHp.raw * int64_t(st.hp) / 255);
        if (w.has<Owner>(e)) w.get<Owner>(e).player = st.owner;
    }
    // What the server no longer tells about is not there (after the first second: the first lists come in parts).
    if (snapshots_ > uint32_t(std::max<uint32_t>(10, client_.tickRate())))
        for (auto it = map_.begin(); it != map_.end();) {
            if (seen.count(it->first)) { ++it; continue; }
            if (w.alive(it->second)) w.destroy(it->second);
            it = map_.erase(it);
        }
    const Entity was = hero_;
    hero_ = localOf(client_.hero());
    if (const auto it = seen.find(client_.hero()); hero_.valid() && it != seen.end()) {
        const double x = double(it->second.state.x) / 64.0, z = double(it->second.state.z) / 64.0;
        const double step = std::hypot(x - heroTo_[0], z - heroTo_[1]);
        serverSpeed_ = 0.5 * serverSpeed_ + 0.5 * step / std::max(0.02, double(interval_));
        heroTo_[0] = x; heroTo_[1] = z;
        if (!heroKnown_ || hero_ != was || step > 6) { heroFrom_[0] = x; heroFrom_[1] = z; lead_[0] = lead_[1] = 0; sticks_.clear(); }   // a new body or a jump: no slide
        heroKnown_ = true;
        heroInside_ = (it->second.state.flags & 2) != 0;
    } else heroKnown_ = false;
    if (game::TopDown* td = m.topDown(); td && td->active())
        if (auto* ts = td->store(sim_); ts && client_.slot() < ts->pilots.size()) ts->pilots[client_.slot()].hero = hero_;
    // His money, as the UI reads it.
    if (game::Commerce* c = m.commerce(); c && c->active() && client_.slot() < m.state(sim_).players.size()) {
        if (bound_ != client_.slot()) { c->bind(sim_, client_.slot(), 1); bound_ = client_.slot(); }
        m.state(sim_).players[client_.slot()].stock[size_t(c->rules().currency)] = Fixed::fromRaw(client_.account().cash);
        if (auto* st = c->store(sim_); st && st->slotRec[bound_]) st->accounts[st->slotRec[bound_]].debt = Fixed::fromRaw(client_.account().debt);
    }
}
}
