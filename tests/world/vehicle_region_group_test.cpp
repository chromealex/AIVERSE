#include "engine/world/VehicleRegionTransfer.h"
#include "tests/AlwaysCheck.h"
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <map>
#include <string>
#include <vector>

using namespace aiverse;
namespace w=aiverse::world;
namespace vs=aiverse::vehicles;

namespace {
constexpr w::Cell first{0,0,0},next{1,0,0},far{2,0,0};
struct Fixture {
    World world;
    w::RegionEntityLedger ledger;
    Entity car=kNullEntity,driver=kNullEntity,passenger=kNullEntity,otherCar=kNullEntity;
    std::map<int64_t,Entity> slots;
    Fixture() {
        car=world.create();
        driver=world.create();
        passenger=world.create();
        otherCar=world.create();
        for(Entity e:{car,driver,passenger,otherCar}) {
            world.add<Transform>(e,Transform{});
        }
        AIVERSE_TEST_CHECK(w::RegionEcsBridge::bindNew(world,ledger,car,1,first));
        AIVERSE_TEST_CHECK(w::RegionEcsBridge::bindNew(world,ledger,driver,2,first));
        AIVERSE_TEST_CHECK(w::RegionEcsBridge::bindNew(world,ledger,passenger,3,first));
        AIVERSE_TEST_CHECK(w::RegionEcsBridge::bindNew(world,ledger,otherCar,4,first));
        vs::VehicleDrive drive;
        drive.driver=driver;
        drive.seats.occupants={{10,101},{20,102}};
        world.add<vs::VehicleDrive>(car,drive);
        vs::VehicleOccupant d;
        d.vehicle=car;
        world.add<vs::VehicleOccupant>(driver,d);
        slots[101]=driver;
        slots[102]=passenger;
    }
    w::SeatEntityResolver resolver()const {
        return [this](int64_t slot)->Entity {
            const auto it=slots.find(slot);
            return it==slots.end()?kNullEntity:it->second;
        };
    }
};
void groupingAndTransfer() {
    Fixture f;
    std::string reason;
    w::VehicleRegionGroup group;
    AIVERSE_TEST_CHECK(w::VehicleRegionTransfer::gather(
        f.world,f.car,f.resolver(),group,&reason));
    AIVERSE_TEST_CHECK(group.vehicle==f.car);
    AIVERSE_TEST_CHECK(group.members.size()==3);
    AIVERSE_TEST_CHECK(std::find(group.members.begin(),group.members.end(),f.car)!=group.members.end());
    AIVERSE_TEST_CHECK(std::find(group.members.begin(),group.members.end(),f.driver)!=group.members.end());
    AIVERSE_TEST_CHECK(std::find(group.members.begin(),group.members.end(),f.passenger)!=group.members.end());
    const auto before=f.ledger.lastRevision();
    AIVERSE_TEST_CHECK(w::VehicleRegionTransfer::transfer(
        f.world,f.ledger,f.car,f.resolver(),first,next,&reason));
    AIVERSE_TEST_CHECK(f.ledger.lastRevision()==before+3);
    AIVERSE_TEST_CHECK(f.ledger.activeIn(next)==std::vector<uint64_t>({1,2,3}));
    AIVERSE_TEST_CHECK(f.ledger.activeIn(first)==std::vector<uint64_t>({4}));
    AIVERSE_TEST_CHECK(!w::VehicleRegionTransfer::transfer(
        f.world,f.ledger,f.car,f.resolver(),first,far,&reason));
    AIVERSE_TEST_CHECK(f.ledger.activeIn(next)==std::vector<uint64_t>({1,2,3}));
    AIVERSE_TEST_CHECK(w::VehicleRegionTransfer::transfer(
        f.world,f.ledger,f.car,f.resolver(),next,far,&reason));
}
void failuresAreAtomic() {
    Fixture f;
    std::string reason;
    const auto baseline=f.ledger.lastRevision();
    auto* drive=f.world.tryGet<vs::VehicleDrive>(f.car);
    AIVERSE_TEST_CHECK(drive);
    f.slots.erase(102); // unresolved passenger
    AIVERSE_TEST_CHECK(!w::VehicleRegionTransfer::transfer(
        f.world,f.ledger,f.car,f.resolver(),first,next,&reason));
    AIVERSE_TEST_CHECK(f.ledger.lastRevision()==baseline);
    f.slots[102]=f.passenger;
    drive->seats.occupants[30]=101; // duplicate seat identity
    AIVERSE_TEST_CHECK(!w::VehicleRegionTransfer::transfer(
        f.world,f.ledger,f.car,f.resolver(),first,next,&reason));
    AIVERSE_TEST_CHECK(f.ledger.lastRevision()==baseline);
    drive->seats.occupants.erase(30);
    vs::VehicleOccupant other;
    other.vehicle=f.otherCar;
    f.world.add<vs::VehicleOccupant>(f.passenger,other);
    AIVERSE_TEST_CHECK(!w::VehicleRegionTransfer::transfer(
        f.world,f.ledger,f.car,f.resolver(),first,next,&reason));
    AIVERSE_TEST_CHECK(f.ledger.lastRevision()==baseline);
    f.world.remove<vs::VehicleOccupant>(f.passenger);
    // Missing stable region binding (the seat is live but not part of W1)
    f.world.remove<w::RegionStableIdentity>(f.passenger);
    AIVERSE_TEST_CHECK(!w::VehicleRegionTransfer::transfer(
        f.world,f.ledger,f.car,f.resolver(),first,next,&reason));
    AIVERSE_TEST_CHECK(f.ledger.lastRevision()==baseline);
    f.world.add<w::RegionStableIdentity>(f.passenger,w::RegionStableIdentity{3});
    f.world.add<w::RegionDormant>(f.driver,w::RegionDormant{});
    AIVERSE_TEST_CHECK(!w::VehicleRegionTransfer::transfer(
        f.world,f.ledger,f.car,f.resolver(),first,next,&reason));
    AIVERSE_TEST_CHECK(f.ledger.lastRevision()==baseline);
    f.world.remove<w::RegionDormant>(f.driver);
    AIVERSE_TEST_CHECK(w::VehicleRegionTransfer::transfer(
        f.world,f.ledger,f.car,f.resolver(),first,next,&reason));
}
void backwardsReferencesNotLost() {
    Fixture f;
    std::string reason;
    // Other seat backlinks may be present even before a host resolves them
    // in the typed seat manifest. They must not be silently left behind.
    auto extra=f.world.create();
    f.world.add<Transform>(extra,Transform{});
    AIVERSE_TEST_CHECK(w::RegionEcsBridge::bindNew(f.world,f.ledger,extra,5,first));
    vs::VehicleOccupant back;
    back.vehicle=f.car;
    f.world.add<vs::VehicleOccupant>(extra,back);
    w::VehicleRegionGroup group;
    AIVERSE_TEST_CHECK(w::VehicleRegionTransfer::gather(
        f.world,f.car,f.resolver(),group,&reason));
    AIVERSE_TEST_CHECK(group.members.size()==4);
    AIVERSE_TEST_CHECK(w::VehicleRegionTransfer::transfer(
        f.world,f.ledger,f.car,f.resolver(),first,next,&reason));
    AIVERSE_TEST_CHECK(f.ledger.find(5)->owner==next);
    // A truly stale backlink is a hard failure, not a silent ignore.
    f.world.destroy(extra);
    AIVERSE_TEST_CHECK(!w::VehicleRegionTransfer::transfer(
        f.world,f.ledger,f.car,f.resolver(),next,far,&reason));
}
}
int main() {
    groupingAndTransfer();
    failuresAreAtomic();
    backwardsReferencesNotLost();
    std::cout<<"PASS vehicle region atomic occupancy groups, bad links and dormant refusal\n";
}
