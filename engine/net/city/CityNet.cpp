#include "CityNet.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <ctime>
#include <fstream>
#include "engine/net/Crypto.h"
#include "engine/net/HeroInput.h"
#include "engine/core/Archive.h"
#include "modules/rts/RtsCareer.h"
#include "modules/rts/RtsCommerce.h"
#include "modules/rts/RtsDriving.h"
#include "modules/rts/RtsModule.h"
#include "modules/rts/RtsTopDown.h"
#include "modules/rts/RtsTraffic.h"

namespace aiverse { void io(Archive& a, Command& c); }   // engine/sim/Simulation.cpp

namespace aiverse::net::city {
namespace {
using game::RtsModule;
constexpr uint32_t kMagic = 0x43495459;   // "CITY"
enum class Msg : uint8_t { Hello = 1, Command = 2, Welcome = 3, Snapshot = 4, Account = 5, Event = 6, Fixed = 7, Gone = 8, Sim = 9, Premises = 10 };
constexpr size_t kEntityBytes = 14;

struct Out {
    Bytes b;
    explicit Out(Msg m) { b.push_back(uint8_t(m)); }
    void u8(uint8_t v) { b.push_back(v); }
    void u16(uint16_t v) { b.push_back(uint8_t(v)); b.push_back(uint8_t(v >> 8)); }
    void u32(uint32_t v) { u16(uint16_t(v)); u16(uint16_t(v >> 16)); }
    void u64(uint64_t v) { u32(uint32_t(v)); u32(uint32_t(v >> 32)); }
};
struct In {
    const Bytes& b; size_t at = 1; bool ok = true;
    explicit In(const Bytes& bytes) : b(bytes) {}
    uint8_t u8() { if (at + 1 > b.size()) { ok = false; return 0; } return b[at++]; }
    uint16_t u16() { const uint16_t lo = u8(); return uint16_t(lo | uint16_t(u8()) << 8); }
    uint32_t u32() { const uint32_t lo = u16(); return lo | uint32_t(u16()) << 16; }
    uint64_t u64() { const uint64_t lo = u32(); return lo | uint64_t(u32()) << 32; }
    bool end() const { return ok && at == b.size(); }
};
uint16_t q64(Fixed v) { return uint16_t(std::clamp<int64_t>((v.raw * 64) >> Fixed::kFracBits, 0, 65535)); }
void put(Out& o, const EntityState& e) { o.u32(e.id); o.u16(e.type); o.u8(e.owner); o.u16(e.x); o.u16(e.z); o.u8(e.yaw); o.u8(e.hp); o.u8(e.flags); }
EntityState get(In& i) { EntityState e; e.id = i.u32(); e.type = i.u16(); e.owner = i.u8(); e.x = i.u16(); e.z = i.u16(); e.yaw = i.u8(); e.hp = i.u8(); e.flags = i.u8(); return e; }
}

uint64_t contentId(const Json& rules, const Json& map) {
    Hasher h;
    h.u32(kProtocol);
    if (rules["types"].isObject()) for (auto& [id, def] : rules["types"].members()) { (void)def; h.str(id); }
    h.str(map["size"].dump(-1)); h.str(map["players"].dump(-1)); h.str(map["entities"].dump(-1));
    return h.value();
}

void prepare(Json& rules, Json& map, int seats) {
    if (Json* c = rules.find("commerce"); c && c->isObject()) c->set("accounts", Json::string("external"));
    Json* players = map.find("players");
    Json* entities = map.find("entities");
    if (!players || !entities || !players->isArray() || !entities->isArray()) throw std::runtime_error("city: the map needs players and entities");
    std::string heroType;
    for (const Json& e : entities->items()) if (e.getInt("owner") == 1 && heroType.empty()) heroType = e.getStr("type");
    if (heroType.empty()) throw std::runtime_error("city: player 1 has no hero on the map");
    std::vector<std::pair<Fixed, Fixed>> spots;
    if (const Json& j = map["city"]["junctions"]; j.isArray())
        for (const Json& n : j.items()) spots.push_back({n["x"].fx(), n["z"].fx()});
    if (spots.size() < size_t(std::max(0, seats)))
        if (const Json& pr = map["premises"]; pr.isArray())
            for (const Json& p : pr.items()) spots.push_back({p["at"][size_t(0)].fx(), p["at"][size_t(1)].fx()});
    for (int i = 1; i < seats; ++i) {
        if (spots.empty() || players->size() >= 250) throw std::runtime_error("city: no room for " + std::to_string(seats) + " seats");
        Json p = Json::object();
        p.set("name", Json::string("Citizen " + std::to_string(i + 1)));
        p.set("team", Json::number(int64_t(1)));
        players->push(p);
        const auto spot = spots[size_t(i) * spots.size() / size_t(seats)];
        Json e = Json::object();
        e.set("type", Json::string(heroType));
        e.set("owner", Json::number(int64_t(players->size())));
        Json pos = Json::array();
        pos.push(Json::number(spot.first)); pos.push(Json::number(spot.second));
        e.set("pos", pos);
        entities->push(e);
    }
}
std::vector<uint8_t> ticket(const std::vector<uint8_t>& key, const std::string& player, int64_t expiresUnix) {
    if (key.size() != 32) return {};
    Key k;
    std::copy(key.begin(), key.end(), k.begin());
    std::vector<uint8_t> msg(player.begin(), player.end());
    for (int i = 0; i < 8; ++i) msg.push_back(uint8_t(uint64_t(expiresUnix) >> (i * 8)));
    std::vector<uint8_t> out;
    for (int i = 0; i < 8; ++i) out.push_back(uint8_t(uint64_t(expiresUnix) >> (i * 8)));
    const auto mac = keyedHash(k, msg.data(), msg.size(), 32);
    out.insert(out.end(), mac.begin(), mac.end());
    return out;   // expiry (8 bytes) + mac (32)
}
std::string toHex(const std::vector<uint8_t>& bytes) {
    static const char* digits = "0123456789abcdef";
    std::string out;
    for (uint8_t b : bytes) { out.push_back(digits[b >> 4]); out.push_back(digits[b & 15]); }
    return out;
}
std::vector<uint8_t> fromHex(const std::string& text) {
    std::vector<uint8_t> out;
    if (text.size() % 2) return out;
    auto digit = [](char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; };
    for (size_t i = 0; i < text.size(); i += 2) {
        const int hi = digit(text[i]), lo = digit(text[i + 1]);
        if (hi < 0 || lo < 0) return {};
        out.push_back(uint8_t(hi << 4 | lo));
    }
    return out;
}
uint64_t accountId(const std::string& player) { Hasher h; h.str("city-account"); h.str(player); const uint64_t v = h.value(); return v ? v : 1; }

// ================================================================= server

Server::Server(const Json& rules, const Json& map, uint64_t seed, Options options, AccountStore* accounts)
    : opt_(options), accounts_(accounts), content_(contentId(rules, map)) {
    SimConfig cfg;
    cfg.seed = seed;
    cfg.inputDelayTicks = 0;
    cfg.recordCommands = false;
    sim_ = std::make_unique<Simulation>(cfg, std::make_unique<RtsModule>());
    sim_->setup(rules, map);
    if (!module().topDown() || !module().topDown()->active()) throw std::runtime_error("city server: the rules have no \"topDown\" heroes");
    // The city goes on from where it was: everybody's property is theirs again before anyone comes in.
    if (game::Commerce* c = module().commerce(); accounts_ && c && c->active())
        for (auto& [player, doc] : accounts_->known())
            if (doc.isObject()) c->importAccount(*sim_, accountId(player), doc["commerce"]);
    game::TopDown* td = module().topDown();
    for (auto& p : sim_->players()) if (td->heroOf(*sim_, p.slot).valid()) seats_.push_back(p.slot);
    if (opt_.emptySeats) {   // nobody is in the city yet
        for (uint8_t slot : seats_) {
            game::TopDownPilot& pilot = td->store(*sim_)->pilots[slot];
            if (sim_->world().alive(pilot.hero)) sim_->world().destroy(pilot.hero);
            pilot.hero = kNullEntity; pilot.respawnAt = 0;
        }
        sim_->world().flush();
    }
    if (!opt_.clock) opt_.clock = [] { return int64_t(std::time(nullptr)); };
    opt_.radius = std::clamp(opt_.radius, 4, 500);
    opt_.snapshotRate = std::clamp(opt_.snapshotRate, 1, int(sim_->tickRate()));
    opt_.maxEntities = std::clamp(opt_.maxEntities, 8, 4000);
}
Server::~Server() = default;
RtsModule& Server::module() { return static_cast<RtsModule&>(sim_->module()); }

std::vector<uint8_t> Server::seats() const { return seats_; }
bool Server::occupied(uint8_t slot) const { for (auto& p : peers_) if (p.slot == slot) return true; return false; }
uint8_t Server::freeSeat() const { for (uint8_t s : seats_) if (!occupied(s)) return s; return 0; }

bool Server::seat(Peer& p, uint8_t slot, const std::string& player) {
    if (slot == 0 || occupied(slot) || std::find(seats_.begin(), seats_.end(), slot) == seats_.end()) return false;
    if (!player.empty()) for (auto& q : peers_) if (q.player == player) return false;
    p.slot = slot; p.player = player;
    game::Commerce* c = module().commerce();
    Json saved;
    if (!player.empty() && c && c->active()) {
        p.account = accountId(player);
        saved = accounts_ ? accounts_->enter(player) : Json{};
        if (saved.isObject() && !c->accountById(*sim_, p.account)) c->importAccount(*sim_, p.account, saved["commerce"]);
        if (!c->bind(*sim_, slot, p.account)) { p.account = 0; return false; }
        if (game::Career* k = module().career(); k && saved.isObject() && saved.has("career")) k->load(*sim_, slot, saved["career"]);
    }
    if (opt_.emptySeats) {   // his hero walks in: where he left, else at the seat's place on the map
        game::TopDownPilot& pilot = module().topDown()->store(*sim_)->pilots[slot];
        if (saved.isObject() && saved["pos"].isArray() && saved["pos"].size() == 2)
            pilot.home = Vec3{Fixed::fromRaw(saved["pos"][size_t(0)].integer(0)), Fixed{}, Fixed::fromRaw(saved["pos"][size_t(1)].integer(0))};
        if (!pilot.hero.valid()) pilot.respawnAt = std::max<uint32_t>(1, sim_->tick());
    }
    return true;
}

bool Server::attach(uint8_t slot, std::unique_ptr<Conn> conn, const std::string& player) {
    if (!conn) return false;
    Peer p;
    p.conn = std::move(conn);
    if (!seat(p, slot, player)) return false;
    peers_.push_back(std::move(p));
    return true;
}

void Server::admit(std::unique_ptr<Conn> conn) {
    if (!conn) return;
    if (waiting_.size() >= 256) { conn->close(); return; }
    Peer p;
    p.conn = std::move(conn);
    waiting_.push_back(std::move(p));
}

void Server::welcome(Peer& p) {
    p.ready = true;
    p.premises.clear();
    Out w(Msg::Welcome);
    w.u8(p.slot); w.u32(module().topDown()->heroOf(*sim_, p.slot).index); w.u32(sim_->tickRate()); w.u16(uint16_t(opt_.radius));
    w.u32(uint32_t(opt_.keepaliveMs));
    p.conn->send(w.b);
    stats_.reliableBytes += w.b.size();
}

// The hello of a connection. A seated one (attach) only has to know the content; a waiting one also says who it
// is and gets a seat. false: refused, the connection is closed.
bool Server::hello(Peer& p, const Bytes& packet, bool seated) {
    In in(packet);
    const uint32_t magic = in.u32(), proto = in.u32();
    const uint64_t id = in.u64();
    std::string name(in.u8(), '\0');
    for (char& ch : name) ch = char(in.u8());
    std::vector<uint8_t> tk(in.u8());
    for (uint8_t& b : tk) b = in.u8();
    auto refuse = [&] { ++stats_.rejected; p.conn->close(); return false; };
    if (!in.end() || magic != kMagic || proto != kProtocol || id != content_) return refuse();
    if (seated) { welcome(p); return true; }
    std::string player;
    if (opt_.key.size() == 32) {
        if (name.empty() || tk.size() != 40) return refuse();
        int64_t expires = 0;
        for (int i = 0; i < 8; ++i) expires |= int64_t(uint64_t(tk[size_t(i)]) << (i * 8));
        const auto good = ticket(opt_.key, name, expires);
        if (good.size() != 40 || !equalBytes(good.data(), tk.data(), 40) || expires < opt_.clock()) return refuse();
        player = name;
    } else {
        for (char ch : name) if (!(std::isalnum(static_cast<unsigned char>(ch)) || ch == '-' || ch == '_')) return refuse();
        player = "guest-" + (name.empty() ? std::to_string(++guests_) : name);
    }
    const uint8_t slot = freeSeat();
    if (!slot || !seat(p, slot, player)) return refuse();
    welcome(p);
    return true;
}

void Server::store(Peer& p, bool leaving) {
    if (!accounts_ || p.player.empty() || !p.account) return;
    Json doc = Json::object();
    doc.set("commerce", module().commerce()->exportAccount(*sim_, p.account));
    if (game::Career* k = module().career()) doc.set("career", k->save(*sim_, p.slot));
    if (const Entity hero = module().topDown()->heroOf(*sim_, p.slot); hero.valid() && sim_->world().alive(hero)) {
        const Vec3 at = sim_->world().get<Transform>(hero).pos;
        Json pos = Json::array();
        pos.push(Json::number(int64_t(at.x.raw))); pos.push(Json::number(int64_t(at.z.raw)));
        doc.set("pos", pos);
    }
    accounts_->save(p.player, doc, leaving);
}

void Server::leave(Peer& p) {
    Command release = heroInputCommand(p.slot, topdown::Input{});   // never leave a stick or a trigger held
    release.tick = sim_->tick();
    sim_->inject(release);
    store(p, true);
    if (p.account) {
        module().commerce()->unbind(*sim_, p.slot);
        if (game::Career* k = module().career()) k->clear(*sim_, p.slot);
    }
    if (opt_.emptySeats) {   // his hero leaves the city with him
        game::TopDownPilot& pilot = module().topDown()->store(*sim_)->pilots[p.slot];
        if (sim_->world().alive(pilot.hero)) { sim_->world().destroy(pilot.hero); sim_->world().flush(); }
        pilot.hero = kNullEntity; pilot.respawnAt = 0; pilot.input = {}; pilot.vel = {};
    }
}

bool Server::sellOut(const std::string& player) {
    game::Commerce* c = module().commerce();
    if (!accounts_ || !c || !c->active() || player.empty()) return false;
    for (auto& p : peers_) if (p.player == player) return false;
    const uint64_t id = accountId(player);
    if (!c->accountById(*sim_, id) || !c->sellOut(*sim_, id)) return false;
    Json doc = Json::object();
    for (auto& [who, old] : accounts_->known()) if (who == player && old.isObject()) doc = old;   // his career stays
    doc.set("commerce", c->exportAccount(*sim_, id));
    accounts_->save(player, doc, true);
    return true;
}

void Server::saveAll() { for (auto& p : peers_) store(p, false); }

void Server::poll(uint64_t now) {
    for (auto& p : peers_) {
        if (!p.born) p.born = now + 1;
        p.conn->update(now);
        if (!p.ready && now + 1 - p.born > 10000) p.conn->close();
        Bytes packet;
        int count = 0;
        while (count++ < 64 && p.conn->recv(packet)) {
            if (packet.empty() || packet.size() > 600) { ++stats_.rejected; p.conn->close(); break; }
            if (Msg(packet[0]) == Msg::Hello && !p.ready) {
                if (!hello(p, packet, true)) break;
                continue;
            }
            if (!p.ready || Msg(packet[0]) != Msg::Command || p.commands.size() >= 16) { ++stats_.rejected; continue; }
            const std::string data(packet.begin() + 1, packet.end());
            Archive a(data);
            Command c;
            io(a, c);
            if (!a.ok() || a.remaining() || c.units.size() > 64 || c.args.size() > 32) { ++stats_.rejected; continue; }
            c.player = p.slot;   // the connection is the identity
            p.commands.push_back(std::move(c));
        }
    }
    for (auto it = peers_.begin(); it != peers_.end();)
        if (it->conn->state() == ConnState::Closed) { leave(*it); it = peers_.erase(it); }
        else ++it;
    for (size_t i = 0; i < waiting_.size();) {   // admitted connections: a hello within ten seconds, then a seat
        Peer& p = waiting_[i];
        if (!p.born) p.born = now + 1;
        p.conn->update(now);
        if (now + 1 - p.born > 10000) p.conn->close();
        Bytes packet;
        bool seated = false;
        if (p.conn->state() != ConnState::Closed && p.conn->recv(packet)) {
            if (packet.empty() || packet.size() > 600 || Msg(packet[0]) != Msg::Hello) { ++stats_.rejected; p.conn->close(); }
            else seated = hello(p, packet, false);
        }
        if (seated) { peers_.push_back(std::move(p)); waiting_.erase(waiting_.begin() + long(i)); }
        else if (waiting_[i].conn->state() == ConnState::Closed) waiting_.erase(waiting_.begin() + long(i));
        else ++i;
    }
}

void Server::step() {
    for (auto& p : peers_) if (auto* pi = sim_->player(p.slot)) pi->alive = true;   // a player in the city plays, whatever the match rules think of a seat that stood empty
    for (auto& p : peers_)
        while (!p.commands.empty()) {
            Command c = std::move(p.commands.front());
            p.commands.pop_front();
            c.tick = sim_->tick();
            sim_->inject(std::move(c));
            ++stats_.commands;
        }
    sim_->step();
    if (opt_.emptySeats)   // a seat nobody plays stays without a hero
        for (uint8_t slot : seats_) if (!occupied(slot)) module().topDown()->store(*sim_)->pilots[slot].respawnAt = 0;
    collectEvents();
    const uint64_t t = sim_->tick(), rate = sim_->tickRate();
    if (t * uint64_t(opt_.snapshotRate) / rate != (t - 1) * uint64_t(opt_.snapshotRate) / rate) {
        const auto from = std::chrono::steady_clock::now();
        broadcast();
        stats_.broadcastNs += uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - from).count());
    }
    if (accounts_ && opt_.saveSeconds > 0 && t % (rate * uint64_t(opt_.saveSeconds)) == 0) saveAll();
}

// What the simulation said this tick, for those who were near (or whom it is about).
void Server::collectEvents() {
    const auto& events = sim_->events();
    if (events.empty() || peers_.empty()) return;
    World& w = sim_->world();
    game::TopDown* td = module().topDown();
    struct Eye { Peer* p; Vec2 at; bool has; };
    std::vector<Eye> eyes;
    for (auto& p : peers_) {
        if (!p.ready) continue;
        const Entity hero = td->heroOf(*sim_, p.slot);
        const bool has = hero.valid() && w.alive(hero);
        const Vec3 at = has ? w.get<Transform>(hero).pos : Vec3{};
        eyes.push_back({&p, Vec2{at.x, at.z}, has});
    }
    const Fixed r2 = Fixed::fromInt(opt_.radius) * Fixed::fromInt(opt_.radius);
    for (const SimEvent& ev : events) {
        Vec3 at = ev.pos;
        bool placed = at.x.raw != 0 || at.z.raw != 0;
        if (!placed && ev.a.valid() && w.alive(ev.a) && w.has<Transform>(ev.a)) { at = w.get<Transform>(ev.a).pos; placed = true; }
        RemoteEvent r;
        r.type = ev.type; r.player = ev.player; r.value = ev.value;
        r.a = ev.a.valid() ? ev.a.index : UINT32_MAX; r.b = ev.b.valid() ? ev.b.index : UINT32_MAX;
        r.x = q64(ev.pos.x); r.z = q64(ev.pos.z);   // as the simulation gave it (0, 0: no place)
        for (Eye& e : eyes) {
            const bool his = ev.player != 0 && ev.player == e.p->slot;
            if (!his && !(placed && e.has && (Vec2{at.x, at.z} - e.at).lengthSq() <= r2)) continue;
            if (e.p->events.size() < 96) e.p->events.push_back(r);
        }
    }
}

// The premises as this player may know them, the ones that changed since he was told.
void Server::tellPremises(Peer& p) {
    game::Commerce* c = module().commerce();
    auto* st = c && c->active() ? c->store(*sim_) : nullptr;
    if (!st) return;
    if (p.premises.size() != st->premises.size()) p.premises.assign(st->premises.size(), PremiseState{});
    const uint16_t me = p.slot < st->slotRec.size() ? st->slotRec[p.slot] : uint16_t(0);
    std::vector<uint16_t> changed;
    for (size_t i = 0; i < st->premises.size(); ++i) {
        const game::Premise& pr = st->premises[i];
        PremiseState s;
        s.owner = pr.owner == 0 ? 0 : me && pr.owner == me ? 1 : 2;
        s.tenant = pr.tenant == 0 ? 0 : me && pr.tenant == me ? 1 : 2;
        s.business = pr.business; s.staff = pr.staff;
        s.flags = uint8_t((pr.manager ? 1 : 0) | (pr.visited ? 2 : 0));
        s.attendance = uint8_t(std::clamp<int64_t>((pr.attendance.raw * 255) >> Fixed::kFracBits, 0, 255));
        const bool mine = me && pr.runBy() == me;
        if (mine) { s.lastNet = pr.lastNet.raw; s.total = pr.total.raw; s.served = pr.served.raw; s.stashCash = pr.stashCash.raw; }   // the books are the owner's business
        if (!(s == p.premises[i])) { p.premises[i] = s; changed.push_back(uint16_t(i)); }
    }
    for (size_t at = 0; at < changed.size(); at += 24) {
        Out o(Msg::Premises);
        const size_t k = std::min<size_t>(24, changed.size() - at);
        o.u16(uint16_t(st->premises.size())); o.u8(uint8_t(k));
        for (size_t i = 0; i < k; ++i) {
            const PremiseState& s = p.premises[changed[at + i]];
            o.u16(changed[at + i]); o.u8(s.owner); o.u8(s.tenant); o.u16(uint16_t(s.business)); o.u8(s.staff); o.u8(s.flags); o.u8(s.attendance);
            o.u64(uint64_t(s.lastNet)); o.u64(uint64_t(s.total)); o.u64(uint64_t(s.served)); o.u64(uint64_t(s.stashCash));
        }
        p.conn->send(o.b);
        stats_.reliableBytes += o.b.size(); stats_.premisesTold += k;
    }
}

void Server::broadcast() {
    Simulation& sim = *sim_;
    RtsModule& m = module();
    World& w = sim.world();
    game::TopDown* td = m.topDown();
    const auto& types = m.rules().types;
    const uint32_t tick = sim.tick();
    const uint32_t keep = std::max<uint32_t>(1, uint32_t(uint64_t(opt_.keepaliveMs) * sim.tickRate() / 1000));

    const uint32_t rest = std::max<uint32_t>(1, uint32_t(uint64_t(opt_.restMs) * sim.tickRate() / 1000));
    if (tick % (sim.tickRate() * 30) < sim.tickRate() / uint32_t(opt_.snapshotRate) + 1)   // the dead leave the book
        for (auto it = known_.begin(); it != known_.end();) it = tick - it->second.seen > sim.tickRate() * 5 ? known_.erase(it) : std::next(it);
    // What may be told at all, in a grid of 16-cell buckets.
    struct Cand { EntityState s; Vec2 p; bool moving = false; };
    std::vector<uint8_t> human(256, 0);
    for (auto& p : peers_) human[p.slot] = 1;
    std::unordered_map<uint32_t, uint8_t> driven;   // car index -> driver's slot
    std::unordered_set<uint32_t> inside;            // heroes in a vehicle
    if (game::Driving* d = m.driving()) if (auto* ds = d->store(sim)) for (auto& c : ds->cars) if (c.player) { driven[c.e.index] = c.player; if (c.driver.valid()) inside.insert(c.driver.index); }
    std::unordered_map<uint32_t, uint8_t> involved;
    game::Traffic* tr = m.traffic();
    auto* ts = tr && tr->active() ? tr->store(sim) : nullptr;
    if (ts && opt_.replicate == Replicate::Involved)
        for (auto& inc : ts->incidents) { if (inc.subject.valid()) involved[inc.subject.index] = 1; }
    std::unordered_map<uint64_t, std::vector<Cand>> grid;
    auto key = [](Vec2 p) { return uint64_t(uint32_t(int(p.x.toInt()) >> 4)) << 32 | uint32_t(int(p.y.toInt()) >> 4); };
    w.each<game::Unit, Transform>([&](Entity e, game::Unit& u, Transform& t) {
        if (u.type >= types.size() || types[u.type].structure) return;
        const uint8_t owner = w.has<Owner>(e) ? w.get<Owner>(e).player : uint8_t(0);
        const auto dr = driven.find(e.index);
        const bool players = human[owner] || dr != driven.end();
        if (opt_.replicate == Replicate::Involved && !players && !involved.count(e.index)) return;
        Cand c;
        c.p = Vec2{t.pos.x, t.pos.z};
        c.s.id = e.index; c.s.type = u.type; c.s.owner = dr != driven.end() ? dr->second : owner;
        c.s.x = q64(t.pos.x); c.s.z = q64(t.pos.z);
        c.s.yaw = uint8_t(int(std::lround(std::atan2(u.facing.z.toDouble(), u.facing.x.toDouble()) * (128.0 / 3.14159265358979323846))) & 255);
        const Fixed max = types[u.type].maxHp;
        c.s.hp = max.raw > 0 ? uint8_t(std::clamp<int64_t>((u.hp.raw * 255) / max.raw, 0, 255)) : uint8_t(0);
        c.s.flags = uint8_t((players ? 1 : 0) | (inside.count(e.index) ? 2 : 0));
        Known& k = known_[e.index];
        if (k.seen && !(k.state == c.s)) { k.moving = true; k.changed = tick; }
        else if (k.moving && tick - k.changed >= rest) k.moving = false;
        k.state = c.s; k.seen = tick;
        c.moving = k.moving;
        grid[key(c.p)].push_back(c);
    });

    const Fixed r = Fixed::fromInt(opt_.radius), r2 = r * r;
    std::vector<const Cand*> changed, same, fresh;
    std::vector<uint32_t> gone;
    for (auto& p : peers_) {
        if (!p.ready) continue;
        const Entity hero = td->heroOf(sim, p.slot);
        if (!hero.valid() || !w.alive(hero)) continue;
        const Vec3 hp3 = w.get<Transform>(hero).pos;
        const Vec2 hp{hp3.x, hp3.z};
        changed.clear(); same.clear(); fresh.clear(); gone.clear();
        const int bx0 = (int(hp.x.toInt()) - opt_.radius) >> 4, bx1 = (int(hp.x.toInt()) + opt_.radius) >> 4;
        const int bz0 = (int(hp.y.toInt()) - opt_.radius) >> 4, bz1 = (int(hp.y.toInt()) + opt_.radius) >> 4;
        for (int bx = bx0; bx <= bx1; ++bx)
            for (int bz = bz0; bz <= bz1; ++bz) {
                const auto it = grid.find(uint64_t(uint32_t(bx)) << 32 | uint32_t(bz));
                if (it == grid.end()) continue;
                for (const Cand& c : it->second) {
                    if ((c.p - hp).lengthSq() > r2) continue;
                    if (!c.moving) {   // a thing that stands: told once, reliably
                        const auto f = p.fixed.try_emplace(c.s.id, tick);
                        if (f.second) fresh.push_back(&c);
                        f.first->second = tick;
                        continue;
                    }
                    const auto s = p.sent.find(c.s.id);
                    if (s == p.sent.end() || !(s->second.state == c.s)) changed.push_back(&c);
                    else if (tick - s->second.tick >= keep) same.push_back(&c);
                }
            }
        Out o(Msg::Snapshot);
        o.u32(tick); o.u32(hero.index);
        const size_t n = std::min<size_t>(changed.size() + same.size(), size_t(opt_.maxEntities));
        o.u16(uint16_t(n));
        size_t put_ = 0;
        for (auto* list : {&changed, &same})
            for (const Cand* c : *list) {
                if (put_ == n) break;
                put(o, c->s);
                p.sent[c->s.id] = {c->s, tick};
                ++stats_.byType[c->s.type];
                ++put_;
            }
        p.conn->sendLatest(o.b);
        ++stats_.snapshots; stats_.entities += n; stats_.snapshotBytes += o.b.size();
        if (tick % (keep * 8) == 0)
            for (auto it = p.sent.begin(); it != p.sent.end();) it = tick - it->second.tick > keep * 4 ? p.sent.erase(it) : std::next(it);

        for (auto it = p.fixed.begin(); it != p.fixed.end();)   // out of view, dead, or it started to move
            if (it->second != tick) { gone.push_back(it->first); it = p.fixed.erase(it); } else ++it;
        for (size_t at = 0; at < fresh.size(); at += 60) {
            Out f(Msg::Fixed);
            const size_t k = std::min<size_t>(60, fresh.size() - at);
            f.u32(tick); f.u16(uint16_t(k));
            for (size_t i = 0; i < k; ++i) put(f, fresh[at + i]->s);
            p.conn->send(f.b);
            stats_.reliableBytes += f.b.size(); stats_.fixedTold += k;
        }
        for (size_t at = 0; at < gone.size(); at += 120) {
            Out g(Msg::Gone);
            const size_t k = std::min<size_t>(120, gone.size() - at);
            g.u32(tick); g.u16(uint16_t(k));
            for (size_t i = 0; i < k; ++i) g.u32(gone[at + i]);
            p.conn->send(g.b);
            stats_.reliableBytes += g.b.size();
        }

        if (!p.events.empty()) {   // what the simulation said since the last snapshot
            Out ev(Msg::Sim);
            ev.u32(tick); ev.u8(uint8_t(p.events.size()));
            for (const RemoteEvent& r : p.events) { ev.u16(r.type); ev.u8(r.player); ev.u32(r.a); ev.u32(r.b); ev.u64(uint64_t(r.value)); ev.u16(r.x); ev.u16(r.z); }
            p.conn->send(ev.b);
            stats_.reliableBytes += ev.b.size(); stats_.simEvents += p.events.size();
            p.events.clear();
        }
        if (tick % sim.tickRate() == (uint32_t(p.slot) * 2) % sim.tickRate() || tick % sim.tickRate() == (uint32_t(p.slot) * 2 + 1) % sim.tickRate()) tellPremises(p);   // once a second, the players in turn

        // What happened in view since the last look.
        if (ts)
            for (auto& inc : ts->incidents) {
                if (inc.id <= lastIncident_ || (inc.pos - hp).lengthSq() > r2) continue;
                Out ev(Msg::Event);
                ev.u32(tick); ev.u32(inc.id); ev.u8(inc.kind); ev.u16(q64(inc.pos.x)); ev.u16(q64(inc.pos.y)); ev.u32(inc.subject.valid() ? inc.subject.index : 0);
                p.conn->send(ev.b);
                ++stats_.events; stats_.reliableBytes += ev.b.size();
            }
        // His own account, when it changed (looked at once a second).
        if (game::Commerce* c = m.commerce(); c && c->active() && tick % sim.tickRate() < uint32_t(sim.tickRate() / opt_.snapshotRate + 1)) {
            AccountState a;
            a.cash = c->cash(sim, p.slot).raw;
            if (const game::CommerceAccount* acc = c->account(sim, p.slot)) a.debt = acc->debt.raw;
            a.day = c->day(sim);
            if (!p.toldAny || !(a == p.told)) {
                Out ac(Msg::Account);
                ac.u64(uint64_t(a.cash)); ac.u64(uint64_t(a.debt)); ac.u32(a.day);
                p.conn->send(ac.b);
                p.told = a; p.toldAny = true;
                stats_.reliableBytes += ac.b.size();
            }
        }
    }
    if (ts) for (auto& inc : ts->incidents) lastIncident_ = std::max(lastIncident_, inc.id);
}

// ================================================================= client

Client::Client(std::unique_ptr<Conn> connection, uint64_t content, std::string player, std::vector<uint8_t> ticket)
    : conn_(std::move(connection)), content_(content), player_(std::move(player)), ticket_(std::move(ticket)) {
    if (player_.size() > 64) player_.resize(64);
    if (ticket_.size() > 64) ticket_.clear();
}

bool Client::submit(const Command& command) {
    if (!ready() || !connected()) return false;
    std::string data;
    Archive a(&data);
    Command c = command;
    io(a, c);
    Bytes b;
    b.reserve(data.size() + 1);
    b.push_back(uint8_t(Msg::Command));
    b.insert(b.end(), data.begin(), data.end());
    conn_->send(b);
    bytesOut_ += b.size();
    return true;
}

void Client::poll(uint64_t now) {
    conn_->update(now);
    if (!sentHello_ && conn_->state() == ConnState::Connected) {
        Out o(Msg::Hello);
        o.u32(kMagic); o.u32(kProtocol); o.u64(content_);
        o.u8(uint8_t(player_.size()));
        for (char ch : player_) o.u8(uint8_t(ch));
        o.u8(uint8_t(ticket_.size()));
        for (uint8_t b : ticket_) o.u8(b);
        conn_->send(o.b);
        bytesOut_ += o.b.size();
        sentHello_ = true;
    }
    Bytes packet;
    int count = 0;
    while (count++ < 256 && conn_->recv(packet)) {
        if (packet.empty() || packet.size() > 131072) continue;
        bytesIn_ += packet.size();
        In in(packet);
        switch (Msg(packet[0])) {
        case Msg::Welcome: {
            const uint8_t slot = in.u8();
            const uint32_t hero = in.u32(), rate = in.u32();
            (void)in.u16();
            const uint32_t keepMs = in.u32();
            if (!in.end() || slot == 0 || rate == 0 || rate > 1000) break;
            slot_ = slot; hero_ = hero; tickRate_ = rate;
            expire_ = std::max<uint32_t>(rate, uint32_t(uint64_t(keepMs) * rate * 5 / 2000));
            break;
        }
        case Msg::Snapshot: {
            const uint32_t tick = in.u32(), hero = in.u32();
            const uint16_t n = in.u16();
            if (!in.ok || packet.size() != 11 + size_t(n) * kEntityBytes || tick < tick_) break;   // late or malformed
            tick_ = tick; hero_ = hero;
            for (uint16_t i = 0; i < n; ++i) { const EntityState e = get(in); entities_[e.id] = {e, tick, false}; }
            ++snapshots_;
            for (auto it = entities_.begin(); it != entities_.end();) it = !it->second.fixed && tick - it->second.tick > expire_ ? entities_.erase(it) : std::next(it);
            break;
        }
        case Msg::Fixed: {
            const uint32_t tick = in.u32();
            const uint16_t n = in.u16();
            if (!in.ok || packet.size() != 7 + size_t(n) * kEntityBytes) break;
            for (uint16_t i = 0; i < n; ++i) {
                const EntityState e = get(in);
                auto& seen = entities_[e.id];
                if (seen.tick <= tick) seen = {e, tick, true};
            }
            break;
        }
        case Msg::Gone: {
            const uint32_t tick = in.u32();
            const uint16_t n = in.u16();
            if (!in.ok || packet.size() != 7 + size_t(n) * 4) break;
            for (uint16_t i = 0; i < n; ++i) {
                const auto it = entities_.find(in.u32());
                if (it != entities_.end() && (it->second.fixed || it->second.tick < tick)) entities_.erase(it);   // a newer moving state stays
            }
            break;
        }
        case Msg::Sim: {
            (void)in.u32();
            const uint8_t n = in.u8();
            if (!in.ok || packet.size() != 6 + size_t(n) * 23) break;
            for (uint8_t i = 0; i < n; ++i) {
                RemoteEvent r;
                r.type = in.u16(); r.player = in.u8(); r.a = in.u32(); r.b = in.u32(); r.value = int64_t(in.u64()); r.x = in.u16(); r.z = in.u16();
                if (simEvents_.size() < 4096) simEvents_.push_back(r);
            }
            break;
        }
        case Msg::Premises: {
            const uint16_t total = in.u16();
            const uint8_t n = in.u8();
            if (!in.ok || total > 20000 || packet.size() != 4 + size_t(n) * 41) break;
            if (premises_.size() != total) premises_.assign(total, PremiseState{});
            for (uint8_t i = 0; i < n; ++i) {
                const uint16_t index = in.u16();
                PremiseState s;
                s.owner = in.u8(); s.tenant = in.u8(); s.business = int16_t(in.u16()); s.staff = in.u8(); s.flags = in.u8(); s.attendance = in.u8();
                s.lastNet = int64_t(in.u64()); s.total = int64_t(in.u64()); s.served = int64_t(in.u64()); s.stashCash = int64_t(in.u64());
                if (index < premises_.size()) premises_[index] = s;
            }
            ++premisesVersion_;
            break;
        }
        case Msg::Account: {
            AccountState a;
            a.cash = int64_t(in.u64()); a.debt = int64_t(in.u64()); a.day = in.u32();
            if (in.end()) account_ = a;
            break;
        }
        case Msg::Event: {
            Event e;
            e.tick = in.u32(); e.id = in.u32(); e.kind = in.u8(); e.x = in.u16(); e.z = in.u16(); e.subject = in.u32();
            if (in.end() && events_.size() < 4096) events_.push_back(e);
            break;
        }
        default: break;
        }
    }
}

// ================================================================= accounts in a file

FileAccounts::FileAccounts(std::string path, std::function<int64_t()> clock) : path_(std::move(path)), clock_(std::move(clock)) {
    if (!clock_) clock_ = [] { return int64_t(std::time(nullptr)); };
    std::ifstream f(path_, std::ios::binary);
    if (f) { const std::string text((std::istreambuf_iterator<char>(f)), {}); if (!text.empty()) data_ = Json::parse(text); }
    if (!data_.isObject()) data_ = Json::object();
}
Json FileAccounts::enter(const std::string& player) {
    Json* rec = data_.find(player);
    if (!rec) { Json fresh = Json::object(); data_.set(player, fresh); rec = data_.find(player); }
    rec->set("seen", Json::number(clock_()));
    rec->set("online", Json::fromBool(true));
    flush();
    return rec->has("account") ? (*rec)["account"] : Json{};
}
void FileAccounts::save(const std::string& player, const Json& account, bool leaving) {
    Json* rec = data_.find(player);
    if (!rec) { data_.set(player, Json::object()); rec = data_.find(player); }
    rec->set("account", account);
    rec->set("seen", Json::number(clock_()));
    rec->set("online", Json::fromBool(!leaving));
    flush();
}
std::vector<std::pair<std::string, Json>> FileAccounts::known() {
    std::vector<std::pair<std::string, Json>> out;
    for (auto& [player, rec] : data_.members()) if (rec.has("account")) out.push_back({player, rec["account"]});
    return out;
}
std::vector<std::string> FileAccounts::absent(int64_t seconds) const {
    std::vector<std::string> out;
    const int64_t now = clock_();
    for (auto& [player, rec] : data_.members())
        if (!rec.getBool("online") && now - rec.getInt("seen") > seconds) out.push_back(player);
    return out;
}
void FileAccounts::flush() {
    if (path_.empty()) return;
    std::ofstream f(path_ + ".tmp", std::ios::binary | std::ios::trunc);
    f << data_.dump(1);
    f.close();
    std::rename((path_ + ".tmp").c_str(), path_.c_str());
}
}
