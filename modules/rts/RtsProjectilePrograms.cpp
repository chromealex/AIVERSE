// Ordered projectile programs on flat maps and planetary surfaces. All structural ECS edits occur outside world.each.
#include "RtsInternal.h"
#include "../../engine/core/FixedMath.h"
#include <algorithm>
#include <optional>

namespace aiverse::game {
using namespace detail;
namespace {
Vec3 direction(Vec3 v) { v.y = Fixed{}; return fxNormalize(v); }


// Distance along a swept disk, including contacts whose centres are beyond the endpoint.
std::optional<Fixed> diskHit(Vec3 from, Vec3 dir, Fixed length, Vec3 centre, Fixed radius, bool spatial=false) {
    auto offset = spatial ? centre-from : flat(centre - from);
    Fixed along = offset.dot(dir), square = offset.dot(offset), rr = radius * radius;
    if (square <= rr) return Fixed{};
    Fixed perpendicular = fxMax(Fixed{}, square - along * along);
    if (perpendicular > rr) return {};
    Fixed enter = along - fxSqrt(rr - perpendicular);
    if (enter < Fixed{} || enter > length) return {};
    return enter;
}
// Slab test against an expanded terrain cell; conservative square footprint at corners.
std::optional<Fixed> boxHit(Vec3 from, Vec3 delta, int x, int z, Fixed radius) {
    Fixed enter{}, leave = kOne;
    auto axis = [&](Fixed p, Fixed d, int cell) {
        Fixed lo = Fixed::fromInt(cell) - radius, hi = Fixed::fromInt(cell + 1) + radius;
        if (!d.raw) return p >= lo && p <= hi;
        Fixed a = (lo-p)/d, b = (hi-p)/d;
        if (a > b) std::swap(a,b);
        enter = fxMax(enter,a); leave = fxMin(leave,b);
        return enter <= leave;
    };
    if (!axis(from.x,delta.x,x) || !axis(from.z,delta.z,z)) return {};
    return enter;
}
std::optional<Fixed> wallHit(const MapGrid& g, Vec3 from, Vec3 dir, Fixed length, Fixed radius) {
    std::optional<Fixed> best;
    // At most one world unit per broad-phase slice; no bounding rectangle quadratic in travel.
    Fixed offset{};
    while (offset < length) {
        Fixed part = fxMin(kOne,length-offset);
        Vec3 a = from + dir*offset, delta = dir*part, b = a+delta;
        int x0 = int((fxMin(a.x,b.x)-radius).floorInt()), x1 = int((fxMax(a.x,b.x)+radius).floorInt());
        int z0 = int((fxMin(a.z,b.z)-radius).floorInt()), z1 = int((fxMax(a.z,b.z)+radius).floorInt());
        for (int z=z0; z<=z1; ++z) for (int x=x0; x<=x1; ++x) {
            bool solid = !g.inside(x,z);
            if (!solid) {
                auto i = size_t(g.index(x,z));
                solid = !(g.flags[i] & (kCellGround|kCellWater)) || (!g.dig.empty() && g.dig[i]);
            }
            if (!solid) continue;
            if (auto t=boxHit(a,delta,x,z,radius)) {
                Fixed d=offset+part*(*t);
                if (!best || d<*best) best=d;
            }
        }
        if (best) return best;
        offset+=part;
    }
    return best;
}
}

const ProjectileFlight* RtsModule::projectileFlight(const Projectile& p) const {
    if (p.effList >= 0) {
        if (size_t(p.effList)>=rules_.payloads.size() || p.effIndex<0 || size_t(p.effIndex)>=rules_.payloads[size_t(p.effList)].size()) return nullptr;
        return &rules_.payloads[size_t(p.effList)][size_t(p.effIndex)].delivery.flight;
    }
    if (p.weapon <= -2) {   // bullet perks (rules "progression"): the weapon's program + the shooter's bullet modifiers
        const auto* pg = rules_.progression.get();
        const int v = -2 - p.weapon;
        if (!pg || size_t(v) >= pg->variants.size() || pg->variants[size_t(v)].type != p.sourceType) return nullptr;
        return &pg->variants[size_t(v)].flight;
    }
    if (p.sourceType>=rules_.types.size() || p.weapon<0 || size_t(p.weapon)>=rules_.types[p.sourceType].weapons.size()) return nullptr;
    return &rules_.types[p.sourceType].weapons[size_t(p.weapon)].flight;
}
const WeaponDef* RtsModule::projectileWeapon(const Projectile& p) const {
    if (p.sourceType >= rules_.types.size()) return nullptr;
    int weapon = p.weapon;
    if (weapon <= -2) {
        const auto* pg = rules_.progression.get();
        const int v = -2 - weapon;
        if (!pg || size_t(v) >= pg->variants.size()) return nullptr;
        weapon = pg->variants[size_t(v)].weapon;
    }
    const auto& ws = rules_.types[p.sourceType].weapons;
    return weapon >= 0 && size_t(weapon) < ws.size() ? &ws[size_t(weapon)] : nullptr;
}
void RtsModule::bakeProjectileProgram(Projectile& p, const ProjectileFlight& f) const {
    projectile::bake(p.program,p.speed,f);
}
void RtsModule::initProjectileProgram(Simulation& sim, Projectile& p, Vec3 start) const {
    const auto* f=projectileFlight(p);
    if (!f || !f->enabled) return;
    p.start=start;
    const auto* surface=sim.world().resource<GameState>()->surface.get();
    Vec3 toward=surface ? fxNormalize(surface->tangent(start,p.aim-start)) : direction(p.aim-start);
    projectile::initialize(p.program,p.speed,*f,toward);
    if(p.effList>=0) {
        const auto& d=rules_.payloads[size_t(p.effList)][size_t(p.effIndex)].delivery;
        if(d.arc) {
            const Fixed distance=surface ? surface->groundDist(start,p.aim) : dist2D(start,p.aim);
            const Fixed duration=fxMax(Fixed::fromRatio(1,10),distance/p.speed);
            p.program.gravity=8_fx*d.arcHeight/(duration*duration);
            p.program.verticalSpeed=4_fx*d.arcHeight/duration;
            p.program.ballistic=true;
        }
    }
}

void RtsModule::sysProjectilePrograms(Simulation& sim, const std::vector<Entity>& projectiles) {
    if (projectiles.empty()) return;
    auto& w=sim.world(); auto& gs=state(sim);
    struct Spawn { Projectile p; Vec3 at; };
    std::vector<Spawn> spawns;
    const auto* surface=gs.surface.get();
    Fixed maxRadius=Fixed::fromRatio(6,10);
    for (const auto& t:rules_.types) maxRadius=fxMax(maxRadius,t.radius);
    for (Entity entity:projectiles) {
        if (!w.alive(entity) || !w.has<Projectile>(entity)) continue;
        Projectile p=w.get<Projectile>(entity); // effects/spawns may reallocate every component pool
        const auto* def=projectileFlight(p);
        if (!def || !def->enabled || p.program.remaining.size()!=def->modifiers.size()) { w.destroyLater(entity); continue; }
        const auto& f=*def; auto& s=p.program;
        Vec3 position=w.get<Transform>(entity).pos, previous=position;
        const bool spatial=surface || s.ballistic;
        auto up=[&](Vec3 at){return surface ? surface->up(at) : Vec3{Fixed{},1_fx,Fixed{}};};
        auto toward=[&](Vec3 at,Vec3 to){return surface ? fxNormalize(surface->tangent(at,to-at)) : direction(to-at);};
        auto distance=[&](Vec3 a,Vec3 b){return surface ? surface->groundDist(a,b) : dist2D(a,b);};
        auto centreOf=[&](Entity e){return lift(sim,w.get<Transform>(e).pos,Fixed::fromRatio(6,10));};
        auto endpoint=[&](Vec3 at,Fixed dt){
            Vec3 next=at+projectile::displacement(s,p.speed,up(at),dt);
            if(surface) {
                const auto body=surface->bodyAt(at);
                const auto altitude=surface->altitude(at)+s.verticalSpeed*dt-s.gravity*dt*dt/2;
                next=surface->toShell(next,surface->bodies[size_t(body)].radius+altitude);
            }
            return next;
        };
        auto transport=[&](Vec3 at,Fixed dt){
            s.verticalSpeed-=s.gravity*dt;
            if(surface)s.direction=fxNormalize(surface->tangent(at,s.direction));
        };
        auto terrain=[&](Vec3 at,Vec3 dir,Fixed length)->std::optional<Fixed>{
            if(!f.blockTerrain)return {};
            if(!surface&&!s.ballistic)return wallHit(gs.grid,at,dir,length,s.radius);
            auto blocked=[&](Vec3 q){
                if(surface) {
                    if(surface->bodyAt(q)!=surface->bodyAt(previous))return true;
                    const int c=surface->cellAt(q);
                    if(c<0)return true;
                    if(!(gs.grid.flags[size_t(c)]&(kCellGround|kCellWater)) || (!gs.grid.dig.empty()&&gs.grid.dig[size_t(c)]))return true;
                    return surface->altitude(q)-s.radius<=surface->heightAt(q);
                }
                return q.y<=s.radius+(gs.grid.hf?gs.grid.surfaceHeight(q.x,q.z):Fixed{});
            };
            auto wall=surface ? std::optional<Fixed>{} : wallHit(gs.grid,at,dir,length,s.radius);
            if(blocked(at))return Fixed{};
            if(!blocked(at+dir*length))return wall;
            Fixed lo{},hi=length;
            for(int n=0;n<16&&hi.raw-lo.raw>1;++n){auto mid=(lo+hi)/2;if(blocked(at+dir*mid))hi=mid;else lo=mid;}
            return wall ? fxMin(*wall,hi) : hi;
        };
        bool alive=true;
        if (s.lifetimeLeft<=Fixed{} || s.distanceLeft<=Fixed{} || s.direction.lengthSq()==Fixed{}) alive=false;
        auto eligible = [&](Entity target) {
            if (target==p.source || !w.alive(target) || !w.has<Unit>(target) || !w.has<Transform>(target) || !w.has<Owner>(target) || seen(s,target)) return false;
            if(surface && surface->bodyAt(w.get<Transform>(target).pos)!=surface->bodyAt(position))return false;
            const auto& u=w.get<Unit>(target);
            if (u.hp<=Fixed{}) return false;
            if (p.effList>=0) return filterOk(sim,rules_.payloads[size_t(p.effList)][size_t(p.effIndex)].filter,target,p.owner,p.source);
            const uint8_t owner=w.get<Owner>(target).player;
            // kLayerHitsNeutral (arsenal free fire): passive neutral bodies (parked cars) stop the shot too
            const bool foe=hostile(sim,p.owner,owner) || ((p.layers&kLayerHitsNeutral) && owner==0 && !rules_.types[u.type].invulnerable);
            return foe && layerHit(p.layers,rules_.types[u.type].layer);
        };
        // Split preserves the source prefix; only children activate the remaining suffix.
        auto split = [&](size_t i, Vec3 at) {
            projectile::split(s,p.speed,f,i,up(at),[&](projectile::Branch branch){
                Projectile child=p;child.program=std::move(branch.state);child.speed=branch.speed;
                child.target=kNullEntity;child.start=at;child.aim=at+child.program.direction*child.program.distanceLeft;
                spawns.push_back({std::move(child),at});
            });
        };
        auto contact = [&](Vec3 at) {
            auto branch=projectile::contact(s,p.speed,f,[&](size_t i){split(i,at);},[&](Fixed radius)->std::optional<projectile::Target>{
                Entity next;Fixed best=radius;
                gs.hash.query(at,radius+maxRadius,[&](Entity candidate){
                    if(!eligible(candidate))return;
                    Fixed d=distance(at,w.get<Transform>(candidate).pos);
                    if(d>radius)return;
                    // Curved shots may clear intervening terrain; actual sweeps remain authoritative.
                    if(!spatial&&terrain(at,toward(at,centreOf(candidate)),d))return;
                    if(!next.valid()||d<best||(d==best&&candidate.index<next.index)){next=candidate;best=d;}
                });
                if(!next.valid())return {};
                return projectile::Target{next,toward(at,centreOf(next))};
            });
            if(!branch)return false;
            Projectile child=p;child.program=std::move(branch->state);child.speed=branch->speed;
            child.target=branch->target;child.start=at;child.aim=centreOf(child.target);
            spawns.push_back({std::move(child),at});return true;
        };
        auto impact = [&](const Projectile& shot, Entity target, Vec3 at) {
            const auto& ps=shot.program;
            if (shot.effList>=0) {
                const auto& e=rules_.payloads[size_t(shot.effList)][size_t(shot.effIndex)];
                EffectCtx ctx; ctx.caster=shot.source; ctx.owner=shot.owner; ctx.sourceType=shot.sourceType;
                ctx.target=target; ctx.point=flat(at); ctx.origin=shot.start; ctx.scale=shot.scale*ps.damageScale;
                sim.emit({kEvImpact,shot.owner,target,shot.source,e.payload,at});
                applyPayload(sim,e.payload,ctx);
            } else {
                const WeaponDef* wd=projectileWeapon(shot);
                dealDamage(sim,target,shot.damage*ps.damageScale,shot.damageType,shot.source,shot.owner,wd);
                if (shot.onHit>=0 && w.alive(target)) {
                    EffectCtx ctx; ctx.caster=shot.source; ctx.owner=shot.owner; ctx.sourceType=shot.sourceType;
                    ctx.target=target; ctx.point=flat(at); ctx.origin=shot.start; ctx.scale=ps.damageScale;
                    applyPayload(sim,shot.onHit,ctx);
                }
                if (shot.splash>Fixed{}) {
                    std::vector<Entity> splash;
                    gs.hash.query(at,shot.splash,[&](Entity t) {
                        if (t!=target && eligible(t) && distance(at,w.get<Transform>(t).pos)<=shot.splash) splash.push_back(t);
                    });
                    std::sort(splash.begin(),splash.end(),[](Entity a,Entity b){return a.index<b.index;});
                    for (Entity t:splash) dealDamage(sim,t,shot.damage*ps.damageScale,shot.damageType,shot.source,shot.owner,wd);
                }
            }
        };
        bool spent=false;   // the flight ended at a wall or at its range without a direct hit
        Fixed timeLeft=fxMin(sim.dt(),s.lifetimeLeft);
        while (alive && timeLeft>Fixed{}) {
            // Stop exactly at the next timed Split so children start at its actual location.
            Fixed slice=projectile::sliceTime(s,p.speed,timeLeft,spatial);
            if(surface) slice=fxMin(slice,fxMax(Fixed::fromRaw(1),surface->cellSize(surface->bodyAt(position))/4/(p.speed+fxAbs(s.verticalSpeed)+1_fx)));
            for (size_t i=0;i<s.cursor;++i) {
                const auto& m=f.modifiers[i];
                if (m.kind!=ProjectileModKind::Split || !m.timed || (s.consumed & (1u<<i))) continue;
                if (s.age>=m.after) split(i,position);
                else slice=fxMin(slice,m.after-s.age);
            }
            if (f.homing && eligible(p.target)) {
                Vec3 heading=toward(position,centreOf(p.target));
                if (heading.lengthSq()>Fixed{}) s.direction=heading;
            }
            Vec3 delta=endpoint(position,slice)-position;
            Fixed fullLength=spatial?projectile::magnitude(delta):p.speed*slice,length=fxMin(s.distanceLeft,fullLength);
            if (length<=Fixed{}) {
                position+=delta;transport(position,slice);
                s.lifetimeLeft=fxMax(Fixed{},s.lifetimeLeft-slice);s.age+=slice;timeLeft-=slice;
                if(s.lifetimeLeft<=Fixed{})alive=false;
                continue;
            }
            Vec3 heading=spatial?projectile::unit(delta):s.direction;
            auto wall=terrain(position,heading,length);
            struct Hit { Fixed distance; Entity target; };
            std::vector<Hit> hits;
            Vec3 centre=position+heading*(length/2);
            gs.hash.query(centre,length/2+s.radius+maxRadius,[&](Entity t) {
                if (!eligible(t)) return;
                auto d=diskHit(position,heading,length,spatial?centreOf(t):w.get<Transform>(t).pos,s.radius+fxMax(spatial?Fixed::fromRatio(6,10):Fixed{},rules_.types[w.get<Unit>(t).type].radius),spatial);
                if (d && (!wall || *d<*wall)) hits.push_back({*d,t}); // wall wins an exact tie
            });
            std::sort(hits.begin(),hits.end(),[](const Hit& a,const Hit& b){return a.distance!=b.distance ? a.distance<b.distance : a.target.index<b.target.index;});
            // Resolve only the first contact, then re-query: a suffix may change radius/speed,
            // and an impact payload may teleport, spawn or kill another candidate.
            if (!hits.empty()) {
                const auto hit=hits.front();
                Fixed elapsed=fxMin(slice,slice*(hit.distance/fullLength));
                position+=heading*hit.distance;
                transport(position,elapsed);
                s.distanceLeft=fxMax(Fixed{},s.distanceLeft-hit.distance);
                s.lifetimeLeft=fxMax(Fixed{},s.lifetimeLeft-elapsed); s.age+=elapsed; timeLeft-=elapsed;
                if (s.hit.size()>=256) { alive=false; break; }
                s.hit.push_back(hit.target);
                Projectile shot=p;
                bool replaced=contact(position);
                impact(shot,hit.target,position);
                if (replaced) { alive=false; break; }
                bool pass=projectile::pass(s,f);
                if (!pass || s.distanceLeft<=Fixed{} || s.lifetimeLeft<=Fixed{}) { alive=false; break; }
                continue;
            }
            Fixed finish=wall ? *wall : length;
            position+=(!wall&&finish==fullLength)?delta:heading*finish;
            transport(position,slice);
            s.distanceLeft=fxMax(Fixed{},s.distanceLeft-finish);
            s.lifetimeLeft=fxMax(Fixed{},s.lifetimeLeft-slice); s.age+=slice; timeLeft-=slice;
            if (wall || s.distanceLeft<=Fixed{} || s.lifetimeLeft<=Fixed{}) { alive=false; spent=true; }
            if (alive) for (size_t i=0;i<s.cursor;++i) {
                const auto& m=f.modifiers[i];
                if (m.kind==ProjectileModKind::Split && m.timed && !(s.consumed&(1u<<i)) && s.age>=m.after) split(i,position);
            }
        }
        // free fire (arsenal, kLayerHitsNeutral): a splash weapon goes off where its flight ends (a rocket into a wall)
        if (spent && p.effList<0 && p.splash>Fixed{} && (p.layers&kLayerHitsNeutral) && p.sourceType<rules_.types.size() && p.weapon>=0) {
            const auto& wd=rules_.types[p.sourceType].weapons[size_t(p.weapon)];
            std::vector<Entity> splash;
            gs.hash.query(position,p.splash+maxRadius,[&](Entity t) {
                if (eligible(t) && distance(position,w.get<Transform>(t).pos)<=p.splash) splash.push_back(t);
            });
            std::sort(splash.begin(),splash.end(),[](Entity a,Entity b){return a.index<b.index;});
            for (Entity t:splash) dealDamage(sim,t,p.damage*s.damageScale,p.damageType,p.source,p.owner,&wd);
        }
        if (w.alive(entity) && w.has<Projectile>(entity)) {
            w.get<Projectile>(entity)=std::move(p);
            auto& tr=w.get<Transform>(entity); tr.prevPos=previous; tr.pos=position;
            if (!alive) w.destroyLater(entity);
        }
    }
    // New projectiles never get a second movement update in their birth tick.
    for (auto& spawn:spawns) {
        Entity e=w.create();
        w.add<Transform>(e,Transform{spawn.at,spawn.at,Fixed{}});
        w.add<Owner>(e,Owner{spawn.p.owner});
        w.add<Projectile>(e,std::move(spawn.p));
        const auto& p=w.get<Projectile>(e);
        sim.emit({kEvProjectileLaunch,p.owner,e,p.source,p.effList,spawn.at});
    }
}
}
