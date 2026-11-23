#include "engine/world/RegionColdStore.h"
#include "engine/sim/Simulation.h"
#include "tests/AlwaysCheck.h"
#include <array>
#include <cstdint>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace aiverse;
using namespace aiverse::world;

// This fixture registers its own position codec. A shipping GameModule
// reuses the existing Transform serializer instead of registering duplicates.
namespace aiverse {
inline void io(Archive& a,Transform& value) {
    a(value.pos,value.prevPos,value.yaw);
}
}
namespace fixture {
struct Brain {
    int32_t alert=0;
    std::string task;
};
inline void io(Archive& a,Brain& v) {a(v.alert,v.task);}
struct Engine {
    int32_t wear=0;
    std::vector<int32_t> gearHistory;
};
inline void io(Archive& a,Engine& v) {a(v.wear,v.gearHistory);}
}
using fixture::Brain;
using fixture::Engine;

namespace {
constexpr Cell home{0,0,0};
const Vec3 placed{Fixed::fromInt(10),{},Fixed::fromInt(5)};

struct Host final: GameModule {
    std::array<Entity,2> members{};
    std::string name()const override{return "region-cold-store-fixture";}
    void setup(Simulation& sim,const Json&,const Json&)override {
        registerRegionEntityLedger(sim);
        registerRegionStableIdentity(sim);
        registerRegionColdState(sim);
        sim.registerPersistentComponent<Transform>("test.region_transform",1);
        sim.registerPersistentComponent<Brain>("test.region_brain",1);
        sim.registerPersistentComponent<Engine>("test.region_engine",1);
        auto& w=sim.world();
        auto& ledger=w.setResource<RegionEntityLedger>(RegionEntityLedger{});
        w.setResource<RegionColdStore>(RegionColdStore{});
        for(size_t i=0;i<members.size();++i) {
            members[i]=w.create();
            w.add<Transform>(members[i],Transform{placed,placed,Fixed{}});
            AIVERSE_TEST_CHECK(RegionEcsBridge::bindNew(
                w,ledger,members[i],100+uint64_t(i),home));
            w.add<Brain>(members[i],Brain{int32_t(20+i),i?"passenger":"driving"});
            w.add<Engine>(members[i],Engine{int32_t(i),{1,2,3,int32_t(i)}});
        }
    }
    void applyCommand(Simulation&,const Command&)override {}
    void hashState(Simulation&,Hasher&)override {}
    bool serializeState(Simulation&,Archive&)override{return true;}
    int result(Simulation&)override{return 0;}
};
std::unique_ptr<Simulation> makeSim() {
    auto sim=std::make_unique<Simulation>(SimConfig{},std::make_unique<Host>());
    sim->setup(Json::object(),Json::object());
    return sim;
}
std::array<Entity,2> members(Simulation& sim) {
    return static_cast<Host&>(sim.module()).members;
}
RegionDormancyController controller() {
    RegionDormancyController result;
    AIVERSE_TEST_CHECK(result.registerComponent<Brain>("sample.brain",1));
    AIVERSE_TEST_CHECK(result.registerComponent<Engine>("sample.engine",1));
    AIVERSE_TEST_CHECK(!result.registerComponent<Brain>("sample.brain",1));
    return result;
}
std::string storeBytes(const RegionColdStore& original) {
    RegionColdStore value=original;
    std::string serialized;
    Archive a(&serialized);
    io(a,value);
    AIVERSE_TEST_CHECK(a.ok());
    return serialized;
}
void basicRoundtrip() {
    auto sim=makeSim();
    auto& w=sim->world();
    auto actors=members(*sim);
    auto owner=w.resource<RegionEntityLedger>();
    auto cold=w.resource<RegionColdStore>();
    auto policy=controller();
    const auto baseline=sim->checksum();
    const auto original=sim->serialize();
    std::string error;
    AIVERSE_TEST_CHECK(policy.parkGroup(w,*owner,*cold,actors,home,&error));
    AIVERSE_TEST_CHECK(cold->size()==2);
    for(Entity actor:actors) {
        AIVERSE_TEST_CHECK(w.alive(actor));
        AIVERSE_TEST_CHECK(w.has<Transform>(actor));
        AIVERSE_TEST_CHECK(w.has<RegionStableIdentity>(actor));
        AIVERSE_TEST_CHECK(w.has<RegionDormant>(actor));
        AIVERSE_TEST_CHECK(!w.has<Brain>(actor) && !w.has<Engine>(actor));
    }
    const auto afterPark=sim->checksum();
    AIVERSE_TEST_CHECK(afterPark!=baseline);
    AIVERSE_TEST_CHECK(!policy.parkGroup(w,*owner,*cold,actors,home,&error));
    AIVERSE_TEST_CHECK(sim->checksum()==afterPark);
    // Movement of the occupant group is forbidden while its active components
    // are parked, even though the identity and Transform remain allocated.
    AIVERSE_TEST_CHECK(!RegionEcsBridge::transferGroup(w,*owner,actors,home,{1,0,0}));
    AIVERSE_TEST_CHECK(!RegionEcsBridge::retire(w,*owner,actors[0],home));
    std::map<RegionEntityId,Entity> stableIndex;
    AIVERSE_TEST_CHECK(RegionEcsBridge::rebuild(w,*owner,stableIndex));
    AIVERSE_TEST_CHECK(stableIndex.at(100)==actors[0]);
    AIVERSE_TEST_CHECK(stableIndex.at(101)==actors[1]);

    const std::string parked=sim->serialize();
    const std::string coldArchive=storeBytes(*cold);
    RegionColdStore recovered;
    Archive a(coldArchive);
    io(a,recovered);
    AIVERSE_TEST_CHECK(a.ok() && a.remaining()==0);
    AIVERSE_TEST_CHECK(storeBytes(recovered)==coldArchive);
    AIVERSE_TEST_CHECK(recovered.size()==2);
    RegionColdStore unmodified=recovered;
    const auto shortArchive=coldArchive.substr(0,coldArchive.size()-1);
    Archive truncated(shortArchive);
    io(truncated,unmodified);
    AIVERSE_TEST_CHECK(!truncated.ok());
    AIVERSE_TEST_CHECK(storeBytes(unmodified)==coldArchive);

    // Wrong codec version must leave the entire group dormant and unchanged.
    RegionDormancyController wrongVersion;
    AIVERSE_TEST_CHECK(wrongVersion.registerComponent<Brain>("sample.brain",2));
    AIVERSE_TEST_CHECK(wrongVersion.registerComponent<Engine>("sample.engine",1));
    AIVERSE_TEST_CHECK(!wrongVersion.wakeGroup(w,*owner,*cold,actors,home,&error));
    AIVERSE_TEST_CHECK(sim->checksum()==afterPark);

    auto independent=makeSim();
    AIVERSE_TEST_CHECK(independent->deserialize(parked,&error));
    AIVERSE_TEST_CHECK(independent->checksum()==afterPark);
    auto& other=independent->world();
    auto otherActors=members(*independent);
    auto* otherOwner=other.resource<RegionEntityLedger>();
    auto* otherCold=other.resource<RegionColdStore>();
    AIVERSE_TEST_CHECK(otherCold && otherCold->size()==2);
    AIVERSE_TEST_CHECK(policy.wakeGroup(other,*otherOwner,*otherCold,otherActors,home,&error));
    AIVERSE_TEST_CHECK(otherCold->size()==0);
    AIVERSE_TEST_CHECK(independent->checksum()==baseline);
    AIVERSE_TEST_CHECK(independent->serialize()==original);

    AIVERSE_TEST_CHECK(policy.wakeGroup(w,*owner,*cold,actors,home,&error));
    AIVERSE_TEST_CHECK(sim->checksum()==baseline);
    AIVERSE_TEST_CHECK(!policy.wakeGroup(w,*owner,*cold,actors,home,&error));
    AIVERSE_TEST_CHECK(sim->checksum()==baseline);
}
void rollbackAndRetirement() {
    auto sim=makeSim();
    auto& w=sim->world();
    auto actors=members(*sim);
    auto policy=controller();
    auto* owner=w.resource<RegionEntityLedger>();
    auto* cold=w.resource<RegionColdStore>();
    std::string error;
    AIVERSE_TEST_CHECK(policy.parkGroup(w,*owner,*cold,actors,home,&error));
    const auto savedCold=sim->checksum();
    sim->world().enableCowSnapshots();
    SimSnapshot snapshot;
    sim->saveState(snapshot);

    // Must re-acquire writable resources after a COW snapshot.
    owner=w.resource<RegionEntityLedger>();
    cold=w.resource<RegionColdStore>();
    AIVERSE_TEST_CHECK(policy.wakeGroup(w,*owner,*cold,actors,home,&error));
    AIVERSE_TEST_CHECK(sim->checksum()!=savedCold);
    sim->loadState(snapshot);
    AIVERSE_TEST_CHECK(sim->checksum()==savedCold);
    owner=w.resource<RegionEntityLedger>();
    cold=w.resource<RegionColdStore>();
    AIVERSE_TEST_CHECK(cold->size()==2 && w.has<RegionDormant>(actors[0]));
    AIVERSE_TEST_CHECK(policy.retireDormant(w,*owner,*cold,actors[0],home,&error));
    AIVERSE_TEST_CHECK(!w.alive(actors[0]));
    AIVERSE_TEST_CHECK(cold->size()==1);
    AIVERSE_TEST_CHECK(owner->find(100) && !owner->find(100)->live);
    AIVERSE_TEST_CHECK(!policy.retireDormant(w,*owner,*cold,actors[0],home,&error));
    AIVERSE_TEST_CHECK(owner->find(101)->live && w.alive(actors[1]));
    const std::string saved=sim->serialize();
    auto fresh=makeSim();
    AIVERSE_TEST_CHECK(fresh->deserialize(saved,&error));
    AIVERSE_TEST_CHECK(fresh->checksum()==sim->checksum());
    AIVERSE_TEST_CHECK(!fresh->world().alive(actors[0]));
    auto* otherOwner=fresh->world().resource<RegionEntityLedger>();
    auto* otherCold=fresh->world().resource<RegionColdStore>();
    auto survivor=std::array<Entity,1>{actors[1]};
    AIVERSE_TEST_CHECK(policy.wakeGroup(fresh->world(),*otherOwner,*otherCold,
                                        survivor,home,&error));
    AIVERSE_TEST_CHECK(fresh->world().get<Brain>(actors[1]).task=="passenger");
    AIVERSE_TEST_CHECK(fresh->world().get<Engine>(actors[1]).gearHistory.size()==4);
    AIVERSE_TEST_CHECK(!otherOwner->find(100)->live); // no resurrection
}
} // namespace

int main() {
    basicRoundtrip();
    rollbackAndRetirement();
    std::cout<<"PASS region_cold_store (real component parking, restore, rollback, retirement)\n";
}
