#include "engine/world/RegionEntityLedger.h"
#include "engine/world/WorldPersistenceDiagnostics.h"
#include "engine/world/UnifiedWalSession.h"
#include "engine/world/NativeWorldFinality.h"
#include "engine/world/UnifiedEntitySection.h"
#include "engine/world/UnifiedComponentSection.h"
#include "engine/world/UnifiedResourceSection.h"
#include <chrono>
#include <atomic>
#include <thread>
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

#if defined(__unix__) || defined(__APPLE__) || defined(_WIN32)
    const auto managed=temp.path/"managed";
    const auto destination=temp.path/"fork";
    AIVERSE_TEST_CHECK(fs::create_directory(managed)&&fs::create_directory(destination));
    // Empty adapters are sufficient for a ledger-only fixture inside the
    // unified four-section format. The production adapters are tested separately.
    for(auto& adapter:adapters){
        adapter.write=[](const UnifiedWorldReplay::State&,std::vector<uint8_t>& bytes){
            bytes.clear();return true;
        };
        adapter.read=[](UnifiedWorldReplay::State&,uint32_t version,std::span<const uint8_t> bytes){
            return version==1&&bytes.empty();
        };
    }
    factory=[](){return std::make_unique<UnifiedWorldReplay::State>();};
    materialize=[](World&,const RegionEntityLedger&,const RegionEntityLedger&){return true;};
    UnifiedWalSession session;
    UnifiedWalSession::State initial;
    AIVERSE_TEST_CHECK(initial.ledger.spawn(99,a));
    AIVERSE_TEST_CHECK(initial.ledger.spawn(100,b)&&initial.ledger.despawn(100,b));
    AIVERSE_TEST_CHECK(initial.ledger.spawn(101,c)&&initial.ledger.despawn(101,c));
    constexpr size_t budget=64*1024;
    const auto world=active.identity.world;
    AIVERSE_TEST_CHECK(session.create(managed,world,initial,adapters,budget)==RegionWalWrite::Ok);
    AIVERSE_TEST_CHECK(session.checkpoint(3,adapters,budget,3)==RegionCheckpointPublishResult::Ok);
    AIVERSE_TEST_CHECK(session.state()->ledger.retiredFloor()==100&&session.state()->ledger.find(101));
    AIVERSE_TEST_CHECK(session.enableAsync(budget));
    AIVERSE_TEST_CHECK(session.queueCheckpoint(5,adapters,budget,5)==UnifiedWalSession::IoResult::Pending);
    AIVERSE_TEST_CHECK(!session.state()&&!session.manifest());
    auto status=UnifiedWalSession::IoResult::Pending;
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    while(status==UnifiedWalSession::IoResult::Pending){
        AIVERSE_TEST_CHECK(std::chrono::steady_clock::now()<deadline);
        status=session.pollIo();
        if(status==UnifiedWalSession::IoResult::Pending)std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    AIVERSE_TEST_CHECK(status==UnifiedWalSession::IoResult::Checkpointed);
    AIVERSE_TEST_CHECK(session.state()->ledger.size()==1&&session.state()->ledger.nextIdentity()==102);
    AIVERSE_TEST_CHECK(session.state()->ledger.historyFloor()==5&&session.state()->ledger.isRetired(101));
    AIVERSE_TEST_CHECK(session.checkpoint(7,adapters,budget)==RegionCheckpointPublishResult::Ok);
    // Corrupt an old checkpoint, inject a foreign-world pair and a future pair.
    std::ofstream(managed/"checkpoint-1.bin",std::ios::binary|std::ios::trunc).put('x');
    auto inject=[&](uint64_t gen,bool foreign){
        auto m=*session.manifest();m.generation=gen;
        if(foreign)m.identity.world[0]=2;
        fs::copy_file(managed/session.manifest()->checkpointName(),managed/m.checkpointName());
        std::vector<uint8_t> prefix;
        AIVERSE_TEST_CHECK(UnifiedCheckpointPublisher::identity(m,prefix));
        std::ofstream wal(managed/m.walName(),std::ios::binary);
        wal.write(reinterpret_cast<const char*>(prefix.data()),prefix.size());
        AIVERSE_TEST_CHECK(wal.good());
    };
    inject(2,true);inject(8,false);
    std::ofstream(managed/"checkpoint-4.bin").put('x'); // orphan
    auto catalog=session.backups(budget,adapters,factory,codecs,materialize,{},true);
    AIVERSE_TEST_CHECK(catalog.complete&&catalog.entries.size()==6);
    AIVERSE_TEST_CHECK(catalog.entries[0].generation==8&&catalog.entries[0].status==Status::Recoverable);
    AIVERSE_TEST_CHECK(catalog.entries[1].active&&catalog.entries[1].status==Status::Recoverable);
    RegionRetentionPolicy policy;policy.keepPrevious=1;policy.maxBytes=1;
    auto plan=session.retentionPlan(policy,budget,adapters,factory,codecs,materialize);
    AIVERSE_TEST_CHECK(plan.valid&&plan.overBudget&&plan.removeGenerations==std::vector<uint64_t>{3});
    auto stale=*session.manifest();stale.generation=5;
    AIVERSE_TEST_CHECK(!UnifiedCheckpointRetention::cleanup(managed,stale,policy,budget,
        adapters,factory,codecs,materialize).ok);
    auto cleaned=session.cleanupRetention(policy,budget,adapters,factory,codecs,materialize);
    AIVERSE_TEST_CHECK(cleaned.ok&&cleaned.removed==std::vector<uint64_t>{3});
    AIVERSE_TEST_CHECK(!fs::exists(managed/"wal-3.bin")&&!fs::exists(managed/"checkpoint-3.bin"));
    for(auto gen:{1,2,5,7,8}){
        RegionCheckpointManifest m;m.generation=gen;
        AIVERSE_TEST_CHECK(fs::exists(managed/m.walName())&&fs::exists(managed/m.checkpointName()));
    }
    AIVERSE_TEST_CHECK(fs::exists(managed/"checkpoint-4.bin"));
    auto forkWorld=world;forkWorld[0]=3;
    AIVERSE_TEST_CHECK(session.forkGeneration(8,destination,forkWorld,budget,adapters,factory,codecs,materialize)
        ==RegionWalWrite::InvalidFrame);
    AIVERSE_TEST_CHECK(session.forkGeneration(5,destination,forkWorld,budget,adapters,factory,codecs,materialize)
        ==RegionWalWrite::Ok);
    UnifiedWalSession forked;
    AIVERSE_TEST_CHECK(forked.openCheckpoint(destination,forkWorld,budget,adapters,factory,codecs,materialize)
        ==RegionWalWrite::Ok);
    AIVERSE_TEST_CHECK(forked.state()->sequence==0&&forked.state()->ledger.find(99));
    AIVERSE_TEST_CHECK(forked.state()->ledger.sameState(session.state()->ledger));
    AIVERSE_TEST_CHECK(session.state()->ledger.find(99)&&session.manifest()->generation==7);
    forked.close();session.close();
    AIVERSE_TEST_CHECK(!session.backups(budget,adapters,factory,codecs,materialize).complete);
#endif
}


struct WalFixtureResource { uint32_t value=0; };
inline void io(Archive& ar,WalFixtureResource& r){ar(r.value);}

static void checkResourceWal(){
    using Replay=UnifiedWorldReplay;
    using Delta=DurableResourceTransaction;
    PersistentEcsRegistry registry;
    registry.registerResource<WalFixtureResource>("test.world.resource",1);
    Delta::Policy policy{&registry,{"test.world.resource"}};
    auto current=std::make_unique<Replay::State>();
    AIVERSE_TEST_CHECK(current->ledger.spawn(1,a));
    current->world.setResource<WalFixtureResource>(WalFixtureResource{10});
    World after=current->world;
    after.setResource<WalFixtureResource>(WalFixtureResource{20});
    std::vector<Delta::Record> changes;
    AIVERSE_TEST_CHECK(Delta::captureChanges(registry,policy.selected,current->world,after,changes));
    AIVERSE_TEST_CHECK(changes.size()==1);
    std::vector<uint8_t> bytes;
    AIVERSE_TEST_CHECK(Delta::encode(changes,bytes));
    std::vector<Delta::Record> decoded;
    AIVERSE_TEST_CHECK(Delta::decode(bytes,decoded)&&decoded.size()==1);
    bytes.pop_back();
    AIVERSE_TEST_CHECK(!Delta::decode(bytes,decoded)&&decoded.size()==1);
    auto duplicate=changes;duplicate.push_back(changes.front());
    AIVERSE_TEST_CHECK(!Delta::encode(duplicate,bytes));
    DurableComponentCodecs codecs;
    Replay::Materializer materialize=[](World&,const RegionEntityLedger&,const RegionEntityLedger&){return true;};
    auto ledger=current->ledger;
    RegionWalFrame frame;
    {
        RegionEntityLedger::Transaction tx(ledger,true);
        AIVERSE_TEST_CHECK(tx.transfer(1,a,b));
        AIVERSE_TEST_CHECK(UnifiedWorldTransaction::encode(tx,{},changes,1,frame));
    }
    AIVERSE_TEST_CHECK(frame.payload[4]==2);
    auto old=current.get();
    AIVERSE_TEST_CHECK(!Replay::apply(current,frame,codecs,materialize));
    AIVERSE_TEST_CHECK(current.get()==old&&current->sequence==0&&current->ledger.find(1)->owner==a);
    AIVERSE_TEST_CHECK(!Replay::apply(current,frame,codecs,materialize,
        [](const Replay::State&){return false;},policy));
    AIVERSE_TEST_CHECK(current.get()==old&&current->world.resourceConst<WalFixtureResource>()->value==10);
    AIVERSE_TEST_CHECK(Replay::apply(current,frame,codecs,materialize,{},policy));
    AIVERSE_TEST_CHECK(current->sequence==1&&current->ledger.find(1)->owner==b&&
        current->world.resourceConst<WalFixtureResource>()->value==20);
    after=current->world;after.eraseResource<WalFixtureResource>();
    AIVERSE_TEST_CHECK(Delta::captureChanges(registry,policy.selected,current->world,after,changes)&&changes.size()==1);
    ledger=current->ledger;
    {
        RegionEntityLedger::Transaction tx(ledger,true);
        AIVERSE_TEST_CHECK(UnifiedWorldTransaction::encode(tx,{},changes,2,frame));
    }
    AIVERSE_TEST_CHECK(Replay::apply(current,frame,codecs,materialize,{},policy));
    AIVERSE_TEST_CHECK(current->sequence==2&&!current->world.resourceConst<WalFixtureResource>());
    AIVERSE_TEST_CHECK(Delta::captureChanges(registry,policy.selected,current->world,after,changes)&&changes.empty());
    ledger=current->ledger;
    {
        RegionEntityLedger::Transaction tx(ledger,true);
        AIVERSE_TEST_CHECK(tx.transfer(1,b,c));
        AIVERSE_TEST_CHECK(UnifiedWorldTransaction::encode(tx,{},3,frame));
    }
    AIVERSE_TEST_CHECK(frame.payload[4]==1&&Replay::apply(current,frame,codecs,materialize));
    AIVERSE_TEST_CHECK(current->sequence==3&&current->ledger.find(1)->owner==c);

#if defined(__unix__) || defined(__APPLE__) || defined(_WIN32)
    namespace fs=std::filesystem;
    struct Temp {
        fs::path dir;
        ~Temp(){std::error_code ec;if(!dir.empty())fs::remove_all(dir,ec);}
    } temp;
    for(unsigned i=0;i<100;++i){
        auto p=fs::temp_directory_path()/("aiverse-resource-wal-"+
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"-"+std::to_string(i));
        std::error_code ec;
        if(fs::create_directory(p,ec)){temp.dir=std::move(p);break;}
    }
    AIVERSE_TEST_CHECK(!temp.dir.empty());
    auto source=temp.dir/"source",fork=temp.dir/"fork";
    AIVERSE_TEST_CHECK(fs::create_directory(source)&&fs::create_directory(fork));
    UnifiedCheckpointSections::Adapters adapters{};
    for(auto& adapter:adapters){
        adapter.write=[](const Replay::State&,std::vector<uint8_t>& out){out.clear();return true;};
        adapter.read=[](Replay::State&,uint32_t ver,std::span<const uint8_t> in){return ver==1&&in.empty();};
    }
    constexpr size_t budget=64*1024;
    adapters[2]=UnifiedResourceSection::adapter(policy,budget);
    UnifiedCheckpointSections::Factory factory=[](){return std::make_unique<Replay::State>();};
    Replay::State initial;initial.world.setResource<WalFixtureResource>(WalFixtureResource{10});
    std::array<uint8_t,16> uuid{};uuid[0]=7;
    UnifiedWalSession session;
    AIVERSE_TEST_CHECK(session.create(source,uuid,initial,adapters,budget)==RegionWalWrite::Ok);
    after=initial.world;after.setResource<WalFixtureResource>(WalFixtureResource{42});
    AIVERSE_TEST_CHECK(Delta::captureChanges(registry,policy.selected,initial.world,after,changes));
    ledger=initial.ledger;
    {
        RegionEntityLedger::Transaction tx(ledger,true);
        AIVERSE_TEST_CHECK(UnifiedWorldTransaction::encode(tx,{},changes,1,frame));
    }
    AIVERSE_TEST_CHECK(session.commit(frame,codecs,materialize,{},policy)==RegionWalWrite::Ok);
    session.close();
    AIVERSE_TEST_CHECK(session.openCheckpoint(source,uuid,budget,adapters,factory,codecs,materialize)==RegionWalWrite::NeedsRecovery);
    AIVERSE_TEST_CHECK(session.openCheckpoint(source,uuid,budget,adapters,factory,codecs,materialize,{},false,policy)==RegionWalWrite::Ok);
    AIVERSE_TEST_CHECK(session.state()->sequence==1&&session.state()->world.resourceConst<WalFixtureResource>()->value==42);
    auto catalog=session.backups(budget,adapters,factory,codecs,materialize,{},true,policy);
    AIVERSE_TEST_CHECK(catalog.complete&&catalog.entries.size()==1&&catalog.entries[0].status==UnifiedBackupStatus::Recoverable);
    auto forkId=uuid;forkId[0]=8;
    AIVERSE_TEST_CHECK(session.forkGeneration(1,fork,forkId,budget,adapters,factory,codecs,materialize,{},policy)==RegionWalWrite::Ok);
    UnifiedWalSession copy;
    AIVERSE_TEST_CHECK(copy.openCheckpoint(fork,forkId,budget,adapters,factory,codecs,materialize,{},false,policy)==RegionWalWrite::Ok);
    AIVERSE_TEST_CHECK(copy.state()->sequence==0&&copy.state()->world.resourceConst<WalFixtureResource>()->value==42);
    copy.close();session.close();
#endif
}


static void checkExactMutationCapture(){
    RegionEntityLedger ledger;
    AIVERSE_TEST_CHECK(ledger.spawn(11,a));
    auto baseline=ledger;
    RegionEntityLedger::MutationCapture capture(ledger);
    AIVERSE_TEST_CHECK(ledger.transfer(11,a,b));
    {
        RegionEntityLedger::Transaction tx(ledger);
        AIVERSE_TEST_CHECK(tx.transfer(11,b,c));
    }
    AIVERSE_TEST_CHECK(ledger.transfer(11,b,a));
    AIVERSE_TEST_CHECK(ledger.spawn(12,b));
    AIVERSE_TEST_CHECK(ledger.despawn(12,b));
    capture.stop();
    AIVERSE_TEST_CHECK(capture.ok()&&capture.records().size()==4);
    RegionWalFrame frame;
    AIVERSE_TEST_CHECK(RegionWalTransaction::encodeCaptured(capture.initialRevision(),
        ledger.lastRevision(),capture.records(),1,frame));
    AIVERSE_TEST_CHECK(RegionWalTransaction::replay(baseline,frame));
    AIVERSE_TEST_CHECK(baseline.records()==ledger.records()&&baseline.lastRevision()==ledger.lastRevision());
    RegionEntityLedger::MutationCapture limited(ledger,1);
    AIVERSE_TEST_CHECK(ledger.transfer(11,a,b)&&ledger.transfer(11,b,c));
    AIVERSE_TEST_CHECK(!limited.ok());
}

struct FinalityFixtureComponent {uint32_t value=0;};
inline void io(Archive& ar,FinalityFixtureComponent& value){ar(value.value);}
static void checkNativeFinality(bool restart=false){
#if defined(__unix__) || defined(__APPLE__) || defined(_WIN32)
    namespace fs=std::filesystem;
    struct Temp {
        fs::path dir;
        ~Temp(){std::error_code ec;if(!dir.empty())fs::remove_all(dir,ec);}
    } temp;
    for(unsigned i=0;i<100;++i){
        auto p=fs::temp_directory_path()/("aiverse-finality-"+
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"-"+std::to_string(i));
        std::error_code ec;
        if(fs::create_directory(p,ec)){temp.dir=std::move(p);break;}
    }
    AIVERSE_TEST_CHECK(!temp.dir.empty());
    auto sim=makeSim();
    auto e=sim->world().create();
    sim->world().add<RegionStableIdentity>(e,RegionStableIdentity{101});
    sim->world().add<FinalityFixtureComponent>(e,FinalityFixtureComponent{10});
    auto* ledger=sim->world().resource<RegionEntityLedger>();
    AIVERSE_TEST_CHECK(ledger->spawn(101,a));
    sim->addSystem("finality.fixture",[e](Simulation& s){
        if(s.tick()!=0)return;
        s.rng().next();
        auto* l=s.world().resource<RegionEntityLedger>();
        AIVERSE_TEST_CHECK(l->transfer(101,a,b)&&l->transfer(101,b,c));
        s.world().remove<FinalityFixtureComponent>(e);
        s.world().add<FinalityFixtureComponent>(e,FinalityFixtureComponent{25});
    });
    DurableComponentCodecs codecs;
    AIVERSE_TEST_CHECK(codecs.addTyped<FinalityFixtureComponent>("test.finality.component"));
    UnifiedCheckpointSections::Adapters adapters{};
    constexpr size_t budget=64*1024;
    adapters[0]=UnifiedEntitySection::adapter([](World& w,RegionEntityId id,const RegionEntityRecord&){
        auto e=w.create();w.add<RegionStableIdentity>(e,RegionStableIdentity{id});return true;
    });
    adapters[1]=UnifiedComponentSection::adapter(codecs,budget);
    adapters[2]={1,[](const UnifiedWorldReplay::State&,std::vector<uint8_t>& out){out.clear();return true;},
        [](UnifiedWorldReplay::State&,uint32_t version,std::span<const uint8_t> in){return version==1&&in.empty();}};
    NativeRestartState::CommandPolicy commandPolicy=[](const Command& cmd){return cmd.type==42&&cmd.args.empty()&&cmd.param==0;};
    if(restart){
        Command cmd;cmd.tick=4;cmd.player=1;cmd.type=42;cmd.units={e};cmd.target=e;
        AIVERSE_TEST_CHECK(sim->submit(cmd)==4);
    }
    UnifiedWorldReplay::State initial;
    initial.ledger=*ledger;initial.world=sim->world();
    initial.world.eraseResource<RegionEntityLedger>();
    if(restart){
        AIVERSE_TEST_CHECK(NativeRestartState::capture(*sim,*ledger,"fixture-content-v1",commandPolicy,initial.restart));
        NativeRestartState::Data decoded;
        AIVERSE_TEST_CHECK(NativeRestartState::decode(initial.restart,decoded)&&decoded.pending.size()==1);
        AIVERSE_TEST_CHECK(decoded.pending[0].target==101&&decoded.pending[0].units==std::vector<RegionEntityId>{101});
        auto truncated=initial.restart;truncated.pop_back();
        AIVERSE_TEST_CHECK(!NativeRestartState::decode(truncated,decoded));
        AIVERSE_TEST_CHECK(!NativeRestartState::capture(*sim,*ledger,"fixture-content-v1",{},truncated));
    }
    UnifiedWalSession session;
    std::array<uint8_t,16> uuid{};uuid[0]=9;
    AIVERSE_TEST_CHECK(session.create(temp.dir,uuid,initial,adapters,budget)==RegionWalWrite::Ok);
    auto materialize=[](World&,const RegionEntityLedger&,const RegionEntityLedger&){return true;};
    NativeWorldFinality finality(*sim,session,codecs,{},materialize,{},budget,
        restart?"fixture-content-v1":"",commandPolicy);
    using Result=NativeWorldFinality::Result;
    AIVERSE_TEST_CHECK(finality.step()==Result::Committed);
    AIVERSE_TEST_CHECK(session.state()->sequence==1&&session.state()->ledger.find(101)->owner==c);
    AIVERSE_TEST_CHECK(finality.step()==(restart?Result::Committed:Result::NoChanges)&&finality.lastFinalizedTick()==2);
    const auto finalizedRng=sim->rng().state();
    // A normal speculative/unowned step is never written to WAL.
    sim->step();
    AIVERSE_TEST_CHECK(finality.step()==Result::NeedsRecovery&&finality.failed());
    AIVERSE_TEST_CHECK(session.state()->sequence==(restart?2:1));
    session.close();
    AIVERSE_TEST_CHECK(session.openCheckpoint(temp.dir,uuid,budget,adapters,
        [restart](){auto state=std::make_unique<UnifiedWorldReplay::State>();if(restart)state->world.create();return state;},codecs,materialize)==RegionWalWrite::Ok);
    PersistentEntityReferences refs;Entity restored;
    AIVERSE_TEST_CHECK(refs.rebuild(session.state()->world,session.state()->ledger));
    AIVERSE_TEST_CHECK(refs.resolve(session.state()->world,101,restored));
    AIVERSE_TEST_CHECK(session.state()->world.tryGetConst<FinalityFixtureComponent>(restored)->value==25);
    if(restart){
        AIVERSE_TEST_CHECK(restored!=e);
        SimSnapshot snapshot;
        auto fresh=makeSim();
        AIVERSE_TEST_CHECK(!NativeRestartState::stage(*fresh,session.state()->world,session.state()->ledger,
            session.state()->restart,"wrong-content",commandPolicy,snapshot));
        AIVERSE_TEST_CHECK(NativeRestartState::stage(*fresh,session.state()->world,session.state()->ledger,
            session.state()->restart,"fixture-content-v1",commandPolicy,snapshot));
        AIVERSE_TEST_CHECK(snapshot.tick==2&&snapshot.rng.state()==finalizedRng);
        AIVERSE_TEST_CHECK(snapshot.pending.at(4).front().target==restored&&snapshot.pending.at(4).front().units.front()==restored);
        AIVERSE_TEST_CHECK(snapshot.seqByPlayer.at(1)==1);
        fresh->loadState(snapshot);
        AIVERSE_TEST_CHECK(fresh->tick()==2&&fresh->rng().state()==finalizedRng);
    }
    session.close();
#endif
}

static void checkAsyncPersistenceMailbox(){
#if !defined(__EMSCRIPTEN__)
    using Io=AsyncPersistenceIo;
    std::atomic<bool> release{false};
    Io io;
    AIVERSE_TEST_CHECK(io.start(32));
    AIVERSE_TEST_CHECK(io.submit(1,33,[]{return true;})==Io::Submit::TooLarge);
    // Scope release ensures an assertion/exception cannot strand the worker.
    struct Release {std::atomic<bool>& flag;~Release(){flag.store(true);}} guard{release};
    AIVERSE_TEST_CHECK(io.submit(1,32,[&]{
        while(!release.load())std::this_thread::yield();
        return true;
    })==Io::Submit::Accepted);
    AIVERSE_TEST_CHECK(io.submit(2,1,[]{return true;})==Io::Submit::Busy);
    Io::Receipt receipt;
    AIVERSE_TEST_CHECK(!io.take(receipt));
    release.store(true);
    auto take=[&](){
        auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
        while(!io.take(receipt)){
            AIVERSE_TEST_CHECK(std::chrono::steady_clock::now()<deadline);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    };
    take();
    AIVERSE_TEST_CHECK(receipt.ticket==1&&receipt.bytes==32&&receipt.status==Io::Status::Durable);
    AIVERSE_TEST_CHECK(io.submit(2,4,[]{return false;})==Io::Submit::Accepted);
    take();
    AIVERSE_TEST_CHECK(receipt.ticket==2&&receipt.status==Io::Status::Failed);
    AIVERSE_TEST_CHECK(io.submit(3,1,[]{return true;})==Io::Submit::Stopped);
    io.stop();
    AIVERSE_TEST_CHECK(io.start(32));
    std::atomic<bool> drained{false};
    AIVERSE_TEST_CHECK(io.submit(4,1,[&]{drained.store(true);return true;})==Io::Submit::Accepted);
    io.stop();
    AIVERSE_TEST_CHECK(drained.load());
#endif
}

static void checkLedgerCompaction(){
    RegionEntityLedger ledger;
    AIVERSE_TEST_CHECK(ledger.spawn(1,a)&&ledger.spawn(9,b));
    AIVERSE_TEST_CHECK(ledger.despawn(9,b));
    const auto retiredRevision=ledger.lastRevision();
    AIVERSE_TEST_CHECK(ledger.spawn(10,a)&&ledger.despawn(10,a));
    const auto latest=ledger.lastRevision();
    {
        RegionEntityLedger::Transaction tx(ledger);
        AIVERSE_TEST_CHECK(ledger.compactRetired(latest)==0);
    }
    AIVERSE_TEST_CHECK(ledger.compactRetired(latest,1)==1);
    AIVERSE_TEST_CHECK(ledger.retiredFloor()==9&&ledger.historyFloor()==retiredRevision);
    AIVERSE_TEST_CHECK(!ledger.find(9)&&ledger.isRetired(9)&&ledger.isRetired(8));
    AIVERSE_TEST_CHECK(ledger.find(1)->live&&!ledger.isRetired(1));
    AIVERSE_TEST_CHECK(!ledger.spawn(9,a)&&!ledger.spawn(8,a));
    AIVERSE_TEST_CHECK(ledger.pageSince(0,100).resetRequired);
    RegionChangeCursor stale;
    AIVERSE_TEST_CHECK(stale.requiresBootstrap(ledger));
    AIVERSE_TEST_CHECK(!stale.consume(ledger,100,[](auto,const auto&){return true;}));
    stale.reset(latest);
    AIVERSE_TEST_CHECK(!stale.requiresBootstrap(ledger));
    AIVERSE_TEST_CHECK(ledger.compactRetired(latest)==1);
    AIVERSE_TEST_CHECK(ledger.lastRevision()==latest&&ledger.nextIdentity()==11&&ledger.size()==1);
    std::vector<uint8_t> bytes;
    AIVERSE_TEST_CHECK(RegionCheckpointArchive::encode(ledger,bytes));
    RegionEntityLedger restored;
    AIVERSE_TEST_CHECK(RegionCheckpointArchive::decode(bytes,restored)&&restored.sameState(ledger));
    RegionWalFrame frame;
    auto next=restored;
    {
        RegionEntityLedger::Transaction tx(next,true);
        AIVERSE_TEST_CHECK(tx.spawn(11,c));
        AIVERSE_TEST_CHECK(RegionWalTransaction::encode(tx,1,frame));
        tx.commit();
    }
    AIVERSE_TEST_CHECK(RegionWalTransaction::replay(restored,frame)&&restored.sameState(next));
    // Legacy v1 ledger archive has no sealed prefix.
    std::string v1;Archive writer(&v1);uint32_t schema=1,n=1;
    uint64_t revision=1,id=77;RegionEntityRecord record{a,1,1};
    writer(schema,revision,n,id,record);
    Archive reader(v1);RegionEntityLedger legacy;io(reader,legacy);
    AIVERSE_TEST_CHECK(reader.ok()&&legacy.nextIdentity()==78&&legacy.historyFloor()==0&&legacy.retiredFloor()==0);
}

static void checkWorldPersistenceDiagnostics(){
    RegionEntityLedger ledger;
    World world;
    constexpr uint64_t large=9007199254740993ull;
    AIVERSE_TEST_CHECK(ledger.spawn(large,a)&&ledger.spawn(large+1,b)&&ledger.despawn(large+1,b));
    auto e=world.create();world.add<RegionStableIdentity>(e,RegionStableIdentity{large});
    AIVERSE_TEST_CHECK(ledger.compactRetired(ledger.lastRevision())==1);
    auto stats=ledgerStatus(ledger,world);
    AIVERSE_TEST_CHECK(stats.getStr("retiredFloor")==std::to_string(large+1));
    AIVERSE_TEST_CHECK(stats.getStr("nextIdentity")==std::to_string(large+2));
    AIVERSE_TEST_CHECK(stats.getInt("live")==1&&stats.getInt("materialized")==1&&stats.getInt("tombstones")==0);
    AIVERSE_TEST_CHECK(ledgerPage(ledger,0,10).getBool("resetRequired"));
    AIVERSE_TEST_CHECK(ledgerPage(ledger,0,10,true,ledger.lastRevision()-1).getBool("resetRequired"));
    auto page=ledgerPage(ledger,0,10,true,ledger.lastRevision());
    AIVERSE_TEST_CHECK(!page.getBool("resetRequired")&&page["records"].size()==1);
    AIVERSE_TEST_CHECK(page["records"][0].getStr("id")==std::to_string(large));
}

int main() {
    checkWorldPersistenceDiagnostics();
    checkLedgerCompaction();
    checkAsyncPersistenceMailbox();
    checkExactMutationCapture();
    checkNativeFinality();
    checkNativeFinality(true);
    checkResourceWal();
    checkUnifiedBackupCatalog();
    checkLedger();
    checkEcsPersistence();
    std::cout<<"PASS region_entity_ledger (transfer, tombstone, archive, ECS v10, rollback, 100 crossings)\n";
}
