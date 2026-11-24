// battle-royale: phased zone, bombardment, supply drops, air insertion, standings, lobby, persistence and JSON.
#include "engine/mechanics/royale/BattleRoyale.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
using namespace aiverse;using namespace aiverse::lib::mechanics;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string(__FILE__)+":"+std::to_string(__LINE__)+": " #x);}while(false)
namespace {
template<class State> void roundtrip(const State& s){
    auto bytes=royaleSave(s);CHECK(!bytes.empty());State copy;CHECK(royaleLoad(bytes,copy));CHECK(copy==s);CHECK(royaleHash(copy)==royaleHash(s));
    CHECK(!royaleLoad(bytes+"x",copy));CHECK(!royaleLoad(bytes.substr(0,bytes.size()-1),copy));
    auto other=bytes;other[4]=char(other[4]+1);CHECK(!royaleLoad(other,copy));   // another record kind
}
// Range-for over `*step(...)` would bind to a temporary optional (dangling before C++23): copy the value.
template<class T> T val(std::optional<T> o){CHECK(o);return std::move(*o);}
Vec2 v(int64_t x,int64_t z){return {Fixed::fromInt(x),Fixed::fromInt(z)};}
bool within(const RoyaleCircle& inner,const RoyaleCircle& outer){return (inner.center-outer.center).length()+inner.radius<=outer.radius+Fixed::fromRatio(1,100);}
SafeZoneDef safeZoneDef(){
    SafeZoneDef d;d.bounds={v(-1000,-1000),v(1000,1000)};d.initial={v(0,0),1400_fx};d.startTick=10;
    d.phases={{20,40,600_fx,1,10,{}},{10,20,250_fx,2,10,{}},{0,0,100_fx,5,5,{}},{5,10,0_fx,9,5,{}}};return d;
}
void zone(){
    auto d=safeZoneDef();auto s=beginSafeZone(7);SafeZoneState blank;CHECK(!stepSafeZone(d,blank,1));   // unseeded state rejected
    CHECK(stepSafeZone(d,s,0)->empty());CHECK(!stepSafeZone(d,s,0));CHECK(safeZoneCircle(d,s,5)==d.initial);CHECK(safeZoneDamage(d,s,0)==0);
    for(uint32_t t=1;t<10;++t)CHECK(stepSafeZone(d,s,t)->empty());
    auto e=*stepSafeZone(d,s,10);CHECK(e.size()==1&&e[0].kind==SafeZoneEventKind::Announced&&e[0].phase==0);
    CHECK(within(e[0].circle,d.initial)&&e[0].circle.radius==600_fx);CHECK(safeZoneTicksLeft(d,s,10)==20);
    auto first=e[0].circle;
    for(uint32_t t=11;t<30;++t)CHECK(stepSafeZone(d,s,t)->empty());
    e=*stepSafeZone(d,s,30);CHECK(e.size()==1&&e[0].kind==SafeZoneEventKind::ShrinkStarted);
    auto mid=safeZoneCircle(d,s,50);CHECK(mid.radius==1000_fx);   // halfway 1400 -> 600
    CHECK(safeZoneCircle(d,s,30)==d.initial&&safeZoneCircle(d,s,70)==first);
    CHECK(safeZoneDamage(d,s,40)==1&&safeZoneDamage(d,s,41)==0);
    std::vector<SafeZoneEvent> all;for(uint32_t t=31;t<=200;++t)for(auto x:val(stepSafeZone(d,s,t)))all.push_back(x);
    // phase 1 announced when phase 0 ends at 70; phase 2 has no wait / no shrink; phase 3 closes at 70+10+20+0+5+10=115
    CHECK(all.front().kind==SafeZoneEventKind::ShrinkEnded&&all.front().phase==0);
    CHECK(s.stage==SafeZoneStage::Closed&&s.phase==3&&safeZoneCircle(d,s,500).radius==Fixed{});
    CHECK(all.back().kind==SafeZoneEventKind::Closed);
    RoyaleCircle prev=first;for(auto x:all)if(x.kind==SafeZoneEventKind::Announced){CHECK(within(x.circle,prev));prev=x.circle;}
    // same seed, one coarse step: identical end state; another seed: another first circle
    auto c=beginSafeZone(7);CHECK(stepSafeZone(d,c,500));CHECK(c.from==s.from&&c.stage==SafeZoneStage::Closed);
    auto o=beginSafeZone(8);stepSafeZone(d,o,10);CHECK(o.to!=first);
    // concentric with centerPull 0, authored centre respected
    auto cd=d;cd.centerPull={};cd.phases[0].center=v(100,-50);auto cs=beginSafeZone(3);stepSafeZone(cd,cs,10);CHECK(cs.to.center==v(100,-50));
    stepSafeZone(cd,cs,70);CHECK(cs.to.center==v(100,-50));   // phase 1 concentric with phase 0
    auto bad=d;bad.phases[1].radius=700_fx;CHECK(!stepSafeZone(bad,s,600));
    roundtrip(s);roundtrip(beginSafeZone(1));
}
void bombardment(){
    BombardmentDef d;d.firstTick=100;d.intervalTicks=200;d.durationTicks=50;d.warningTicks=20;d.areaRadius=50_fx;d.shellInterval=5;d.shellsPerVolley=2;d.maxStrikes=2;
    auto s=beginBombardment(d,9);RoyaleCircle safe{v(10,10),300_fx};std::vector<BombardmentEvent> all;
    for(uint32_t t=0;t<=600;++t)for(auto e:val(stepBombardment(d,s,safe,t))){all.push_back(e);if(e.kind==BombardmentEventKind::Shell){CHECK(insideCircle(s.area,e.circle.center));CHECK(t>=100);}}
    CHECK(all[0].kind==BombardmentEventKind::Warning&&within(all[0].circle,safe));
    int shells=0,starts=0,ends=0;for(auto e:all){shells+=e.kind==BombardmentEventKind::Shell;starts+=e.kind==BombardmentEventKind::Started;ends+=e.kind==BombardmentEventKind::Ended;}
    CHECK(starts==2&&ends==2&&shells==2*2*10&&s.stage==BombardmentStage::Done);
    CHECK(!stepBombardment(d,s,safe,600));roundtrip(s);
}
void supply(){
    SupplyDropDef d;d.firstTick=50;d.intervalTicks=100;d.maxDrops=3;d.fallTicks=30;d.edgeMargin=5_fx;
    auto s=beginSupplyDrops(4);RoyaleCircle area{v(0,0),100_fx};int announced=0,landed=0;
    for(uint32_t t=0;t<=400;++t)for(auto e:val(stepSupplyDrops(d,s,area,t))){
        if(e.kind==SupplyDropEventKind::Announced){++announced;CHECK(t==50+uint32_t(announced-1)*100&&e.lands==t+30&&insideCircle(RoyaleCircle{{},95_fx},e.point));}
        else{++landed;CHECK(t==e.lands);}
        if(t==60&&!s.falling.empty())CHECK(supplyDropPosition(d,s.falling[0],65,10_fx).y==Fixed::fromInt(10)+d.altitude/2);
    }
    CHECK(announced==3&&landed==3&&s.falling.empty());roundtrip(s);
}
void insertion(){
    InsertionFlightDef fd;fd.crossRadius=100_fx;fd.doorOpenTicks=10;RoyaleBounds b{v(-1000,-1000),v(1000,1000)};
    auto f=*planInsertionFlight(fd,b,11,100);CHECK(f.durationTicks>0&&planInsertionFlight(fd,b,11,100)==f&&!(planInsertionFlight(fd,b,12,100)==f));
    auto mid=(f.start+f.end)/2_fx;CHECK(mid.xz().length()<=101_fx);CHECK(fxAbs(f.start.x)>1000_fx||fxAbs(f.start.z)>1000_fx);
    CHECK(insertionPosition(f,100)==f.start&&insertionPosition(f,100+f.durationTicks)==f.end&&!insertionDoorOpen(fd,f,105)&&insertionDoorOpen(fd,f,110));
    SkydiveDef d;SkydiverState s;SkydiveInput none;CHECK(stepSkydiver(d,fd,f,s,none,Fixed{},100)->empty());
    SkydiveInput jump;jump.jump=true;CHECK(stepSkydiver(d,fd,f,s,jump,Fixed{},105)->empty());   // door still closed
    auto e=*stepSkydiver(d,fd,f,s,jump,Fixed{},110);CHECK(e.size()==1&&e[0].kind==SkydiveEventKind::Jumped&&s.phase==SkydivePhase::Freefall);
    auto dive=s;SkydiveInput diving;diving.dive=true;diving.move=v(1,0);
    uint32_t t=111;for(;t<220;++t){stepSkydiver(d,fd,f,s,none,Fixed{},t);stepSkydiver(d,fd,f,dive,diving,Fixed{},t);}
    CHECK(dive.position.y<s.position.y&&dive.velocity.x>s.velocity.x);
    SkydiveInput deploy;deploy.deploy=true;
    bool opened=false,landed=false;
    for(;t<5000&&!landed;++t)for(auto x:val(stepSkydiver(d,fd,f,s,deploy,Fixed{},t))){
        if(x.kind==SkydiveEventKind::ParachuteOpened){opened=true;}
        if(x.kind==SkydiveEventKind::Landed){landed=true;CHECK(x.position.y==Fixed{});}
    }
    CHECK(opened&&landed&&s.phase==SkydivePhase::Landed&&s.velocity==Vec3{});
    // auto deploy: no input opens the chute at autoDeployHeight above the ground (ground at 500 m)
    SkydiverState a;stepSkydiver(d,fd,f,a,jump,500_fx,110);bool auto_=false;
    for(uint32_t u=111;u<3000&&a.phase!=SkydivePhase::Landed;++u)for(auto x:val(stepSkydiver(d,fd,f,a,none,500_fx,u)))
        if(x.kind==SkydiveEventKind::ParachuteOpened){auto_=true;CHECK(x.position.y-500_fx<=d.autoDeployHeight&&x.position.y-500_fx>d.autoDeployHeight-d.fallSpeed*2);}
    CHECK(auto_);
    // forced eject at the end of the path
    SkydiverState late;for(uint32_t u=100;u<=100+f.durationTicks;++u){auto x=*stepSkydiver(d,fd,f,late,none,Fixed{},u);if(u==100+f.durationTicks)CHECK(x.size()==1&&x[0].kind==SkydiveEventKind::Ejected);}
    SkydiveInput wild;wild.move=v(2,0);auto copy=s;CHECK(!stepSkydiver(d,fd,f,s,wild,Fixed{},t+1)&&s==copy);
    roundtrip(f);roundtrip(s);roundtrip(late);
}
void standings(){
    RoyaleStandingsConfig c;RoyaleStandings s;
    for(int64_t p=1;p<=6;++p)CHECK(addRoyaleCompetitor(s,p,(p+1)/2));   // teams 1:{1,2} 2:{3,4} 3:{5,6}
    CHECK(!addRoyaleCompetitor(s,1,1));CHECK(!reportDown(c,s,1,3,0,1));CHECK(startRoyaleStandings(s,0));CHECK(!addRoyaleCompetitor(s,7,4));
    auto e=*reportDown(c,s,1,3,100,5);CHECK(e.size()==1&&e[0].kind==KillFeedKind::Knocked&&s.players[3].knocks==1);
    CHECK(reportDamage(s,3,1,50,5)&&!reportDamage(s,3,4,10,5)&&s.players[3].damage==50);
    e=*reportRevive(s,1,2,6);CHECK(e[0].kind==KillFeedKind::Revived&&s.players[1].life==RoyaleLife::Standing);
    CHECK(reportDown(c,s,1,3,100,7));
    e=*reportDown(c,s,2,4,101,8);   // last standing goes down: the knocked mate is finished (credit to its knocker)
    CHECK(e.size()==3&&e[0].kind==KillFeedKind::Eliminated&&e[0].subject==2&&e[1].subject==1&&e[1].actor==3&&e[2].kind==KillFeedKind::TeamOut&&e[2].value==3);
    CHECK(s.players[3].kills==1&&s.players[4].kills==1&&s.teams[1].placement==3);
    CHECK(!reportDown(c,s,1,3,0,9));CHECK(!reportDown(c,s,5,3,0,4));   // already out; tick back
    // the last two teams go out together: a draw sharing first place
    CHECK(reportDown(c,s,3,0,0,10));
    e=*reportOutTogether(c,s,{{4,0},{5,6},{6,0}},12);
    CHECK(s.finished&&s.winner==-1&&s.teams[3].placement==1&&s.teams[2].placement==1&&e.back().kind==KillFeedKind::Draw&&s.players[6].kills==0);
    CHECK(!reportOut(c,s,4,0,0,13));
    // three solos: two out on one tick share second place
    RoyaleStandings t3;for(int64_t p=1;p<=3;++p)addRoyaleCompetitor(t3,p,p);startRoyaleStandings(t3,0);
    CHECK(reportOut(c,t3,1,3,0,5));CHECK(t3.teams[1].placement==3);
    RoyaleStandings t4;for(int64_t p=1;p<=4;++p)addRoyaleCompetitor(t4,p,p);startRoyaleStandings(t4,0);
    reportOut(c,t4,1,4,0,5);reportOut(c,t4,2,4,0,5);CHECK(t4.teams[1].placement==3&&t4.teams[2].placement==3&&!t4.finished);
    roundtrip(s);
    // solo winner, no knockdowns
    RoyaleStandingsConfig solo;solo.knockdowns=false;RoyaleStandings w;for(int64_t p=1;p<=3;++p)addRoyaleCompetitor(w,p,p);startRoyaleStandings(w,0);
    e=*reportDown(solo,w,1,2,0,1);CHECK(e.size()==2&&e[0].kind==KillFeedKind::Eliminated&&e[1].value==3);
    e=*reportDown(solo,w,3,2,0,2);CHECK(w.finished&&w.winner==2&&e.back().kind==KillFeedKind::Winner);
    auto r=royaleRanking(w);CHECK(r[0]==std::make_pair(int64_t(2),1u)&&r[2].first==1);
    RoyaleStandings one;addRoyaleCompetitor(one,1,1);CHECK(startRoyaleStandings(one,0)->size()==1&&one.winner==1);
}
void lobby(){
    RoyaleLobbyDef d;d.minPlayers=2;d.fillPlayers=4;d.maxPlayers=4;d.maxWaitTicks=100;d.countdownTicks=10;RoyaleLobbyState s;
    CHECK(stepRoyaleLobby(d,s,1,0)->empty());CHECK(stepRoyaleLobby(d,s,2,50)->empty());
    auto e=*stepRoyaleLobby(d,s,2,100);CHECK(e.size()==1&&e[0].kind==RoyaleLobbyEventKind::CountdownStarted);
    e=*stepRoyaleLobby(d,s,1,105);CHECK(e[0].kind==RoyaleLobbyEventKind::CountdownCancelled);
    e=*stepRoyaleLobby(d,s,4,106);CHECK(e[0].kind==RoyaleLobbyEventKind::CountdownStarted&&royaleLobbyTicksLeft(d,s,110)==6);
    e=*stepRoyaleLobby(d,s,3,116);CHECK(e.size()==1&&e[0].kind==RoyaleLobbyEventKind::Started&&e[0].botFill==1);
    CHECK(!stepRoyaleLobby(d,s,5,117));roundtrip(s);
}
std::string read(const char* p){std::ifstream f(p);std::stringstream ss;ss<<f.rdbuf();return ss.str();}
void data(const char* path){
    auto j=Json::parse(read(path));auto r=readRoyale(j);
    CHECK(r.zone.phases.size()==7&&r.flight&&r.flight->exitCarry==Fixed::fromRatio(1,4)&&r.bombardment&&r.supplyDrops&&r.lobby&&r.zone.startTick==3600);
    CHECK(r.zone.initial.center==Vec2{}&&r.zone.initial.radius==5700_fx&&royaleDataHash(j)==royaleDataHash(Json::parse(read(path))));
    auto fails=[&](const std::string& text,const std::string& needle){
        try{readRoyale(Json::parse(text));}catch(const JsonError& e){if(std::string(e.what()).find(needle)!=std::string::npos)return true;std::cerr<<e.what()<<"\n";}
        return false;
    };
    std::string head=R"({"bounds":{"min":[-10,-10],"max":[10,10]},)";
    CHECK(fails(head+R"("zone":{"phases":[{"radius":5,"bogus":1}]}})","royale.zone.phases[0]: unknown key 'bogus'"));
    CHECK(fails(head+R"("zone":{"phases":[{"radius":5},{"radius":6}]}})","royale.zone.phases[1].radius: must not exceed"));
    CHECK(fails(head+R"("zone":{"phases":[]}})","royale.zone.phases: expected"));
    CHECK(fails(R"({"zone":{"phases":[{"radius":1}]}})","royale.bounds: required"));
    CHECK(fails(head+R"("zone":{"phases":[{"radius":5}]},"drop":{"fallSpeed":3,"diveFallSpeed":2}})","diveFallSpeed: must be at least"));
    CHECK(fails(head+R"("zone":{"phases":[{"radius":5,"wait":1.5}]}})","wait: expected integer"));
    // the example runs a whole zone schedule
    auto s=beginSafeZone(42);uint32_t closed=0;for(uint32_t t=0;t<60000&&!closed;++t)for(auto e:val(stepSafeZone(r.zone,s,t)))if(e.kind==SafeZoneEventKind::Closed)closed=t;
    CHECK(closed==3600+5400*2+3600*2+2700*2+1800*2+1200*2+900*2+600+1200);
}
// Golden checksums: the seeded policies must give the same bytes on every compiler / platform.
void golden(){
    auto d=safeZoneDef();auto s=beginSafeZone(2026);stepSafeZone(d,s,500);
    InsertionFlightDef fd;auto f=*planInsertionFlight(fd,{v(-1000,-1000),v(1000,1000)},2026,0);
    std::cout<<"golden zone "<<royaleHash(s)<<" flight "<<royaleHash(f)<<"\n";
    CHECK(royaleHash(s)==13358533664497283348ull);CHECK(royaleHash(f)==5352959581620080163ull);
}
}
int main(int argc,char** argv){
    try{zone();bombardment();supply();insertion();standings();lobby();data((std::string(argc>1?argv[1]:".")+"/examples/libraries/mechanics/royale/royale.json").c_str());golden();}
    catch(const std::exception& e){std::cerr<<"royale tests failed: "<<e.what()<<"\n";return 1;}
    std::cout<<"royale tests passed\n";return 0;
}
