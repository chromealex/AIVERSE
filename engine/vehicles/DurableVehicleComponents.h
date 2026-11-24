#pragma once
#include "Vehicles.h"
#include "engine/world/DurableComponentCodecs.h"

namespace aiverse::vehicles {
// Shared portable vehicle codec; the native rollback/archive wire format
// stays unchanged. The host owns stable bindings and seat-ID semantics.
inline bool registerDurableVehicleComponents(world::DurableComponentCodecs& codecs){
    using Context=world::DurableComponentCodecs::Context;
    return codecs.addContextual<VehicleDrive>("aiverse.vehicles.drive",1,
        [](Archive& a,VehicleDrive& d,const World& w,const Context& refs){
            if(a.writing()&&!d.requests.empty())return false;
            a(d.type);lm::io(a,d.controller);
            if(!world::ioPersistentEntity(a,w,refs,d.driver))return false;
            a(d.player,d.doorUntil,d.locked,d.deck,d.autopilot);
            a(d.pilot.throttle,d.pilot.brake,d.pilot.steer,d.pilot.pitch,d.pilot.roll,
                d.pilot.yaw,d.pilot.handbrake,d.pilot.shiftUp,d.pilot.shiftDown);
            lm::io(a,d.seats);
            return a.ok()&&valid(d);
        })&&codecs.addContextual<VehicleOccupant>("aiverse.vehicles.occupant",1,
        [](Archive& a,VehicleOccupant& o,const World& w,const Context& refs){
            if(!world::ioPersistentEntity(a,w,refs,o.vehicle))return false;
            a(o.type,o.position,o.yaw,o.speed,o.deck);
            return a.ok()&&o.vehicle.valid()&&o.deck>=-1;
        });
}
}
