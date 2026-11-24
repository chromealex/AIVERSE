// Batch 4 of policy_mechanics_tests.cpp: watercraft, secret-state, lip-sync, touch-accessibility.
#include <aiverselib/mechanics/Watercraft.h>
#include <aiverselib/mechanics/SecretState.h>
#include <aiverselib/animation/LipSync.h>
#include <aiverselib/gameplay/TouchAccessibility.h>
#include <cmath>

inline void watercraft(){
    std::vector<HullBlock> blocks;int64_t id=1;for(int x=-1;x<=1;++x)for(int z=-2;z<=2;++z)blocks.push_back({id++,{Fixed::fromInt(x),{},Fixed::fromInt(z)},100_fx,Fixed::fromRatio(1,4)});
    auto hull=assembleHull(blocks);CHECK(hull&&hull->mass==1500_fx&&hull->points.size()==15);
    BoatConfig c;BoatState s;s.position={{},2_fx,{}};auto calm=[](Vec3){return WaterSample{};};
    for(uint32_t t=1;t<=1200;++t)CHECK(stepBoat(c,*hull,s,{},t,dt,calm));
    CHECK(fxAbs(s.velocity.y)<Fixed::fromRatio(1,2)&&s.position.y<1_fx&&s.position.y> -1_fx&&!s.sunk);     // floats near the surface
    BoatInput go{1_fx,Fixed::fromRatio(1,2)};auto yaw0=s.yaw;for(uint32_t t=1201;t<=1800;++t)stepBoat(c,*hull,s,go,t,dt,calm);
    CHECK(s.velocity.length()>1_fx&&s.yaw!=yaw0);roundtrip(s);
    auto heavy=blocks;for(auto& b:heavy)b.mass=1000_fx;auto sinker=assembleHull(heavy);BoatState d;d.position={};bool sunk=false;
    for(uint32_t t=1;t<=600&&!sunk;++t)sunk=stepBoat(c,*sinker,d,{},t,dt,calm)->sunk;CHECK(sunk);
    AircraftConfig ac;AircraftState a;a.position={{},500_fx,{}};a.velocity={{},{},50_fx};AircraftInput cruise{Fixed::fromRatio(7,10),Fixed::fromRatio(1,20)};
    for(uint32_t t=1;t<=120;++t)CHECK(stepAircraft(ac,a,cruise,t,dt));auto r=stepAircraft(ac,a,cruise,121,dt);CHECK(r&&r->lift>Fixed{}&&!r->stalled&&r->airspeed>20_fx);
    AircraftInput pullUp{{},1_fx};bool stall=false;for(uint32_t t=122;t<=900&&!stall;++t)stall=stepAircraft(ac,a,pullUp,t,dt)->stalled;CHECK(stall);roundtrip(a);
}
inline void secrets(){
    SecretState s;s.secret=0x1234;CHECK(addZone(s,{1,1,Visibility::Owner}));CHECK(addZone(s,{2,0,Visibility::Nobody,{},true}));CHECK(addZone(s,{3,0,Visibility::Everyone}));
    for(int64_t c=1;c<=5;++c)CHECK(placeObject(s,c,2));CHECK(shuffleZone(s,2,99));
    std::vector<RevealEvent> rev;std::array<int32_t,2> seats{1,2};auto drawn=s.zoneObjects[2].back();CHECK(placeObject(s,drawn,1,&rev,seats));
    CHECK(rev.size()==1&&rev[0].seat==1&&rev[0].object==drawn);
    auto v1=projectForSeat(s,1),v2=projectForSeat(s,2);
    CHECK(v1.zones[0].objects[0].object==drawn&&v2.zones[0].objects[0].object==0&&v2.zones[0].objects[0].handle!=v1.zones[0].objects[0].handle);
    for(auto& o:v2.zones[1].objects)CHECK(o.object==0);                                   // the deck is hidden from both
    auto h=v1.zones[0].objects[0].handle;CHECK(resolveHandle(s,1,1,h)==drawn&&!resolveHandle(s,2,1,h));
    auto before=projectForSeat(s,2).zones[1].objects;CHECK(shuffleZone(s,2,100));auto after=projectForSeat(s,2).zones[1].objects;
    for(auto& a:after)for(auto& b:before)CHECK(a.handle!=b.handle);                         // re-keyed: cards cannot be followed
    CHECK(placeObject(s,drawn,3,&rev,seats));CHECK(rev.back().seat==2&&rev.back().object==drawn);roundtrip(s);CHECK(snapshotForSeat(s,2).version==s.version);
    CommitRound round{{1,2},{},{},100,-1};CHECK(submitCommit(round,1,fnvCommit(7,11))==CommitResult::Accepted);
    CHECK(submitReveal(round,1,7,11)==CommitResult::Rejected);                              // not everyone committed yet
    CHECK(submitCommit(round,2,fnvCommit(3,5))==CommitResult::Accepted);CHECK(submitReveal(round,2,4,5)==CommitResult::BadReveal);
    CHECK(submitReveal(round,1,7,11)==CommitResult::Accepted);CHECK(!resolveRound(round,50));auto res=resolveRound(round,100);CHECK(res&&res->at(1)==7&&res->at(2)==-1);roundtrip(round);
}
inline void lipSync(){
    using namespace aiverse::lib::animation;const int rate=16000;std::vector<float> pcm;
    auto tone=[&](float seconds,float freq,float amp){for(int i=0;i<int(seconds*rate);++i)pcm.push_back(amp*std::sin(6.2831853f*freq*float(i)/rate));};
    auto noise=[&](float seconds,float amp){uint32_t x=1;for(int i=0;i<int(seconds*rate);++i){x=x*1664525u+1013904223u;pcm.push_back(amp*(float(x>>8)/float(1<<24)*2-1));}};
    tone(0.2f,0,0);tone(0.3f,180,0.5f);tone(0.04f,0,0);tone(0.3f,150,0.5f);noise(0.2f,0.3f);tone(0.2f,0,0);
    auto keys=analyseSpeech(pcm,rate);CHECK(keys.size()>4);bool open=false,closed=false,fric=false;
    for(auto& k:keys){open|=(k.viseme==LipViseme::Aa||k.viseme==LipViseme::Oh)&&k.weight>0.2f;closed|=k.viseme==LipViseme::Mbp;fric|=k.viseme==LipViseme::Fv;}
    CHECK(open&&closed&&fric);CHECK(keys.back().viseme==LipViseme::Silence);
    auto w=sampleVisemes(keys,0.05f);CHECK(w[0]>0.9f);
    auto txt=visemesFromText("Hello, my friend",1.5f);CHECK(txt.size()>5&&txt.back().time==1.5f);bool mbp=false;for(auto& k:txt)mbp|=k.viseme==LipViseme::Mbp;CHECK(mbp);
    float sum=0;for(float x:sampleVisemes(txt,0.7f))sum+=x;CHECK(std::abs(sum-1)<1e-3f);
}
inline void touchAccess(){
    namespace t=aiverse::lib::gameplay::touch;namespace a=aiverse::lib::gameplay::accessibility;
    t::Layout l;l.sticks.push_back({"move",100,500,80,0.15f,true,0,300,400,700});l.buttons.push_back({"jump",900,600,50,false});l.buttons.push_back({"crouch",800,600,40,true});
    t::TouchState s;std::vector<t::Touch> f1{{1,150,520},{2,900,600}};auto fr=t::updateTouch(l,s,f1,0.016f);
    CHECK(fr.sticks[0].active&&std::abs(fr.sticks[0].x)<0.01f&&fr.buttons[0].pressed&&fr.buttons[0].held);
    std::vector<t::Touch> f2{{1,230,520},{2,900,600}};fr=t::updateTouch(l,s,f2,0.016f);CHECK(fr.sticks[0].x>0.9f&&!fr.buttons[0].pressed&&fr.buttons[0].held);
    std::vector<t::Touch> f3{{1,230,520,false},{2,900,600,false}};fr=t::updateTouch(l,s,f3,0.016f);CHECK(fr.buttons[0].released);
    fr=t::updateTouch(l,s,{},0.016f);CHECK(!fr.sticks[0].active);
    std::vector<t::Touch> c1{{3,800,600}},c2{{3,800,600,false}};t::updateTouch(l,s,c1,0.016f);fr=t::updateTouch(l,s,c2,0.016f);fr=t::updateTouch(l,s,{},0.016f);CHECK(fr.buttons[1].held);   // toggle on
    std::vector<t::Touch> tap1{{4,600,100}},tap2{{4,600,100,false}};t::updateTouch(l,s,tap1,0.016f);fr=t::updateTouch(l,s,tap2,0.05f);CHECK(!fr.gestures.empty()&&fr.gestures[0].kind==t::GestureKind::Tap);
    std::vector<t::Touch> tap3{{5,602,101}},tap4{{5,602,101,false}};t::updateTouch(l,s,tap3,0.05f);fr=t::updateTouch(l,s,tap4,0.05f);CHECK(fr.gestures[0].kind==t::GestureKind::DoubleTap);
    std::vector<t::Touch> sw1{{6,500,100}},sw2{{6,700,110}},sw3{{6,700,110,false}};t::updateTouch(l,s,sw1,0.5f);t::updateTouch(l,s,sw2,0.05f);fr=t::updateTouch(l,s,sw3,0.05f);
    CHECK(fr.gestures[0].kind==t::GestureKind::Swipe&&fr.gestures[0].dx>150);
    std::vector<t::Touch> p1{{7,500,100},{8,600,100}},p2{{7,450,100},{8,650,100}};t::updateTouch(l,s,p1,0.05f);fr=t::updateTouch(l,s,p2,0.05f);
    bool pinch=false;for(auto& g:fr.gestures)pinch|=g.kind==t::GestureKind::Pinch&&g.scale>1.9f;CHECK(pinch);
    a::Settings st;CHECK(!a::validate(st));st.textScale=5;CHECK(a::validate(st)=="textScale");st.textScale=1;
    CHECK(a::rebind(st,"jump",0,"Space")&&a::rebind(st,"dash",0,"Space")&&a::rebind(st,"jump",1,"A"));auto cf=a::bindingConflicts(st);CHECK(cf.size()==1&&cf[0].first=="Space");
    st.reducedMotion=true;CHECK(a::shakeScale(st)==0&&a::flashScale(st)<=0.3f);
    auto red=a::compensate(a::ColorVision::Deuteranopia,{1,0,0});auto green=a::compensate(a::ColorVision::Deuteranopia,{0,1,0});
    CHECK(red[2]>0.3f&&green[2]==0&&red[1]>0);                                            // the red/green error moves into blue and green
    auto same=a::compensate(a::ColorVision::Normal,{0.2f,0.4f,0.6f});CHECK(same[0]==0.2f&&same[2]==0.6f);
}
inline void policyBatch4(){watercraft();secrets();lipSync();touchAccess();}
