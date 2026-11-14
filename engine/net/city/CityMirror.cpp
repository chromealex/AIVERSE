#include "CityMirror.h"
#include <cmath>
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
    sim_.setCommandSink([this](Command&& c) { client_.submit(c); return sim_.tick(); });
}
Mirror::~Mirror() { sim_.setCommandSink({}); }

Entity Mirror::localOf(uint32_t serverId) const {
    const auto it = map_.find(serverId);
    return it == map_.end() ? kNullEntity : it->second;
}

void Mirror::update(double seconds) {
    if (client_.ready() && client_.tick() != tick_) apply();
    sinceSnapshot_ += seconds;
    alpha_ = float(std::min(1.0, sinceSnapshot_ / double(interval_)));
}

void Mirror::apply() {
    game::RtsModule& m = mod(sim_);
    World& w = sim_.world();
    const auto& types = m.rules().types;
    if (tick_ && client_.tick() > tick_)   // the time between two snapshots, smoothed: what the interpolation runs over
        interval_ = std::clamp(0.7f * interval_ + 0.3f * float(client_.tick() - tick_) / float(client_.tickRate()), 0.02f, 0.5f);
    tick_ = client_.tick();
    ++snapshots_;
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
    hero_ = localOf(client_.hero());
    if (game::TopDown* td = m.topDown(); td && td->active())
        if (auto* ts = td->store(sim_); ts && client_.slot() < ts->pilots.size()) ts->pilots[client_.slot()].hero = hero_;
    // His money, as the UI reads it.
    if (game::Commerce* c = m.commerce(); c && c->active() && client_.slot() < m.state(sim_).players.size()) {
        if (bound_ != client_.slot()) { c->bind(sim_, client_.slot(), 1); bound_ = client_.slot(); }
        m.state(sim_).players[client_.slot()].stock[size_t(c->rules().currency)] = Fixed::fromRaw(client_.account().cash);
    }
}
}
