// vehicle-controllers + sports-rules: controllers, damage zones, seats, race checkpoints/positions, race bot,
// ball sports rules, formations, sports bot, persistence, JSON data and settings. Argument: repository root.
#include <aiverselib/genre/VehicleConfigured.h>
#include <aiverselib/genre/VehicleData.h>
#include <aiverselib/genre/VehiclePersistence.h>
#include <aiverselib/genre/SportsConfigured.h>
#include <aiverselib/genre/SportsData.h>
#include <aiverselib/genre/SportsPersistence.h>
#include <iostream>
#include <stdexcept>
#include <string>
using namespace aiverse;using namespace aiverse::lib;using namespace aiverse::lib::genre;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string(__FILE__)+":"+std::to_string(__LINE__)+": " #x);}while(false)
namespace {
const Fixed dt=Fixed::fromRatio(1,60);
template<class State> void roundtrip(const State& s){
    auto bytes=save(s);CHECK(!bytes.empty());State copy;CHECK(load(bytes,copy));CHECK(save(copy)==bytes);
    CHECK(!load(bytes+"x",copy));CHECK(!load(bytes.substr(0,bytes.size()-1),copy));auto tampered=bytes;tampered[4]^=1;CHECK(!load(tampered,copy));
}
template<class F> void throwsWith(F&& f,const std::string& text){
    try{f();}catch(const JsonError& e){if(std::string(e.what()).find(text)==std::string::npos)throw std::runtime_error("unexpected error: "+std::string(e.what())+" (wanted "+text+")");return;}
    throw std::runtime_error("expected JsonError containing "+text);
}
bool countEvent(const std::vector<VehicleEvent>& events,VehicleEventKind kind){for(const auto& e:events)if(e.kind==kind)return true;return false;}
bool hasEvent(const std::vector<SportsEvent>& events,SportsEventKind kind){for(const auto& e:events)if(e.kind==kind)return true;return false;}

void wheeledController(){
    VehicleControllerDef def;auto s=makeVehicleState(def,{},0_fx);VehicleInput full{1_fx};
    std::vector<int32_t> gears;uint32_t tick=0;
    for(;tick<600;++tick){auto r=stepVehicle(def,s,tick,dt,full,{});CHECK(r);for(const auto& e:r->events)if(e.kind==VehicleEventKind::GearChanged)gears.push_back(int32_t(e.value));}
    CHECK(vehicleForwardSpeed(s)>Fixed::fromInt(25)&&s.position.z>Fixed::fromInt(100)&&fxAbs(s.position.x)<Fixed::fromRatio(1,100));
    CHECK(gears.size()>=2&&gears[0]==2&&gears[1]==3);CHECK(s.rpm<=def.wheeled.redlineRpm);roundtrip(s);
    // Repeated tick, invalid input and dt are rejected without touching the state.
    auto before=save(s);CHECK(!stepVehicle(def,s,tick-1,dt,full,{}));CHECK(!stepVehicle(def,s,tick,dt,{2_fx},{}));CHECK(!stepVehicle(def,s,tick,Fixed{},full,{}));CHECK(save(s)==before);
    // Brake to a standstill, then the automatic gearbox engages reverse and drives backward.
    for(int i=0;i<600;++i)CHECK(stepVehicle(def,s,tick++,dt,{Fixed{},1_fx},{}));
    CHECK(s.gear==-1&&vehicleForwardSpeed(s)<Fixed{});
    // Manual gearbox: shift edges, neutral rolls without drive.
    auto manual=def;manual.wheeled.automatic=false;auto m=makeVehicleState(manual,{},0_fx);
    CHECK(stepVehicle(manual,m,0,dt,{1_fx},{}));VehicleInput up{1_fx};up.shiftUp=true;auto r=stepVehicle(manual,m,1,dt,up,{});CHECK(r&&m.gear==2&&countEvent(r->events,VehicleEventKind::GearChanged));
    VehicleInput down{};down.shiftDown=true;CHECK(stepVehicle(manual,m,2,dt,down,{})&&m.gear==1);CHECK(stepVehicle(manual,m,3,dt,down,{})&&m.gear==0);
    for(uint32_t t=4;t<64;++t)CHECK(stepVehicle(manual,m,t,dt,{1_fx},{}));CHECK(fxAbs(vehicleForwardSpeed(m))<Fixed::fromRatio(1,10)&&m.rpm>def.wheeled.idleRpm);
    // Steering turns right (positive yaw); a handbrake turn at speed slides the rear: wheel spin / drift events.
    auto turn=makeVehicleState(def,{},0_fx);bool drift=false;
    for(uint32_t t=0;t<300;++t){VehicleInput in{1_fx};if(t>=200){in.steer=1_fx;in.handbrake=true;in.throttle=Fixed{};}auto x=stepVehicle(def,turn,t,dt,in,{});CHECK(x);drift=drift||countEvent(x->events,VehicleEventKind::DriftStarted);}
    CHECK(drift&&turn.yaw>Fixed{}&&turn.position.x>Fixed{});
    // Less grip (surface or damage) means less acceleration; a disabled engine gives none.
    auto slow=makeVehicleState(def,{},0_fx),dead=slow;VehiclePerformance noEngine;noEngine.engine={};VehicleContacts ice;ice.grip=Fixed::fromRatio(2,10);auto fast=slow;
    for(uint32_t t=0;t<120;++t){CHECK(stepVehicle(def,slow,t,dt,full,ice));CHECK(stepVehicle(def,dead,t,dt,full,{},noEngine));CHECK(stepVehicle(def,fast,t,dt,full,{}));}
    CHECK(slow.position.z<fast.position.z&&dead.position.z==Fixed{});
    // Airborne / landing events from owner contacts; gravity only while airborne.
    auto air=makeVehicleState(def,{0_fx,5_fx,0_fx},0_fx);VehicleContacts none;none.grounded=false;
    auto a=stepVehicle(def,air,0,dt,{},none);CHECK(a&&countEvent(a->events,VehicleEventKind::Airborne)&&air.velocity.y<Fixed{});
    auto l=stepVehicle(def,air,1,dt,{},{});CHECK(l&&countEvent(l->events,VehicleEventKind::Landed)&&air.position.y==Fixed{});
    // Downhill ground normal accelerates a parked car; collision response removes the approaching velocity.
    VehicleContacts hill;hill.groundNormal=fxNormalize(Vec3{Fixed{},1_fx,Fixed::fromRatio(2,10)});auto roll=makeVehicleState(def,{},0_fx);
    for(uint32_t t=0;t<60;++t)CHECK(stepVehicle(def,roll,t,dt,{},hill));CHECK(roll.position.z>Fixed::fromRatio(1,2));
    auto hit=fast;auto impulse=vehicleResolveCollision(hit,hit.position,{Fixed{},Fixed{},-1_fx});CHECK(impulse>Fixed::fromInt(5)&&fxAbs(hit.velocity.z)<Fixed::fromRatio(1,100));
    // Wrong wheel contact count is invalid.
    VehicleContacts wheels;wheels.wheels.resize(3);CHECK(!stepVehicle(def,fast,999,dt,{},wheels));
}

void otherControllers(){
    VehicleControllerDef tank;tank.kind=VehicleKind::Tracked;auto t=makeVehicleState(tank,{},0_fx);
    for(uint32_t i=0;i<60;++i)CHECK(stepVehicle(tank,t,i,dt,{Fixed{},Fixed{},1_fx},{}));          // pivot turn in place
    CHECK(t.yaw>Fixed::fromRatio(1,10)&&t.position.length()<Fixed::fromRatio(1,10));
    auto drive=makeVehicleState(tank,{},0_fx);for(uint32_t i=0;i<300;++i)CHECK(stepVehicle(tank,drive,i,dt,{1_fx},{}));
    CHECK(vehicleForwardSpeed(drive)>Fixed::fromInt(12)&&vehicleForwardSpeed(drive)<=tank.tracked.maxSpeed);roundtrip(drive);
    VehicleControllerDef hover;hover.kind=VehicleKind::Hover;auto h=makeVehicleState(hover,{},0_fx);VehicleContacts ground;ground.groundHeight=2_fx;
    for(uint32_t i=0;i<600;++i)CHECK(stepVehicle(hover,h,i,dt,{},ground));
    CHECK(fxAbs(h.position.y-(2_fx+hover.hover.height))<Fixed::fromRatio(5,100));
    h.velocity.x=10_fx;for(uint32_t i=600;i<720;++i)CHECK(stepVehicle(hover,h,i,dt,{},ground));CHECK(fxAbs(h.velocity.x)<2_fx);   // lateral drift damped
    VehicleControllerDef plane;plane.kind=VehicleKind::Flight;auto p=makeVehicleState(plane,{},0_fx);bool airborne=false,stalled=false,recovered=false;
    for(uint32_t i=0;i<1500;++i){
        VehicleInput in{1_fx};if(i>=300&&i<700)in.pitch=1_fx;if(i>=700&&i<1100){in.throttle={};in.pitch=1_fx;}if(i>=1100){in.pitch=-1_fx;in.throttle=1_fx;}
        auto r=stepVehicle(plane,p,i,dt,in,{});CHECK(r);
        airborne=airborne||countEvent(r->events,VehicleEventKind::Airborne);stalled=stalled||countEvent(r->events,VehicleEventKind::Stalled);recovered=recovered||countEvent(r->events,VehicleEventKind::StallRecovered);
    }
    CHECK(airborne&&stalled&&recovered);roundtrip(p);
    VehicleControllerDef bad;bad.wheeled.gears={1_fx,2_fx};CHECK(!valid(bad));bad=VehicleControllerDef{};bad.wheeled.shiftDownRpm=bad.wheeled.shiftUpRpm;CHECK(!valid(bad));
}

void damageZones(){
    VehicleDamageDef d{{{1,{Fixed{},Fixed{},2_fx},{1_fx,1_fx,1_fx},1000,1_fx,false,{{VehicleStat::Steering,Fixed::fromRatio(1,2)}}},
                        {2,{Fixed{},Fixed{},-2_fx},{1_fx,1_fx,1_fx},500,Fixed::fromRatio(1,2),true,{{VehicleStat::Engine,Fixed{}}}}}};
    CHECK(valid(d));auto s=makeVehicleDamage(d);CHECK(vehicleZoneAt(d,{Fixed{},Fixed{},Fixed::fromRatio(25,10)})==1&&vehicleZoneAt(d,{Fixed{},Fixed{},-9_fx})==2);
    std::vector<VehicleDamageInput> in{{2,0,{Fixed{},Fixed{},3_fx},500},{1,1,{},100}};auto r=stepVehicleDamage(d,s,0,in);
    CHECK(r&&s.zones.at(1).health==400&&r->front().kind==VehicleEventKind::ZoneDamaged);   // id order: 1 then 2
    CHECK(vehiclePerformance(d,s).steering==Fixed::fromRatio(1,2)+Fixed::fromRatio(1,2)*Fixed::fromRatio(400,1000));
    std::vector<VehicleDamageInput> stale{{2,1,{},10}};auto st=stepVehicleDamage(d,s,1,stale);CHECK(st&&st->front().kind==VehicleEventKind::DamageIgnored&&s.zones.at(1).health==400);
    std::vector<VehicleDamageInput> engine{{3,2,{},2000}};auto e=stepVehicleDamage(d,s,2,engine);   // armor halves: 1000 >= 500 health
    CHECK(e&&s.disabled&&countEvent(*e,VehicleEventKind::Disabled)&&countEvent(*e,VehicleEventKind::ZoneDestroyed)&&vehiclePerformance(d,s).engine==Fixed{});
    std::vector<VehicleDamageInput> fix{{4,2,{},-250}};auto f=stepVehicleDamage(d,s,3,fix);CHECK(f&&!s.disabled&&s.zones.at(2).health==250&&countEvent(*f,VehicleEventKind::Repaired));
    roundtrip(s);auto before=save(s);CHECK(!stepVehicleDamage(d,s,3,fix));std::vector<VehicleDamageInput> dup{{5,1,{},1},{5,1,{},1}};CHECK(!stepVehicleDamage(d,s,9,dup));CHECK(save(s)==before);
}

void seats(){
    VehicleSeatsDef d{{{1,VehicleSeatRole::Driver,{-1_fx,Fixed{},Fixed{}},Fixed::fromRatio(3,2),{{-2_fx,Fixed{},Fixed{}}}},
                       {2,VehicleSeatRole::Gunner,{1_fx,Fixed{},Fixed{}},Fixed::fromRatio(3,2),{{2_fx,Fixed{},Fixed{}}}}},2_fx,3_fx};
    VehicleControllerDef def;auto car=makeVehicleState(def,{10_fx,Fixed{},Fixed{}},Fixed::fromRatio(1,4));   // facing +X: local right = -Z
    VehicleSeatState s;auto open=[](Vec3,int64_t){return true;};
    auto entry=vehicleWorldPoint(car,d.seats[0].entry);CHECK(entry.z==1_fx||fxAbs(entry.z-1_fx)<Fixed::fromRatio(1,100));
    std::vector<VehicleSeatRequest> req{{1,7,VehicleSeatAction::Enter,0,VehicleSeatRole::Driver,{10_fx,Fixed{},5_fx},1},{2,8,VehicleSeatAction::Enter,0,VehicleSeatRole::Driver,entry,1}};
    auto r=stepVehicleSeats(d,s,0,car,req,open);CHECK(r&&r->events[0].kind==VehicleEventKind::EnterRefused&&r->events[0].value==kVehicleSeatTooFar&&vehicleDriver(d,s)==8&&s.team==1);
    std::vector<VehicleSeatRequest> other{{3,9,VehicleSeatAction::Enter,1,VehicleSeatRole::Driver,entry,2}};auto o=stepVehicleSeats(d,s,1,car,other,open);
    CHECK(o&&o->events[0].value==kVehicleSeatWrongTeam);
    std::vector<VehicleSeatRequest> gunner{{4,9,VehicleSeatAction::Enter,0,VehicleSeatRole::Gunner,vehicleWorldPoint(car,d.seats[1].entry),1}};CHECK(stepVehicleSeats(d,s,2,car,gunner,open)&&s.occupants.at(2)==9);
    roundtrip(s);
    // Exit: the seat's own exit is blocked, so the next seat's exit point is used; all blocked -> refused.
    auto blockLeft=[&](Vec3 p,int64_t){return (p-vehicleWorldPoint(car,{-2_fx,Fixed{},Fixed{}})).length()>Fixed::fromRatio(1,10);};
    std::vector<VehicleSeatRequest> leave{{5,8,VehicleSeatAction::Exit}};auto x=stepVehicleSeats(d,s,3,car,leave,blockLeft);
    CHECK(x&&x->exits.size()==1&&x->exits[0].position==vehicleWorldPoint(car,{2_fx,Fixed{},Fixed{}})&&!vehicleDriver(d,s));
    std::vector<VehicleSeatRequest> leave2{{6,9,VehicleSeatAction::Exit}};auto blocked=stepVehicleSeats(d,s,4,car,leave2,[](Vec3,int64_t){return false;});
    CHECK(blocked&&blocked->events[0].value==kVehicleSeatExitBlocked&&s.occupants.size()==1);
    // Moving vehicle: entering/exiting refused, ejecting works.
    car.velocity={5_fx,Fixed{},Fixed{}};std::vector<VehicleSeatRequest> fast{{7,9,VehicleSeatAction::Exit},{8,9,VehicleSeatAction::Eject}};auto ej=stepVehicleSeats(d,s,5,car,fast,open);
    CHECK(ej&&ej->events[0].value==kVehicleSeatTooFast&&ej->events[1].kind==VehicleEventKind::Ejected&&s.occupants.empty()&&s.team==0);
    auto before=save(s);CHECK(!stepVehicleSeats(d,s,5,car,fast,open));CHECK(save(s)==before);
}

RaceCourse squareCourse(){
    RaceCourse c;c.laps=2;
    auto gate=[](int64_t id,Fixed x,Fixed z,Fixed nx,Fixed nz){RaceCheckpoint g;g.gate={id,{x,Fixed{},z},{nx,Fixed{},nz},5_fx};g.respawn=g.gate.center;g.respawnYaw=fxAtan2Turns(nx,nz);return g;};
    c.checkpoints={gate(1,Fixed{},20_fx,Fixed{},1_fx),gate(2,20_fx,40_fx,1_fx,Fixed{}),gate(3,40_fx,20_fx,Fixed{},-1_fx),gate(4,20_fx,Fixed{},-1_fx,Fixed{})};
    c.grid={{{Fixed{},Fixed{},5_fx},Fixed{}},{{2_fx,Fixed{},5_fx},Fixed{}}};return c;
}
void raceCourse(){
    auto c=squareCourse();RaceConfig cfg;cfg.wrongWayTicks=3;cfg.respawnWrongWayTicks=6;std::vector<std::pair<int64_t,int64_t>> racers{{1,1},{2,2}};
    auto s=makeRace(c,racers);CHECK(s);uint32_t tick=0;
    auto step=[&](Vec3 a,Vec3 b,Vec3 va=Vec3{},Vec3 vb=Vec3{}){std::vector<RaceSample> x{{1,a,va},{2,b,vb}};auto r=stepRace(c,cfg,*s,tick++,x);CHECK(r);return *r;};
    step({Fixed{},Fixed{},18_fx},{Fixed{},Fixed{},10_fx});
    auto r=step({Fixed{},Fixed{},22_fx},{Fixed{},Fixed{},12_fx});CHECK(r.events.size()==1&&r.events[0].kind==RaceEventKind::CheckpointPassed&&r.standings[0].racer==1);
    // Out-of-order gate is ignored; distance to the next checkpoint ranks racers on the same checkpoint.
    r=step({30_fx,Fixed{},Fixed::fromRatio(1,2)},{Fixed{},Fixed{},14_fx});CHECK(r.events.empty());
    r=step({Fixed{},Fixed{},25_fx},{Fixed{},Fixed{},19_fx});
    r=step({Fixed{},Fixed{},26_fx},{Fixed{},Fixed{},21_fx});CHECK(r.standings[0].racer==1&&r.standings[1].nextCheckpoint==1);
    r=step({Fixed{},Fixed{},26_fx},{Fixed{},Fixed{},39_fx});CHECK(r.standings[0].racer==2);   // racer 2 now closer to gate 2
    // Wrong way: driving backwards along the course for wrongWayTicks, then an automatic respawn.
    Vec3 back{Fixed{},Fixed{},-5_fx};bool wrong=false,respawned=false;
    for(int i=0;i<8;++i){r=step({Fixed{},Fixed{},26_fx},{Fixed{},Fixed{},39_fx},back);for(const auto& e:r.events){wrong=wrong||e.kind==RaceEventKind::WrongWay;respawned=respawned||e.kind==RaceEventKind::RespawnRequested;}
        if(!r.respawns.empty())CHECK(r.respawns[0].racer==1&&r.respawns[0].position==c.checkpoints[0].respawn&&r.respawns[0].checkpoint==0);}
    CHECK(wrong&&respawned&&s->racers.at(1).respawns==1);
    roundtrip(*s);auto before=save(*s);std::vector<RaceSample> dup{{1,{},{}},{1,{},{}}};CHECK(!stepRace(c,cfg,*s,tick,dup));CHECK(!stepRace(c,cfg,*s,tick-1,{}));CHECK(save(*s)==before);
    // Full laps: both racers drive the square twice; the earlier finisher wins.
    auto lapRace=makeRace(c,racers);uint32_t t=0;std::vector<Vec3> path{{Fixed{},Fixed{},10_fx},{Fixed{},Fixed{},30_fx},{10_fx,Fixed{},40_fx},{30_fx,Fixed{},40_fx},{40_fx,Fixed{},30_fx},{40_fx,Fixed{},10_fx},{30_fx,Fixed{},Fixed{}},{10_fx,Fixed{},Fixed{}}};
    std::vector<int64_t> finish;
    for(int lap=0;lap<2;++lap)for(std::size_t i=0;i<path.size();++i){
        std::vector<RaceSample> x{{1,path[i],{}},{2,path[(i+path.size()-1)%path.size()],{}}};auto rr=stepRace(c,cfg,*lapRace,t++,x);CHECK(rr);
        for(const auto& e:rr->events)if(e.kind==RaceEventKind::Finished)finish.push_back(e.racer);
    }
    std::vector<RaceSample> last{{1,path[0],{}},{2,path[7],{}}};auto lr=stepRace(c,cfg,*lapRace,t++,last);CHECK(lr);for(const auto& e:lr->events)if(e.kind==RaceEventKind::Finished)finish.push_back(e.racer);
    std::vector<RaceSample> close{{1,path[1],{}},{2,path[0],{}}};auto end=stepRace(c,cfg,*lapRace,t++,close);CHECK(end);
    for(const auto& e:end->events)if(e.kind==RaceEventKind::Finished)finish.push_back(e.racer);
    CHECK(finish.size()==2&&finish[0]==1&&raceWinner(*lapRace)==1&&raceFinished(*lapRace));
}

void raceBot(const std::string& root){
    auto defs=readVehicleDefinitions(Json::parseFile(root+"/examples/libraries/genre/vehicles-sports/vehicles.json"));
    const auto& def=defs.vehicles.at(1);const auto& track=defs.tracks.at(1);auto line=bots::makeRaceLine(track.line);CHECK(line&&line->length>Fixed::fromInt(400));
    std::vector<Vec3> dupe{{},{},{1_fx,Fixed{},Fixed{}}};CHECK(!bots::makeRaceLine(dupe));
    // One bot completes a lap of the example track through every checkpoint without leaving the course.
    auto course=track.course;course.laps=1;std::vector<std::pair<int64_t,int64_t>> one{{1,1}};auto race=makeRace(course,one);CHECK(race);
    auto car=makeVehicleState(def.controller,course.grid[0].position,course.grid[0].yaw);bots::RaceBotState bot;bots::RaceBotConfig cfg;cfg.cornerAccel=Fixed::fromRatio(84,10);
    uint32_t tick=0;Fixed worst{};
    for(;tick<3000&&!raceFinished(*race);++tick){
        auto out=bots::stepRaceBot(cfg,*line,bot,tick,{car.position,car.velocity,car.yaw,1,1,{}});CHECK(out);
        CHECK(stepVehicle(def.controller,car,tick,dt,out->input,{}));uint32_t hint=bot.segment;
        auto p=bots::raceLineProgress(*line,car.position,hint);auto [onLine,n]=bots::raceLinePoint(*line,p);(void)n;worst=fxMax(worst,Vec2{car.position.x-onLine.x,car.position.z-onLine.z}.length());
        std::vector<RaceSample> x{{1,car.position,car.velocity}};CHECK(stepRace(course,{},*race,tick,x));
    }
    CHECK(raceFinished(*race)&&tick<2000&&worst<4_fx);roundtrip(bot);
    // Overtaking: a slower car straight ahead moves the lane target to the side and lowers the target speed.
    bots::RaceBotState a;std::vector<bots::RaceBotOpponent> slowAhead{{2,{30_fx,Fixed{},-30_fx},{Fixed{},Fixed{},5_fx}}};
    auto o=bots::stepRaceBot(cfg,*line,a,0,{{30_fx,Fixed{},-36_fx},{Fixed{},Fixed{},15_fx},Fixed{},2,2,slowAhead});CHECK(o&&o->overtaking==2&&a.laneTarget!=Fixed{});
    bots::RaceBotState free;auto f=bots::stepRaceBot(cfg,*line,free,0,{{30_fx,Fixed{},-36_fx},{Fixed{},Fixed{},15_fx},Fixed{},2,2,{}});CHECK(f&&o->targetSpeed<f->targetSpeed);
    // Rubber band: last place is given a faster straight-line target than the leader.
    cfg.rubberBandMilli=100;bots::RaceBotState lead,last;
    auto x1=bots::stepRaceBot(cfg,*line,lead,0,{{30_fx,Fixed{},-20_fx},{},Fixed{},1,4,{}}),x4=bots::stepRaceBot(cfg,*line,last,0,{{30_fx,Fixed{},-20_fx},{},Fixed{},4,4,{}});
    CHECK(x1&&x4&&x4->targetSpeed>x1->targetSpeed);
    CHECK(!bots::stepRaceBot(cfg,*line,lead,0,{{30_fx,Fixed{},-20_fx},{},Fixed{},1,4,{}}));   // repeated tick
}

// ---- Sports ----
std::vector<SportsPlayer> pair2(Vec2 a,Vec2 b){SportsPlayer x;x.id=1;x.team=1;x.position=a;SportsPlayer y;y.id=2;y.team=2;y.position=b;return {x,y};}
SportsState liveMatch(const SportsRulesConfig& c,const SportsField& f,std::vector<SportsPlayer>& players,Random& rng){
    auto s=makeSportsMatch(1,2);CHECK(s);uint32_t tick=0;
    while(s->phase!=SportsPhase::Live){CHECK(stepSports(c,f,*s,tick++,dt,players,{},rng));CHECK(tick<1000);}
    return *s;
}
void sportsRules(){
    SportsRulesConfig c;c.restartDelayTicks=2;SportsField f;Random rng(5);
    auto players=pair2({-1_fx,Fixed{}},{5_fx,Fixed{}});auto s=liveMatch(c,f,players,rng);
    CHECK(s.ball.owner==1&&s.restart==SportsRestart::None&&sportsAttackDirection(c,s,1)==1&&sportsAttackDirection(c,s,2)==-1);roundtrip(s);
    // Possession lock: an immediate tackle is ignored; a pass leads a moving receiver.
    std::vector<SportsAction> tackle{{2,SportsActionKind::Tackle}};players[1].position={Fixed::fromRatio(-1,2),Fixed{}};
    auto r=stepSports(c,f,s,s.lastTick+1,dt,players,tackle,rng);CHECK(r&&hasEvent(r->events,SportsEventKind::ActionIgnored)&&s.ball.owner==1);
    // Invalid input leaves state and RNG untouched.
    auto before=save(s);auto rngState=rng.state();std::vector<SportsAction> bad{{9,SportsActionKind::Pass}};
    CHECK(!stepSports(c,f,s,s.lastTick+1,dt,players,bad,rng));CHECK(!stepSports(c,f,s,s.lastTick,dt,players,{},rng));CHECK(save(s)==before&&rng.state()==rngState);
    // Shot from close range into the +x goal (perfect accuracy): goal, kickoff for the other team.
    players={players[0]};players[0].position={15_fx,Fixed{}};players[0].accuracyMilli=1000;auto g=makeSportsMatch(1,2);uint32_t t=0;
    std::vector<SportsPlayer> both=players;SportsPlayer other;other.id=2;other.team=2;other.position={-15_fx,8_fx};both.push_back(other);
    while(g->phase!=SportsPhase::Live)CHECK(stepSports(c,f,*g,t++,dt,both,{},rng));
    both[0].position={15_fx,Fixed{}};g->ball.owner=1;
    std::vector<SportsAction> shot{{1,SportsActionKind::Shoot,{20_fx,Fixed{}},0,1000}};bool scored=false;
    for(int i=0;i<60&&!scored;++i){auto x=stepSports(c,f,*g,t++,dt,both,i==0?shot:std::vector<SportsAction>{},rng);CHECK(x);scored=hasEvent(x->events,SportsEventKind::Goal);}
    CHECK(scored&&g->score[0]==1&&g->restart==SportsRestart::Kickoff&&g->restartTeam==2&&g->phase==SportsPhase::Dead);
}
void sportsBoundaries(){
    SportsRulesConfig c;c.restartDelayTicks=1;SportsField f;Random rng(9);
    auto kick=[&](SportsField field,Vec2 from,Vec2 to,int64_t lastTeam)->SportsState{
        std::vector<SportsPlayer> players=pair2({-19_fx,-11_fx},{19_fx,11_fx});auto s=makeSportsMatch(1,2);uint32_t t=0;
        while(s->phase!=SportsPhase::Live)CHECK(stepSports(c,field,*s,t++,dt,players,{},rng));
        players[0].position={-19_fx,-11_fx};players[1].position={19_fx,11_fx};
        s->ball.owner=0;s->ball.position=from;s->ball.velocity=to;s->ball.lastTeam=lastTeam;s->ball.lastTouch=lastTeam;s->ball.kicker=0;
        for(int i=0;i<200&&s->phase==SportsPhase::Live;++i)CHECK(stepSports(c,field,*s,t++,dt,players,{},rng));return *s;
    };
    auto throwIn=kick(f,{Fixed{},10_fx},{Fixed{},20_fx},1);CHECK(throwIn.restart==SportsRestart::ThrowIn&&throwIn.restartTeam==2&&throwIn.restartSpot.y==f.halfWidth);
    auto corner=kick(f,{-15_fx,6_fx},{-20_fx,Fixed{}},1);CHECK(corner.restart==SportsRestart::Corner&&corner.restartTeam==2&&corner.restartSpot.x==-f.halfLength);   // team 1 defends -x
    auto goalKick=kick(f,{-15_fx,6_fx},{-20_fx,Fixed{}},2);CHECK(goalKick.restart==SportsRestart::GoalKick&&goalKick.restartTeam==1);
    auto walls=f;walls.boundary=SportsBoundary::Walls;auto rebound=kick(walls,{Fixed{},10_fx},{Fixed{},20_fx},1);CHECK(rebound.phase==SportsPhase::Live&&rebound.ball.velocity.y<=Fixed{});
    auto rink=f;rink.boundary=SportsBoundary::FaceOff;rink.faceoffSpots={{-10_fx,5_fx},{10_fx,5_fx}};auto face=kick(rink,{8_fx,10_fx},{Fixed{},20_fx},1);
    CHECK(face.restart==SportsRestart::FaceOff&&face.restartSpot.x==10_fx&&face.restartSpot.y==5_fx&&face.restartTeam==0);
}
void sportsContests(){
    // Tackles are contested with the passed RNG: same seed, same outcomes; failed tackles stun; fouls in the box give penalties.
    SportsRulesConfig c;c.restartDelayTicks=1;c.possessionLockTicks=0;c.behindFoulMilli=1000;SportsField f;
    auto run=[&](uint64_t seed){
        Random rng(seed);auto players=pair2({16_fx,Fixed{}},{17_fx,Fixed{}});auto s=liveMatch(c,f,players,rng);
        players[0].velocity={1_fx,Fixed{}};players[1].position={15_fx,Fixed{}};   // tackler behind the holder running into team 2's box
        std::vector<SportsAction> tackle{{2,SportsActionKind::Tackle}};auto r=stepSports(c,f,s,s.lastTick+1,dt,players,tackle,rng);CHECK(r);return std::make_pair(s,r->events);
    };
    auto a=run(1),b=run(1);CHECK(save(a.first)==save(b.first)&&a.second==b.second);
    CHECK(hasEvent(a.second,SportsEventKind::Foul)&&a.first.restart==SportsRestart::Penalty&&a.first.restartTeam==1&&a.first.restartSpot.x==f.halfLength-f.penaltySpot);
    c.behindFoulMilli=0;int won=0,lost=0;
    for(uint64_t seed=0;seed<40;++seed){
        Random rng(seed);auto players=pair2({Fixed{},Fixed{}},{1_fx,Fixed{}});auto s=liveMatch(c,f,players,rng);std::vector<SportsAction> tackle{{2,SportsActionKind::Tackle}};
        auto r=stepSports(c,f,s,s.lastTick+1,dt,players,tackle,rng);CHECK(r);if(s.ball.owner==2)++won;else{++lost;CHECK(s.stunned.at(2)>s.lastTick);}
    }
    CHECK(won>5&&lost>5);
    // Interception at the closest approach: the defender on the ball path takes it before the receiver.
    Random rng(3);std::vector<SportsPlayer> three=pair2({-10_fx,Fixed{}},{Fixed{},Fixed::fromRatio(1,2)});SportsPlayer mate;mate.id=3;mate.team=1;mate.position={10_fx,Fixed{}};three.push_back(mate);
    auto s=liveMatch(c,f,three,rng);three[0].position={-10_fx,Fixed{}};three[0].accuracyMilli=1000;s.ball.owner=1;s.ball.ownedSince=0;
    std::vector<SportsAction> pass{{1,SportsActionKind::Pass,{},3,300}};bool intercepted=false;
    for(int i=0;i<120&&!intercepted;++i){auto r=stepSports(c,f,s,s.lastTick+1,dt,three,i==0?pass:std::vector<SportsAction>{},rng);CHECK(r);intercepted=hasEvent(r->events,SportsEventKind::Interception);}
    CHECK(intercepted&&s.ball.owner==2);
    SportsBall ball;ball.position={Fixed{},Fixed{}};ball.velocity={10_fx,Fixed{}};auto ip=sportsInterceptPoint(c,ball,{8_fx,3_fx},6_fx,dt);CHECK(ip.found&&ip.point.x>Fixed{}&&ip.ticks>0);
    auto far=sportsInterceptPoint(c,ball,{8_fx,40_fx},1_fx,dt,30);CHECK(!far.found);
}
void sportsClock(){
    SportsRulesConfig c;c.periods=2;c.periodTicks=50;c.restartDelayTicks=5;c.clock=SportsClockMode::Added;c.maxStoppageTicks=3;c.goldenGoal=true;c.overtimeTicks=0;
    SportsField f;Random rng(1);auto players=pair2({-1_fx,Fixed{}},{1_fx,Fixed{}});auto s=makeSportsMatch(1,2);std::vector<SportsEvent> all;uint32_t t=0;
    for(;t<200&&!s->overtime;++t){auto r=stepSports(c,f,*s,t,dt,players,{},rng);CHECK(r);all.insert(all.end(),r->events.begin(),r->events.end());}
    // Period 1 = 50 ticks + 3 added (5 dead ticks before kickoff, capped at 3); then period 2 (ends swapped) and golden-goal overtime.
    std::vector<uint32_t> ends;uint32_t at=0;for(const auto& e:all){if(e.kind==SportsEventKind::PeriodEnded)ends.push_back(at);}(void)at;
    CHECK(s->overtime&&s->period==3&&hasEvent(all,SportsEventKind::OvertimeStarted));
    CHECK(sportsAttackDirection(c,*s,1)==1);   // period 3: swapped twice
    roundtrip(*s);
    s->ball.owner=0;while(s->phase!=SportsPhase::Live){CHECK(stepSports(c,f,*s,t++,dt,players,{},rng));}
    s->ball.owner=0;s->ball.position={19_fx,Fixed{}};s->ball.velocity={10_fx,Fixed{}};s->ball.lastTeam=1;s->ball.lastTouch=1;
    bool ended=false;for(int i=0;i<30&&!ended;++i){auto r=stepSports(c,f,*s,t++,dt,players,{},rng);CHECK(r);ended=hasEvent(r->events,SportsEventKind::MatchEnded);}
    CHECK(ended&&s->phase==SportsPhase::Finished&&s->winner==1);
    auto stopped=c;stopped.clock=SportsClockMode::Stopped;auto x=makeSportsMatch(1,2);uint32_t tt=0;
    for(;tt<10;++tt)CHECK(stepSports(stopped,f,*x,tt,dt,players,{},rng));CHECK(x->elapsed==10-5);   // dead ticks do not count
    auto timed=c;timed.goldenGoal=false;timed.overtimeTicks=0;auto d=makeSportsMatch(1,2);for(uint32_t i=0;i<400&&d->phase!=SportsPhase::Finished;++i)CHECK(stepSports(timed,f,*d,i,dt,players,{},rng));
    CHECK(d->phase==SportsPhase::Finished&&d->winner==-1);
}
void formations(const std::string& root){
    auto defs=readSportsDefinitions(Json::parseFile(root+"/examples/libraries/genre/vehicles-sports/sports.json"));const auto& form=defs.formations.at(1);const auto& field=defs.field;
    CHECK(valid(form)&&form.slots.size()==3);
    auto keeper=sportsSlotTarget(field,form.slots[0],1,SportsTeamPhase::Neutral,{});auto mirrored=sportsSlotTarget(field,form.slots[0],-1,SportsTeamPhase::Neutral,{});
    CHECK(keeper.x<Fixed::fromInt(-15)&&mirrored.x==-keeper.x);
    auto attack=sportsSlotTarget(field,form.slots[2],1,SportsTeamPhase::Attack,{}),defend=sportsSlotTarget(field,form.slots[2],1,SportsTeamPhase::Defend,{});CHECK(attack.x>defend.x);
    std::vector<SportsSlotPlayer> roster{{5,SportsRole::Forward,{}},{6,SportsRole::Keeper,{}},{7,SportsRole::Defender,{}}};
    auto slots=sportsAssignSlots(field,form,1,roster);CHECK(slots.at(1)==6&&slots.at(2)==7&&slots.at(3)==5);
    std::vector<SportsSlotPlayer> two{{5,SportsRole::Forward,{}},{7,SportsRole::Defender,{}}};auto kept=sportsAssignSlots(field,form,1,two,slots);CHECK(kept.size()==2&&kept.at(2)==7&&kept.at(3)==5&&!kept.contains(1));
}
void sportsBot(const std::string& root){
    auto defs=readSportsDefinitions(Json::parseFile(root+"/examples/libraries/genre/vehicles-sports/sports.json"));const auto& form=defs.formations.at(1);const auto& field=defs.field;
    SportsRulesConfig rules;rules.restartDelayTicks=1;rules.possessionLockTicks=0;bots::SportsBotConfig cfg;cfg.reactionTicks=0;cfg.tackleMilli=1000;Random rng(11);
    std::vector<SportsPlayer> players;std::vector<SportsSlotPlayer> a,b;const SportsRole roles[3]={SportsRole::Keeper,SportsRole::Defender,SportsRole::Forward};
    for(int t=0;t<2;++t)for(int i=0;i<3;++i){SportsPlayer p;p.id=t*10+i+1;p.team=t+1;p.position={Fixed::fromInt((t?1:-1)*(3+i*5)),Fixed::fromInt(i*2-2)};players.push_back(p);(t?b:a).push_back({p.id,roles[i],p.position});}
    auto s=makeSportsMatch(1,2);uint32_t tick=0;while(s->phase!=SportsPhase::Live)CHECK(stepSports(rules,field,*s,tick++,dt,players,{},rng));
    // Holder of team 1 inside shooting range with an open lane shoots; team 2's nearest player presses and tackles.
    s->ball.owner=3;players[2].position={10_fx,Fixed{}};players[3].position={11_fx,Fixed{}};players[3].position={11_fx,5_fx};
    bots::SportsBotState sa,sb;auto oa=bots::stepSportsBot(cfg,rules,field,form,sa,tick,dt,1,a,players,*s,rng);CHECK(oa);
    bool shoots=false;for(const auto& o:*oa)if(o.player==3)shoots=o.action.kind==SportsActionKind::Shoot&&o.intent==bots::SportsBotIntent::Shoot;CHECK(shoots);
    auto ob=bots::stepSportsBot(cfg,rules,field,form,sb,tick,dt,2,b,players,*s,rng);CHECK(ob);
    int64_t presser=0;for(const auto& o:*ob)if(o.intent==bots::SportsBotIntent::Press)presser=o.player;CHECK(presser==12);
    bool marks=false;for(const auto& o:*ob)marks=marks||o.intent==bots::SportsBotIntent::Mark;CHECK(marks);
    // Free ball: the player with the earliest interception chases it.
    s->ball.owner=0;s->ball.position={Fixed{},Fixed{}};s->ball.velocity={-5_fx,Fixed{}};bots::SportsBotState sc;auto oc=bots::stepSportsBot(cfg,rules,field,form,sc,tick+1,dt,1,a,players,*s,rng);CHECK(oc);
    int64_t chaser=0;for(const auto& o:*oc)if(o.intent==bots::SportsBotIntent::Chase)chaser=o.player;CHECK(chaser==1);
    roundtrip(sa);CHECK(!bots::stepSportsBot(cfg,rules,field,form,sa,tick,dt,1,a,players,*s,rng));
}
// Full bot-vs-bot simulation used for determinism: reruns and a mid-match restore give identical hashes.
uint64_t footballHash(const std::string& root,uint32_t ticks,uint32_t restoreAt){
    auto defs=readSportsDefinitions(Json::parseFile(root+"/examples/libraries/genre/vehicles-sports/sports.json"));SportsRulesConfig rules;rules.periodTicks=600;
    bots::SportsBotConfig cfg;auto s=*makeSportsMatch(1,2);Random rng(42);std::vector<SportsPlayer> players;std::vector<SportsSlotPlayer> a,b;const SportsRole roles[3]={SportsRole::Keeper,SportsRole::Defender,SportsRole::Forward};
    for(int t=0;t<2;++t)for(int i=0;i<3;++i){SportsPlayer p;p.id=t*10+i+1;p.team=t+1;p.position={Fixed::fromInt((t?1:-1)*(4+i*5)),Fixed::fromInt(i*3-3)};players.push_back(p);(t?b:a).push_back({p.id,roles[i],p.position});}
    bots::SportsBotState sa,sb;Hasher h;
    for(uint32_t tick=0;tick<ticks;++tick){
        if(tick==restoreAt){SportsState x;CHECK(load(save(s),x));s=x;bots::SportsBotState y;CHECK(load(save(sa),y));sa=y;CHECK(load(save(sb),y));sb=y;Random r;r.restore(rng.state(),rng.inc());rng=r;}
        auto oa=bots::stepSportsBot(cfg,rules,defs.field,defs.formations.at(1),sa,tick,dt,1,a,players,s,rng),ob=bots::stepSportsBot(cfg,rules,defs.field,defs.formations.at(1),sb,tick,dt,2,b,players,s,rng);CHECK(oa&&ob);
        std::vector<SportsAction> acts;for(const auto* os:{&*oa,&*ob})for(const auto& o:*os){if(o.action.kind!=SportsActionKind::None)acts.push_back(o.action);
            auto& p=*std::find_if(players.begin(),players.end(),[&](const auto& x){return x.id==o.player;});auto d=o.moveTo-p.position;auto l=d.length();
            p.velocity=l>Fixed::fromRatio(1,10)?d*(fxMin(6_fx,l*4)/l):Vec2{};p.position+=p.velocity*dt;}
        auto r=stepSports(rules,defs.field,s,tick,dt,players,acts,rng);CHECK(r);hashSports(h,*r);
    }
    hashSports(h,s);CHECK(s.score[0]+s.score[1]>0);return h.value();
}
void persistenceAndKinds(const std::string& root){
    CHECK(stateKind(VehicleState{})==88&&stateKind(VehicleDamageState{})==89&&stateKind(VehicleSeatState{})==90&&stateKind(RaceState{})==91&&stateKind(SportsState{})==92);
    CHECK(bots::stateKind(bots::RaceBotState{})==94&&bots::stateKind(bots::SportsBotState{})==95);
    VehicleState bad;bad.gear=-2;CHECK(save(bad).empty());SportsState badMatch;badMatch.teams={1,1};CHECK(save(badMatch).empty());
    auto a=footballHash(root,1500,0),b=footballHash(root,1500,700),c=footballHash(root,1500,9999);CHECK(a==b&&b==c);
    auto defs=readVehicleDefinitions(Json::parseFile(root+"/examples/libraries/genre/vehicles-sports/vehicles.json"));
    CHECK(vehicleDefinitionHash(defs.vehicles.at(1))==vehicleDefinitionHash(defs.vehicles.at(1)));auto changed=defs.vehicles.at(1);changed.controller.wheeled.friction=1_fx;
    CHECK(vehicleDefinitionHash(changed)!=vehicleDefinitionHash(defs.vehicles.at(1)));
    auto line=bots::makeRaceLine(defs.tracks.at(1).line);CHECK(line&&bots::raceLineHash(*line)!=0&&raceCourseHash(defs.tracks.at(1).course)!=0);
}
void dataAndSettings(const std::string& root){
    auto dir=root+"/examples/libraries/genre/vehicles-sports/";
    auto settings=gameplay::readGameplaySettings(Json::parseFile(dir+"settings.json"));CHECK(settings.value&&vehicleSettingsErrors(*settings.value).empty()&&sportsSettingsErrors(*settings.value).empty());
    CHECK(vehicleRaceLaps(*settings.value)==3&&vehicleRaceConfig(*settings.value).stuckTicks==300&&bots::raceBotConfig(*settings.value).rubberBandMilli==40);
    CHECK(sportsRulesConfig(*settings.value).clock==SportsClockMode::Added&&valid(sportsRulesConfig(*settings.value))&&valid(bots::raceBotConfig(*settings.value))&&valid(bots::sportsBotConfig(*settings.value)));
    auto seats=vehicleSeatLimits(*settings.value,{});CHECK(seats.maxEnterSpeed==2_fx);
    auto relation=gameplay::readGameplaySettings(Json::parse(R"({"schemaVersion":1,"sports":{"passSpeedMinMilli":20000,"passSpeedMaxMilli":10000}})"));
    CHECK(!relation.value&&relation.errors[0]=="sports.passSpeedMinMilli: must not exceed passSpeedMaxMilli");
    auto respawn=gameplay::readGameplaySettings(Json::parse(R"({"schemaVersion":1,"vehicles":{"wrongWayTicks":100,"respawnWrongWayTicks":50}})"));
    CHECK(respawn.value&&vehicleSettingsErrors(*respawn.value).size()==1);
    auto defs=readVehicleDefinitions(Json::parseFile(dir+"vehicles.json"));CHECK(defs.vehicles.size()==1&&defs.tracks.at(1).course.checkpoints.size()==5&&defs.vehicles.at(1).damage.zones.size()==2);
    throwsWith([]{readVehicleDefinitions(Json::parse(R"({"vehicles":[{"id":1,"kind":"boat"}]})"));},"vehicles.vehicles[0].kind: expected wheeled");
    throwsWith([]{readVehicleDefinitions(Json::parse(R"({"vehicles":[{"id":1,"kind":"wheeled","wheeled":{"gearsMilli":[1000,2000]}}]})"));},"vehicles.vehicles[0].wheeled.gearsMilli[1]: ratios must be strictly descending");
    throwsWith([]{readVehicleDefinitions(Json::parse(R"({"vehicles":[{"id":1,"kind":"hover","wheeled":{}}]})"));},"vehicles.vehicles[0].wheeled: only allowed for kind wheeled");
    throwsWith([]{readVehicleDefinitions(Json::parse(R"({"vehicles":[{"id":1,"kind":"tracked","tracked":{"speed":1}}]})"));},"vehicles.vehicles[0].tracked: unknown key 'speed'");
    throwsWith([]{readVehicleDefinitions(Json::parse(R"({"vehicles":[{"id":1,"kind":"flight","zones":[{"id":2,"halfMilli":[1,1,1]},{"id":1,"halfMilli":[1,1,1]}]}]})"));},"vehicles.vehicles[0].zones[1].id: zone ids must be strictly ascending");
    throwsWith([]{readVehicleDefinitions(Json::parse(R"({"vehicles":[{"id":1,"kind":"flight","seats":[{"id":1,"role":"pilot"}]}]})"));},"vehicles.vehicles[0].seats[0].role: expected driver");
    throwsWith([]{readVehicleDefinitions(Json::parse(R"({"tracks":[{"id":1,"checkpoints":[{"centerMilli":[0,0,0],"normalMilli":[0,0,0],"radiusMilli":1}]}]})"));},"vehicles.tracks[0].checkpoints[0].normalMilli: must not be zero");
    throwsWith([]{readVehicleDefinitions(Json::parse(R"({"vehicles":[{"id":1,"kind":"flight"},{"id":1,"kind":"hover"}]})"));},"vehicles.vehicles[1].id: duplicate id 1");
    auto sports=readSportsDefinitions(Json::parseFile(dir+"sports.json"));CHECK(sports.field.halfLength==20_fx);
    throwsWith([]{readSportsDefinitions(Json::parse(R"({"field":{"boundary":"net"}})"));},"sports.field.boundary: expected lines, walls or faceoff");
    throwsWith([]{readSportsDefinitions(Json::parse(R"({"field":{"halfWidthMilli":2000,"goalHalfWidthMilli":3000}})"));},"sports.field.goalHalfWidthMilli: must not exceed halfWidthMilli");
    throwsWith([]{readSportsDefinitions(Json::parse(R"({"formations":[{"id":1,"slots":[{"id":1,"role":"striker","baseMilli":[0,0]}]}]})"));},"sports.formations[0].slots[0].role: expected keeper");
    throwsWith([]{readSportsDefinitions(Json::parse(R"({"formations":[{"id":1,"slots":[{"id":1,"role":"keeper","baseMilli":[2000,0]}]}]})"));},"sports.formations[0].slots[0].baseMilli[0]: must be in -1000..1000");
}
}

int main(int argc,char** argv){
    try{
        CHECK(argc==2);std::string root=argv[1];
        wheeledController();otherControllers();damageZones();seats();raceCourse();raceBot(root);
        sportsRules();sportsBoundaries();sportsContests();sportsClock();formations(root);sportsBot(root);persistenceAndKinds(root);dataAndSettings(root);
        std::cout<<"Vehicles + sports: controllers, damage zones, seats, race positions, race bot, ball rules, formations, sports bot, persistence, data: OK\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
