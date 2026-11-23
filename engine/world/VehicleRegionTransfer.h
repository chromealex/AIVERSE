// Generic vehicle/occupant atomic region transfer policy. Uses the ONE
// vehicles::VehicleDrive/VehicleOccupant ECS model; no alternative car store.
#pragma once
#include "engine/vehicles/Vehicles.h"
#include "engine/world/RegionEcsBridge.h"
#include <algorithm>
#include <cstdint>
#include <functional>
#include <set>
#include <span>
#include <string>
#include <vector>

namespace aiverse::world {

// Some hosts store a seat's actor as an integer slot rather than ECS Entity.
// Supply one mapping at the host boundary; never cast a seat number to Entity.
using SeatEntityResolver = std::function<Entity(int64_t)>;

struct VehicleRegionGroup {
    Entity vehicle=kNullEntity;
    std::vector<Entity> members; // includes vehicle and every known occupant, sorted
};

class VehicleRegionTransfer {
    static bool fail(std::string* error,const char* reason){
        if(error)*error=reason;
        return false;
    }
public:
    // Read-only discovery: no ECS mutation and no RNG/renderer access.
    // All occupants must be resolved, live and unambiguously associated with
    // the same vehicle. Refuse missing or stale links rather than dropping an
    // occupant when crossing a streaming border.
    static bool gather(const World& world,Entity vehicle,
                       const SeatEntityResolver& resolveSeat,
                       VehicleRegionGroup& output,std::string* error=nullptr) {
        if(!world.alive(vehicle))
            return fail(error,"vehicle entity is dead");
        const auto* drive=world.tryGetConst<vehicles::VehicleDrive>(vehicle);
        if(!drive)return fail(error,"entity is not a shared VehicleDrive");
        if(!resolveSeat && !drive->seats.occupants.empty())
            return fail(error,"seat mapping is required for occupied vehicle");
        if(drive->driver.valid() && !world.alive(drive->driver))
            return fail(error,"vehicle driver is stale");
        std::set<uint64_t> packed;
        std::vector<Entity> members;
        auto append=[&](Entity e)->bool {
            if(!e.valid() || !world.alive(e))return false;
            if(packed.insert(e.packed()).second)members.push_back(e);
            return true;
        };
        if(!append(vehicle))return fail(error,"vehicle is invalid");
        if(drive->driver.valid()) {
            const auto* occupant=world.tryGetConst<vehicles::VehicleOccupant>(drive->driver);
            if(occupant && occupant->vehicle!=vehicle)
                return fail(error,"driver occupant points to another vehicle");
            if(!append(drive->driver))return fail(error,"vehicle driver is missing");
        }
        std::set<int64_t> seatActors;
        for(const auto& [seat,actor]:drive->seats.occupants){
            if(seat<=0 || actor<=0 || !seatActors.insert(actor).second)
                return fail(error,"duplicate or invalid seat occupants");
            Entity e=resolveSeat(actor);
            if(!e.valid() || !world.alive(e) || e==vehicle)
                return fail(error,"seat actor has no live ECS entity");
            const auto* reverse=world.tryGetConst<vehicles::VehicleOccupant>(e);
            if(reverse && reverse->vehicle!=vehicle)
                return fail(error,"seat actor belongs to another vehicle");
            if(!append(e))return fail(error,"cannot resolve seat occupant");
        }
        // Scan backwards references so a driver/occupant cannot be omitted
        // from a transfer by a stale seat manifest or host lookup.
        bool conflict=false;
        world.eachExistingConst<vehicles::VehicleOccupant>(
            [&](Entity e,const vehicles::VehicleOccupant& o){
                if(o.vehicle==vehicle) {
                    if(!append(e))conflict=true;
                }else if(packed.contains(e.packed())) {
                    conflict=true;
                }
            });
        if(conflict)return fail(error,"inconsistent reverse occupant link");
        std::sort(members.begin(),members.end(),[](Entity a,Entity b){
            return a.packed()<b.packed();
        });
        VehicleRegionGroup result;
        result.vehicle=vehicle;
        result.members=std::move(members);
        output=std::move(result);
        return true;
    }

    // Requires each member to have RegionStableIdentity + an authoritative position and to
    // belong to expected region. RegionEcsBridge validates ALL before changing
    // any owner; caller separately gates destination collision/nav readiness.
    static bool transfer(const World& world,RegionEntityLedger& ledger,
                         Entity vehicle,const SeatEntityResolver& resolveSeat,
                         Cell expected,Cell destination,
                         const RegionPositionReader& position,
                         std::string* error=nullptr) {
        VehicleRegionGroup group;
        if(!gather(world,vehicle,resolveSeat,group,error))return false;
        return RegionEcsBridge::transferGroup(
            world,ledger,std::span<const Entity>(group.members),
            expected,destination,position,error);
    }
    // Existing Transform-only owners keep their source and simulation format.
    static bool transfer(const World& world,RegionEntityLedger& ledger,
                         Entity vehicle,const SeatEntityResolver& resolveSeat,
                         Cell expected,Cell destination,
                         std::string* error=nullptr) {
        return transfer(world,ledger,vehicle,resolveSeat,expected,destination,
                        regionTransformPosition,error);
    }
};

} // namespace aiverse::world
