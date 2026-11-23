#include "engine/world/RegionDirtyQueue.h"
#include "tests/AlwaysCheck.h"
#include <cstdint>
#include <iostream>
#include <string>

using namespace aiverse;
using namespace aiverse::world;

static std::string save(RegionDirtyQueue queue) {
    std::string data;
    Archive out(&data);
    io(out,queue);
    AIVERSE_TEST_CHECK(out.ok());
    return data;
}

static void testDirtyCoalescingAndArchive() {
    World world;
    const Entity first=world.create();
    const Entity second=world.create();
    RegionDirtyQueue a;
    AIVERSE_TEST_CHECK(a.mark(first,RegionDirtyKind::Spawn));
    AIVERSE_TEST_CHECK(a.mark(first,RegionDirtyKind::Move));
    AIVERSE_TEST_CHECK(a.mark(first,RegionDirtyKind::Retire));
    AIVERSE_TEST_CHECK(a.mark(second,RegionDirtyKind::Move));
    AIVERSE_TEST_CHECK(a.entries.size()==2);
    AIVERSE_TEST_CHECK(a.marked(first) && a.marked(second));
    AIVERSE_TEST_CHECK(a.entries.at(first.packed()+1)==7);
    AIVERSE_TEST_CHECK(a.entries.at(second.packed()+1)==2);

    a.acknowledged=120;
    const auto bytes=save(a);
    RegionDirtyQueue restored;
    Archive reader(bytes);
    io(reader,restored);
    AIVERSE_TEST_CHECK(reader.ok() && reader.remaining()==0);
    AIVERSE_TEST_CHECK(restored.entries==a.entries);
    AIVERSE_TEST_CHECK(restored.acknowledged==120);
    AIVERSE_TEST_CHECK(save(restored)==bytes);

    RegionDirtyQueue preserved=restored;
    const auto original=save(preserved);
    const auto truncated=bytes.substr(0,bytes.size()-1);
    Archive shortened(truncated);
    io(shortened,preserved);
    AIVERSE_TEST_CHECK(!shortened.ok());
    AIVERSE_TEST_CHECK(save(preserved)==original);
    auto invalid=bytes;
    invalid[0]=char(99);
    Archive corrupted(invalid);
    io(corrupted,preserved);
    AIVERSE_TEST_CHECK(!corrupted.ok());
    AIVERSE_TEST_CHECK(save(preserved)==original);
}
static void testCowAndRecycledEntityKeys() {
    World world;
    world.enableCowSnapshots();
    world.setResource<RegionDirtyQueue>(RegionDirtyQueue{});
    Entity old=world.create();
    AIVERSE_TEST_CHECK(world.resource<RegionDirtyQueue>()->mark(
        old,RegionDirtyKind::Spawn));
    World snapshot=world;
    world.destroy(old);
    const Entity recycled=world.create();
    AIVERSE_TEST_CHECK(recycled.index==old.index);
    AIVERSE_TEST_CHECK(recycled.gen!=old.gen);
    AIVERSE_TEST_CHECK(world.resource<RegionDirtyQueue>()->mark(
        recycled,RegionDirtyKind::Spawn));
    const auto* queue=world.resourceConst<RegionDirtyQueue>();
    AIVERSE_TEST_CHECK(queue && queue->entries.size()==2);
    AIVERSE_TEST_CHECK(queue->marked(old) && queue->marked(recycled));
    world=snapshot;
    const auto* previous=world.resourceConst<RegionDirtyQueue>();
    AIVERSE_TEST_CHECK(previous && previous->entries.size()==1);
    AIVERSE_TEST_CHECK(previous->marked(old) && !previous->marked(recycled));
}
int main() {
    testDirtyCoalescingAndArchive();
    testCowAndRecycledEntityKeys();
    std::cout<<"PASS region dirty queue coalescing/archive/COW rollback\n";
}
