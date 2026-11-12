// Deterministic gameplay example for vehicle-controllers and sports-rules, driven only by the library bot
// policies: a 4-car race on a looped track (wheeled controllers, seats, damage zones from car-to-car contacts,
// checkpoints/laps/positions, race bots with overtaking and rubber band) and a 3v3 arcade football match
// (possession, passes, shots, tackles/fouls, restarts, periods with stoppage, golden goal, positioning bots).
// Both simulations save every record mid-run, continue from the loaded copies and print a final hash.
// Usage: aiverse_vehicles_sports_example <data dir with vehicles.json, sports.json, settings.json>
#include <aiverselib/genre/VehicleConfigured.h>
#include <aiverselib/genre/VehicleData.h>
#include <aiverselib/genre/VehiclePersistence.h>
#include <aiverselib/genre/SportsConfigured.h>
#include <aiverselib/genre/SportsData.h>
#include <aiverselib/genre/SportsPersistence.h>
#include <cinttypes>
#include <cstdio>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>
using namespace aiverse;
using namespace aiverse::lib;
using namespace aiverse::lib::genre;
namespace {
void require(bool ok,const char* what){if(!ok)throw std::runtime_error(what);}
template<class T> T roundtrip(const T& value){T copy;require(load(save(value),copy),"record failed to load");return copy;}
const Fixed kDt=Fixed::fromRatio(1,60);

struct Car{int64_t id=0,driver=0;VehicleState body;VehicleDamageState damage;VehicleSeatState seats;bots::RaceBotState bot;bots::RaceBotConfig botConfig;};
struct RaceResult{int64_t winner=0;uint64_t hash=0;};

RaceResult runRace(const VehicleDefinitions& defs,const gameplay::GameplaySettings& settings){
    const auto& def=defs.vehicles.at(1);auto course=defs.tracks.at(1).course;course.laps=vehicleRaceLaps(settings);
    auto line=bots::makeRaceLine(defs.tracks.at(1).line);require(line.has_value(),"racing line");
    const auto raceConfig=vehicleRaceConfig(settings);const auto base=bots::raceBotConfig(settings);
    // Four drivers with different pace and cornering; the rubber band keeps the pack together.
    const int64_t skill[4]={1000,990,975,1010};const int64_t corner[4]={8400,8800,8000,7600};
    std::vector<Car> cars(4);std::vector<std::pair<int64_t,int64_t>> roster;
    for(std::size_t i=0;i<4;++i){
        auto& c=cars[i];c.id=int64_t(i+1);c.driver=100+c.id;c.body=makeVehicleState(def.controller,course.grid[i].position,course.grid[i].yaw);
        c.damage=makeVehicleDamage(def.damage);c.botConfig=base;c.botConfig.skillMilli=skill[i];c.botConfig.cornerAccel=Fixed::fromRatio(corner[i],1000);
        roster.push_back({c.id,c.id});
    }
    auto race=makeRace(course,roster);require(race.has_value(),"race");
    std::vector<RaceStanding> standings;for(std::size_t i=0;i<4;++i)standings.push_back({cars[i].id,uint32_t(i+1)});
    std::map<VehicleEventKind,int> vehicleEvents;std::map<RaceEventKind,int> raceEvents;int64_t damageId=0,contacts=0;
    auto noObstacle=[](Vec3,int64_t){return true;};
    // Drivers walk up to their cars and enter the driver seat (validated: speed, distance, free seat).
    for(auto& c:cars){
        VehicleSeatRequest enter{1,c.driver,VehicleSeatAction::Enter,0,VehicleSeatRole::Driver,vehicleWorldPoint(c.body,def.seats.seats[0].entry),c.id};
        auto r=stepVehicleSeats(def.seats,c.seats,0,c.body,std::span(&enter,1),noObstacle);require(r&&vehicleDriver(def.seats,c.seats)==c.driver,"driver entered");
    }
    uint32_t tick=1;
    for(;tick<30000&&!raceFinished(*race);++tick){
        if(tick==1500){   // save every record and continue from the loaded copies
            for(auto& c:cars){c.body=roundtrip(c.body);c.damage=roundtrip(c.damage);c.seats=roundtrip(c.seats);c.bot=roundtrip(c.bot);}
            *race=roundtrip(*race);
        }
        std::map<int64_t,uint32_t> place;for(const auto& s:standings)place[s.racer]=s.place;
        for(auto& c:cars){
            std::vector<bots::RaceBotOpponent> others;for(const auto& o:cars)if(o.id!=c.id)others.push_back({o.id,o.body.position,o.body.velocity});
            VehicleInput input{};
            if(vehicleDriver(def.seats,c.seats)){
                auto out=bots::stepRaceBot(c.botConfig,*line,c.bot,tick,{c.body.position,c.body.velocity,c.body.yaw,place.at(c.id),4,others});require(out.has_value(),"race bot");input=out->input;
            }
            auto step=stepVehicle(def.controller,c.body,tick,kDt,input,{},vehiclePerformance(def.damage,c.damage));require(step.has_value(),"vehicle step");
            for(const auto& e:step->events)++vehicleEvents[e.kind];
        }
        // Collision owner: car bodies are circles (radius 1.3 m); separate, resolve velocities, turn impulses into damage.
        std::map<int64_t,std::vector<VehicleDamageInput>> hits;
        for(std::size_t a=0;a<cars.size();++a)for(std::size_t b=a+1;b<cars.size();++b){
            auto& x=cars[a];auto& y=cars[b];Vec3 d=y.body.position-x.body.position;d.y={};auto dist=d.length();const auto reach=Fixed::fromRatio(26,10);
            if(dist>=reach||dist<=Fixed{})continue;
            auto n=d/dist;auto push=(reach-dist)/2;++contacts;
            auto ix=vehicleResolveCollision(x.body,x.body.position-n*push,Vec3{}-n,Fixed::fromRatio(2,10));
            auto iy=vehicleResolveCollision(y.body,y.body.position+n*push,n,Fixed::fromRatio(2,10));
            auto contact=x.body.position+n*Fixed::fromRatio(13,10);
            auto hurt=[&](Car& car,Fixed impulse){if(impulse>2_fx)hits[car.id].push_back({++damageId,0,vehicleLocalPoint(car.body,contact),(impulse*Fixed::fromInt(15)).toInt()});};
            hurt(x,ix+iy);hurt(y,ix+iy);
        }
        for(auto& c:cars){auto r=stepVehicleDamage(def.damage,c.damage,tick,hits[c.id]);require(r.has_value(),"damage step");for(const auto& e:*r)++vehicleEvents[e.kind];}
        std::vector<RaceSample> samples;for(const auto& c:cars)samples.push_back({c.id,c.body.position,c.body.velocity});
        auto report=stepRace(course,raceConfig,*race,tick,samples);require(report.has_value(),"race step");
        for(const auto& e:report->events){++raceEvents[e.kind];if(e.kind==RaceEventKind::Finished)std::printf("race: tick %u car %" PRId64 " finished place %" PRId64 "\n",tick,e.racer,e.value);}
        for(const auto& r:report->respawns){auto& c=cars[std::size_t(r.racer-1)];respawnVehicle(def.controller,c.body,r.position,r.yaw);c.bot=bots::RaceBotState{};}
        standings=report->standings;
    }
    RaceResult result;result.winner=raceWinner(*race);
    Hasher h;
    for(const auto& c:cars){
        hashVehicle(h,c.body);hashVehicle(h,c.damage);hashVehicle(h,c.seats);h.u64(bots::raceBotHash(c.bot));
        auto perf=vehiclePerformance(def.damage,c.damage);
        std::printf("race: car %" PRId64 " place %u laps %u respawns %u engine %" PRId64 "/1000 steering %" PRId64 "/1000\n",c.id,race->route.racers.at(c.id).place,race->route.racers.at(c.id).lap,
            race->racers.at(c.id).respawns,(perf.engine*Fixed::fromInt(1000)).toInt(),(perf.steering*Fixed::fromInt(1000)).toInt());
    }
    hashVehicle(h,*race);result.hash=h.value();
    std::printf("race: ticks %u contacts %" PRId64 " gear changes %d drifts %d checkpoints %d wrong-way %d respawns %d\n",tick,contacts,vehicleEvents[VehicleEventKind::GearChanged],
        vehicleEvents[VehicleEventKind::DriftStarted],raceEvents[RaceEventKind::CheckpointPassed],raceEvents[RaceEventKind::WrongWay],raceEvents[RaceEventKind::RespawnRequested]);
    return result;
}

struct MatchResult{int64_t winner=0;int64_t a=0,b=0;uint64_t hash=0;};
MatchResult runFootball(const SportsDefinitions& defs,const gameplay::GameplaySettings& settings){
    const auto rules=sportsRulesConfig(settings);const auto& field=defs.field;const auto& formation=defs.formations.at(1);
    auto red=bots::sportsBotConfig(settings);auto blue=red;blue.tackleMilli=red.tackleMilli*3/4;blue.shootRange=red.shootRange-Fixed::fromInt(2);blue.passMinGain=1_fx;
    auto match=makeSportsMatch(1,2);require(match.has_value(),"match");Random rng(20261112);
    std::vector<SportsPlayer> players;std::vector<SportsSlotPlayer> rosterA,rosterB;const SportsRole roles[3]={SportsRole::Keeper,SportsRole::Defender,SportsRole::Forward};
    for(int team=0;team<2;++team)for(int i=0;i<3;++i){
        SportsPlayer p;p.id=team*10+i+1;p.team=team+1;p.position={Fixed::fromInt((team?1:-1)*(4+i*5)),Fixed::fromInt(i*3-3)};
        p.accuracyMilli=team?520:600;p.controlMilli=team?600:520;p.tackleMilli=500;players.push_back(p);(team?rosterB:rosterA).push_back({p.id,roles[i],p.position});
    }
    bots::SportsBotState botA,botB;std::map<SportsEventKind,int> counts;uint32_t tick=0;
    for(;tick<60*60*20&&match->phase!=SportsPhase::Finished;++tick){
        if(tick==3000){*match=roundtrip(*match);botA=roundtrip(botA);botB=roundtrip(botB);Random copy;copy.restore(rng.state(),rng.inc());rng=copy;}
        auto ordersA=bots::stepSportsBot(red,rules,field,formation,botA,tick,kDt,1,rosterA,players,*match,rng);
        auto ordersB=bots::stepSportsBot(blue,rules,field,formation,botB,tick,kDt,2,rosterB,players,*match,rng);
        require(ordersA&&ordersB,"sports bot");
        // Movement owner: accelerate toward the order's target (sprint 6.5 m/s, jog 5 m/s, 20 m/s^2), stay on the pitch.
        std::vector<SportsAction> actions;
        for(const auto* orders:{&*ordersA,&*ordersB})for(const auto& o:*orders){
            if(o.action.kind!=SportsActionKind::None)actions.push_back(o.action);
            auto& p=*std::find_if(players.begin(),players.end(),[&](const auto& x){return x.id==o.player;});
            auto d=o.moveTo-p.position;auto l=d.length();auto top=o.sprint?Fixed::fromRatio(65,10):Fixed::fromInt(5);
            Vec2 want=l>Fixed::fromRatio(1,10)?d*(fxMin(top,l*4)/l):Vec2{};auto dv=want-p.velocity;auto accel=Fixed::fromInt(20)*kDt;auto dl=dv.length();
            if(dl>accel)dv=dv*(accel/dl);p.velocity+=dv;p.position+=p.velocity*kDt;
            p.position={fxClamp(p.position.x,-field.halfLength-1_fx,field.halfLength+1_fx),fxClamp(p.position.y,-field.halfWidth-1_fx,field.halfWidth+1_fx)};
        }
        auto report=stepSports(rules,field,*match,tick,kDt,players,actions,rng);require(report.has_value(),"sports step");
        for(const auto& e:report->events){
            ++counts[e.kind];
            if(e.kind==SportsEventKind::Goal)std::printf("football: tick %u goal team %" PRId64 " scorer %" PRId64 " (%" PRId64 "-%" PRId64 ")\n",tick,e.team,e.extra,match->score[0],match->score[1]);
            if(e.kind==SportsEventKind::OvertimeStarted)std::printf("football: tick %u golden-goal overtime\n",tick);
        }
    }
    std::printf("football: ticks %u passes %d shots %d interceptions %d tackles won %d lost %d fouls %d restarts %d\n",tick,counts[SportsEventKind::Pass],counts[SportsEventKind::Shot],
        counts[SportsEventKind::Interception],counts[SportsEventKind::TackleWon],counts[SportsEventKind::TackleLost],counts[SportsEventKind::Foul],counts[SportsEventKind::RestartTaken]);
    Hasher h;hashSports(h,*match);h.u64(bots::sportsBotHash(botA));h.u64(bots::sportsBotHash(botB));h.u64(rng.state());
    return {match->winner,match->score[0],match->score[1],h.value()};
}
}

int main(int argc,char** argv){
    try{
        std::string dir=argc>1?argv[1]:".";
        auto parsed=gameplay::readGameplaySettings(Json::parseFile(dir+"/settings.json"));
        if(!parsed.value)throw std::runtime_error("settings: "+parsed.errors.front());
        for(const auto& e:vehicleSettingsErrors(*parsed.value))throw std::runtime_error(e);
        for(const auto& e:sportsSettingsErrors(*parsed.value))throw std::runtime_error(e);
        auto vehicles=readVehicleDefinitions(Json::parseFile(dir+"/vehicles.json"));
        auto sports=readSportsDefinitions(Json::parseFile(dir+"/sports.json"));
        auto race=runRace(vehicles,*parsed.value);
        auto football=runFootball(sports,*parsed.value);
        Hasher h;h.u64(race.hash);h.u64(football.hash);
        std::printf("race winner=%" PRId64 "\n",race.winner);
        std::printf("match winner=%" PRId64 " score=%" PRId64 "-%" PRId64 "\n",football.winner,football.a,football.b);
        std::printf("hash=%016" PRIx64 "\n",h.value());
        return race.winner>0&&football.winner>0?0:2;
    }catch(const std::exception& e){std::fprintf(stderr,"vehicles-sports example: %s\n",e.what());return 1;}
}
