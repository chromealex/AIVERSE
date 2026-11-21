// Typed FPS vehicles over the vehicle-controllers package (libraries/.../genre/Vehicle*.h): map vehicles with a
// `type` from `fps.vehicleTypes` drive with the package controller (wheeled / tracked / hover / flight) on the
// ground height under them, collide with static geometry through the package's collision response, and carry a
// driver plus passengers / gunners through the package's seats. Legacy vehicles (no type) are untouched.
// Passengers and gunners may shoot; the driver steers. A destroyed vehicle kills its occupants.
#include "FpsModule.h"
#include "libraries/include/aiverselib/genre/VehiclePersistence.h"
#include "libraries/include/aiverselib/genre/VehicleDrive.h"
#include "engine/core/FixedMath.h"
#include <algorithm>
namespace aiverse::fps {
using namespace lib::genre;
VehicleRun* Module::vehicleRun(State& s,uint32_t vehicle) const {
    for(auto& r:s.vehicleRuns)if(r.vehicle==vehicle)return &r;return nullptr;
}
void Module::setupVehicles(Simulation&,const Json& map,State& s) {
    s.vehicleRuns.clear();
    const auto& list=map["vehicles"];if(!list.isArray())return;
    for(size_t i=0;i<list.size()&&i<s.vehicles.size();++i){
        const auto& j=list[i];if(!j.has("type"))continue;
        if(!config_.vehicleTypes)throw JsonError("fps vehicle.type: requires fps.vehicleTypes");
        auto type=j.getInt("type");auto it=config_.vehicleTypes->vehicles.find(type);
        if(it==config_.vehicleTypes->vehicles.end())throw JsonError("fps vehicle.type: unknown vehicle type "+std::to_string(type));
        if(j.has("yaw")&&!j["yaw"].isNumber())throw JsonError("fps vehicle.yaw: expected turns");
        auto& v=s.vehicles[i];v.yaw=j.getFx("yaw",Fixed{});
        VehicleRun run;run.vehicle=v.id;run.type=type;run.controller=makeVehicleState(it->second.controller,v.pos,v.yaw);
        s.vehicleRuns.push_back(std::move(run));
    }
}
bool Module::canFireFromVehicle(State& s,const Actor& a) const {
    auto* run=vehicleRun(s,a.vehicle);if(!run)return false;
    const auto& def=config_.vehicleTypes->vehicles.at(run->type);auto seat=vehicleSeatOf(run->seats,a.slot);
    for(const auto& d:def.seats.seats)if(d.id==seat)return d.role!=VehicleSeatRole::Driver;
    return false;
}
// Interact on or in a typed vehicle becomes a seat request handled by tickVehicles this tick. False: not typed.
bool Module::vehicleInteract(Simulation& sim,Actor& a) {
    auto& s=state(sim);if(s.vehicleRuns.empty())return false;
    if(s.race.enabled)return true;   // racers stay at the wheel: Interact does nothing during a race
    VehicleSeatRequest r;r.actor=a.slot;r.team=a.team;r.actorPosition=a.pos;
    if(a.vehicle){
        auto* run=vehicleRun(s,a.vehicle);if(!run)return false;
        r.id=run->seats.lastRequest+1+int64_t(run->requests.size());r.action=VehicleSeatAction::Exit;r.seat=vehicleSeatOf(run->seats,a.slot);
        run->requests.push_back(r);return true;
    }
    VehicleRun* best=nullptr;Fixed bestDistance;
    for(auto& run:s.vehicleRuns){
        const Vehicle* v=nullptr;for(const auto& x:s.vehicles)if(x.id==run.vehicle)v=&x;
        if(!v||v->health<=0)continue;auto d=(v->pos-a.pos).lengthSq();if(d>36_fx)continue;
        if(!best||d<bestDistance){best=&run;bestDistance=d;}
    }
    if(!best)return false;
    r.id=best->seats.lastRequest+1+int64_t(best->requests.size());r.action=VehicleSeatAction::Enter;r.preferred=VehicleSeatRole::Driver;
    best->requests.push_back(r);return true;
}
// The driver's held controls (released after the input timeout) through the shared axes mapping (genre/VehicleDrive.h):
// forward / back -> throttle / brake, right steers, Jump is the handbrake, pitch drives flight.
VehicleInput Module::driverInput(const Actor& a,uint32_t now) const {
    if(now-a.lastInputTick>config_.inputTimeout)return {};
    return vehicleInputFromAxes({Fixed::fromRatio(a.input.forward,1000),Fixed::fromRatio(a.input.right,1000),a.input.pitch,(a.input.buttons&Jump)!=0});
}
// drive: pedals that replace the driver's own for these vehicle ids this tick (race countdown, race bots).
void Module::tickVehicles(Simulation& sim,std::span<const std::pair<uint32_t,VehicleInput>> drive) {
    auto& s=state(sim);if(s.vehicleRuns.empty())return;auto now=sim.tick();
    for(auto& run:s.vehicleRuns){
        Vehicle* v=nullptr;for(auto& x:s.vehicles)if(x.id==run.vehicle)v=&x;if(!v)continue;
        const auto& def=config_.vehicleTypes->vehicles.at(run.type);
        // Seats: enter / exit requests of this tick; an occupant with a free seat ahead takes the driver seat first.
        auto clear=[&](Vec3 p,int64_t){Actor probe;probe.pos=p;auto b=actorBounds(probe,config_.motor);b.min.y+=Fixed::fromRatio(1,100);return !collision_.blocked(b)&&fxAbs(p.x)<config_.worldLimit&&fxAbs(p.z)<config_.worldLimit;};
        std::vector<VehicleSeatRequest> requests;std::swap(requests,run.requests);
        for(auto [seat,actor]:run.seats.occupants){
            const auto& who=s.actors[size_t(actor-1)];if(v->health>0&&who.health>0&&!who.downed&&who.vehicle==v->id)continue;   // knocked / killed / thrown out by damage
            VehicleSeatRequest r;r.id=run.seats.lastRequest+1+int64_t(requests.size());r.actor=actor;r.action=VehicleSeatAction::Eject;r.seat=seat;requests.push_back(r);}
        if(auto report=stepVehicleSeats(def.seats,run.seats,now,run.controller,std::span<const VehicleSeatRequest>(requests),clear)){
            for(const auto& e:report->events){
                if(e.actor<=0||e.actor>int64_t(s.actors.size()))continue;auto& a=s.actors[size_t(e.actor-1)];
                if(e.kind==VehicleEventKind::SeatEntered){a.vehicle=v->id;a.velocity={};}
            }
            for(const auto& x:report->exits){auto& a=s.actors[size_t(x.actor-1)];a.vehicle=0;a.pos=x.position;a.velocity=run.controller.velocity;
                if(v->health<=0)damage(sim,0,a,1000);}
        }
        auto driver=vehicleDriver(def.seats,run.seats);v->driver=uint8_t(driver);
        // Controller step: the driver's held input (released after the input timeout) -> pedals and steering.
        VehicleInput in;
        if(driver&&v->health>0){
            in=driverInput(s.actors[size_t(driver-1)],now);
            for(const auto& [id,x]:drive)if(id==v->id){in=x;break;}
        }else in.brake=1_fx;
        auto under=run.controller.position+Vec3{{},2_fx,{}};auto hit=collision_.cast(under,{{},-64_fx,{}});
        VehicleContacts k;k.groundHeight=hit.hit?under.y-64_fx*hit.fraction:groundBelow(run.controller.position);k.groundNormal=hit.hit&&hit.normal.y>Fixed{}?hit.normal:Vec3{{},1_fx,{}};
        k.grounded=run.controller.position.y<=k.groundHeight+Fixed::fromRatio(1,10);
        auto before=run.controller.position;
        if(stepVehicle(def.controller,run.controller,now,sim.dt(),in,k)){
            auto delta=run.controller.position-before;Vec3 half{1_fx,Fixed::fromRatio(1,2),2_fx};
            auto sweep=collision_.cast(before+Vec3{{},1_fx,{}},Vec3{delta.x,{},delta.z},half);
            if(sweep.hit&&sweep.normal.y<Fixed::fromRatio(1,2)){
                auto stop=before+Vec3{delta.x,{},delta.z}*fxMax(Fixed{},sweep.fraction-Fixed::fromRatio(1,100));stop.y=run.controller.position.y;
                auto impulse=vehicleResolveCollision(run.controller,stop,sweep.normal);
                if(impulse>8_fx&&(!raceRules_||raceRules_->crashDamage))v->health=std::max(0,v->health-int32_t((impulse-8_fx).toInt()*10));   // hard crash
            }
            auto& p=run.controller.position;p.x=fxClamp(p.x,-config_.worldLimit,config_.worldLimit);p.z=fxClamp(p.z,-config_.worldLimit,config_.worldLimit);
        }
        v->pos=run.controller.position;v->yaw=run.controller.yaw;v->velocity=run.controller.velocity;
        for(auto [seat,actor]:run.seats.occupants){
            if(actor<=0||actor>int64_t(s.actors.size()))continue;auto& a=s.actors[size_t(actor-1)];
            for(const auto& d:def.seats.seats)if(d.id==seat)a.pos=vehicleWorldPoint(run.controller,d.entry)+Vec3{{},Fixed::fromRatio(1,2),{}};
            a.velocity=run.controller.velocity;a.grounded=true;
        }
    }
}
void ioVehicleRuns(Archive& a,std::vector<VehicleRun>& runs) {
    uint32_t n=a.count(runs.size());if(n>32){a.fail();return;}if(!a.writing())runs.resize(n);
    for(auto& r:runs){a(r.vehicle,r.type);lib::genre::io(a,r.controller);lib::genre::io(a,r.seats);}
}
}
