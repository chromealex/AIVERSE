#pragma once
// JSON definitions for vehicle-controllers (example: examples/libraries/genre/vehicles-sports/vehicles.json).
// Top level: "vehicles" (id, kind wheeled|tracked|hover|flight, the kind's parameter object, zones, seats) and
// "tracks" (id, laps, checkpoints, grid, racing line). Lengths/speeds/forces/masses are integer milli-units
// (metres, m/s, N, kg), angles milli-turns, rpm and ticks literal integers. Omitted parameters keep the C++
// defaults. Unknown keys, wrong types, out-of-range values, duplicate/unsorted ids and invalid combinations throw
// JsonError("vehicles.<array>[<index>].<field>: ...").
#include "Vehicles.h"
#include "engine/data/Json.h"
#include <charconv>
#include <initializer_list>
#include <map>
#include <string>
#include <string_view>
namespace aiverse::lib::genre {
struct VehicleTrackDef{int64_t id=0;RaceCourse course;std::vector<Vec3> line;};
struct VehicleDefinitions{std::map<int64_t,VehicleDefinition> vehicles;std::map<int64_t,VehicleTrackDef> tracks;};
namespace vehicleDataDetail {
inline int64_t integer(const Json& j,const std::string& path,int64_t low,int64_t high){
    if(!j.isNumber())throw JsonError(path+": expected integer");
    const auto& text=j.numberLiteral();int64_t value=0;auto parsed=std::from_chars(text.data(),text.data()+text.size(),value);
    if(parsed.ec!=std::errc{}||parsed.ptr!=text.data()+text.size())throw JsonError(path+": expected integer");
    if(value<low||value>high)throw JsonError(path+": must be in "+std::to_string(low)+".."+std::to_string(high));return value;
}
inline void keys(const Json& j,const std::string& path,std::initializer_list<std::string_view> names){
    if(!j.isObject())throw JsonError(path+": expected object");
    for(const auto& [key,value]:j.members()){(void)value;if(std::find(names.begin(),names.end(),key)==names.end())throw JsonError(path+": unknown key '"+key+"'");}
}
inline void require(const Json& j,const std::string& path,std::initializer_list<const char*> names){for(auto n:names)if(!j.has(n))throw JsonError(path+"."+n+": required");}
inline void array(const Json& j,const std::string& path,std::size_t limit){if(!j.isArray()||j.size()>limit)throw JsonError(path+": expected an array of at most "+std::to_string(limit)+" entries");}
inline std::string at(const std::string& path,std::size_t i){return path+"["+std::to_string(i)+"]";}
inline Fixed milli(const Json& j,const std::string& path,int64_t low,int64_t high){return Fixed::fromRatio(integer(j,path,low,high),1000);}
inline void opt(const Json& j,const std::string& path,const char* key,Fixed& out,int64_t low,int64_t high){if(j.has(key))out=milli(j[key],path+"."+key,low,high);}
inline void optInt(const Json& j,const std::string& path,const char* key,Fixed& out,int64_t low,int64_t high){if(j.has(key))out=Fixed::fromInt(integer(j[key],path+"."+key,low,high));}
inline void optTicks(const Json& j,const std::string& path,const char* key,uint32_t& out,int64_t high){if(j.has(key))out=uint32_t(integer(j[key],path+"."+key,0,high));}
inline void optBool(const Json& j,const std::string& path,const char* key,bool& out){if(j.has(key)){if(j[key].type()!=Json::Type::Bool)throw JsonError(path+"."+key+": expected boolean");out=j[key].boolean();}}
inline Vec3 vec(const Json& j,const std::string& path,int64_t limit=100000000){
    if(!j.isArray()||j.size()!=3)throw JsonError(path+": expected [x,y,z] in milli-units");
    return {milli(j[0],path+"[0]",-limit,limit),milli(j[1],path+"[1]",-limit,limit),milli(j[2],path+"[2]",-limit,limit)};
}
// [[x, y], ...] with strictly ascending x; xScale/yScale: 1 = literal integers, 1000 = milli-units.
inline VehicleCurve curve(const Json& j,const std::string& path,int64_t xScale,int64_t yScale){
    array(j,path,kVehicleMaxCurve);if(!j.size())throw JsonError(path+": expected at least one point");VehicleCurve out;
    for(std::size_t i=0;i<j.size();++i){
        auto p=at(path,i);if(!j[i].isArray()||j[i].size()!=2)throw JsonError(p+": expected [x, y]");
        VehicleCurvePoint pt{Fixed::fromRatio(integer(j[i][0],p+"[0]",-100000000,100000000),xScale),Fixed::fromRatio(integer(j[i][1],p+"[1]",-100000000,100000000),yScale)};
        if(!out.empty()&&!(out.back().x<pt.x))throw JsonError(p+"[0]: x must be strictly ascending");out.push_back(pt);
    }
    return out;
}
inline void wheeled(const Json& j,const std::string& path,VehicleWheeledConfig& c){
    keys(j,path,{"massMilli","cgHeightMilli","torque","idleRpm","redlineRpm","shiftUpRpm","shiftDownRpm","gearsMilli","reverseGearMilli","finalDriveMilli","efficiencyMilli",
        "shiftTicks","automatic","brakeForce","handbrakeForce","handbrakeGripMilli","maxSteerMilli","steerRateMilli","steerFalloffSpeedMilli","frictionMilli","longitudinalGrip",
        "lateralGrip","dragMilli","rollingMilli","driftEnterMilli","driftExitMilli","driftMinSpeedMilli","wheels"});
    opt(j,path,"massMilli",c.mass,1,1000000000);opt(j,path,"cgHeightMilli",c.cgHeight,0,10000);
    if(j.has("torque"))c.torque=curve(j["torque"],path+".torque",1,1);
    optInt(j,path,"idleRpm",c.idleRpm,1,30000);optInt(j,path,"redlineRpm",c.redlineRpm,1,30000);optInt(j,path,"shiftUpRpm",c.shiftUpRpm,1,30000);optInt(j,path,"shiftDownRpm",c.shiftDownRpm,1,30000);
    if(j.has("gearsMilli")){const auto& g=j["gearsMilli"];array(g,path+".gearsMilli",kVehicleMaxGears);if(!g.size())throw JsonError(path+".gearsMilli: expected at least one gear");c.gears.clear();
        for(std::size_t i=0;i<g.size();++i){c.gears.push_back(milli(g[i],at(path+".gearsMilli",i),1,50000));if(i&&!(c.gears[i]<c.gears[i-1]))throw JsonError(at(path+".gearsMilli",i)+": ratios must be strictly descending");}}
    opt(j,path,"reverseGearMilli",c.reverseGear,1,50000);opt(j,path,"finalDriveMilli",c.finalDrive,1,50000);opt(j,path,"efficiencyMilli",c.efficiency,1,1000);
    optTicks(j,path,"shiftTicks",c.shiftTicks,600);optBool(j,path,"automatic",c.automatic);
    optInt(j,path,"brakeForce",c.brakeForce,0,10000000);optInt(j,path,"handbrakeForce",c.handbrakeForce,0,10000000);opt(j,path,"handbrakeGripMilli",c.handbrakeGrip,0,1000);
    opt(j,path,"maxSteerMilli",c.maxSteer,0,250);opt(j,path,"steerRateMilli",c.steerRate,0,10000);opt(j,path,"steerFalloffSpeedMilli",c.steerFalloffSpeed,0,1000000);
    opt(j,path,"frictionMilli",c.friction,1,10000);
    if(j.has("longitudinalGrip"))c.longitudinalGrip=curve(j["longitudinalGrip"],path+".longitudinalGrip",1000,1000);
    if(j.has("lateralGrip"))c.lateralGrip=curve(j["lateralGrip"],path+".lateralGrip",1000,1000);
    opt(j,path,"dragMilli",c.drag,0,1000000);opt(j,path,"rollingMilli",c.rolling,0,100000000);
    opt(j,path,"driftEnterMilli",c.driftEnter,0,250);opt(j,path,"driftExitMilli",c.driftExit,0,250);opt(j,path,"driftMinSpeedMilli",c.driftMinSpeed,0,1000000);
    if(j.has("wheels")){
        const auto& w=j["wheels"];array(w,path+".wheels",kVehicleMaxWheels);if(!w.size())throw JsonError(path+".wheels: expected at least one wheel");c.wheels.clear();
        for(std::size_t i=0;i<w.size();++i){
            auto p=at(path+".wheels",i);keys(w[i],p,{"offsetMilli","radiusMilli","driven","steered","braked","handbrake"});require(w[i],p,{"offsetMilli"});
            VehicleWheelDef d;d.offset=vec(w[i]["offsetMilli"],p+".offsetMilli",50000);opt(w[i],p,"radiusMilli",d.radius,1,5000);
            optBool(w[i],p,"driven",d.driven);optBool(w[i],p,"steered",d.steered);optBool(w[i],p,"braked",d.braked);optBool(w[i],p,"handbrake",d.handbrake);c.wheels.push_back(d);
        }
    }
    if(c.shiftDownRpm>=c.shiftUpRpm)throw JsonError(path+".shiftDownRpm: must be below shiftUpRpm");
    if(c.idleRpm>=c.redlineRpm)throw JsonError(path+".idleRpm: must be below redlineRpm");
    if(c.driftExit>c.driftEnter)throw JsonError(path+".driftExitMilli: must not exceed driftEnterMilli");
    if(std::none_of(c.wheels.begin(),c.wheels.end(),[](const auto& w){return w.driven;}))throw JsonError(path+".wheels: at least one wheel must be driven");
    if(!valid(c))throw JsonError(path+": inconsistent wheeled parameters (rpm order idle <= shiftDown < shiftUp <= redline)");
}
inline void tracked(const Json& j,const std::string& path,VehicleTrackedConfig& c){
    keys(j,path,{"massMilli","maxSpeedMilli","reverseSpeedMilli","accelerationMilli","brakingMilli","trackWidthMilli","lateralGripMilli","dragMilli","pivotSpeedMilli"});
    opt(j,path,"massMilli",c.mass,1,1000000000);opt(j,path,"maxSpeedMilli",c.maxSpeed,1,1000000);opt(j,path,"reverseSpeedMilli",c.reverseSpeed,0,1000000);
    opt(j,path,"accelerationMilli",c.acceleration,1,1000000);opt(j,path,"brakingMilli",c.braking,1,1000000);opt(j,path,"trackWidthMilli",c.trackWidth,1,50000);
    opt(j,path,"lateralGripMilli",c.lateralGrip,0,1000000);opt(j,path,"dragMilli",c.drag,0,100000);opt(j,path,"pivotSpeedMilli",c.pivotSpeed,0,1000000);
}
inline void hover(const Json& j,const std::string& path,VehicleHoverConfig& c){
    keys(j,path,{"massMilli","heightMilli","stiffnessMilli","dampingMilli","thrust","reverseThrust","dragMilli","lateralDampingMilli","turnRateMilli","yawResponseMilli"});
    opt(j,path,"massMilli",c.mass,1,1000000000);opt(j,path,"heightMilli",c.height,1,100000);opt(j,path,"stiffnessMilli",c.stiffness,0,1000000);opt(j,path,"dampingMilli",c.damping,0,1000000);
    optInt(j,path,"thrust",c.thrust,0,100000000);optInt(j,path,"reverseThrust",c.reverseThrust,0,100000000);opt(j,path,"dragMilli",c.drag,0,10000000);
    opt(j,path,"lateralDampingMilli",c.lateralDamping,0,1000000);opt(j,path,"turnRateMilli",c.turnRate,0,10000);opt(j,path,"yawResponseMilli",c.yawResponse,1,100000);
}
inline void flight(const Json& j,const std::string& path,VehicleFlightConfig& c){
    keys(j,path,{"massMilli","thrust","dragMilli","stallSpeedMilli","recoverSpeedMilli","pitchRateMilli","rollRateMilli","yawRateMilli","maxPitchMilli","maxRollMilli",
        "bankTurnRateMilli","levelRateMilli","stallAuthorityMilli","stallPitchDropMilli","sinkRecoveryMilli","groundFrictionMilli"});
    opt(j,path,"massMilli",c.mass,1,1000000000);optInt(j,path,"thrust",c.thrust,0,100000000);opt(j,path,"dragMilli",c.drag,0,10000000);
    opt(j,path,"stallSpeedMilli",c.stallSpeed,1,1000000);opt(j,path,"recoverSpeedMilli",c.recoverSpeed,1,1000000);opt(j,path,"pitchRateMilli",c.pitchRate,0,10000);
    opt(j,path,"rollRateMilli",c.rollRate,0,10000);opt(j,path,"yawRateMilli",c.yawRate,0,10000);opt(j,path,"maxPitchMilli",c.maxPitch,1,240);opt(j,path,"maxRollMilli",c.maxRoll,1,240);
    opt(j,path,"bankTurnRateMilli",c.bankTurnRate,0,10000);opt(j,path,"levelRateMilli",c.levelRate,0,10000);opt(j,path,"stallAuthorityMilli",c.stallAuthority,0,1000);
    opt(j,path,"stallPitchDropMilli",c.stallPitchDrop,0,10000);opt(j,path,"sinkRecoveryMilli",c.sinkRecovery,0,1000000);opt(j,path,"groundFrictionMilli",c.groundFriction,0,1000000);
    if(c.recoverSpeed<c.stallSpeed)throw JsonError(path+".recoverSpeedMilli: must not be below stallSpeedMilli");
}
inline VehicleStat stat(const Json& j,const std::string& path){
    auto s=j.str();if(s=="engine")return VehicleStat::Engine;if(s=="grip")return VehicleStat::Grip;if(s=="steering")return VehicleStat::Steering;if(s=="brakes")return VehicleStat::Brakes;if(s=="lift")return VehicleStat::Lift;
    throw JsonError(path+": expected engine, grip, steering, brakes or lift");
}
inline VehicleSeatRole role(const Json& j,const std::string& path){
    auto s=j.str();if(s=="driver")return VehicleSeatRole::Driver;if(s=="passenger")return VehicleSeatRole::Passenger;if(s=="gunner")return VehicleSeatRole::Gunner;
    throw JsonError(path+": expected driver, passenger or gunner");
}
inline VehicleDefinition vehicle(const Json& j,const std::string& path){
    keys(j,path,{"id","kind","wheeled","tracked","hover","flight","zones","seats","maxEnterSpeedMilli","maxExitSpeedMilli"});require(j,path,{"id","kind"});
    VehicleDefinition d;d.id=integer(j["id"],path+".id",1,INT64_MAX);
    auto kind=j["kind"].str();const char* names[4]={"wheeled","tracked","hover","flight"};int k=-1;for(int i=0;i<4;++i)if(kind==names[i])k=i;
    if(k<0)throw JsonError(path+".kind: expected wheeled, tracked, hover or flight");d.controller.kind=VehicleKind(k);
    for(int i=0;i<4;++i)if(i!=k&&j.has(names[i]))throw JsonError(path+"."+names[i]+": only allowed for kind "+names[i]);
    auto params=j.has(names[k])?j[names[k]]:Json::object();auto p=path+"."+names[k];
    if(k==0)wheeled(params,p,d.controller.wheeled);else if(k==1)tracked(params,p,d.controller.tracked);else if(k==2)hover(params,p,d.controller.hover);else flight(params,p,d.controller.flight);
    if(j.has("zones")){
        const auto& zs=j["zones"];array(zs,path+".zones",kVehicleMaxZones);
        for(std::size_t i=0;i<zs.size();++i){
            auto zp=at(path+".zones",i);keys(zs[i],zp,{"id","centerMilli","halfMilli","health","armorMilli","critical","penalties"});require(zs[i],zp,{"id","halfMilli"});
            VehicleZoneDef z;z.id=integer(zs[i]["id"],zp+".id",1,INT64_MAX);if(!d.damage.zones.empty()&&z.id<=d.damage.zones.back().id)throw JsonError(zp+".id: zone ids must be strictly ascending");
            if(zs[i].has("centerMilli"))z.center=vec(zs[i]["centerMilli"],zp+".centerMilli",1000000);z.half=vec(zs[i]["halfMilli"],zp+".halfMilli",1000000);
            if(z.half.x<=Fixed{}||z.half.y<=Fixed{}||z.half.z<=Fixed{})throw JsonError(zp+".halfMilli: half extents must be positive");
            if(zs[i].has("health"))z.health=integer(zs[i]["health"],zp+".health",1,kVehicleMaxHealth);opt(zs[i],zp,"armorMilli",z.armor,0,100000);optBool(zs[i],zp,"critical",z.critical);
            if(zs[i].has("penalties")){const auto& ps=zs[i]["penalties"];array(ps,zp+".penalties",kVehicleMaxPenalties);
                for(std::size_t m=0;m<ps.size();++m){auto pp=at(zp+".penalties",m);keys(ps[m],pp,{"stat","atZeroMilli"});require(ps[m],pp,{"stat","atZeroMilli"});
                    z.penalties.push_back({stat(ps[m]["stat"],pp+".stat"),milli(ps[m]["atZeroMilli"],pp+".atZeroMilli",0,1000)});}}
            d.damage.zones.push_back(z);
        }
    }
    opt(j,path,"maxEnterSpeedMilli",d.seats.maxEnterSpeed,0,1000000);opt(j,path,"maxExitSpeedMilli",d.seats.maxExitSpeed,0,1000000);
    if(j.has("seats")){
        const auto& ss=j["seats"];array(ss,path+".seats",kVehicleMaxSeats);
        for(std::size_t i=0;i<ss.size();++i){
            auto sp=at(path+".seats",i);keys(ss[i],sp,{"id","role","entryMilli","enterRadiusMilli","exitsMilli"});require(ss[i],sp,{"id","role"});
            VehicleSeatDef s;s.id=integer(ss[i]["id"],sp+".id",1,INT64_MAX);if(!d.seats.seats.empty()&&s.id<=d.seats.seats.back().id)throw JsonError(sp+".id: seat ids must be strictly ascending");
            s.role=role(ss[i]["role"],sp+".role");if(ss[i].has("entryMilli"))s.entry=vec(ss[i]["entryMilli"],sp+".entryMilli",1000000);opt(ss[i],sp,"enterRadiusMilli",s.enterRadius,1,1000000);
            if(ss[i].has("exitsMilli")){const auto& es=ss[i]["exitsMilli"];array(es,sp+".exitsMilli",kVehicleMaxExits);for(std::size_t m=0;m<es.size();++m)s.exits.push_back(vec(es[m],at(sp+".exitsMilli",m),1000000));}
            d.seats.seats.push_back(s);
        }
    }
    if(!valid(d))throw JsonError(path+": invalid vehicle definition");
    return d;
}
inline VehicleTrackDef track(const Json& j,const std::string& path){
    keys(j,path,{"id","laps","checkpoints","grid","line"});require(j,path,{"id","checkpoints"});
    VehicleTrackDef t;t.id=integer(j["id"],path+".id",1,INT64_MAX);if(j.has("laps"))t.course.laps=uint32_t(integer(j["laps"],path+".laps",1,1000));
    const auto& cs=j["checkpoints"];array(cs,path+".checkpoints",kRaceMaxCheckpoints);if(!cs.size())throw JsonError(path+".checkpoints: expected at least one checkpoint");
    for(std::size_t i=0;i<cs.size();++i){
        auto cp=at(path+".checkpoints",i);keys(cs[i],cp,{"centerMilli","normalMilli","radiusMilli","respawnMilli","respawnYawMilli"});require(cs[i],cp,{"centerMilli","normalMilli","radiusMilli"});
        RaceCheckpoint c;c.gate.id=int64_t(i+1);c.gate.center=vec(cs[i]["centerMilli"],cp+".centerMilli");c.gate.normal=fxNormalize(vec(cs[i]["normalMilli"],cp+".normalMilli",1000000));
        if(c.gate.normal==Vec3{})throw JsonError(cp+".normalMilli: must not be zero");
        c.gate.radius=milli(cs[i]["radiusMilli"],cp+".radiusMilli",1,1000000);c.respawn=cs[i].has("respawnMilli")?vec(cs[i]["respawnMilli"],cp+".respawnMilli"):c.gate.center;
        c.respawnYaw=cs[i].has("respawnYawMilli")?milli(cs[i]["respawnYawMilli"],cp+".respawnYawMilli",-1000,1000):fxAtan2Turns(c.gate.normal.x,c.gate.normal.z);
        t.course.checkpoints.push_back(c);
    }
    if(j.has("grid")){const auto& gs=j["grid"];array(gs,path+".grid",kRaceMaxRacers);
        for(std::size_t i=0;i<gs.size();++i){auto gp=at(path+".grid",i);keys(gs[i],gp,{"positionMilli","yawMilli"});require(gs[i],gp,{"positionMilli"});
            RaceGridSlot g;g.position=vec(gs[i]["positionMilli"],gp+".positionMilli");if(gs[i].has("yawMilli"))g.yaw=milli(gs[i]["yawMilli"],gp+".yawMilli",-1000,1000);t.course.grid.push_back(g);}}
    if(j.has("line")){const auto& ls=j["line"];array(ls,path+".line",bots::kRaceBotMaxPoints);for(std::size_t i=0;i<ls.size();++i)t.line.push_back(vec(ls[i],at(path+".line",i)));
        if(!bots::makeRaceLine(t.line))throw JsonError(path+".line: expected a closed loop of at least 3 points without repeats");}
    if(!valid(t.course))throw JsonError(path+": invalid course");
    return t;
}
}
inline VehicleDefinitions readVehicleDefinitions(const Json& root){
    using namespace vehicleDataDetail;VehicleDefinitions out;keys(root,"vehicles",{"vehicles","tracks"});
    if(root.has("vehicles")){const auto& vs=root["vehicles"];array(vs,"vehicles.vehicles",1024);
        for(std::size_t i=0;i<vs.size();++i){auto p=at("vehicles.vehicles",i);auto d=vehicle(vs[i],p);if(out.vehicles.contains(d.id))throw JsonError(p+".id: duplicate id "+std::to_string(d.id));out.vehicles[d.id]=std::move(d);}}
    if(root.has("tracks")){const auto& ts=root["tracks"];array(ts,"vehicles.tracks",256);
        for(std::size_t i=0;i<ts.size();++i){auto p=at("vehicles.tracks",i);auto t=track(ts[i],p);if(out.tracks.contains(t.id))throw JsonError(p+".id: duplicate id "+std::to_string(t.id));out.tracks[t.id]=std::move(t);}}
    return out;
}
}
