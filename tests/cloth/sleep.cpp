#include "engine/cloth/TemporalLod.h"
#include <iostream>
#include <stdexcept>
#include <cstring>
#include <limits>
using namespace aiverse::cloth;
void check(bool b,const char* s){if(!b)throw std::runtime_error(s);}
int main(){try{
    Settings settings;settings.cells=8;auto grid=std::make_shared<Grid>(settings);
    Environment still;still.gravity={};still.wind={};still.floor=-10;
    std::vector<Cloth> cloths;for(int i=0;i<4;++i)cloths.emplace_back(grid,Vec{140,0,0});
    Batch batch(2,true);LodSettings ls;ls.stagger=false;TemporalLod lod(ls);
    auto step=[&]{lod.step(cloths,batch,{},still);};
    for(int t=0;t<120;++t)step();
    for(auto& c:cloths)check(c.sleeping(),"rest cloth never slept");
    check(batch.stats().simulatedObjects==0&&batch.stats().sleepingObjects==4,"sleep still submits CPU jobs");
    std::vector<Vec> snapshot(cloths[0].positions().begin(),cloths[0].positions().end());
    for(int t=0;t<180;++t)step();
    check(std::memcmp(snapshot.data(),cloths[0].positions().data(),snapshot.size()*sizeof(Vec))==0,"sleep drifts");
    cloths[0].wake();step();
    check(!cloths[0].sleeping()&&cloths[1].sleeping()&&batch.stats().simulatedObjects==1,"one touch woke all clones");
    check(lod.timeSteps()[0]==1.f/60,"wake delayed or accumulated sleeping time");
    for(int t=0;t<60;++t)step();check(cloths[0].sleeping(),"touched cloth did not return to sleep");
    lod.requestUpdate(1);step();check(!cloths[1].sleeping()&&cloths[0].sleeping(),"LOD request did not wake one clone");
    for(int t=0;t<60;++t)step();
    still.wind.z=2;step();for(auto& c:cloths)check(!c.sleeping(),"wind change failed to wake");
    check(lod.timeSteps()[0]==1.f/60,"wind wake waited for far LOD");
    for(int t=0;t<40;++t){still.wind.z=float(t%2)*3;step();for(auto& c:cloths)check(!c.sleeping(),"changing forces slept");}
    still.wind={};for(auto& c:cloths)c.reset();for(int t=0;t<100;++t)step();
    Capsule capsule{{1,-1,1},{1,-1,1},.1f};still.capsules={&capsule,1};step();check(!cloths[0].sleeping(),"new collider ignored");
    for(int t=0;t<100;++t)step();check(cloths[0].sleeping(),"constant distant collider prevents sleep");
    capsule.radius=.2f;step();check(!cloths[0].sleeping(),"in-place collider mutation ignored");
    for(int t=0;t<100;++t)step();still.capsules={};step();check(!cloths[0].sleeping(),"collider removal ignored");
    for(int t=0;t<100;++t)step();cloths[0].windScale=2;step();check(!cloths[0].sleeping()&&cloths[1].sleeping(),"wind multiplier wake wrong scope");
    for(int t=0;t<100;++t)step();cloths[0].active=false;step();cloths[0].active=true;step();check(!cloths[0].sleeping()&&lod.timeSteps()[0]==1.f/60,"reactivation catch-up");
    for(int t=0;t<100;++t)step();cloths[0].movePins({.1f,0,0});step();check(!cloths[0].sleeping()&&cloths[0].positions()[0].x==.1f,"pin wake failed");
    for(auto& c:cloths)c.reset();for(int t=0;t<100;++t)step();
    cloths[0].movePins({});check(cloths[0].sleeping(),"unchanged pins woke cloth");
    still.floor=-9;step();check(!cloths[0].sleeping(),"floor change ignored");
    // Policies must split groups, including disabled sleeping and different quiet durations.
    std::vector<Cloth> policies;for(int i=0;i<3;++i)policies.emplace_back(grid);
    SleepSettings slow;slow.quietSeconds=2;check(policies[1].setSleepSettings(slow),"valid policy rejected");
    SleepSettings off;off.enabled=false;policies[2].setSleepSettings(off);
    for(int t=0;t<60;++t)batch.step(policies,1.f/60,still);
    check(policies[0].sleeping()&&!policies[1].sleeping()&&!policies[2].sleeping(),"different sleep policies shared state");
    for(int t=0;t<120;++t)batch.step(policies,1.f/60,still);
    check(policies[1].sleeping()&&!policies[2].sleeping(),"sleep disabled or duration wrong");
    auto invalid=slow;invalid.speedThreshold=std::numeric_limits<float>::quiet_NaN();check(!policies[0].setSleepSettings(invalid),"NaN threshold accepted");
    invalid=slow;invalid.quietSeconds=0;check(!policies[0].setSleepSettings(invalid),"zero quiet duration accepted");
    // Non-zero constant gravity can settle; no forces changing is insufficient while moving.
    Cloth falling(grid);Environment gravity;gravity.wind={};gravity.floor=-10;
    falling.step(1.f/60,gravity);check(!falling.sleeping(),"moving cloth slept immediately");
    for(int t=0;t<3600&&!falling.sleeping();++t)falling.step(1.f/60,gravity);
    check(falling.sleeping()&&falling.healthy(),"constant gravity never settled");
    gravity.gravity.y=-8;falling.step(1.f/60,gravity);check(!falling.sleeping(),"gravity change ignored");
    std::cout<<"cloth sleep: rest, sharing, policies, wake sources, constant gravity, LOD and no catch-up passed\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
