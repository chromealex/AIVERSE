#include "engine/sim/SharedStaticPersistence.h"
#include "tests/AlwaysCheck.h"
#include <array>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

namespace sample {
struct ActorConfig {
    int32_t hp = 0;
    int32_t velocity = 0;
    std::array<int32_t,20> attributes{};
    bool operator==(const ActorConfig&) const = default;
};
inline void io(aiverse::Archive& archive, ActorConfig& value) {
    archive(value.hp, value.velocity);
    for(auto& v:value.attributes) archive(v);
}
inline ActorConfig config(int32_t hp) {
    ActorConfig result;
    result.hp=hp;result.velocity=hp*2;
    for(size_t i=0;i<result.attributes.size();++i)
        result.attributes[i]=hp+int32_t(i);
    return result;
}
struct ReadonlyConfig {
    int32_t capacity=0;
    std::array<int32_t,20> slots{};
    bool operator==(const ReadonlyConfig&) const = default;
};
inline void io(aiverse::Archive& archive,ReadonlyConfig& value) {
    archive(value.capacity);
    for(auto& v:value.slots)archive(v);
}
inline ReadonlyConfig staticConfig(int32_t x) {
    ReadonlyConfig out;out.capacity=x;
    for(size_t i=0;i<out.slots.size();++i)out.slots[i]=x+int32_t(i);
    return out;
}
}

using namespace aiverse;
namespace st = aiverse::storage;

static_assert(sizeof(st::SharedRef<sample::ActorConfig>)==4);
static_assert(sizeof(st::StaticRef<sample::ReadonlyConfig>)==4);
static_assert(std::is_same_v<
    decltype(std::declval<const st::StaticTable<sample::ReadonlyConfig>&>()
         .get(st::StaticRef<sample::ReadonlyConfig>{})),
    const sample::ReadonlyConfig&>);

static std::shared_ptr<const st::StaticTable<sample::ReadonlyConfig>> table() {
    st::StaticTableBuilder<sample::ReadonlyConfig> builder;
    AIVERSE_TEST_CHECK(builder.define(sample::staticConfig(100)).slot==1);
    AIVERSE_TEST_CHECK(builder.define(sample::staticConfig(100)).slot==1);
    AIVERSE_TEST_CHECK(builder.define(sample::staticConfig(200)).slot==2);
    auto immutable=builder.seal();
    AIVERSE_TEST_CHECK(immutable->uniqueCount()==2);
    AIVERSE_TEST_CHECK(immutable->get({2}).capacity==200);
    AIVERSE_TEST_CHECK(immutable->tryGet({3})==nullptr);
    bool threw=false;
    try { (void)builder.define(sample::staticConfig(300)); }
    catch(const std::logic_error&) {threw=true;}
    AIVERSE_TEST_CHECK(threw);
    return immutable;
}

static void internedAndStatic() {
    auto immutable=table();
    World w;
    AIVERSE_TEST_CHECK(st::bindStaticTable(w,*immutable));
    Entity a=w.create(),b=w.create(),c=w.create();
    const auto sharedA=st::setShared(w,a,sample::config(5));
    const auto sharedB=st::setShared(w,b,sample::config(5));
    const auto sharedC=st::setShared(w,c,sample::config(8));
    AIVERSE_TEST_CHECK(sharedA==sharedB && sharedA!=sharedC);
    AIVERSE_TEST_CHECK(sharedA.slot==1 && sharedC.slot==2);
    AIVERSE_TEST_CHECK(st::readShared<sample::ActorConfig>(w,a)->hp==5);
    AIVERSE_TEST_CHECK(st::readShared<sample::ActorConfig>(w,b)->hp==5);
    AIVERSE_TEST_CHECK(w.resourceConst<st::SharedStore<sample::ActorConfig>>()->uniqueCount()==2);
    AIVERSE_TEST_CHECK(st::setStatic(w,a,{1},*immutable));
    AIVERSE_TEST_CHECK(st::setStatic(w,b,{1},*immutable));
    AIVERSE_TEST_CHECK(st::setStatic(w,c,{2},*immutable));
    AIVERSE_TEST_CHECK(!st::setStatic(w,b,{2},*immutable)); // bind once
    AIVERSE_TEST_CHECK(st::readStatic(w,a,*immutable)->capacity==100);
    AIVERSE_TEST_CHECK(st::validateShared<sample::ActorConfig>(w));
    AIVERSE_TEST_CHECK(st::validateStatic(w,*immutable));

    st::SharedReadView<sample::ActorConfig> sharedView(w);
    st::StaticReadView<sample::ReadonlyConfig> staticView(w,*immutable);
    AIVERSE_TEST_CHECK(sharedView.valid()&&staticView.valid());
    int sumShared=0,sumStatic=0;
    w.eachExistingConst<st::SharedRef<sample::ActorConfig>>(
        [&](Entity,const auto& ref){
            const auto* value=sharedView.get(ref);
            AIVERSE_TEST_CHECK(value);
            sumShared+=value->hp;
        });
    w.eachExistingConst<st::StaticRef<sample::ReadonlyConfig>>(
        [&](Entity,const auto& ref){
            const auto* value=staticView.get(ref);
            AIVERSE_TEST_CHECK(value);
            sumStatic+=value->capacity;
        });
    AIVERSE_TEST_CHECK(sumShared==18 && sumStatic==400);

    w.enableCowSnapshots(true);
    World checkpoint=w;
    AIVERSE_TEST_CHECK(st::updateShared<sample::ActorConfig>(
        w,a,[](auto& value){value.hp=30;}));
    AIVERSE_TEST_CHECK(st::readShared<sample::ActorConfig>(w,a)->hp==30);
    AIVERSE_TEST_CHECK(st::readShared<sample::ActorConfig>(w,b)->hp==5);
    AIVERSE_TEST_CHECK(st::readShared<sample::ActorConfig>(checkpoint,a)->hp==5);
    AIVERSE_TEST_CHECK(w.resourceConst<st::SharedStore<sample::ActorConfig>>()->uniqueCount()==3);
    w.destroy(c);
    // There are three unique records but only two live values.
    const size_t removed=st::compactShared<sample::ActorConfig>(w);
    AIVERSE_TEST_CHECK(removed==1);
    AIVERSE_TEST_CHECK(w.resourceConst<st::SharedStore<sample::ActorConfig>>()->uniqueCount()==2);
    AIVERSE_TEST_CHECK(st::readShared<sample::ActorConfig>(w,a)->hp==30);
    AIVERSE_TEST_CHECK(st::readShared<sample::ActorConfig>(w,b)->hp==5);
    AIVERSE_TEST_CHECK(st::readShared<sample::ActorConfig>(checkpoint,c)->hp==8);
    AIVERSE_TEST_CHECK(st::validateShared<sample::ActorConfig>(w));
    w.copyFrom(checkpoint);
    AIVERSE_TEST_CHECK(w.alive(c));
    AIVERSE_TEST_CHECK(st::readShared<sample::ActorConfig>(w,a)->hp==5);
    AIVERSE_TEST_CHECK(st::validateStatic(w,*immutable));
}

class NativeModule final : public GameModule {
public:
    explicit NativeModule(std::shared_ptr<const st::StaticTable<sample::ReadonlyConfig>> defs)
        : definitions(std::move(defs)) {}
    std::string name() const override {return "native-shared-static-fixture";}
    std::shared_ptr<const st::StaticTable<sample::ReadonlyConfig>> definitions;
    void setup(Simulation& sim,const Json&,const Json&) override {
        st::registerSharedPersistence<sample::ActorConfig>(sim,"sample.actor",1);
        st::registerStaticPersistence<sample::ReadonlyConfig>(sim,"sample.static",1);
        World& w=sim.world();
        AIVERSE_TEST_CHECK(st::bindStaticTable(w,*definitions));
        for(int i=0;i<6;++i) {
            Entity e=w.create();
            auto ref=st::setShared(w,e,sample::config(i%2?5:10));
            AIVERSE_TEST_CHECK(ref.valid());
            AIVERSE_TEST_CHECK(st::setStatic(w,e,{uint32_t(i%2+1)},*definitions));
        }
    }
    void applyCommand(Simulation&,const Command&) override {}
    void hashState(Simulation&,Hasher&) override {}
    bool serializeState(Simulation&,Archive&) override {return true;}
    int result(Simulation&) override {return 0;}
};

static std::unique_ptr<Simulation> simulation(
    const std::shared_ptr<const st::StaticTable<sample::ReadonlyConfig>>& defs) {
    auto s=std::make_unique<Simulation>(SimConfig{17,20,0,true},
        std::make_unique<NativeModule>(defs));
    s->setup(Json::object(),Json::object());
    return s;
}
static void nativeSaveAndRollback() {
    auto immutable=table();
    auto primary=simulation(immutable);
    const uint64_t original=primary->checksum();
    const std::string bytes=primary->serialize();
    AIVERSE_TEST_CHECK(!bytes.empty());
    SimSnapshot snapshot;
    primary->saveState(snapshot);
    primary->world().eachExistingConst<st::SharedRef<sample::ActorConfig>>(
        [&](Entity e,const auto&) {
            AIVERSE_TEST_CHECK(st::readShared<sample::ActorConfig>(primary->world(),e));
        });
    Entity additional=primary->world().create();
    st::setShared(primary->world(),additional,sample::config(99));
    AIVERSE_TEST_CHECK(primary->checksum()!=original);
    primary->loadState(snapshot);
    AIVERSE_TEST_CHECK(primary->checksum()==original);
    AIVERSE_TEST_CHECK(st::validateShared<sample::ActorConfig>(primary->world()));
    AIVERSE_TEST_CHECK(st::validateStatic(primary->world(),*immutable));

    auto restored=simulation(immutable);
    std::string error;
    AIVERSE_TEST_CHECK(restored->deserialize(bytes,&error));
    AIVERSE_TEST_CHECK(restored->checksum()==original);
    AIVERSE_TEST_CHECK(st::validateShared<sample::ActorConfig>(restored->world()));
    AIVERSE_TEST_CHECK(st::validateStatic(restored->world(),*immutable));

    auto wrong=std::make_shared<st::StaticTableBuilder<sample::ReadonlyConfig>>();
    const auto ignored=wrong->define(sample::staticConfig(999));
    (void)ignored;
    auto wrongTable=wrong->seal();
    AIVERSE_TEST_CHECK(!st::validateStatic(restored->world(),*wrongTable));
    AIVERSE_TEST_CHECK(!st::bindStaticTable(restored->world(),*wrongTable));

    // Review 2026-11-21: a save whose SharedRef slot points past the loaded
    // SharedStore is rejected at load with a readable error, and the target
    // simulation is left unchanged.
    auto corrupt=simulation(immutable);
    Entity victim=kNullEntity;
    corrupt->world().eachExistingConst<st::SharedRef<sample::ActorConfig>>(
        [&](Entity e,const auto&) { if(!victim.valid()) victim=e; });
    AIVERSE_TEST_CHECK(victim.valid());
    corrupt->world().get<st::SharedRef<sample::ActorConfig>>(victim).slot=999;
    AIVERSE_TEST_CHECK(!st::validateShared<sample::ActorConfig>(corrupt->world()));
    const std::string badBytes=corrupt->serialize();
    auto target=simulation(immutable);
    const uint64_t before=target->checksum();
    error.clear();
    AIVERSE_TEST_CHECK(!target->deserialize(badBytes,&error));
    AIVERSE_TEST_CHECK(error.find("sample.actor.shared_ref")!=std::string::npos);
    AIVERSE_TEST_CHECK(error.find("slot 999")!=std::string::npos);
    AIVERSE_TEST_CHECK(target->checksum()==before);
    AIVERSE_TEST_CHECK(st::validateShared<sample::ActorConfig>(target->world()));
}

int main() {
    internedAndStatic();
    nativeSaveAndRollback();
    std::cout<<"PASS shared/static immutable refs, COW, compaction, native v10 save and rollback\n";
}
