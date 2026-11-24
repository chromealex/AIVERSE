// Same flight component/system + collision policy in two independent genre
// modules, with canonical impact events, COW snapshot and replay parity.
// No GameCore, actor::ActorHost or genre-specific Actor dependency.
#include "engine/mechanics/EcsProjectileSystems.h"
#include "tests/AlwaysCheck.h"
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>
using namespace aiverse;
namespace lg=aiverse::lib::mechanics;

static void checkSweep(bool condition,const char* message) {
    if(!condition)throw std::runtime_error(message);
}
static void exerciseSharedSweptContacts() {
    struct Candidate {uint32_t id;world::Bounds hitbox;};
    const Vec3 origin{0_fx,1_fx,0_fx}, delta{0_fx,0_fx,10_fx};
    const Vec3 noExtent{};
    const auto query=[&](const std::vector<Candidate>& targets,Vec3 extent) {
        return lg::selectSweptProjectileContact(
            targets,origin,delta,extent,
            [](const Candidate& t){return t.hitbox;},
            [](const Candidate& a,const Candidate& b){return a.id<b.id;});
    };
    // Dynamic candidates can arrive in an arbitrary order; exact-distance
    // ties select the smallest semantic target ID, matching FPS projectile
    // program contact arbitration.
    const std::vector<Candidate> tied{
        {9,{{-1_fx,0_fx,3_fx},{1_fx,2_fx,4_fx}}},
        {4,{{-1_fx,0_fx,3_fx},{1_fx,2_fx,4_fx}}},
        {12,{{-1_fx,0_fx,5_fx},{1_fx,2_fx,6_fx}}}
    };
    // SpatialWorld::intersect computes (lo-start)/delta using Fixed::operator/
    // (truncate), not Fixed::fromRatio (round-to-nearest).
    const auto nearest=query(tied,noExtent);
    checkSweep(nearest && nearest.target->id==4 &&
               nearest.fraction==3_fx/10_fx,
               "shared projectile sweep lost nearest-hit tie semantics");
    const std::vector<Candidate> endpoint{
        {18,{{-1_fx,0_fx,10_fx},{1_fx,2_fx,11_fx}}}
    };
    const auto atEnd=query(endpoint,noExtent);
    checkSweep(atEnd && atEnd.target->id==18 &&
               atEnd.fraction==1_fx,
               "projectile endpoint contact must be accepted");
    const auto expanded=query(tied,{Fixed{},Fixed{},1_fx});
    checkSweep(expanded && expanded.target->id==4 &&
               expanded.fraction==2_fx/10_fx,
               "swept radius did not expand the target hitbox");
    const std::vector<Candidate> missed{
        {21,{{3_fx,0_fx,3_fx},{5_fx,2_fx,4_fx}}}
    };
    checkSweep(!query(missed,noExtent),
               "nonoverlapping target incorrectly hit by shared sweep");
}

// Reproduce the exact legacy FPS grenade bounce expression as an independent
// reference (including fixed-point division, collision epsilon and velocity
// update-before-cast). Both collision and free flight must match byte-for-byte.
static void exerciseSharedBounceMotion() {
    world::SpatialWorld collision;
    checkSweep(collision.add({51,{{-10_fx,-2_fx,-10_fx},
                                   {10_fx,0_fx,10_fx}}}),
               "bounce fixture ground collider invalid");
    const Fixed dt=Fixed::fromRatio(1,20);
    struct Case {Vec3 position,velocity;};
    const std::vector<Case> cases{
        {{0_fx,1_fx,0_fx},{0_fx,-20_fx,0_fx}},
        {{0_fx,1_fx,0_fx},{2_fx,4_fx,1_fx}},
        {{0_fx,Fixed::fromRatio(1,10),0_fx},{1_fx,-3_fx,0_fx}}
    };
    for(const Case& initial:cases) {
        Vec3 expectedPosition=initial.position;
        Vec3 expectedVelocity=initial.velocity;
        expectedVelocity.y-=10_fx*dt;
        const auto delta=expectedVelocity*dt;
        const Vec3 half{Fixed::fromRatio(1,10),
                        Fixed::fromRatio(1,10),
                        Fixed::fromRatio(1,10)};
        const auto hit=collision.cast(expectedPosition,delta,half);
        if(hit.hit) {
            expectedPosition+=delta*fxMax(Fixed{},
                hit.fraction-Fixed::fromRatio(1,100));
            expectedVelocity=(expectedVelocity-hit.normal*
                expectedVelocity.dot(hit.normal)*2_fx)/2_fx;
        } else expectedPosition+=delta;
        Vec3 actualPosition=initial.position;
        Vec3 actualVelocity=initial.velocity;
        lg::stepBouncingProjectile(actualPosition,actualVelocity,collision,dt);
        checkSweep(actualPosition==expectedPosition &&
                   actualVelocity==expectedVelocity,
                   "shared bounce physics diverged from legacy FPS formula");
    }
}

struct ShooterWeaponSource {int32_t magazine=20;};
struct StrategyTurretSource {int32_t range=200;};
struct ImpactLog {
    std::vector<uint32_t> shots;
    std::vector<uint32_t> walls;
};

template<class Source>
class Host final : public GameModule {
    world::SpatialWorld collision_;
public:
    std::string name() const override {return "cross-genre-projectiles";}
    void setup(Simulation& sim,const Json&,const Json&) override {
        World& w=sim.world();
        // The same collision provider is used by either genre.
        AIVERSE_TEST_CHECK(collision_.add({42,{{-2_fx,0_fx,5_fx},{2_fx,3_fx,6_fx}}}));
        w.setResource<ImpactLog>();
        Entity owner=w.create();
        w.add<Source>(owner);
        for(int i=0;i<4;++i) {
            Entity projectile=w.create();
            lg::ProjectileFlightComponent data;
            data.config.gravity=Fixed{};
            data.config.lifetime=60;
            data.config.maxBounces=0;
            data.config.radius=Fixed::fromRatio(1,20);
            data.state.position={Fixed::fromInt(i*2-3),1_fx,Fixed{}};
            data.state.velocity={Fixed{},Fixed{},20_fx};
            data.owner=owner;
            data.shotId=uint32_t(100+i);
            w.add<lg::ProjectileFlightComponent>(projectile,data);
        }
        lg::addProjectileFlightSystem(sim,"universal.projectile.flight",
            [this](Simulation&) -> const world::SpatialWorld& {return collision_;},
            [](Simulation& current,const lg::ProjectileImpactEvent& event) {
                auto* log=current.world().resource<ImpactLog>();
                AIVERSE_TEST_CHECK(log);
                log->shots.push_back(event.shotId);
                log->walls.push_back(event.contact.collider);
            });
    }
    void applyCommand(Simulation&,const Command&) override {}
    void hashState(Simulation& sim,Hasher& h) override {
        const World& w=sim.world();
        w.eachExistingConst<lg::ProjectileFlightComponent>(
            [&](Entity e,const lg::ProjectileFlightComponent& v) {
                if(!w.alive(e))return;
                h.u64(e.packed());
                h.u32(v.shotId);
                h.i64(v.state.position.x.raw);
                h.i64(v.state.position.y.raw);
                h.i64(v.state.position.z.raw);
                h.i64(v.state.velocity.z.raw);
                h.u32(v.state.age);
            });
        const auto* log=w.resourceConst<ImpactLog>();
        AIVERSE_TEST_CHECK(log);
        for(size_t i=0;i<log->shots.size();++i) {
            h.u32(log->shots[i]);
            h.u32(log->walls[i]);
        }
    }
    int result(Simulation&) override{return 0;}
};

template<class Source>
uint64_t test(bool cow) {
    Simulation sim(SimConfig{71,20,0,true},std::make_unique<Host<Source>>());
    sim.setup(Json::object(),Json::object());
    if(cow)sim.world().enableCowSnapshots(true);
    for(int i=0;i<3;++i)sim.step();
    SimSnapshot checkpoint;
    sim.saveState(checkpoint);
    for(int i=0;i<7;++i)sim.step();
    const auto expected=sim.checksum();
    const auto* log=sim.world().resourceConst<ImpactLog>();
    AIVERSE_TEST_CHECK(log && log->shots.size()==2);
    AIVERSE_TEST_CHECK(log->shots[0]==101 && log->shots[1]==102);
    AIVERSE_TEST_CHECK(log->walls[0]==42 && log->walls[1]==42);
    sim.loadState(checkpoint);
    for(int i=0;i<7;++i)sim.step();
    AIVERSE_TEST_CHECK(sim.checksum()==expected);
    return expected;
}
int main(){
    exerciseSharedSweptContacts();
    exerciseSharedBounceMotion();
    const uint64_t shooter=test<ShooterWeaponSource>(false);
    AIVERSE_TEST_CHECK(test<StrategyTurretSource>(false)==shooter);
    AIVERSE_TEST_CHECK(test<ShooterWeaponSource>(true)==shooter);
    AIVERSE_TEST_CHECK(test<StrategyTurretSource>(true)==shooter);
    std::cout<<"PASS shared projectile ECS system in shooter and strategy modules, impact order and COW replay\n";
}
