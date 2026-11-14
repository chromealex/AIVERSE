// The living city (rules traffic.services / traffic.incidents, RtsTraffic.h): incidents and the services that answer
// them. A person run over leaves a body and an ambulance comes for it; a crime somebody saw brings the police, who
// get out and fight; an exploded car burns until the fire engine puts it out, then the tow truck takes the wreck to
// the lot; a car left on a driving lane is towed too. Fires hurt what stands next to them, so one thing leads to
// another. Service vehicles are traffic cars with a destination (exitToward: the shortest way over the junction
// graph) that drive out of the nearest station object, or come from out of sight when none is near.
// Fixed maths only, entity-index order, randomness from sim.rng().
#include "RtsTraffic.h"
#include "RtsInternal.h"
#include "KingstonPolicePolicy.h"
#include "../../engine/core/FixedMath.h"
#include <algorithm>
#include <stdexcept>

namespace aiverse::game {
using namespace detail;
namespace tl = lib::genre;

namespace {
Vec3 v3(Vec2 p) { return {p.x, Fixed{}, p.y}; }
Vec2 v2(Vec3 p) { return {p.x, p.z}; }
uint32_t ticksOf(Fixed seconds, uint32_t rate) { return uint32_t(std::max<int64_t>(0, (seconds * int64_t(rate)).toInt())); }
TrafficCar* carOf(TrafficStore& st, Entity e) {
    auto it = std::lower_bound(st.cars.begin(), st.cars.end(), e.index, [](const TrafficCar& c, uint32_t i) { return c.e.index < i; });
    return it != st.cars.end() && it->e == e ? &*it : nullptr;
}
TrafficIncident* incidentOf(TrafficStore& st, uint32_t id) {
    for (auto& i : st.incidents) if (i.id == id) return &i;
    return nullptr;
}
void silentRemove(Simulation& sim, Entity e) {
    World& w = sim.world();
    if (!e.valid() || !w.alive(e)) return;
    Unit& u = w.get<Unit>(e);
    u.flags |= kUnitSilent;
    u.hp = Fixed{};
}
bool living(World& w, Entity e) { return e.valid() && w.alive(e) && w.get<Unit>(e).hp.raw > 0; }
const char* kKindNames[kIncidentKinds] = {"injured", "crime", "fire", "wreck"};
} // namespace

// ================================================================= data

void Traffic::parseCity(const Json& j, TrafficRules& r, const std::vector<std::string>& ids, uint32_t rate) {
    auto typeOf = [&](const std::string& id, const std::string& where) {
        for (size_t i = 0; i < ids.size(); ++i) if (ids[i] == id) return uint16_t(i);
        throw std::runtime_error(where + ": unknown type '" + id + "'");
    };
    auto typeList = [&](const Json& v, const std::string& where) {
        std::vector<uint16_t> out;
        if (v.isString()) out.push_back(typeOf(v.str(), where));
        else if (v.isArray()) for (const Json& e : v.items()) out.push_back(typeOf(e.str(), where));
        else if (!v.isNull()) throw std::runtime_error(where + ": a type id or a list of type ids");
        std::sort(out.begin(), out.end());
        return out;
    };
    auto seconds = [&](const Json& o, const char* key, uint32_t def, const std::string& where) {
        if (!o.isObject() || !o.has(key)) return def;
        const Fixed v = o.getFx(key);
        if (v.raw < 0) throw std::runtime_error(where + "." + key + ": must not be negative");
        return ticksOf(v, rate);
    };
    if (const Json& sv = j["services"]; sv.isObject())
        for (const auto& [id, v] : sv.members()) {
            const std::string where = "traffic.services." + id;
            TrafficService s;
            s.id = id;
            const std::string answers = v.getStr("answers");
            int kind = -1;
            for (int k = 0; k < kIncidentKinds; ++k) if (answers == kKindNames[k]) kind = k;
            if (kind < 0) throw std::runtime_error(where + ".answers: 'injured', 'crime', 'fire' or 'wreck' (got '" + answers + "')");
            s.answers = uint8_t(kind);
            const Json& types = v["types"];
            if (types.isArray()) for (const Json& e : types.items()) s.types.push_back({typeOf(e.str(), where + ".types"), 1});
            else if (types.isObject()) for (const auto& [tid, wgt] : types.members()) s.types.push_back({typeOf(tid, where + ".types"), int(std::clamp<int64_t>(wgt.integer(1), 1, 1000000))});
            if (s.types.empty()) throw std::runtime_error(where + ".types: the vehicle types of the service");
            s.stations = typeList(v["stations"], where + ".stations");
            if (v.has("range")) s.range = v.getFx("range");
            if (v.has("crew")) s.crew = typeOf(v.getStr("crew"), where + ".crew");
            s.crewCount = int(std::clamp<int64_t>(v.getInt("crewCount", s.crewCount), 1, 8));
            s.custody = v.getBool("custody", false);
            s.carry = v.getBool("carry", false);
            s.workTicks = std::max<uint32_t>(1, seconds(v, "work", ticksOf(Fixed::fromInt(5), rate), where));
            s.maxWorkTicks = std::max(s.workTicks, seconds(v, "maxWork", ticksOf(Fixed::fromInt(60), rate), where));
            s.max = int(std::clamp<int64_t>(v.getInt("max", 1), 1, 32));
            if (const Json& sp = v["speed"]; sp.isArray() && sp.size() == 2) { s.speedMin = sp[0].fx(); s.speedMax = sp[1].fx(); }
            else if (sp.isNumber()) s.speedMin = s.speedMax = sp.fx();
            else if (!sp.isNull()) throw std::runtime_error(where + ".speed: a number or [min, max]");
            if (s.speedMin.raw <= 0 || s.speedMax < s.speedMin) throw std::runtime_error(where + ".speed: 0 < min <= max");
            s.siren = v.getBool("siren", kind != kIncidentWreck);
            s.owner = uint8_t(std::clamp<int64_t>(v.getInt("owner", 0), 0, 255));
            if (r.services.size() >= 100) throw std::runtime_error("traffic.services: at most 100");
            r.services.push_back(std::move(s));
        }
    const Json& in = j["incidents"];
    if (!in.isObject()) return;
    TrafficIncidentRules& ir = r.incidents;
    const std::string where = "traffic.incidents";
    if (in.has("body")) ir.body = typeOf(in.getStr("body"), where + ".body");
    if (in.has("wreck")) ir.wreck = typeOf(in.getStr("wreck"), where + ".wreck");
    if (in.has("fire")) ir.fire = typeOf(in.getStr("fire"), where + ".fire");
    ir.bodyTicks = seconds(in, "bodyTime", ticksOf(Fixed::fromInt(90), rate), where);
    ir.wreckTicks = seconds(in, "wreckTime", ticksOf(Fixed::fromInt(300), rate), where);
    ir.burnTicks = seconds(in, "burn", ticksOf(Fixed::fromInt(45), rate), where);
    ir.crimeTicks = seconds(in, "crimeTime", ticksOf(Fixed::fromInt(60), rate), where);
    ir.timeoutTicks = std::max<uint32_t>(rate, seconds(in, "timeout", ticksOf(Fixed::fromInt(150), rate), where));
    ir.abandonTicks = seconds(in, "abandon", 0, where);
    ir.treatTicks = seconds(in, "treat", ticksOf(Fixed::fromInt(60), rate), where);
    if (in.has("fireDamage")) ir.fireDamage = in.getFx("fireDamage");
    if (in.has("fireRadius")) ir.fireRadius = in.getFx("fireRadius");
    if (in.has("witness")) ir.witness = in.getFx("witness");
    if (in.has("lethal")) ir.lethal = in.getFx("lethal");
    if (ir.fireDamage.raw < 0 || ir.fireRadius.raw < 0 || ir.witness.raw < 0) throw std::runtime_error(where + ": fireDamage, fireRadius and witness must not be negative");
    ir.lots = typeList(in["lots"], where + ".lots");
}

bool Traffic::roadSpot(Vec2 p, int radius, int32_t& piece, Fixed& along) const {
    const int cx = int(p.x.floorInt()), cz = int(p.y.floorInt());
    for (int r = 0; r <= radius; ++r)
        for (int dz = -r; dz <= r; ++dz)
            for (int dx = -r; dx <= r; ++dx) {
                if (std::max(dx < 0 ? -dx : dx, dz < 0 ? -dz : dz) != r) continue;   // the ring only
                const int x = cx + dx, z = cz + dz;
                if (x < 0 || z < 0 || x >= net_.w || z >= net_.h) continue;
                const int pi = net_.pieceAt[size_t(z * net_.w + x)];
                if (pi < 0 || net_.pieces[size_t(pi)].lanes <= 0 || net_.pieces[size_t(pi)].length() < 3) continue;
                const auto& P = net_.pieces[size_t(pi)];
                piece = pi;
                along = fxClamp(tl::trafficAlongOf(P, p), Fixed::fromInt(P.along0 + 1), Fixed::fromInt(P.along1));
                return true;
            }
    return false;
}

void Traffic::setupCity(Simulation& sim) {
    stations_.assign(rules_.services.size(), {});
    lots_.clear();
    bool any = !rules_.incidents.lots.empty();
    for (auto& s : rules_.services) any = any || !s.stations.empty();
    if (!any) return;
    World& w = sim.world();
    struct Found { uint32_t index; uint16_t type; Vec2 p; };
    std::vector<Found> found;
    w.each<Unit, Transform>([&](Entity e, Unit& u, Transform& t) { found.push_back({e.index, u.type, v2(t.pos)}); });
    std::sort(found.begin(), found.end(), [](const Found& a, const Found& b) { return a.index < b.index; });
    for (auto& f : found) {
        if (std::binary_search(rules_.incidents.lots.begin(), rules_.incidents.lots.end(), f.type)) lots_.push_back(f.p);
        for (size_t si = 0; si < rules_.services.size(); ++si) {
            if (!std::binary_search(rules_.services[si].stations.begin(), rules_.services[si].stations.end(), f.type)) continue;
            Station s;
            if (!roadSpot(f.p, 8, s.piece, s.along)) continue;
            const auto& P = net_.pieces[size_t(s.piece)];
            const bool high = tl::trafficAcrossOf(P, f.p) >= P.centre();   // pulls out on the station's side of the road
            s.dir = int8_t((tl::trafficLaneAcross(net_, P, 1, 0) >= P.centre()) == high ? 1 : -1);
            s.pos = f.p;
            stations_[si].push_back(s);
        }
    }
}

// The side to leave junction `J` through on the shortest way (in road pieces) to `destPiece`.
int Traffic::exitToward(int J, int inSide, int32_t fromPiece, int32_t destPiece) const {
    if (fromPiece == destPiece) return inSide;   // drove past the address: turn round
    const auto& jn = net_.junctions[size_t(J)];
    std::vector<int32_t> dist(net_.junctions.size(), -1), queue;
    for (int end = 0; end < 2; ++end) {
        const int j = net_.pieces[size_t(destPiece)].junction[size_t(end)];
        if (j >= 0 && dist[size_t(j)] < 0) { dist[size_t(j)] = 0; queue.push_back(j); }
    }
    for (size_t head = 0; head < queue.size() && dist[size_t(J)] < 0; ++head) {
        const int a = queue[head];
        for (int sd = 0; sd < 4; ++sd) {
            const int q = net_.junctions[size_t(a)].piece[size_t(sd)];
            if (q < 0 || net_.pieces[size_t(q)].lanes <= 0) continue;
            const auto& P = net_.pieces[size_t(q)];
            const int b = P.junction[0] == a ? P.junction[1] : P.junction[0];
            if (b >= 0 && dist[size_t(b)] < 0) { dist[size_t(b)] = dist[size_t(a)] + 1; queue.push_back(b); }
        }
    }
    int best = inSide;
    int32_t bestCost = INT32_MAX;
    for (int sd = 0; sd < 4; ++sd) {
        const int q = jn.piece[size_t(sd)];
        if (sd == inSide || q < 0 || net_.pieces[size_t(q)].lanes <= 0) continue;
        const auto& P = net_.pieces[size_t(q)];
        const int b = P.junction[0] == J ? P.junction[1] : P.junction[0];
        const int32_t cost = q == destPiece ? -1 : (b >= 0 && dist[size_t(b)] >= 0 ? dist[size_t(b)] : INT32_MAX - 1);
        if (cost < bestCost) { bestCost = cost; best = sd; }
    }
    return best;
}

bool Traffic::isCasualtyType(uint16_t t) const {
    if (isPersonType(t) || t == rules_.driverPerson || m_->rules().findType("npc_body_" + m_->rules().types[t].id) >= 0) return true;
    for (auto& s : rules_.services) if (s.crew == t) return true;
    return false;
}

// ================================================================= incidents

uint32_t Traffic::report(Simulation& sim, uint8_t kind, Vec2 pos, Entity subject, Entity offender, bool witnessed) {
    auto* st = store(sim);
    if (!st || kind >= kIncidentKinds) return 0;
    World& w = sim.world();
    if (kind == kIncidentCrime && !witnessed) {   // nobody saw it: nobody calls the police
        bool seen = false;
        const Fixed r2 = rules_.incidents.witness * rules_.incidents.witness;
        for (auto& p : st->people) if (living(w, p.e) && (v2(w.get<Transform>(p.e).pos) - pos).lengthSq() <= r2) { seen = true; break; }
        if (!seen) return 0;
    }
    // the same thing again at the same place: one incident
    const Fixed merge = kind == kIncidentCrime ? Fixed::fromInt(10) : Fixed::fromInt(2);
    for (auto& i : st->incidents)
        if (i.kind == kind && (i.pos - pos).lengthSq() <= merge * merge && (kind == kIncidentCrime || i.subject == subject || !subject.valid())) {
            if (kind == kIncidentCrime && i.state < 2) { i.pos = pos; if (offender.valid()) i.offender = offender; }
            return i.id;
        }
    if (st->incidents.size() >= 256) return 0;
    TrafficIncident inc;
    inc.id = st->nextIncident++;
    inc.kind = kind; inc.pos = pos; inc.subject = subject; inc.offender = offender; inc.born = sim.tick();
    st->incidents.push_back(inc);
    return inc.id;
}

void Traffic::incidents(Simulation& sim, TrafficStore& st) {
    const TrafficIncidentRules& ir = rules_.incidents;
    if (rules_.services.empty() && ir.body == UINT16_MAX && ir.wreck == UINT16_MAX && ir.fire == UINT16_MAX && ir.abandonTicks == 0 && st.incidents.empty()) return;
    World& w = sim.world();
    GameState& gs = m_->state(sim);
    Driving* drv = m_->driving();
    TopDown* td = m_->topDown();
    auto* pilots = td && td->active() ? td->store(sim) : nullptr;
    // the hero behind an entity (himself, or the car he drives), null: none
    auto heroBehind = [&](Entity e) -> Entity {
        if (!pilots || !e.valid() || !w.alive(e)) return kNullEntity;
        Entity who = e;
        if (drv && drv->active()) if (const DrivingCar* c = drv->car(sim, e); c && c->driver.valid()) who = c->driver;
        for (auto& p : pilots->pilots) if (p.hero == who) return who;
        return kNullEntity;
    };
    auto isVehicle = [&](uint16_t t) {
        if (!drv || !drv->active()) return false;
        for (auto& v : drv->rules().vehicles) if (v.type == t) return true;
        return false;
    };

    // 1) what happened last tick
    struct Happened { uint16_t ev; uint16_t type; Vec2 pos; Entity by; Fixed blow; Entity who; };
    std::vector<Happened> happened;
    for (const TrafficEvent& ev : st.happened) {
        if (ev.type == kEvDied) happened.push_back({kEvDied, ev.unitType, v2(ev.pos), ev.b, ev.blow, ev.a});
        else if (ev.type == kEvAttack) happened.push_back({kEvAttack, 0, v2(ev.pos), ev.a, Fixed{}, kNullEntity});
    }
    for (auto& h : happened) {
        const Entity hero = heroBehind(h.by);
        if (h.ev == kEvAttack) { if (hero.valid()) report(sim, kIncidentCrime, h.pos, kNullEntity, hero); continue; }
        if ((isCasualtyType(h.type) || kingston::policeHeroVictim(sim,h.who)) && kingston::policeOfficer(sim,h.by)) {
            Entity body=kingston::policeBody(sim,h.who,h.type,kingston::policeVictimOwner(sim,h.who),v3(h.pos));
            if(body.valid()){
                bool assigned=false;
                for(auto& call:st.incidents)if(call.kind==kIncidentCrime&&call.offender==h.who){call.offender=body;call.subject=body;call.pos=h.pos;assigned=true;}
                if(!assigned)report(sim,kIncidentCrime,h.pos,body,body,true);
                for(auto& p:st.people)if(p.e==h.who&&p.citizen>=0){st.citizens[size_t(p.citizen)].state=kCitizenDead;st.citizens[size_t(p.citizen)].e=kNullEntity;}
            }
            continue; // Police collect their casualty. Never create an ambulance call.
        }
        if (isCasualtyType(h.type)) {
            // a citizen: he is in hospital now (back home by himself if no ambulance ever takes him there), or dead
            int32_t citizen = -1;
            const bool fatal = h.blow >= m_->rules().types[h.type].maxHp * ir.lethal;
            for (auto& p : st.people) if (p.e == h.who) citizen = p.citizen;
            if (citizen >= 0 && size_t(citizen) < st.citizens.size()) {
                TrafficCitizen& c = st.citizens[size_t(citizen)];
                c.e = kNullEntity;
                c.state = fatal ? kCitizenDead : kCitizenHospital;
                c.eta = sim.tick() + ir.bodyTicks + ir.treatTicks;
            }
            if (ir.body != UINT16_MAX) {
                const int posedBody = m_->rules().findType("npc_body_" + m_->rules().types[h.type].id);
                const Entity body = m_->spawn(sim, posedBody >= 0 ? uint16_t(posedBody) : ir.body, 0, v3(h.pos));
                if (body.valid()) if (TrafficIncident* inc = incidentOf(st, report(sim, kIncidentInjured, h.pos, body))) {
                    inc->victim = h.type;
                    inc->citizen = citizen;
                    inc->fatal = fatal;   // a pistol shot or a slow car knocks a person down; a rocket or a car at speed kills
                }
            }
            if (hero.valid()) report(sim, kIncidentCrime, h.pos, kNullEntity, hero);
        } else if (isVehicle(h.type) && h.type != ir.wreck) {
            Entity wreck = kNullEntity;
            if (ir.wreck != UINT16_MAX && gs.grid.passableAt(v3(h.pos), kLayerGround)) {
                wreck = m_->spawn(sim, ir.wreck, 0, v3(h.pos));
                int32_t piece; Fixed along;
                if (wreck.valid() && roadSpot(h.pos, 1, piece, along)) w.get<Unit>(wreck).facing = v3(tl::trafficDetail::axisDir(net_.pieces[size_t(piece)].axis, 1));
            }
            if (ir.fire != UINT16_MAX) {
                const Entity fire = m_->spawn(sim, ir.fire, 0, v3(h.pos));
                if (fire.valid()) report(sim, kIncidentFire, h.pos, fire);
            }
            if (wreck.valid()) report(sim, kIncidentWreck, h.pos, wreck);
            if (hero.valid()) report(sim, kIncidentCrime, h.pos, kNullEntity, hero);
        }
    }

    const bool slow = sim.tick() % 10 == 0;
    // 2) cars the heroes drove and left on a driving lane
    if (slow && ir.abandonTicks > 0 && pilots && drv && drv->active()) {
        for (auto& p : pilots->pilots) {
            const DrivingCar* c = p.hero.valid() && w.alive(p.hero) ? drv->carOf(sim, p.hero) : nullptr;
            if (!c) continue;
            auto it = std::lower_bound(st.used.begin(), st.used.end(), c->e.index, [](const TrafficUsedCar& u, uint32_t i) { return u.e.index < i; });
            if (it == st.used.end() || it->e != c->e) st.used.insert(it, {c->e, 0});
        }
        for (size_t i = 0; i < st.used.size();) {
            TrafficUsedCar& u = st.used[i];
            const DrivingCar* c = living(w, u.e) ? drv->car(sim, u.e) : nullptr;
            if (!c) { st.used.erase(st.used.begin() + long(i)); continue; }
            ++i;
            if (c->driver.valid() || fxAbs(tl::vehicleForwardSpeed(c->st)) > kHalf) { u.since = 0; continue; }
            if (u.since == 0) { u.since = std::max<uint32_t>(sim.tick(), 1); continue; }
            if (sim.tick() - u.since < ir.abandonTicks) continue;
            // in the way: a junction, or a driving lane (not the parking lane at the kerb)
            const Vec2 p = v2(w.get<Transform>(u.e).pos);
            const int cell = gs.grid.cellOf(v3(p));
            if (cell < 0) continue;
            bool inWay = net_.junctionAt[size_t(cell)] >= 0;
            if (const int pi = net_.pieceAt[size_t(cell)]; pi >= 0) {
                const auto& P = net_.pieces[size_t(pi)];
                inWay = P.lanes > 0 && fxAbs(tl::trafficAcrossOf(P, p) - P.centre()) < rules_.net.laneWidth * P.lanes;
            }
            if (inWay) report(sim, kIncidentWreck, p, u.e);
        }
    }

    // the treated walk out of the hospital and go on with their day
    if (slow)
        for (size_t i = 0; i < st.patients.size();) {
            const TrafficPatient q = st.patients[i];
            if (sim.tick() < q.release) { ++i; continue; }
            st.patients.erase(st.patients.begin() + long(i));
            if (q.citizen >= 0 && size_t(q.citizen) < st.citizens.size()) {   // a citizen: home from the hospital door, then his day goes on
                TrafficCitizen& c = st.citizens[size_t(q.citizen)];
                if (c.state == kCitizenHospital) {
                    c.state = kCitizenToHome;
                    c.stopA = c.stopB = -1;
                    travel(sim, c, kLegWalk, q.at);
                }
                continue;
            }
            const int cx = int(q.at.x.floorInt()), cz = int(q.at.y.floorInt());
            int cell = -1;
            for (int r = 0; r <= 5 && cell < 0; ++r)
                for (int dz = -r; dz <= r && cell < 0; ++dz)
                    for (int dx = -r; dx <= r; ++dx) {
                        const int x = cx + dx, z = cz + dz;
                        if (x < 0 || z < 0 || x >= net_.w || z >= net_.h || !side_[size_t(z * net_.w + x)]) continue;
                        cell = z * net_.w + x; break;
                    }
            if (cell < 0) continue;
            const Vec3 spot{Fixed::fromInt(cell % net_.w) + kHalf, Fixed{}, Fixed::fromInt(cell / net_.w) + kHalf};
            const Entity e = m_->spawn(sim, q.type, rules_.peopleOwner, spot);
            if (!e.valid() || !isPersonType(q.type)) continue;   // other types (crews) just stand there for their owner
            TrafficPerson p;
            p.e = e; p.hp = w.get<Unit>(e).hp; p.lastPos = spot; p.until = sim.tick();
            auto it = std::lower_bound(st.people.begin(), st.people.end(), e.index, [](const TrafficPerson& a, uint32_t k) { return a.e.index < k; });
            st.people.insert(it, p);
        }

    // 3) upkeep: fires burn, bodies and wrecks do not lie there for ever, lost vehicles are replaced
    struct Burn { Entity e; Entity by; };
    std::vector<Burn> burns;
    for (size_t i = 0; i < st.incidents.size();) {
        TrafficIncident& inc = st.incidents[i];
        bool over = false;
        const uint32_t age = sim.tick() - inc.born;
        if (inc.kind == kIncidentInjured && living(w, inc.subject) && m_->rules().types[w.get<Unit>(inc.subject).type].id.starts_with("npc_body_"))
            w.get<Transform>(inc.subject).pos = v3(inc.pos); // Kingston: bodies do not drift during separation
        if (inc.kind != kIncidentCrime && !living(w, inc.subject)) over = true;
        else if (inc.kind == kIncidentInjured) over = inc.state == 0 && age > ir.bodyTicks;
        else if (inc.kind == kIncidentCrime) over = inc.state == 0 && age > ir.crimeTicks;
        else if (inc.kind == kIncidentWreck) over = inc.state == 0 && age > ir.wreckTicks && w.get<Unit>(inc.subject).type == ir.wreck;
        else if (inc.kind == kIncidentFire) {
            over = false; // Kingston persistent wreck fires are extinguished by responders.
            if (!over && slow && ir.fireDamage.raw > 0)
                gs.hash.query(v3(inc.pos), ir.fireRadius + kOne, [&](Entity o) {
                    auto* ou = w.tryGet<Unit>(o);
                    if (!ou || ou->hp.raw <= 0 || (ou->flags & kUnitOffMap) || o == inc.subject) return;
                    const UnitType& ot = m_->rules().types[ou->type];
                    if (ot.structure || ou->type == ir.fire || ou->type == ir.wreck || ou->type == ir.body) return;
                    for (auto& s : rules_.services) if (s.crew == ou->type) return;   // they know what they are doing
                    if (const TrafficCar* tc = carOf(st, o); tc && tc->task != kTaskNone) return;
                    if ((v2(w.get<Transform>(o).pos) - inc.pos).length() > ir.fireRadius + ot.radius) return;
                    burns.push_back({o, inc.subject});
                });
        }
        if (inc.state > 0) {   // its vehicle was wrecked, stolen or gave up: the call is open again
            const TrafficCar* c = carOf(st, inc.responder);
            if (!c || c->call != inc.id) { inc.state = 0; inc.responder = kNullEntity; }
        }
        if (over) {
            if (inc.kind != kIncidentCrime && (inc.kind != kIncidentWreck || (living(w, inc.subject) && w.get<Unit>(inc.subject).type == ir.wreck))) silentRemove(sim, inc.subject);
            st.incidents.erase(st.incidents.begin() + long(i));
            continue;
        }
        ++i;
    }
    std::sort(burns.begin(), burns.end(), [](const Burn& a, const Burn& b) { return a.e.index < b.e.index; });
    for (size_t i = 0; i < burns.size(); ++i)
        if (i == 0 || burns[i].e != burns[i - 1].e) m_->dealDamage(sim, burns[i].e, ir.fireDamage / 2, 0, burns[i].by, 0);

    // 4) dispatch: the first free vehicle of a service that answers this kind. A wreck waits for the fire to be out.
    if (!slow) return;
    for (size_t i = 0; i < st.incidents.size(); ++i) {
        if (st.incidents[i].state != 0) continue;
        const TrafficIncident inc = st.incidents[i];
        if (inc.kind == kIncidentWreck) {
            bool burning = false;
            for (auto& f : st.incidents) burning = burning || (f.kind == kIncidentFire && (f.pos - inc.pos).lengthSq() < Fixed::fromInt(9));
            if (burning) continue;
        }
        for (size_t si = 0; si < rules_.services.size(); ++si) {
            if (rules_.services[si].answers != inc.kind) continue;
            int busy = 0;
            for (auto& c : st.cars) busy += c.service == int(si) && c.task != kTaskNone && !(c.task == kTaskHome && c.call == 0 && c.crew.empty());
            if (busy >= rules_.services[si].max) continue;
            if (dispatch(sim, st, st.incidents[i], int(si))) break;
        }
    }
}

bool Traffic::dispatch(Simulation& sim, TrafficStore& st, TrafficIncident& inc, int si) {
    World& w = sim.world();
    GameState& gs = m_->state(sim);
    const TrafficService& sv = rules_.services[size_t(si)];
    int32_t destPiece; Fixed destAlong;
    if (!roadSpot(inc.pos, 10, destPiece, destAlong)) return false;
    // Reuse a free returning crew before allocating another station vehicle.
    // CrewBack is deliberately ineligible: everyone must board first.
    std::vector<std::pair<Fixed, Entity>> availableCrews;
    for (const auto& c : st.cars) {
        if (c.service != si || c.call || !c.crew.empty() ||
            (c.task != kTaskHome && c.task != kTaskNone) || !living(w,c.e)) continue;
        const auto* dc=m_->driving() ? m_->driving()->car(sim,c.e) : nullptr;
        if (!dc || dc->driver.valid() || (w.get<Unit>(c.e).flags & kUnitOffMap)) continue;
        const Fixed distance=(v2(w.get<Transform>(c.e).pos)-inc.pos).lengthSq();
        if(distance<=sv.range*sv.range) availableCrews.push_back({distance,c.e});
    }
    std::sort(availableCrews.begin(),availableCrews.end(),[](const auto& a,const auto& b){
        return a.first!=b.first ? a.first<b.first : a.second.index<b.second.index;
    });
    for(const auto& candidate:availableCrews) {
        auto* c=carOf(st,candidate.second);
        const TrafficCar previous=*c;
        c->task=kTaskGoing;c->call=inc.id;c->destPiece=destPiece;c->destAlong=destAlong;
        c->kind=sv.siren?kTrafficEmergency:kTrafficCar;
        const auto f=w.get<Unit>(c->e).facing;
        if(!reroute(sim,*c,v2(w.get<Transform>(c->e).pos),{f.x,f.z})) {*c=previous;continue;}
        c->stopped=0;c->lostSince=0;c->reverseUntil=0;
        inc.state=1;inc.responder=c->e;inc.assigned=sim.tick();
        gs.trig.vars["dispatch_reused"]=gs.trig.vars["dispatch_reused"]+Fixed::fromInt(1);
        return true;
    }
    const Station* from = nullptr;
    Fixed best{};
    for (auto& s : stations_[size_t(si)]) {
        const auto& piece=net_.pieces[size_t(s.piece)];
        const Vec2 gate=tl::trafficLanePoint(net_,piece,s.dir,0,s.along);
        const Vec2 direction=tl::trafficDetail::axisDir(piece.axis,s.dir);
        bool available=true;
        gs.hash.query(v3(gate),Fixed::fromInt(5),[&](Entity o){
            const auto* u=w.tryGet<Unit>(o);
            if(!u||u->hp.raw<=0||(u->flags&kUnitOffMap)||m_->rules().types[u->type].structure)return;
            const Vec2 rel=v2(w.get<Transform>(o).pos)-gate;
            if(fxAbs(tl::trafficDetail::cross(direction,rel))<Fixed::fromRatio(7,10)&&fxAbs(rel.dot(direction))<Fixed::fromInt(3)+m_->rules().types[u->type].radius)available=false;
        });
        if(!available)continue;
        const Fixed d = (s.pos - inc.pos).lengthSq();
        if (d <= sv.range * sv.range && (!from || d < best)) { from = &s; best = d; }
    }
    Entity e = kNullEntity;
    if (from) {   // out of the station gate
        const auto& P = net_.pieces[size_t(from->piece)];
        const uint16_t type = pick(sim, sv.types);
        const Vec2 pos = tl::trafficLanePoint(net_, P, from->dir, 0, from->along), dir = tl::trafficDetail::axisDir(P.axis, from->dir);
        const Fixed body = m_->rules().types[type].radius;
        bool clear = true;
        gs.hash.query(v3(pos), Fixed::fromInt(3) + body, [&](Entity o) {
            auto* ou = w.tryGet<Unit>(o);
            if (!ou || ou->hp.raw <= 0 || (ou->flags & kUnitOffMap) || m_->rules().types[ou->type].structure) return;
            const Vec2 rel = v2(w.get<Transform>(o).pos) - pos;
            if (fxAbs(tl::trafficDetail::cross(dir, rel)) < Fixed::fromRatio(7, 10) && fxAbs(rel.dot(dir)) < Fixed::fromRatio(3, 2) + body + m_->rules().types[ou->type].radius) clear = false;
        });
        if (!clear) return false;   // the gate is blocked: next time
        e = m_->spawn(sim, type, sv.owner, v3(pos));
        if (!e.valid()) return false;
        w.get<Unit>(e).facing = v3(dir);
        TrafficCar c;
        c.e = e;
        c.cruise = sv.speedMin + (sv.speedMax - sv.speedMin) * sim.rng().unit();
        c.tailPiece = from->piece; c.tailDir = from->dir; c.tailLane = 0;
        c.route.push_back({pos, -1, 0, 0});
        c.route.push_back({tl::trafficLanePoint(net_, P, from->dir, 0, tl::trafficPieceEnd(P, from->dir)), int32_t(P.junction[from->dir > 0 ? 1 : 0]), uint8_t(P.axis), 0});
        c.wp = 1;
        c.service = int8_t(si);
        c.homePiece = from->piece; c.homeAlong = from->along;
        m_->driving()->adopt(sim, e, Fixed{});
        auto it = std::lower_bound(st.cars.begin(), st.cars.end(), e.index, [](const TrafficCar& x, uint32_t i) { return x.e.index < i; });
        st.cars.insert(it, std::move(c));
    } else {   // Services without authored stations may enter from the map edge.
        if (!stations_[size_t(si)].empty()) return false;
        const std::vector<Vec2> around{inc.pos};
        if (!spawnCar(sim, st, around, rules_.spawnMin, kTrafficCar, si, &e)) return false;
    }
    TrafficCar* c = carOf(st, e);
    if (!c) return false;
    c->kind = sv.siren ? kTrafficEmergency : kTrafficCar;
    c->task = kTaskGoing; c->call = inc.id;
    c->destPiece = destPiece; c->destAlong = destAlong;
    inc.state = 1; inc.responder = e; inc.assigned = sim.tick();
    return true;
}

// ================================================================= the vehicles on call

void Traffic::dropCall(Simulation& sim, TrafficStore& st, TrafficCar& c) {
    for (Entity m : c.crew) silentRemove(sim, m);
    c.crew.clear();
    if (TrafficIncident* inc = incidentOf(st, c.call); inc && inc->responder == c.e) { inc->state = 0; inc->responder = kNullEntity; }
    c.call = 0; c.task = kTaskNone;
}

// The work is done (or cannot be): the crew walks back, then the vehicle goes home.
void Traffic::finish(Simulation& sim, TrafficStore& st, TrafficCar& c, bool done) {
    World& w = sim.world();
    if (!done) if (TrafficIncident* inc = incidentOf(st, c.call); inc && inc->responder == c.e) { inc->state = 0; inc->responder = kNullEntity; }
    c.call = 0;
    c.crew.erase(std::remove_if(c.crew.begin(), c.crew.end(), [&](Entity m) { return !living(w, m); }), c.crew.end());
    if (!c.crew.empty()) {
        Order back; back.type = OrderType::Move; back.point = w.get<Transform>(c.e).pos;
        for (Entity m : c.crew) m_->issue(sim, m, back, false);
        c.task = kTaskCrewBack;
        c.taskUntil = sim.tick() + sim.tickRate() * 12;
        return;
    }
    const Vec2 pos = v2(w.get<Transform>(c.e).pos);
    const Vec3 f = w.get<Unit>(c.e).facing;
    if (c.homePiece >= 0) {
        c.destPiece = c.homePiece; c.destAlong = c.homeAlong;
        c.task = kTaskHome;
        c.taskUntil = sim.tick() + rules_.incidents.timeoutTicks;
    } else {   // came from out of sight: leaves the same way, as ordinary traffic
        c.destPiece = -1; c.task = kTaskNone; c.service = -1; c.kind = kTrafficCar;
    }
    reroute(sim, c, pos, Vec2{f.x, f.z});
}

// What the service does to the incident it worked on.
void Traffic::resolve(Simulation& sim, TrafficStore& st, TrafficIncident& inc) {
    World& w = sim.world();
    const TrafficIncidentRules& ir = rules_.incidents;
    if (inc.kind == kIncidentInjured) {   // to the hospital the ambulance came from; treated there, then back on his feet
        if (const TrafficCar* c = carOf(st, inc.responder); c && c->homePiece >= 0 && inc.victim != UINT16_MAX && !inc.fatal && st.patients.size() < 256)
            for (auto& s : stations_[size_t(c->service)])
                if (s.piece == c->homePiece && s.along == c->homeAlong) { st.patients.push_back({inc.victim, s.pos, sim.tick() + ir.treatTicks, inc.citizen}); break; }
        silentRemove(sim, inc.subject);
    } else if (inc.kind == kIncidentFire) silentRemove(sim, inc.subject);
    else if (inc.kind == kIncidentWreck && living(w, inc.subject)) {
        const DrivingCar* dc = m_->driving() ? m_->driving()->car(sim, inc.subject) : nullptr;
        if (!(dc && dc->driver.valid())) {   // somebody got back in: it stays
            const uint16_t type = w.get<Unit>(inc.subject).type;
            const uint8_t owner = w.get<Owner>(inc.subject).player;
            silentRemove(sim, inc.subject);
            if (!lots_.empty()) {   // to the lot: rows of four next to the nearest lot object
                Vec2 lot = lots_[0];
                for (auto& l : lots_) if ((l - inc.pos).lengthSq() < (lot - inc.pos).lengthSq()) lot = l;
                const uint32_t n = st.towed++ % 16;
                const Vec3 spot{lot.x + Fixed::fromRatio(int(n % 4) * 13 - 20, 10), Fixed{}, lot.y + Fixed::fromRatio(15 + int(n / 4) * 9, 10)};
                if (m_->state(sim).grid.passableAt(spot, kLayerGround)) m_->spawn(sim, type, owner, spot);
            }
        }
    }
    const uint32_t id = inc.id;
    for (size_t i = 0; i < st.incidents.size(); ++i) if (st.incidents[i].id == id) { st.incidents.erase(st.incidents.begin() + long(i)); break; }
}

void Traffic::responders(Simulation& sim, TrafficStore& st) {
    World& w = sim.world();
    GameState& gs = m_->state(sim);
    Driving* drv = m_->driving();
    for (auto& c : st.cars) {
        if (c.task == kTaskNone || c.task == kTaskTrip || c.task == 250) continue;
        const TrafficService& sv = rules_.services[size_t(c.service)];
        TrafficIncident* inc = incidentOf(st, c.call);
        const Vec3 pos3 = w.get<Transform>(c.e).pos;
        const Vec2 pos = v2(pos3);
        const bool slowNow = fxAbs(drv->speed(sim, c.e)) < Fixed::fromRatio(3, 5);
        auto atAddress = [&]() {
            const int cell = gs.grid.cellOf(pos3);
            if (cell < 0 || net_.pieceAt[size_t(cell)] != c.destPiece || c.destPiece < 0) return false;
            return fxAbs(c.destAlong - tl::trafficAlongOf(net_.pieces[size_t(c.destPiece)], pos)) < Fixed::fromRatio(7, 10);
        };
        c.crew.erase(std::remove_if(c.crew.begin(), c.crew.end(), [&](Entity m) { return !living(w, m); }), c.crew.end());
        if (sv.carry) {     // rule traffic.services.<id>.carry: the crew carries the patient and delivers him
        #include "KingstonAmbulance.inc"
        }
        if (sv.custody) {   // rule traffic.services.<id>.custody: officers detain instead of fighting
        #include "KingstonPoliceResponse.inc"
        }
        switch (c.task) {
        case kTaskGoing: {
            if (!inc) { finish(sim, st, c, false); break; }   // it is over already
            if (sim.tick() - inc->assigned > rules_.incidents.timeoutTicks) { finish(sim, st, c, false); break; }   // cannot get there
            if (!slowNow || !(atAddress() || (pos - inc->pos).lengthSq() < (sv.answers==kIncidentFire ? Fixed::fromInt(16) : Fixed::fromRatio(9, 4)))) break;
            inc->state = 2;
            inc->assigned = sim.tick();
            if (sv.crew == UINT16_MAX) { c.task = kTaskWorking; c.taskUntil = sim.tick() + sv.workTicks; break; }
            // the crew gets out on the kerb side and goes to the scene
            const Vec3 f = w.get<Unit>(c.e).facing;
            Vec3 right{-f.z, Fixed{}, f.x};
            if ((v3(inc->pos)-pos3).dot(right).raw<0) right=right*Fixed::fromInt(-1);
            // Outside the separation body, on the patient's side: a straight path
            // from the far-side door otherwise gets pinned against the ambulance.
            const Fixed exitDistance=m_->rules().types[w.get<Unit>(c.e).type].radius+m_->rules().types[sv.crew].radius+Fixed::fromRatio(1,5);
            for (int i = 0; i < sv.crewCount; ++i) {
                Vec3 spot = pos3 + right * (sv.answers==kIncidentFire ? fxMax(exitDistance,Fixed::fromRatio(9,10)) : exitDistance) + f * Fixed::fromRatio(i * 2 - (sv.crewCount - 1), 5);
                if (!gs.grid.passableAt(spot, kLayerGround)) spot = pos3 - right * exitDistance;
                if (!gs.grid.passableAt(spot, kLayerGround)) continue;
                const Entity m = m_->spawn(sim, sv.crew, sv.owner, spot);
                if (!m.valid()) continue;
                Order go; go.point = v3(inc->pos);
                go.type = sv.answers == kIncidentCrime ? OrderType::AttackMove : OrderType::Move;
                m_->issue(sim, m, go, false);
                c.crew.push_back(m);
                gs.trig.vars["service_exit_"+std::to_string(m.packed())]=Fixed::fromInt(sim.tick());
            }
            c.task = c.crew.empty() ? kTaskWorking : kTaskCrewOut;
            c.taskUntil = sim.tick() + (c.crew.empty() ? sv.workTicks : sim.tickRate() * 15);
            break;
        }
        case kTaskCrewOut: {
            if (!inc || c.crew.empty()) { finish(sim, st, c, false); break; }
            const Fixed reach = sv.answers == kIncidentCrime ? Fixed::fromInt(3) : Fixed::fromRatio(9, 10);
            bool there = sv.answers != kIncidentFire && sv.answers != kIncidentInjured && sim.tick() >= c.taskUntil;
            for (Entity m : c.crew) there = there || (v2(w.get<Transform>(m).pos) - inc->pos).lengthSq() < reach * reach;
            if (sv.answers == kIncidentFire) break; // Project fire action controller owns hose/nozzle readiness.
            if (there) { c.task = kTaskWorking; c.taskUntil = sim.tick() + sv.workTicks; }
            break;
        }
        case kTaskWorking: {
            if (!inc || (sv.crew != UINT16_MAX && c.crew.empty())) { finish(sim, st, c, false); break; }
            if (sv.answers == kIncidentCrime && sim.tick() % sim.tickRate() == 0 && living(w, inc->offender) && !(w.get<Unit>(inc->offender).flags & kUnitOffMap)) {
                // the offender is still around: they go for him, and stay as long as it lasts
                const Vec3 op = w.get<Transform>(inc->offender).pos;
                if ((v2(op) - inc->pos).lengthSq() < Fixed::fromInt(15 * 15)) {
                    Order at; at.type = OrderType::Attack; at.target = inc->offender; at.point = op;
                    for (Entity m : c.crew) if (auto* o = w.tryGet<Orders>(m); o && o->queue.empty()) m_->issue(sim, m, at, false);
                    if (sim.tick() - inc->assigned < sv.maxWorkTicks) c.taskUntil = std::max(c.taskUntil, sim.tick() + sim.tickRate() * 3);
                }
            }
            if (sim.tick() < c.taskUntil) break;
            resolve(sim, st, *inc);
            finish(sim, st, c, true);
            break;
        }
        case kTaskCrewBack: {
            if (gs.trig.vars["service_return_"+std::to_string(c.e.packed())].raw>0) break; // project door boarding owns completion
            if (sv.answers == kIncidentFire && m_->state(sim).trig.vars["fire_service_"+std::to_string(c.e.packed())+"_phase"].toInt()==4) break;
            bool back = sv.answers != kIncidentFire && sim.tick() >= c.taskUntil;
            if (!back) { back = true; for (Entity m : c.crew) back = back && (v2(w.get<Transform>(m).pos) - pos).lengthSq() < Fixed::fromRatio(36, 25); }
            if (!back) break;
            for (Entity m : c.crew) silentRemove(sim, m);
            c.crew.clear();
            finish(sim, st, c, true);
            break;
        }
        case kTaskHome:
            if (slowNow && atAddress()) silentRemove(sim, c.e);   // through the gate
            else if (sim.tick() >= c.taskUntil) { c.homePiece = -1; finish(sim, st, c, true); }
            break;
        default: break;
        }
    }
}

} // namespace aiverse::game
