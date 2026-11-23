#include "engine/world/RegionEntityLedger.h"
#include "engine/world/UnifiedBackupCatalog.h"
#include <chrono>
#include <fstream>
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
    AIVERSE_TEST_CHECK(first.liveCount()==2);
    AIVERSE_TEST_CHECK(!first.liveIds().contains(102));
    AIVERSE_TEST_CHECK(first.liveIds().contains(101) &&
                       first.liveIds().contains(103));
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
    AIVERSE_TEST_CHECK(recovered.liveIds()==first.liveIds());
    AIVERSE_TEST_CHECK(recovered.liveCount()==2);
    // A long history of tombstones never increases the live-ID index.
    RegionEntityLedger history;
    for(uint64_t i=1;i<=2000;++i) {
        AIVERSE_TEST_CHECK(history.spawn(i,a));
        AIVERSE_TEST_CHECK(history.despawn(i,a));
    }
    AIVERSE_TEST_CHECK(history.size()==2000 && history.liveCount()==0);
    AIVERSE_TEST_CHECK(history.liveIds().empty());
    AIVERSE_TEST_CHECK(history.spawn(2001,b));
    AIVERSE_TEST_CHECK(history.liveCount()==1 &&
                       *history.liveIds().begin()==2001);
    const auto historicBytes=encode(history);
    Archive historicInput(historicBytes);
    RegionEntityLedger loadedHistory;
    io(historicInput,loadedHistory);
    AIVERSE_TEST_CHECK(historicInput.ok());
    AIVERSE_TEST_CHECK(loadedHistory.liveIds()==history.liveIds());
    AIVERSE_TEST_CHECK(encode(loadedHistory)==historicBytes);
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

    // Mixed-region boarding: a driver and passenger may arrive from
    // distinct source cells but must land in one atomic vehicle region.
    RegionEntityLedger mixed;
    AIVERSE_TEST_CHECK(mixed.spawn(1000,a));
    AIVERSE_TEST_CHECK(mixed.spawn(1001,b));
    AIVERSE_TEST_CHECK(mixed.spawn(1002,c));
    std::array<std::pair<RegionEntityId,Cell>,3> members{{
        {1002,c},{1000,a},{1001,b}
    }};
    const auto mixedRevision=mixed.lastRevision();
    AIVERSE_TEST_CHECK(mixed.transferGroup(members,c));
    AIVERSE_TEST_CHECK(mixed.lastRevision()==mixedRevision+2);
    AIVERSE_TEST_CHECK(mixed.activeIn(c)==
                       std::vector<uint64_t>({1000,1001,1002}));
    AIVERSE_TEST_CHECK(mixed.transferGroup(
        std::array<std::pair<RegionEntityId,Cell>,3>{{
            {1000,c},{1001,c},{1002,c}}},c));
    AIVERSE_TEST_CHECK(mixed.lastRevision()==mixedRevision+2);
    auto invalid=members;
    invalid[1].second=b;
    const auto beforeFailed=mixed.lastRevision();
    AIVERSE_TEST_CHECK(!mixed.transferGroup(invalid,a));
    AIVERSE_TEST_CHECK(mixed.lastRevision()==beforeFailed);
    AIVERSE_TEST_CHECK(mixed.activeIn(c).size()==3);

    // Incremental latest-state feed: paged reads must equal full since()
    // output after mixed-origin moves and tombstones, in revision order.
    RegionEntityLedger changes;
    for(uint64_t id=100;id<180;++id)
        AIVERSE_TEST_CHECK(changes.spawn(id,a));
    for(uint64_t id=100;id<160;++id)
        AIVERSE_TEST_CHECK(changes.transfer(id,a,b));
    for(uint64_t id=160;id<175;++id)
        AIVERSE_TEST_CHECK(changes.despawn(id,a));
    auto expected=changes.since(0);
    std::vector<std::pair<RegionEntityId,RegionEntityRecord>> pages;
    uint64_t cursor=0;
    int chunks=0;
    while(true) {
        auto page=changes.pageSince(cursor,7);
        for(const auto& [id,record]:page.changes) {
            AIVERSE_TEST_CHECK(record.revision>cursor);
            pages.emplace_back(id,record);
        }
        AIVERSE_TEST_CHECK(page.nextRevision>=cursor);
        cursor=page.nextRevision;
        ++chunks;
        AIVERSE_TEST_CHECK(chunks<30);
        if(!page.more)break;
        AIVERSE_TEST_CHECK(page.changes.size()==7);
    }
    AIVERSE_TEST_CHECK(pages==expected);
    AIVERSE_TEST_CHECK(changes.pageSince(cursor,7).changes.empty());
    AIVERSE_TEST_CHECK(changes.pageSince(0,0).changes.empty());
    AIVERSE_TEST_CHECK(changes.pageSince(UINT64_MAX,7).changes.empty());
    const auto encodedChanges=encode(changes);
    Archive changeReader(encodedChanges);
    RegionEntityLedger decodedChanges;
    io(changeReader,decodedChanges);
    AIVERSE_TEST_CHECK(changeReader.ok());
    AIVERSE_TEST_CHECK(decodedChanges.since(0)==expected);
    AIVERSE_TEST_CHECK(decodedChanges.pageSince(0,100).changes==expected);
    AIVERSE_TEST_CHECK(encode(decodedChanges)==encodedChanges);
    // Bounded acknowledged consumer: failed delivery can retry exactly the
    // failed entry, without replaying successful records or losing cursor.
    RegionChangeCursor cursorConsumer;
    std::vector<std::pair<RegionEntityId,RegionEntityRecord>> delivered;
    bool failOnce=true;
    bool complete=false;
    int batches=0;
    while(!complete) {
        complete=cursorConsumer.consume(decodedChanges,5,
            [&](RegionEntityId id,const RegionEntityRecord& rec) {
                if(failOnce && delivered.size()==3) {
                    failOnce=false;
                    return false;
                }
                delivered.emplace_back(id,rec);
                return true;
            });
        AIVERSE_TEST_CHECK(++batches<30);
    }
    AIVERSE_TEST_CHECK(delivered==expected);
    AIVERSE_TEST_CHECK(cursorConsumer.lastRevision()==
                       decodedChanges.lastRevision());
    AIVERSE_TEST_CHECK(cursorConsumer.consume(decodedChanges,5,
        [](RegionEntityId,const RegionEntityRecord&){return true;}));
    cursorConsumer.reset();
    AIVERSE_TEST_CHECK(cursorConsumer.lastRevision()==0);
    AIVERSE_TEST_CHECK(!cursorConsumer.consume(decodedChanges,0,
        [](RegionEntityId,const RegionEntityRecord&){return true;}));
    AIVERSE_TEST_CHECK(cursorConsumer.consume(decodedChanges,200,
        [](RegionEntityId,const RegionEntityRecord&){return true;}));


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

static void checkUnifiedBackupCatalog() {
    namespace fs=std::filesystem;
    using Status=UnifiedBackupStatus;
    struct TempDir {
        fs::path path;
        ~TempDir(){std::error_code ec;if(!path.empty())fs::remove_all(path,ec);}
    } temp;
    for(unsigned i=0;i<100;++i){
        auto candidate=fs::temp_directory_path()/("aiverse-unified-catalog-"+
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+
            "-"+std::to_string(i));
        std::error_code ec;
        if(fs::create_directory(candidate,ec)){temp.path=std::move(candidate);break;}
    }
    AIVERSE_TEST_CHECK(!temp.path.empty());
    RegionCheckpointManifest active;
    active.generation=5;active.checkpointBytes=1;
    active.identity.world[0]=1;active.identity.digest[0]=1;
    auto pair=[&](uint64_t generation,bool otherWorld,bool checkpoint,bool unified){
        auto m=active;m.generation=generation;
        if(otherWorld)m.identity.world[0]=2;
        std::vector<uint8_t> prefix;
        AIVERSE_TEST_CHECK(unified?UnifiedCheckpointPublisher::identity(m,prefix):
            RegionCheckpointManifestCodec::encode(m,prefix));
        std::ofstream wal(temp.path/m.walName(),std::ios::binary);
        wal.write(reinterpret_cast<const char*>(prefix.data()),prefix.size());
        AIVERSE_TEST_CHECK(wal.good());
        if(checkpoint){
            std::ofstream cp(temp.path/m.checkpointName(),std::ios::binary);
            cp.put('x');AIVERSE_TEST_CHECK(cp.good());
        }
    };
    pair(5,false,true,true);
    pair(4,true,true,true);
    pair(3,false,false,true);
    pair(2,false,true,false);
    pair(1,false,true,true);
    std::ofstream(temp.path/"wal-01.bin").put('x');
    std::ofstream(temp.path/"wal-0.bin").put('x');
    UnifiedCheckpointSections::Adapters adapters{};
    UnifiedCheckpointSections::Factory factory;
    DurableComponentCodecs codecs;
    UnifiedWorldReplay::Materializer materialize;
    auto result=UnifiedBackupCatalog::list(temp.path,active,1024,adapters,factory,codecs,materialize);
    AIVERSE_TEST_CHECK(result.complete && result.entries.size()==5);
    AIVERSE_TEST_CHECK(result.entries[0].generation==5 && result.entries[0].active &&
        result.entries[0].status==Status::MetadataOnly);
    AIVERSE_TEST_CHECK(result.entries[1].status==Status::WrongWorld);
    AIVERSE_TEST_CHECK(result.entries[2].status==Status::MissingCheckpoint);
    AIVERSE_TEST_CHECK(result.entries[3].status==Status::InvalidMetadata);
    AIVERSE_TEST_CHECK(result.entries[4].generation==1 && !result.entries[4].active &&
        result.entries[4].status==Status::MetadataOnly);
    auto verified=UnifiedBackupCatalog::list(temp.path,active,1024,adapters,factory,codecs,materialize,{},true);
    AIVERSE_TEST_CHECK(verified.complete && verified.entries.size()==5);
    AIVERSE_TEST_CHECK(verified.entries[0].status==Status::RecoveryFailed);
    AIVERSE_TEST_CHECK(verified.entries[4].status==Status::RecoveryFailed);
}

int main() {
    checkUnifiedBackupCatalog();
    checkLedger();
    checkEcsPersistence();
    std::cout<<"PASS region_entity_ledger (transfer, tombstone, archive, ECS v10, rollback, 100 crossings)\n";
}
