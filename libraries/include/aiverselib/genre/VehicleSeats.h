#pragma once
// vehicle-controllers: seats. Driver / passenger / gunner roles, validated enter/exit/switch requests and an exit
// point search through an owner callback clear(worldPoint, actor) -> bool (the owner's collision query).
// Only the driver's input drives the vehicle (vehicleDriver); gunners aim mounted weapons through their owners.
#include "VehicleDamage.h"
#include <map>
namespace aiverse::lib::genre {
inline constexpr std::size_t kVehicleMaxSeats=32,kVehicleMaxExits=16,kVehicleMaxSeatRequests=256;
enum class VehicleSeatRole:uint8_t{Driver,Passenger,Gunner};
enum class VehicleSeatAction:uint8_t{Enter,Exit,Switch,Eject,Lock,Unlock};
// Refusal codes in VehicleEvent::value.
enum VehicleSeatReason:int64_t{
    kVehicleSeatTooFast=1,kVehicleSeatTooFar=2,kVehicleSeatOccupied=3,kVehicleSeatNoSeat=4,kVehicleSeatAlreadySeated=5,
    kVehicleSeatNotSeated=6,kVehicleSeatLocked=7,kVehicleSeatExitBlocked=8,kVehicleSeatWrongTeam=9,kVehicleSeatDisabled=10
};
// entry/exits are vehicle-local points (x right, y up, z forward); exits are tried in authored order.
struct VehicleSeatDef{int64_t id=0;VehicleSeatRole role=VehicleSeatRole::Passenger;Vec3 entry{};Fixed enterRadius=Fixed::fromRatio(3,2);std::vector<Vec3> exits;};
struct VehicleSeatsDef{std::vector<VehicleSeatDef> seats;Fixed maxEnterSpeed=Fixed::fromInt(2),maxExitSpeed=Fixed::fromInt(3);};   // ascending seat ids
struct VehicleSeatState{std::map<int64_t,int64_t> occupants;int64_t team=0;int64_t lastRequest=0;uint32_t lastTick=0;bool locked=false,initialized=false;};
// seat 0 = first free seat of `preferred` role, then any free seat (ascending id). team: the actor's team (0 = none).
struct VehicleSeatRequest{int64_t id=0,actor=0;VehicleSeatAction action=VehicleSeatAction::Enter;int64_t seat=0;VehicleSeatRole preferred=VehicleSeatRole::Driver;Vec3 actorPosition{};int64_t team=0;};
struct VehicleExitPlacement{int64_t actor=0,seat=0;Vec3 position{};bool operator==(const VehicleExitPlacement&)const=default;};
struct VehicleSeatReport{std::vector<VehicleEvent> events;std::vector<VehicleExitPlacement> exits;};

inline bool valid(const VehicleSeatsDef& d){
    if(d.seats.size()>kVehicleMaxSeats||!vehicleDetail::vehicleNonneg(d.maxEnterSpeed,kVehicleMaxSpeed)||!vehicleDetail::vehicleNonneg(d.maxExitSpeed,kVehicleMaxSpeed))return false;
    for(std::size_t i=0;i<d.seats.size();++i){
        const auto& s=d.seats[i];if(s.id<=0||(i&&s.id<=d.seats[i-1].id)||uint8_t(s.role)>uint8_t(VehicleSeatRole::Gunner)||!vehicleDetail::vehicleBounded(s.entry,kVehicleMaxExtent)
            ||!vehicleDetail::vehiclePositive(s.enterRadius,kVehicleMaxExtent)||s.exits.size()>kVehicleMaxExits)return false;
        for(auto e:s.exits)if(!vehicleDetail::vehicleBounded(e,kVehicleMaxExtent))return false;
    }
    return true;
}
inline bool valid(const VehicleSeatState& s){
    if(s.occupants.size()>kVehicleMaxSeats||s.team<0||s.lastRequest<0)return false;
    std::vector<int64_t> actors;for(auto [seat,actor]:s.occupants){if(seat<=0||actor<=0)return false;actors.push_back(actor);}
    std::sort(actors.begin(),actors.end());return std::adjacent_find(actors.begin(),actors.end())==actors.end();
}
inline int64_t vehicleSeatOf(const VehicleSeatState& s,int64_t actor){for(auto [seat,a]:s.occupants)if(a==actor)return seat;return 0;}
// Driver occupant (first Driver seat by id), 0 when nobody drives.
inline int64_t vehicleDriver(const VehicleSeatsDef& d,const VehicleSeatState& s){
    for(const auto& seat:d.seats)if(seat.role==VehicleSeatRole::Driver){auto it=s.occupants.find(seat.id);if(it!=s.occupants.end())return it->second;}return 0;
}
inline std::vector<int64_t> vehicleOccupants(const VehicleSeatState& s,const VehicleSeatsDef& d,VehicleSeatRole role){
    std::vector<int64_t> out;for(const auto& seat:d.seats)if(seat.role==role){auto it=s.occupants.find(seat.id);if(it!=s.occupants.end())out.push_back(it->second);}return out;
}

// Requests in ascending id (ids must exceed earlier accepted ones; stale ones are refused silently as duplicates).
// Enter: speed <= maxEnterSpeed, unlocked or same team, actor within enterRadius of the seat entry, seat free.
// Exit: speed <= maxExitSpeed (Eject ignores speed), first clear exit point: the seat's exits, then every other
// seat's exits by seat id; none clear -> ExitRefused(ExitBlocked). Switch: target seat free. Lock/Unlock: occupants only.
// Disabled vehicles still allow exits. The first occupant entering an unowned vehicle sets its team.
template<class Clear>
std::optional<VehicleSeatReport> stepVehicleSeats(const VehicleSeatsDef& d,VehicleSeatState& s,uint32_t tick,const VehicleState& vehicle,
    std::span<const VehicleSeatRequest> requests,Clear&& clear){
    if(!valid(d)||!valid(s)||!valid(vehicle)||requests.size()>kVehicleMaxSeatRequests||!vehicleDetail::tickOk(s.initialized,s.lastTick,tick))return {};
    for(auto [seat,actor]:s.occupants){(void)actor;if(std::none_of(d.seats.begin(),d.seats.end(),[&](const auto& x){return x.id==seat;}))return {};}
    std::vector<const VehicleSeatRequest*> order;
    for(const auto& r:requests){if(r.id<=0||r.actor<=0||r.seat<0||r.team<0||uint8_t(r.action)>uint8_t(VehicleSeatAction::Unlock)||uint8_t(r.preferred)>uint8_t(VehicleSeatRole::Gunner)
        ||!vehicleDetail::vehicleBounded(r.actorPosition,kVehicleCoordLimit))return {};
        order.push_back(&r);}
    std::sort(order.begin(),order.end(),[](auto a,auto b){return a->id<b->id;});
    for(std::size_t i=1;i<order.size();++i)if(order[i]->id==order[i-1]->id)return {};
    auto next=s;VehicleSeatReport report;using K=VehicleEventKind;using A=VehicleSeatAction;
    auto speed=vehicle.velocity.length();
    auto refuse=[&](K kind,const VehicleSeatRequest& r,int64_t seat,int64_t reason){report.events.push_back({kind,seat,r.actor,reason});};
    auto seatDef=[&](int64_t id)->const VehicleSeatDef*{for(const auto& x:d.seats)if(x.id==id)return &x;return nullptr;};
    for(auto rp:order){
        const auto& r=*rp;if(r.id<=next.lastRequest)continue;next.lastRequest=r.id;
        auto current=vehicleSeatOf(next,r.actor);
        switch(r.action){
        case A::Enter:{
            if(current){refuse(K::EnterRefused,r,current,kVehicleSeatAlreadySeated);break;}
            if(speed>d.maxEnterSpeed){refuse(K::EnterRefused,r,r.seat,kVehicleSeatTooFast);break;}
            if(next.locked&&next.team!=0&&r.team!=next.team){refuse(K::EnterRefused,r,r.seat,kVehicleSeatLocked);break;}
            if(!next.occupants.empty()&&next.team!=0&&r.team!=next.team){refuse(K::EnterRefused,r,r.seat,kVehicleSeatWrongTeam);break;}
            const VehicleSeatDef* target=nullptr;
            if(r.seat){target=seatDef(r.seat);if(!target){refuse(K::EnterRefused,r,r.seat,kVehicleSeatNoSeat);break;}if(next.occupants.contains(r.seat)){refuse(K::EnterRefused,r,r.seat,kVehicleSeatOccupied);break;}}
            else{
                for(const auto& x:d.seats)if(x.role==r.preferred&&!next.occupants.contains(x.id)){target=&x;break;}
                if(!target)for(const auto& x:d.seats)if(!next.occupants.contains(x.id)){target=&x;break;}
                if(!target){refuse(K::EnterRefused,r,0,kVehicleSeatNoSeat);break;}
            }
            auto entry=vehicleWorldPoint(vehicle,target->entry);
            if((entry-r.actorPosition).lengthSq()>target->enterRadius*target->enterRadius){refuse(K::EnterRefused,r,target->id,kVehicleSeatTooFar);break;}
            if(next.occupants.empty()&&next.team==0)next.team=r.team;
            next.occupants[target->id]=r.actor;report.events.push_back({K::SeatEntered,target->id,r.actor,int64_t(target->role)});break;
        }
        case A::Exit:case A::Eject:{
            if(!current){refuse(K::ExitRefused,r,0,kVehicleSeatNotSeated);break;}
            if(r.action==A::Exit&&speed>d.maxExitSpeed){refuse(K::ExitRefused,r,current,kVehicleSeatTooFast);break;}
            std::optional<Vec3> spot;
            auto tryExits=[&](const VehicleSeatDef& x){for(auto e:x.exits){auto w=vehicleWorldPoint(vehicle,e);if(clear(w,r.actor)){spot=w;return;}}};
            tryExits(*seatDef(current));
            for(const auto& x:d.seats){if(spot)break;if(x.id!=current)tryExits(x);}
            if(!spot){refuse(K::ExitRefused,r,current,kVehicleSeatExitBlocked);break;}
            next.occupants.erase(current);if(next.occupants.empty()&&!next.locked)next.team=0;
            report.exits.push_back({r.actor,current,*spot});report.events.push_back({r.action==A::Eject?K::Ejected:K::SeatExited,current,r.actor,0});break;
        }
        case A::Switch:{
            auto target=seatDef(r.seat);
            if(!current){refuse(K::SwitchRefused,r,r.seat,kVehicleSeatNotSeated);break;}
            if(!target){refuse(K::SwitchRefused,r,r.seat,kVehicleSeatNoSeat);break;}
            if(next.occupants.contains(r.seat)){refuse(K::SwitchRefused,r,r.seat,kVehicleSeatOccupied);break;}
            next.occupants.erase(current);next.occupants[r.seat]=r.actor;report.events.push_back({K::SeatSwitched,r.seat,r.actor,current});break;
        }
        case A::Lock:case A::Unlock:{
            if(!current){refuse(K::EnterRefused,r,0,kVehicleSeatNotSeated);break;}
            next.locked=r.action==A::Lock;report.events.push_back({next.locked?K::Locked:K::Unlocked,current,r.actor,0});break;
        }
        }
    }
    next.lastTick=tick;next.initialized=true;s=std::move(next);return report;
}
}
