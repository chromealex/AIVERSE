// Typed vehicles and races on a metre map: the FPS host of the one vehicle system (engine/vehicles/Vehicles.h,
// VehicleRace.h). Map vehicles with a `type` from `fps.vehicleTypes` are Vehicle entities with the shared VehicleDrive
// component; the package controller (wheeled / tracked / hover / flight) steps through vehicles::stepVehicles. This
// file is only what the metre map knows: the ground height under a vehicle (collision_ casts, groundBelow), the body
// sweep against the static triangle world, the seat list glue (passengers / gunners; Interact = seat requests), the
// characters' held controls as the driver's pedals (shared axes mapping, mechanics/VehicleDrive.h), the Vehicle
// health, and the race seats (player i drives the i-th typed map vehicle in ascending vehicle id).
// Vehicles without a type keep the arcade car (ActorSystems actorSystems). Passengers and gunners may shoot; the driver
// steers. A destroyed vehicle ejects and kills its occupants.
#include "ActorModule.h"
#include "engine/mechanics/VehiclePersistence.h"
#include "engine/mechanics/VehicleDrive.h"
#include "engine/core/FixedMath.h"
#include <algorithm>
#include <set>
namespace aiverse::actor {
using namespace lib::mechanics;
namespace vs = vehicles;
namespace {
const VehicleSeatDef* driverSeat(const VehicleSeatsDef& d){for(const auto& seat:d.seats)if(seat.role==VehicleSeatRole::Driver)return &seat;return nullptr;}
}

// ---------------------------------------------------------------- the vehicle system's world query on a metre map
class VehicleWorld final : public vs::VehicleHost {
public:
    VehicleWorld(ActorHost& m,Simulation& sim):m_(m),sim_(sim),w_(sim.world()),now_(sim.tick()){}
    bool driverPedals(Entity e,const vs::VehicleDrive& d,VehicleInput& in) override {
        const Vehicle* v=w_.tryGet<Vehicle>(e);if(!d.player||!v||v->health<=0)return false;
        in=m_.driverInput(m_.character(static_cast<const Simulation&>(sim_),d.player),now_);return true;
    }
    VehicleInput parked(Entity,const vs::VehicleDrive&) override {VehicleInput in;in.brake=1_fx;return in;}
    bool controllable(Entity e,const vs::VehicleDrive& d) override {const Vehicle* v=w_.tryGet<Vehicle>(e);return d.player&&v&&v->health>0;}
    VehicleContacts ground(Entity,const vs::VehicleDrive& d) override {
        auto under=d.controller.position+Vec3{{},2_fx,{}};auto hit=m_.collision_.cast(under,{{},-64_fx,{}});
        VehicleContacts k;k.groundHeight=hit.hit?under.y-64_fx*hit.fraction:m_.groundBelow(d.controller.position);k.groundNormal=hit.hit&&hit.normal.y>Fixed{}?hit.normal:Vec3{{},1_fx,{}};
        k.grounded=d.controller.position.y<=k.groundHeight+Fixed::fromRatio(1,10);
        return k;
    }
    void sweep(Entity,vs::VehicleDrive& d,const VehicleStepResult& step,vs::VehicleImpacts& impacts) override {
        const auto before=step.from;auto delta=d.controller.position-before;Vec3 half{1_fx,Fixed::fromRatio(1,2),2_fx};
        auto hit=m_.collision_.cast(before+Vec3{{},1_fx,{}},Vec3{delta.x,{},delta.z},half);
        if(hit.hit&&hit.normal.y<Fixed::fromRatio(1,2)){
            auto stop=before+Vec3{delta.x,{},delta.z}*fxMax(Fixed{},hit.fraction-Fixed::fromRatio(1,100));stop.y=d.controller.position.y;
            vs::VehicleSweep s;s.hit=true;s.impact=vehicleResolveCollision(d.controller,stop,hit.normal);
            impacts.apply(s);   // hard crash: (impact - 8 m/s) * 10 health (racers: rules race.crashDamage)
        }
        auto& p=d.controller.position;const auto limit=m_.config_.worldLimit;p.x=fxClamp(p.x,-limit,limit);p.z=fxClamp(p.z,-limit,limit);
    }
    void placed(Entity e,vs::VehicleDrive& d) override {
        Vehicle* v=w_.tryGet<Vehicle>(e);if(!v)return;
        v->pos=d.controller.position;v->yaw=d.controller.yaw;v->velocity=d.controller.velocity;
        const auto* type=m_.drive_.type(d.type);if(!type)return;
        const auto count=int64_t(m_.characterCount(sim_));
        for(auto [seat,actor]:d.seats.occupants){
            if(actor<=0||actor>count)continue;auto a=m_.character(sim_,uint8_t(actor));
            for(const auto& s:type->seats.seats)if(s.id==seat)a.pos=vehicleWorldPoint(d.controller,s.entry)+Vec3{{},Fixed::fromRatio(1,2),{}};
            a.velocity=d.controller.velocity;a.grounded=true;
        }
    }
    void damage(Entity victim,Fixed amount,Entity,uint8_t) override {
        if(Vehicle* v=w_.tryGet<Vehicle>(victim))v->health=std::max(0,v->health-int32_t(amount.toInt()));
    }
private:
    ActorHost& m_;Simulation& sim_;World& w_;uint32_t now_;
};

// ---------------------------------------------------------------- the race's seats (engine/vehicles/VehicleRace.h)
class RaceSeats final : public vs::RaceHost {
public:
    RaceSeats(ActorHost& m,Simulation& sim):m_(m),sim_(sim),w_(sim.world()){}
    void placeOnGrid(size_t,Entity,vs::VehicleDrive& d,const RaceGridSlot& slot) override {
        d.controller=makeVehicleState(m_.drive_.type(d.type)->def,slot.position,slot.yaw);
    }
    bool seat(size_t i,Entity car,vs::VehicleDrive& d) override {
        auto& v=*w_.tryGet<Vehicle>(car);auto a=m_.character(sim_,uint8_t(i+1));
        const auto* seat=driverSeat(m_.drive_.type(d.type)->seats);
        v.pos=d.controller.position;v.yaw=d.controller.yaw;v.velocity={};
        d.seats.occupants[seat->id]=a.slot;d.seats.team=a.team;d.seats.locked=true;d.driver=a.entity;d.player=a.slot;
        a.vehicle=v.id;a.velocity={};a.pos=vehicleWorldPoint(d.controller,seat->entry)+Vec3{{},Fixed::fromRatio(1,2),{}};v.driver=a.slot;
        return i<m_.bots_.size()&&m_.bots_[i];
    }
    VehicleInput pedals(size_t i,Entity,const vs::VehicleDrive&) override {
        return m_.driverInput(m_.character(static_cast<const Simulation&>(sim_),uint8_t(i+1)),sim_.tick());
    }
    bool wantsRespawn(size_t,Entity car,const vs::VehicleDrive& d) override {   // wrecked or fallen off the world
        const Vehicle* v=w_.tryGet<Vehicle>(car);return (v&&v->health<=0)||d.controller.position.y< -50_fx;
    }
    void respawned(size_t,Entity car,vs::VehicleDrive& d) override {
        if(Vehicle* v=w_.tryGet<Vehicle>(car)){v->pos=d.controller.position;v->yaw=d.controller.yaw;v->velocity={};if(v->health<=0)v->health=Vehicle{}.health;}
        const auto count=int64_t(m_.characterCount(sim_));const auto& type=*m_.drive_.type(d.type);
        for(auto [seat,actor]:d.seats.occupants)for(const auto& s:type.seats.seats)if(s.id==seat&&actor>=1&&actor<=count){
            auto a=m_.character(sim_,uint8_t(actor));a.pos=vehicleWorldPoint(d.controller,s.entry)+Vec3{{},Fixed::fromRatio(1,2),{}};a.velocity={};}
    }
    int32_t raceOver(int64_t winner) override {   // the race decides the round
        auto& s=m_.state(sim_);
        if(!s.winner)s.winner=winner?teamOf(winner):-1;
        return std::max(s.winner,0);
    }
    int32_t teamOf(int64_t racer) const {
        const auto count=int64_t(m_.characterCount(static_cast<const Simulation&>(sim_)));
        return racer>=1&&racer<=count?int32_t(m_.character(static_cast<const Simulation&>(sim_),uint8_t(racer)).team):-1;
    }
private:
    ActorHost& m_;Simulation& sim_;World& w_;
};

// ---------------------------------------------------------------- typed vehicles
vs::VehicleDrive* ActorHost::vehicleDrive(Simulation& sim,uint32_t vehicle) const {
    return vs::driveOf(sim.world(),vehicleEntity(sim,vehicle));
}
void ActorHost::setupVehicles(Simulation& sim,const Json& map) {
    // fps.vehicleTypes as the shared vehicle types (metres; seat lists); a hard crash costs (impact - 8 m/s) * 10 health
    drive_={};
    if(config_.vehicleTypes){
        drive_.enabled=true;drive_.metersPerCell=1_fx;drive_.crashMinSpeed=8_fx;drive_.crashDamage=10_fx;drive_.restitution={};
        for(const auto& [id,def]:config_.vehicleTypes->vehicles){vs::VehicleType t;t.type=id;t.length=4_fx;t.width=2_fx;t.def=def.controller;t.seats=def.seats;drive_.vehicles.push_back(std::move(t));}
    }
    const auto& list=map["vehicles"];if(!list.isArray())return;
    World& w=sim.world();const auto entities=vehicleEntities(sim);
    for(size_t i=0;i<list.size()&&i<entities.size();++i){
        const auto& j=list[i];if(!j.has("type"))continue;
        if(!config_.vehicleTypes)throw JsonError("actor vehicle.type: requires fps.vehicleTypes");
        auto type=j.getInt("type");const auto* t=drive_.type(type);
        if(!t)throw JsonError("actor vehicle.type: unknown vehicle type "+std::to_string(type));
        if(j.has("yaw")&&!j["yaw"].isNumber())throw JsonError("actor vehicle.yaw: expected turns");
        auto& v=*w.tryGet<Vehicle>(entities[i]);v.yaw=j.getFx("yaw",Fixed{});
        vs::addVehicle(w,entities[i],*t,v.pos,v.yaw);
    }
}
world::RegionPositionReader ActorHost::regionPositionReader() {
    return [](const World& world,Entity entity)->std::optional<Vec3> {
        // ActorHost already owns these authoritative positions. Do not add
        // a second Transform that could drift from real vehicle/actor state.
        if(const auto* actor=world.tryGetConst<CharacterBody>(entity))
            return actor->pos;
        if(const auto* vehicle=world.tryGetConst<Vehicle>(entity))
            return vehicle->pos;
        if(const auto* loot=world.tryGetConst<Loot>(entity))
            return loot->pos;
        if(const auto* projectile=world.tryGetConst<Projectile>(entity))
            return projectile->pos;
        return world::regionTransformPosition(world,entity);
    };
}

bool ActorHost::transferVehicleRegion(Simulation& sim,Entity vehicle,
                                     world::Cell from,world::Cell to,
                                     std::string* error) {
    auto reject=[&](const char* reason) {
        if(error)*error=reason;
        return false;
    };
    auto& ecs=sim.world();
    const auto* ledger=ecs.resourceConst<world::RegionEntityLedger>();
    const auto* car=ecs.tryGetConst<Vehicle>(vehicle);
    if(!ledger || !car)return reject("world-space region ownership or vehicle is unavailable");
    // A transfer only publishes ownership after the existing host gate has
    // admitted collision and derived navigation. Never step into unloaded
    // geometry, and never let an async camera decide authoritative ownership.
    if(!readyToStep(sim))return reject("destination collision/navigation is not ready");
    if(streamManifest_.getBool("async")) {
        if(!streamedCollision_)return reject("collision streaming reader missing");
        const auto pos=car->pos;
        const world::Bounds required{
            pos-Vec3{2_fx,2_fx,2_fx},pos+Vec3{2_fx,2_fx,2_fx}};
        if(!streamedCollision_->covers(required))
            return reject("destination vehicle bounds are not collision resident");
    }
    if(world::SpatialWorld(32).cell(car->pos)!=to)
        return reject("vehicle has not reached the destination region");
    const auto seatMap=[this,&sim](int64_t actor)->Entity {
        if(actor<=0 || actor>int64_t(characterCount(sim)) || actor>UINT8_MAX)
            return kNullEntity;
        return characterEntity(sim,uint8_t(actor));
    };
    // Read-only preflight avoids copy-on-write ledger detach on failure.
    world::VehicleRegionGroup group;
    if(!world::VehicleRegionTransfer::gather(ecs,vehicle,seatMap,group,error))
        return false;
    for(Entity member:group.members) {
        const auto* tag=ecs.tryGetConst<world::RegionStableIdentity>(member);
        if(!tag || !tag->id)return reject("vehicle group member lacks stable region identity");
        const auto* record=ledger->find(tag->id);
        if(!record || !record->live || record->owner!=from)
            return reject("vehicle group has stale or conflicting region owners");
    }
    auto* mutableLedger=ecs.resource<world::RegionEntityLedger>();
    return world::VehicleRegionTransfer::transfer(
        ecs,*mutableLedger,vehicle,seatMap,from,to,regionPositionReader(),error);
}

bool ActorHost::typedRegionMoveGroup(const Simulation& sim,Entity car,
                                     std::vector<Entity>& out,
                                     std::string* error)const {
    auto refuse=[&](const char* why){
        if(error)*error=why;
        return false;
    };
    const World& ecs=sim.world();
    const auto* state=ecs.tryGetConst<Vehicle>(car);
    const auto* drive=ecs.tryGetConst<vs::VehicleDrive>(car);
    if(!state || !drive)
        return refuse("typed car requires Vehicle and VehicleDrive");
    const auto* definition=drive_.type(drive->type);
    if(!definition)return refuse("typed car has no registered controller definition");
    if(!vs::valid(*drive))
        return refuse("invalid typed vehicle controller or seats");
    if(drive->driver.valid() &&
       (drive->player==0 || drive->player>characterCount(sim) ||
        characterEntity(sim,drive->player)!=drive->driver))
        return refuse("typed driver slot does not match ECS Entity");
    world::VehicleRegionGroup group;
    if(!vehicleRegionGroup(sim,car,group,error))return false;
    // The typed vehicle's seat map is the authoritative passenger list.
    // Reject incomplete links and seats without definitions before any move.
    std::set<int64_t> assigned;
    for(const auto& [seat,slot]:drive->seats.occupants) {
        auto found=std::find_if(definition->seats.seats.begin(),
                                definition->seats.seats.end(),
                                [seat](const auto& item){return item.id==seat;});
        if(found==definition->seats.seats.end() || slot<=0 ||
           slot>int64_t(characterCount(sim)) || !assigned.insert(slot).second)
            return refuse("missing or duplicated typed car seat definition");
        const Entity passenger=characterEntity(sim,uint8_t(slot));
        const auto* body=ecs.tryGetConst<CharacterBody>(passenger);
        if(!body || body->vehicle!=state->id)
            return refuse("seat passenger is not bound to this car");
    }
    // The legacy ActorHost character backlink must never introduce a hidden
    // occupant omitted from the shared seat inventory.
    bool hidden=false;
    ecs.eachExistingConst<CharacterBody>(
        [&](Entity e,const CharacterBody& body) {
            if(body.vehicle==state->id &&
               std::find(group.members.begin(),group.members.end(),e)==
                   group.members.end())hidden=true;
        });
    if(hidden)return refuse("typed car is missing an occupied actor");
    // Driver reverse components are optional for ActorHost seat manifests.
    // A reverse component for another car was already rejected by gather().
    out=std::move(group.members);
    return true;
}

bool ActorHost::placeTypedRegionMove(Simulation& sim,Entity car,Vec3 destination,
                                    std::string* error) {
    auto refuse=[&](const char* why){
        if(error)*error=why;
        return false;
    };
    std::vector<Entity> members;
    if(!typedRegionMoveGroup(sim,car,members,error))return false;
    auto& ecs=sim.world();
    const auto* current=ecs.tryGetConst<vs::VehicleDrive>(car);
    const auto* definition=current?drive_.type(current->type):nullptr;
    if(!definition)return refuse("missing controller before typed move commit");
    // Preserve yaw/gear/lastTick, reset wheel/flight motion through the
    // existing shared authoritative respawn function.
    auto controller=current->controller;
    lib::mechanics::respawnVehicle(definition->def,controller,destination,
                                   controller.yaw);
    if(!lib::mechanics::valid(controller))
        return refuse("destination cannot be represented by vehicle state");
    auto* vehicle=ecs.tryGet<Vehicle>(car);
    auto* drive=ecs.tryGet<vs::VehicleDrive>(car);
    if(!vehicle || !drive)return refuse("typed vehicle destroyed before commit");
    drive->controller=controller;
    vehicle->pos=controller.position;
    vehicle->yaw=controller.yaw;
    vehicle->velocity=controller.velocity;
    // Keep existing seat assignments and authoritative character data.
    // Reposition each occupied seat from the controller's new world frame,
    // just as VehicleWorld::placed does during normal vehicle stepping.
    for(const auto& [seat,slot]:drive->seats.occupants) {
        const auto it=std::find_if(definition->seats.seats.begin(),
                                   definition->seats.seats.end(),
                                   [seat](const auto& s){return s.id==seat;});
        const Entity occupant=characterEntity(sim,uint8_t(slot));
        if(auto* body=ecs.tryGet<CharacterBody>(occupant)) {
            body->pos=lib::mechanics::vehicleWorldPoint(
                controller,it->entry)+Vec3{{},Fixed::fromRatio(1,2),{}};
            body->velocity=controller.velocity;
            body->grounded=true;
        }
    }
    // The shared VehicleOccupant component is owned by the generic vehicle
    // subsystem; its pose must not lag one tick behind teleportation.
    for(Entity occupant:members) {
        if(occupant==car)continue;
        if(auto* back=ecs.tryGet<vs::VehicleOccupant>(occupant)) {
            back->vehicle=car;
            back->type=drive->type;
            back->position=controller.position;
            back->yaw=controller.yaw;
            back->speed=lib::mechanics::vehicleForwardSpeed(controller);
            back->deck=drive->deck;
        }
    }
    return true;
}

bool ActorHost::vehicleRegionGroup(const Simulation& sim,Entity vehicle,
                                   world::VehicleRegionGroup& out,
                                   std::string* error) const {
    const auto& ecs=sim.world();
    return world::VehicleRegionTransfer::gather(
        ecs,vehicle,
        [this,&sim](int64_t seatActor)->Entity {
            // World-space seat manifests use player slot numbers. Resolve
            // through CharacterIndex; never cast seat slots to Entity handles.
            if(seatActor<=0 || seatActor>int64_t(characterCount(sim)) ||
               seatActor>UINT8_MAX)return kNullEntity;
            return characterEntity(sim,uint8_t(seatActor));
        },out,error);
}

bool ActorHost::canFireFromVehicle(Simulation& sim,const CharacterRef& a) const {
    auto* run=vehicleDrive(sim,a.vehicle);if(!run)return false;
    const auto* type=drive_.type(run->type);if(!type)return false;auto seat=vehicleSeatOf(run->seats,a.slot);
    for(const auto& d:type->seats.seats)if(d.id==seat)return d.role!=VehicleSeatRole::Driver;
    return false;
}
// Interact on or in a typed vehicle becomes a seat request handled by tickVehicles this tick. False: not typed.
bool ActorHost::vehicleInteract(Simulation& sim,CharacterRef a) {
    World& w=sim.world();if(!w.countExisting<vs::VehicleDrive>())return false;
    if(race_&&a.vehicle&&race_->holds(w,vehicleEntity(sim,a.vehicle)))return true;   // racers stay at the wheel until the race is over
    VehicleSeatRequest r;r.actor=a.slot;r.team=a.team;r.actorPosition=a.pos;
    if(a.vehicle){
        auto* run=vehicleDrive(sim,a.vehicle);if(!run)return false;
        r.id=run->seats.lastRequest+1+int64_t(run->requests.size());r.action=VehicleSeatAction::Exit;r.seat=vehicleSeatOf(run->seats,a.slot);
        run->requests.push_back(r);return true;
    }
    vs::VehicleDrive* best=nullptr;Fixed bestDistance;
    for(const Entity e:vehicleEntities(sim)){
        auto* run=vs::driveOf(w,e);if(!run)continue;
        const auto& v=*w.tryGet<Vehicle>(e);
        if(v.health<=0)continue;auto d=(v.pos-a.pos).lengthSq();if(d>36_fx)continue;
        if(!best||d<bestDistance){best=run;bestDistance=d;}
    }
    if(!best)return false;
    r.id=best->seats.lastRequest+1+int64_t(best->requests.size());r.action=VehicleSeatAction::Enter;r.preferred=VehicleSeatRole::Driver;
    best->requests.push_back(r);return true;
}
// The driver's held controls (released after the input timeout) through the shared axes mapping (mechanics/VehicleDrive.h):
// forward / back -> throttle / brake, right steers, Jump is the handbrake, pitch drives flight.
VehicleInput ActorHost::driverInput(const ConstCharacterRef& a,uint32_t now) const {
    if(now-a.lastInputTick>config_.inputTimeout)return {};
    return vehicleInputFromAxes({Fixed::fromRatio(a.input.forward,1000),Fixed::fromRatio(a.input.right,1000),a.input.pitch,(a.input.buttons&Jump)!=0});
}
// 1. Seats of every typed vehicle (ascending id): enter / exit requests of this tick, ejections of knocked / killed /
//    thrown-out occupants; the driver seat's occupant becomes VehicleDrive::driver. 2. The vehicle system steps them all.
void ActorHost::tickVehicles(Simulation& sim,std::span<const vs::RacePedals> race) {
    World& w=sim.world();if(!w.countExisting<vs::VehicleDrive>())return;auto now=sim.tick();
    const auto count=int64_t(characterCount(sim));
    for(const Entity entity:vehicleEntities(sim)){
        auto* typed=vs::driveOf(w,entity);if(!typed)continue;
        auto& run=*typed;Vehicle* v=w.tryGet<Vehicle>(entity);
        const auto& seats=drive_.type(run.type)->seats;
        auto clear=[&](Vec3 p,int64_t){auto b=bodyBounds(p,config_.motor.height,config_.motor);b.min.y+=Fixed::fromRatio(1,100);return !collision_.blocked(b)&&fxAbs(p.x)<config_.worldLimit&&fxAbs(p.z)<config_.worldLimit;};
        std::vector<VehicleSeatRequest> requests;std::swap(requests,run.requests);
        for(auto [seat,actor]:run.seats.occupants){
            const auto who=character(sim,uint8_t(actor));if(v->health>0&&who.health>0&&!who.downed&&who.vehicle==v->id)continue;   // knocked / killed / thrown out by damage
            VehicleSeatRequest r;r.id=run.seats.lastRequest+1+int64_t(requests.size());r.actor=actor;r.action=VehicleSeatAction::Eject;r.seat=seat;requests.push_back(r);}
        if(auto report=stepVehicleSeats(seats,run.seats,now,run.controller,std::span<const VehicleSeatRequest>(requests),clear)){
            for(const auto& e:report->events){
                if(e.actor<=0||e.actor>count)continue;auto a=character(sim,uint8_t(e.actor));
                if(e.kind==VehicleEventKind::SeatEntered){a.vehicle=v->id;a.velocity={};}
            }
            for(const auto& x:report->exits){auto a=character(sim,uint8_t(x.actor));a.vehicle=0;a.pos=x.position;a.velocity=run.controller.velocity;
                if(v->health<=0)damage(sim,0,a,1000);}
        }
        auto driver=vehicleDriver(seats,run.seats);v->driver=uint8_t(driver);
        run.player=uint8_t(driver);run.driver=driver?characterEntity(sim,uint8_t(driver)):kNullEntity;
    }
    VehicleWorld host(*this,sim);
    vs::stepVehicles(w,host,drive_,now,sim.dt(),race,race_.get());
}

// ---------------------------------------------------------------- races
void ActorHost::setupRace(Simulation& sim,const Json& rules,const Json& map){
    race_.reset();World& w=sim.world();w.eraseResource<vs::RaceState>();
    if(!map.has("race")){if(rules.has("race")&&!rules["race"].isObject())throw JsonError("race: expected an object");return;}
    if(!config_.vehicleTypes)throw JsonError("map race: the cars are typed vehicles (fps.vehicleTypes)");
    auto race=vs::VehicleRace::read(w,rules,map,1_fx,config_.tickRate,drive_);   // metre map
    const auto n=characterCount(sim);
    std::vector<Entity> cars;for(const Entity e:vehicleEntities(sim))if(vs::driveOf(w,e))cars.push_back(e);
    if(cars.size()<n)throw JsonError("map race: one typed map vehicle (\"type\") per player required");
    if(race->mapGrid&&race->course.grid.size()<n)throw JsonError("map race.grid: one slot per player");
    cars.resize(n);
    for(const Entity e:cars){const auto& d=*vs::driveOf(w,e);if(!driverSeat(drive_.type(d.type)->seats))throw JsonError("actor.race: vehicle type "+std::to_string(d.type)+" needs a driver seat");}
    race_=std::move(race);
    RaceSeats seats(*this,sim);
    if(!race_->start(w,seats,drive_,cars,sim.tick()))throw JsonError(race_->line?"map race: invalid race":"map race: bots need a racing line (\"line\")");
}
void ActorHost::tickRace(Simulation& sim){
    World& w=sim.world();auto now=sim.tick();RaceSeats seats(*this,sim);
    // 1. Pedals: countdown brakes, race bots, players' own input. 2. The vehicle system steps the cars (seats too).
    const auto pedals=race_->pedals(w,seats,now);
    tickVehicles(sim,pedals);
    // 3. Race: checkpoints, laps, standings; wrecked or fallen cars go back to their last checkpoint; the result.
    race_->after(w,seats,drive_,now);
    // 4. At the round limit the leader wins.
    auto& s=state(sim);if(s.winner)return;
    const auto* rs=vs::VehicleRace::state(static_cast<const World&>(w));
    if(rs&&now+1>=config_.roundTicks)s.winner=rs->session.standings.empty()?-1:seats.teamOf(rs->session.standings.front().racer);
}
bool ActorHost::validRace(const vs::RaceState& r,const std::vector<Entity>& typedVehicles) const {
    const size_t n=spawns_.size();
    if(!r.started||!lib::mechanics::valid(r.session)||r.racers.size()!=n||r.session.race.racers.size()!=n)return false;
    for(size_t i=0;i<n;++i){
        if(!r.session.race.racers.contains(int64_t(i+1)))return false;
        if(std::find(typedVehicles.begin(),typedVehicles.end(),r.racers[i])==typedVehicles.end())return false;
    }
    for(const auto& [id,b]:r.session.bots){(void)b;if(id<1||id>int64_t(bots_.size())||!bots_[size_t(id-1)])return false;}
    return true;
}
RaceView ActorHost::raceView(Simulation& sim,uint8_t slot) const {
    return race_?race_->view(sim.world(),slot,sim.tick()):RaceView{};
}
}
