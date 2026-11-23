#include "engine/world/RegionEntityLedger.h"
#include "engine/sim/Simulation.h"
#include "tests/AlwaysCheck.h"
#include <array>
#include <cstdint>
#include <iostream>
#include <memory>
#include <span>
#include <string>
#include <vector>

using namespace aiverse;
using namespace aiverse::world;

static Cell a{0,0,0}, b{1,0,0}, c{2,0,0};

static std::string encode(RegionEntityLedger value) {
    std::string bytes;
    Archive ar(&bytes);
    io(ar,value);
    AIVERSE_TEST_CHECK(ar.ok());
    return bytes;
}
static uint64_t digest(RegionEntityLedger value) {
    Hasher hash;
    Archive ar(hash);
    io(ar,value);
    AIVERSE_TEST_CHECK(ar.ok());
    return hash.value();
}
static void checkLedger() {
    RegionEntityLedger first,second;
    AIVERSE_TEST_CHECK(!first.spawn(0,a));
    AIVERSE_TEST_CHECK(first.spawn(101,a) && first.spawn(102,a) && first.spawn(103,a));
    AIVERSE_TEST_CHECK(second.spawn(101,a) && second.spawn(102,a) && second.spawn(103,a));
    AIVERSE_TEST_CHECK(!first.spawn(101,b));
    std::array<uint64_t,3> ordered{101,102,103}, reversed{103,102,101};
    AIVERSE_TEST_CHECK(first.transfer(ordered,a,b));
    AIVERSE_TEST_CHECK(second.transfer(reversed,a,b));
    AIVERSE_TEST_CHECK(encode(first)==encode(second));
    AIVERSE_TEST_CHECK(first.activeIn(b)==std::vector<uint64_t>({101,102,103}));
    AIVERSE_TEST_CHECK(first.activeIn(a).empty());
    const auto revision=first.lastRevision();
    AIVERSE_TEST_CHECK(!first.transfer(ordered,a,c)); // stale expected owner: atomic refusal
    AIVERSE_TEST_CHECK(first.lastRevision()==revision);
    AIVERSE_TEST_CHECK(first.transfer(ordered,b,b)); // no-op
    AIVERSE_TEST_CHECK(first.lastRevision()==revision);
    std::array<uint64_t,2> duplicates{101,101};
    AIVERSE_TEST_CHECK(!first.transfer(duplicates,b,c));
    AIVERSE_TEST_CHECK(first.lastRevision()==revision);
    AIVERSE_TEST_CHECK(first.despawn(102,b));
    AIVERSE_TEST_CHECK(!first.despawn(102,b));
    AIVERSE_TEST_CHECK(!first.spawn(102,a)); // tombstone prevents resurrection
    AIVERSE_TEST_CHECK(!first.transfer(102,b,c));
    AIVERSE_TEST_CHECK(first.find(102) && !first.find(102)->live);
    AIVERSE_TEST_CHECK(first.activeIn(b)==std::vector<uint64_t>({101,103}));
    const auto changes=first.since(revision);
    AIVERSE_TEST_CHECK(changes.size()==1 && changes[0].first==102 && !changes[0].second.live);

    const auto encoded=encode(first);
    RegionEntityLedger recovered;
    Archive read(encoded);
    io(read,recovered);
    AIVERSE_TEST_CHECK(read.ok() && read.remaining()==0);
    AIVERSE_TEST_CHECK(digest(first)==digest(recovered));
    AIVERSE_TEST_CHECK(encode(first)==encode(recovered));
    RegionEntityLedger preserved=first;
    std::string shortBytes=encoded.substr(0,encoded.size()-1);
    Archive truncated(shortBytes);
    io(truncated,preserved);
    AIVERSE_TEST_CHECK(!truncated.ok());
    AIVERSE_TEST_CHECK(encode(preserved)==encoded); // failed load transactional
    std::string corrupt=encoded;
    corrupt[0]=char(99); // schema mismatch
    Archive bad(corrupt);
    io(bad,preserved);
    AIVERSE_TEST_CHECK(!bad.ok() && encode(preserved)==encoded);

    // Persistent changes are authoritative even when an observer is far away.
    RegionEntityLedger roaming;
    AIVERSE_TEST_CHECK(roaming.spawn(500,a) && roaming.spawn(501,a));
    std::array<uint64_t,2> vehicle{500,501}; // driver/passenger ownership together
    for(int i=0;i<100;++i) {
        Cell from{i,0,0},to{i+1,0,0};
        AIVERSE_TEST_CHECK(roaming.transfer(vehicle,from,to));
    }
    AIVERSE_TEST_CHECK(roaming.activeIn({100,0,0})==std::vector<uint64_t>({500,501}));
    RegionEntityLedger loaded;
    std::string bytes=encode(roaming);
    Archive rd(bytes);
    io(rd,loaded);
    AIVERSE_TEST_CHECK(rd.ok() && encode(loaded)==bytes);
    AIVERSE_TEST_CHECK(loaded.lastRevision()==roaming.lastRevision());
}

struct Host final:GameModule {
    std::string name() const override {return "region-ledger-fixture";}
    void setup(Simulation& sim,const Json&,const Json&) override {
        registerRegionEntityLedger(sim);
        sim.world().setResource<RegionEntityLedger>(RegionEntityLedger{});
    }
    void applyCommand(Simulation&,const Command&) override {}
    void hashState(Simulation&,Hasher&) override {}
    bool serializeState(Simulation&,Archive&) override {return true;}
    int result(Simulation&) override {return 0;}
};
static std::unique_ptr<Simulation> makeSim() {
    auto sim=std::make_unique<Simulation>(SimConfig{},std::make_unique<Host>());
    sim->setup(Json::object(),Json::object());
    return sim;
}
static void checkEcsPersistence() {
    auto one=makeSim();
    auto* state=one->world().resource<RegionEntityLedger>();
    AIVERSE_TEST_CHECK(state);
    AIVERSE_TEST_CHECK(state->spawn(700,a) && state->spawn(701,a));
    const std::string initial=one->serialize();
    auto baseline=one->checksum();
    one->world().enableCowSnapshots();
    SimSnapshot rollback;
    one->saveState(rollback);
    // Reacquire the mutable resource after COW snapshot; the old pointer may
    // still reference a page shared with the saved snapshot.
    state=one->world().resource<RegionEntityLedger>();
    std::array<uint64_t,2> group{700,701};
    AIVERSE_TEST_CHECK(state->transfer(group,a,b));
    AIVERSE_TEST_CHECK(one->checksum()!=baseline);
    one->loadState(rollback);
    AIVERSE_TEST_CHECK(one->checksum()==baseline);
    state=one->world().resource<RegionEntityLedger>();
    AIVERSE_TEST_CHECK(state && state->activeIn(a)==std::vector<uint64_t>({700,701}));
    auto other=makeSim();
    std::string error;
    AIVERSE_TEST_CHECK(other->deserialize(initial,&error));
    AIVERSE_TEST_CHECK(other->checksum()==baseline);
    auto* restored=other->world().resource<RegionEntityLedger>();
    AIVERSE_TEST_CHECK(restored && restored->activeIn(a)==std::vector<uint64_t>({700,701}));
    // Subsequent identical transfers stay lockstep across independent ECS worlds.
    AIVERSE_TEST_CHECK(one->world().resource<RegionEntityLedger>()->transfer(group,a,c));
    AIVERSE_TEST_CHECK(restored->transfer(group,a,c));
    one->step();
    other->step();
    AIVERSE_TEST_CHECK(one->checksum()==other->checksum());
    const auto saved=one->serialize();
    auto final=makeSim();
    AIVERSE_TEST_CHECK(final->deserialize(saved,&error));
    AIVERSE_TEST_CHECK(final->checksum()==one->checksum());
}
int main() {
    checkLedger();
    checkEcsPersistence();
    std::cout<<"PASS region_entity_ledger (transfer, tombstone, archive, ECS v10, rollback, 100 crossings)\n";
}
