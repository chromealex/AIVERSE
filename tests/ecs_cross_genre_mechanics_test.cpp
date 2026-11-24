// Cross-genre compile/runtime contract: the exact same ECS mechanic policies
// run in two unrelated GameModule/component types with no RTS/FPS includes.
// Does NOT imply all existing GameCore/actor::ActorHost methods are universal.
#include "engine/mechanics/EcsMechanicSystems.h"
#include "engine/mechanics/Locomotion.h"
#include "engine/mechanics/Weapons.h"
#include "engine/mechanics/Match.h"
#include "tests/AlwaysCheck.h"
#include <cstdint>
#include <iostream>
#include <map>
#include <memory>
#include <string>
using namespace aiverse;
namespace lg=aiverse::lib::mechanics;

struct MechanicData {
    lg::MotionState motion;
    lg::RecoilState recoil;
};
struct ShooterCharacter {MechanicData gameplay;};
struct StrategySoldier {MechanicData gameplay;};

template<class Component>
class GenreHost final:public GameModule {
public:
    std::string name() const override{return "generic-policy-host";}
    void setup(Simulation& sim,const Json&,const Json&) override {
        World& world=sim.world();
        for(int i=0;i<32;++i) {
            Entity entity=world.create();
            world.add<Component>(entity);
        }
        world.setResource<lg::CaptureState>();
        // The SAME movement and recoil policies, regardless of the genre or
        // name/layout of the component stored in the ECS pool.
        lg::addEcsMechanicSystem<Component>(sim,"shared-locomotion-and-recoil",
            [](Simulation& s,Entity,Component& c) {
                lg::MotionConfig config;
                lg::MotionInput input;
                input.wish={1_fx,Fixed{}};
                input.grounded=true;
                const auto result=lg::stepMotion(config,c.gameplay.motion,
                                                  input,s.tick(),s.dt());
                AIVERSE_TEST_CHECK(result.has_value());
                const bool accepted=lg::stepRecoil(
                    lg::RecoilConfig{},c.gameplay.recoil,s.tick()%3==0);
                AIVERSE_TEST_CHECK(accepted);
            });
        sim.addSystem("shared-capture-objective",[](Simulation& s) {
            auto* capture=s.world().resource<lg::CaptureState>();
            AIVERSE_TEST_CHECK(capture);
            std::map<int64_t,uint32_t> occupants{{1,32}};
            const auto result=lg::stepCapture(*capture,occupants,4,s.tick());
            AIVERSE_TEST_CHECK(result.has_value());
        });
    }
    void applyCommand(Simulation&,const Command&) override {}
    void hashState(Simulation& sim,Hasher& hash) override {
        const World& world=sim.world();
        world.eachExistingConst<Component>([&](Entity e,const Component& actor) {
            hash.u64(e.packed());
            const auto& m=actor.gameplay;
            hash.i64(m.motion.velocity.x.raw);
            hash.i64(m.motion.velocity.y.raw);
            hash.i64(m.recoil.yaw.raw);
            hash.i64(m.recoil.pitch.raw);
        });
        const auto* objective=world.resourceConst<lg::CaptureState>();
        AIVERSE_TEST_CHECK(objective);
        hash.i64(objective->owner);
        hash.i64(objective->candidate);
        hash.u32(objective->progress);
    }
    int result(Simulation&) override{return 0;}
};

template<class T>
uint64_t runGenre(bool cow) {
    Simulation sim(SimConfig{73,20,0,true},
                   std::make_unique<GenreHost<T>>());
    sim.setup(Json::object(),Json::object());
    if(cow)sim.world().enableCowSnapshots(true);
    for(int i=0;i<10;++i)sim.step();
    SimSnapshot snapshot;
    sim.saveState(snapshot);
    for(int i=0;i<10;++i)sim.step();
    const auto expected=sim.checksum();
    sim.loadState(snapshot);
    for(int i=0;i<10;++i)sim.step();
    AIVERSE_TEST_CHECK(sim.checksum()==expected);
    return expected;
}
int main(){
    const auto baseline=runGenre<ShooterCharacter>(false);
    AIVERSE_TEST_CHECK(runGenre<StrategySoldier>(false)==baseline);
    AIVERSE_TEST_CHECK(runGenre<ShooterCharacter>(true)==baseline);
    AIVERSE_TEST_CHECK(runGenre<StrategySoldier>(true)==baseline);
    std::cout<<"PASS cross-genre ECS locomotion/recoil/capture policies, COW rollback and checksum parity\n";
}
