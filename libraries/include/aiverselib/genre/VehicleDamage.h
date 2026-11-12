#pragma once
// vehicle-controllers: damage zones. Each zone is a local box with integer health; its health fraction scales
// performance stats (engine, grip, steering, brakes, lift) linearly down to the authored value at zero health.
// Damage comes from the owner (collision impulses, weapons) as id-ordered VehicleDamageInput; critical zones
// disable the vehicle at zero. vehiclePerformance(def, state) feeds stepVehicle.
#include "VehicleControllers.h"
#include <map>
namespace aiverse::lib::genre {
inline constexpr std::size_t kVehicleMaxZones=32,kVehicleMaxPenalties=8,kVehicleMaxDamageInputs=1024;
inline constexpr int64_t kVehicleMaxHealth=1000000000;
enum class VehicleStat:uint8_t{Engine,Grip,Steering,Brakes,Lift};
struct VehiclePenalty{VehicleStat stat=VehicleStat::Engine;Fixed atZero;};   // stat multiplier at zero health (0..1)
struct VehicleZoneDef{int64_t id=0;Vec3 center{},half{1_fx,1_fx,1_fx};int64_t health=1000;Fixed armor=1_fx;bool critical=false;std::vector<VehiclePenalty> penalties;};
struct VehicleDamageDef{std::vector<VehicleZoneDef> zones;};   // ascending zone ids
struct VehicleZoneState{int64_t health=0;bool destroyed=false;};
struct VehicleDamageState{std::map<int64_t,VehicleZoneState> zones;int64_t lastInput=0;uint32_t lastTick=0;bool disabled=false,initialized=false;};
// amount > 0 damages (scaled by the zone armor), amount < 0 repairs. zone 0: locate by localPoint (vehicleZoneAt).
struct VehicleDamageInput{int64_t id=0,zone=0;Vec3 localPoint{};int64_t amount=0;};

inline bool valid(const VehicleDamageDef& d){
    if(d.zones.size()>kVehicleMaxZones)return false;
    for(std::size_t i=0;i<d.zones.size();++i){
        const auto& z=d.zones[i];if(z.id<=0||(i&&z.id<=d.zones[i-1].id)||z.health<=0||z.health>kVehicleMaxHealth||!vehicleDetail::vehicleBounded(z.center,kVehicleMaxExtent)
            ||!vehicleDetail::vehiclePositive(z.half.x,kVehicleMaxExtent)||!vehicleDetail::vehiclePositive(z.half.y,kVehicleMaxExtent)||!vehicleDetail::vehiclePositive(z.half.z,kVehicleMaxExtent)
            ||!vehicleDetail::vehicleNonneg(z.armor,Fixed::fromInt(100))||z.penalties.size()>kVehicleMaxPenalties)return false;
        for(const auto& p:z.penalties)if(uint8_t(p.stat)>uint8_t(VehicleStat::Lift)||!vehicleDetail::vehicleNonneg(p.atZero,1_fx))return false;
    }
    return true;
}
inline bool valid(const VehicleDamageState& s){
    if(s.zones.size()>kVehicleMaxZones||s.lastInput<0)return false;
    for(const auto& [id,z]:s.zones)if(id<=0||z.health<0||z.health>kVehicleMaxHealth||z.destroyed!=(z.health==0))return false;return true;
}
inline VehicleDamageState makeVehicleDamage(const VehicleDamageDef& d){VehicleDamageState s;for(const auto& z:d.zones)s.zones[z.id]={z.health,false};return s;}
inline bool vehicleDamageMatches(const VehicleDamageDef& d,const VehicleDamageState& s){
    if(s.zones.size()!=d.zones.size())return false;for(const auto& z:d.zones){auto it=s.zones.find(z.id);if(it==s.zones.end()||it->second.health>z.health)return false;}return true;
}
// World point -> vehicle-local point (yaw only; pitch/roll are presentation for ground vehicles).
inline Vec3 vehicleLocalPoint(const VehicleState& s,Vec3 world){
    auto d=world-s.position;auto f=vehicleDetail::forward(s.yaw);auto r=vehicleDetail::rightOf(f);Vec2 h{d.x,d.z};return {h.dot(r),d.y,h.dot(f)};
}
inline Vec3 vehicleWorldPoint(const VehicleState& s,Vec3 local){
    auto f=vehicleDetail::forward(s.yaw);auto r=vehicleDetail::rightOf(f);auto h=r*local.x+f*local.z;return {s.position.x+h.x,s.position.y+local.y,s.position.z+h.y};
}
// Zone containing the point (smallest id), else the zone whose box is nearest (distance, id); 0 without zones.
inline int64_t vehicleZoneAt(const VehicleDamageDef& d,Vec3 local){
    int64_t best=0;Fixed bestDistance{};
    for(const auto& z:d.zones){
        auto axis=[](Fixed p,Fixed c,Fixed h){auto o=fxAbs(p-c)-h;return o>Fixed{}?o:Fixed{};};
        Vec3 o{axis(local.x,z.center.x,z.half.x),axis(local.y,z.center.y,z.half.y),axis(local.z,z.center.z,z.half.z)};
        auto dist=o.lengthSq();if(!best||dist<bestDistance){best=z.id;bestDistance=dist;}
    }
    return best;
}
inline VehiclePerformance vehiclePerformance(const VehicleDamageDef& d,const VehicleDamageState& s){
    VehiclePerformance p;if(s.disabled){p.engine={};}
    for(const auto& z:d.zones){
        auto it=s.zones.find(z.id);if(it==s.zones.end())continue;auto fraction=Fixed::fromRatio(it->second.health,z.health);
        for(const auto& pen:z.penalties){
            auto scale=pen.atZero+(1_fx-pen.atZero)*fraction;
            switch(pen.stat){case VehicleStat::Engine:p.engine=p.engine*scale;break;case VehicleStat::Grip:p.grip=p.grip*scale;break;case VehicleStat::Steering:p.steering=p.steering*scale;break;
                case VehicleStat::Brakes:p.brakes=p.brakes*scale;break;case VehicleStat::Lift:p.lift=p.lift*scale;break;}
        }
    }
    return p;
}
// Applies inputs in ascending id order (ids must exceed every earlier accepted id: DamageIgnored otherwise).
// Events: ZoneDamaged/ZoneRepaired (value = new health), ZoneDestroyed, Disabled (critical zone at 0), Repaired.
inline std::optional<std::vector<VehicleEvent>> stepVehicleDamage(const VehicleDamageDef& d,VehicleDamageState& s,uint32_t tick,std::span<const VehicleDamageInput> inputs){
    if(!valid(d)||!valid(s)||!vehicleDamageMatches(d,s)||inputs.size()>kVehicleMaxDamageInputs||!vehicleDetail::tickOk(s.initialized,s.lastTick,tick))return {};
    std::vector<const VehicleDamageInput*> order;
    for(const auto& in:inputs){if(in.id<=0||in.zone<0||!vehicleDetail::vehicleBounded(in.localPoint,kVehicleCoordLimit)||in.amount< -kVehicleMaxHealth||in.amount>kVehicleMaxHealth)return {};order.push_back(&in);}
    std::sort(order.begin(),order.end(),[](auto a,auto b){return a->id<b->id;});
    for(std::size_t i=1;i<order.size();++i)if(order[i]->id==order[i-1]->id)return {};
    auto next=s;std::vector<VehicleEvent> events;using K=VehicleEventKind;
    for(auto in:order){
        auto zone=in->zone?in->zone:vehicleZoneAt(d,in->localPoint);auto it=next.zones.find(zone);
        if(in->id<=next.lastInput||it==next.zones.end()){events.push_back({K::DamageIgnored,zone,0,in->id});continue;}
        next.lastInput=in->id;const auto& def=*std::find_if(d.zones.begin(),d.zones.end(),[&](const auto& z){return z.id==zone;});
        auto& z=it->second;
        if(in->amount>=0){
            auto amount=(Fixed::fromInt(in->amount)*def.armor).toInt();if(amount<=0||z.destroyed){events.push_back({K::DamageIgnored,zone,0,in->id});continue;}
            z.health=std::max<int64_t>(0,z.health-amount);events.push_back({K::ZoneDamaged,zone,0,z.health});
            if(z.health==0){z.destroyed=true;events.push_back({K::ZoneDestroyed,zone,0,0});if(def.critical&&!next.disabled){next.disabled=true;events.push_back({K::Disabled,zone,0,0});}}
        }else{
            z.health=std::min(def.health,z.health-in->amount);z.destroyed=false;events.push_back({K::ZoneRepaired,zone,0,z.health});
            bool broken=false;for(const auto& c:d.zones)if(c.critical&&next.zones.at(c.id).destroyed)broken=true;
            if(next.disabled&&!broken){next.disabled=false;events.push_back({K::Repaired,zone,0,0});}
        }
    }
    next.lastTick=tick;next.initialized=true;s=std::move(next);return events;
}
}
