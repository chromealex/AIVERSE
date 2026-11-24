// Later batches of policy_mechanics_tests.cpp (included after CHECK/roundtrip).
#include <aiverselib/mechanics/ItemAffixes.h>
#include <aiverselib/mechanics/QuestJournal.h>
#include <aiverselib/mechanics/WoundsDisease.h>
#include <aiverselib/mechanics/HerdsTaming.h>
#include <aiverselib/mechanics/ColonistRelations.h>
#include <aiverselib/mechanics/UtilityNetworks.h>
#include <aiverselib/mechanics/Railways.h>
#include "policy_mechanics_batch3.inc"

inline void affixes(){
    AffixData d;
    d.affixes[1]={1,1,10,true,1,100,1,~0u,5,10};d.affixes[2]={2,1,10,true,2,50,10,~0u,11,20};   // same group: never together
    d.affixes[3]={3,0,11,true,1,100,1,~0u,1,3};d.affixes[4]={4,0,12,false,1,100,1,~0u,2,4};d.affixes[5]={5,0,13,false,1,100,1,~0u,1,1};
    d.bases[1]={1,1,2,2,{1,2},{{14,7}}};d.gems[1]={1,15,5,1};d.gems[2]={2,15,9,2};CHECK(valid(d));
    auto a=rollItem(d,1,1,20,2,2,42),b=rollItem(d,1,1,20,2,2,42);CHECK(a&&b&&*a==*b&&valid(d,*a));
    CHECK(a->affixes.size()==4);int groups=0;for(auto& x:a->affixes)groups+=d.affixes[x.affix].group==1;CHECK(groups==1);
    auto low=rollItem(d,2,1,1,2,0,42);for(auto& x:low->affixes)CHECK(x.affix!=2);           // item level gate
    CHECK(!addAffix(d,*a,true,7));                                                         // prefixes full
    auto before=*a;CHECK(rerollValues(d,*a,99));CHECK(a->affixes.size()==before.affixes.size());
    CHECK(socketGem(d,*a,0,1));CHECK(!socketGem(d,*a,0,2));CHECK(!socketGem(d,*a,1,1));CHECK(socketGem(d,*a,1,2));
    auto stats=itemStats(d,*a);CHECK(stats[14]==7&&stats[15]==14);
    CHECK(unsocketGem(*a,1)==2);roundtrip(*a);
    auto suffix=std::find_if(a->affixes.begin(),a->affixes.end(),[&](auto& x){return !d.affixes[x.affix].prefix;})->affix;
    CHECK(rerollAffix(d,*a,suffix,5));CHECK(valid(d,*a));
    auto d2=d;d2.version=2;d2.affixes.erase(3);d2.affixes[6]={6,0,11,true,1,100,1,~0u,2,6};
    ItemMigration m;m.fromVersion=1;m.renamed[3]=6;m.rescale[6]={2,1};
    ItemInstance old{9,1,20,1,0,{{3,2},{4,3}},{0,0}};auto mig=migrateItem(d2,old,m);CHECK(mig&&mig->version==2&&mig->affixes[0].affix==4&&mig->affixes[1].affix==6&&mig->affixes[1].value==4);
    ReactionData rd;rd.elements[1]={1,300,true};rd.elements[2]={2,300,true};rd.elements[3]={3,300,false};rd.reactions.push_back({1,2,1,2000,true});
    AuraState au;ElementHit fire{5,1},water{5,2};auto e=applyElements(rd,au,1,std::span(&fire,1));CHECK(e&&e->empty()&&au.auras[5].contains(1));
    e=applyElements(rd,au,2,std::span(&water,1));CHECK(e->size()==1&&e->front().multiplierMilli==2000&&!au.auras.contains(5));
    applyElements(rd,au,3,std::span(&fire,1));CHECK(applyElements(rd,au,400,{})&&au.auras.empty());roundtrip(au);
}
inline void quests(){
    QuestDefs defs;
    defs[1]={1,1,{{10,{{1,100,3}},{},20},{20,{{2,200,1}},{{1,30},{2,0}}},{30,{{3,300,1},{4,400,1,true}},{},0,600}},{},false,true,0};
    defs[2]={2,1,{{10,{{1,100,1}},{},0}},{1}};CHECK(valid(defs));
    QuestLog log;QuestCommand start{QuestCommandKind::Start,1},start2{QuestCommandKind::Start,2};
    auto j=stepQuests(defs,log,1,std::array{start2,start},{});CHECK(j&&log.quests.size()==1&&log.tracked==1);   // quest 2 requires quest 1
    QuestEvent kill{100,2};j=stepQuests(defs,log,2,{},std::span(&kill,1));CHECK(log.quests[1].counters[1]==2);
    j=stepQuests(defs,log,3,{},std::span(&kill,1));CHECK(log.quests[1].stage==20);
    QuestEvent talk{200,1};stepQuests(defs,log,4,{},std::span(&talk,1));CHECK(log.quests[1].stage==20);                // waits for a choice
    QuestCommand choose{QuestCommandKind::Choose,1,1};stepQuests(defs,log,5,std::span(&choose,1),{});CHECK(log.quests[1].stage==30);
    QuestEvent item{300,1};stepQuests(defs,log,6,{},std::span(&item,1));CHECK(log.quests[1].status==QuestStatus::Completed);
    stepQuests(defs,log,7,std::span(&start2,1),{});CHECK(log.quests[2].status==QuestStatus::Active);
    bool done=false;for(auto& e:log.journal)done|=e.kind==JournalKind::Completed&&e.quest==1;CHECK(done);roundtrip(log);
    QuestLog timed;stepQuests(defs,timed,1,std::span(&start,1),{});QuestEvent k3{100,3};stepQuests(defs,timed,2,{},std::span(&k3,1));
    stepQuests(defs,timed,3,{},std::span(&talk,1));stepQuests(defs,timed,4,std::span(&choose,1),{});
    stepQuests(defs,timed,700,{},{});CHECK(timed.quests[1].status==QuestStatus::Failed);
    CHECK(!stepQuests(defs,timed,700,{},{}));
    auto v2=defs;v2[1].version=2;v2[1].stages.erase(v2[1].stages.begin()+2);v2[1].stages[1].branches={{1,0},{2,0}};v2[1].fallbackStage=10;CHECK(valid(v2));
    QuestLog mid;stepQuests(defs,mid,1,std::span(&start,1),{});stepQuests(defs,mid,2,{},std::span(&k3,1));stepQuests(defs,mid,3,{},std::span(&talk,1));stepQuests(defs,mid,4,std::span(&choose,1),{});
    CHECK(mid.quests[1].stage==30);CHECK(migrateQuests(v2,mid,5));CHECK(mid.quests[1].stage==10&&mid.quests[1].version==2);
}
inline void wounds(){
    HealthData d;d.wounds[1]={1,500,2,10,TreatBandage,0,0,TreatDisinfect,0};d.wounds[2]={2,0,1,10,TreatBandage,0,1000000,TreatDisinfect,1};
    d.diseases[1]={1,5,20,100,1000000,0,300,200,TreatSurgery};CHECK(valid(d));HealthState s;
    NewWound cut{1,1,3,100};auto r=stepHealth(d,s,1,std::span(&cut,1),{},{});CHECK(r&&s.bodies[1].wounds.size()==1&&r->bodies[0].bleeding);
    for(uint32_t t=2;t<12;++t)stepHealth(d,s,t,{},{},{});CHECK(s.bodies[1].bloodMilli<1000000);
    Treatment band{1,0,TreatBandage,0};stepHealth(d,s,12,{},std::span(&band,1),{});CHECK(!s.bodies[1].wounds.begin()->second.bleeding);
    auto blood=s.bodies[1].bloodMilli;for(uint32_t t=13;t<1100;++t)stepHealth(d,s,t,{},{},{});CHECK(s.bodies[1].bloodMilli==blood&&s.bodies[1].wounds.empty());   // healed
    // Infection -> disease -> contagion to a contact -> recovery with immunity.
    NewWound bite{2,2,1,10};stepHealth(d,s,1200,std::span(&bite,1),{},{});stepHealth(d,s,1201,{},{},{});
    CHECK(s.bodies[2].diseases.contains(1));NewWound none{3,1,1,1};stepHealth(d,s,1202,std::span(&none,1),{},{});
    for(uint32_t t=1203;t<1210;++t)stepHealth(d,s,t,{},{},{});CHECK(s.bodies[2].diseases[1].phase==DiseasePhase::Sick);
    Contact c{2,3};stepHealth(d,s,1210,{},{},std::span(&c,1));CHECK(s.bodies[3].diseases.contains(1));
    for(uint32_t t=1211;t<1240;++t)stepHealth(d,s,t,{},{},{});CHECK(!s.bodies[2].diseases.contains(1)&&s.bodies[2].immuneUntil.contains(1));
    Treatment cure{3,0,TreatSurgery,1};stepHealth(d,s,1240,{},std::span(&cure,1),{});CHECK(!s.bodies[3].diseases.contains(1));
    roundtrip(s);auto copy=s;stepHealth(d,s,1300,{},{},{});stepHealth(d,copy,1300,{},{},{});CHECK(policyHash(s)==policyHash(copy));
    NewWound fatal{4,1,1,1000};HealthState x;stepHealth(d,x,1,std::span(&fatal,1),{},{});bool died=false;
    for(uint32_t t=2;t<300&&!died;++t){auto rr=stepHealth(d,x,t,{},{},{});for(auto& e:rr->events)died|=e.kind==HealthEventKind::Died;}CHECK(died&&x.bodies[4].dead);
}
inline void herds(){
    HerdConfig c;HerdState s;CHECK(addHerd(s,{1,7,0,{},{},{3,1,2}}));CHECK(s.herds[1].leader==1);
    std::map<int64_t,TamingDef> tame{{7,{7,50,400,100,300,100,1000,10}}};
    std::vector<MemberFact> f{{1,{0_fx,{},0_fx}},{2,{20_fx,{},0_fx}},{3,{1_fx,{},{}}}};
    auto r=stepHerds(c,tame,s,1,f,{});CHECK(r&&r->steer.size()==3);
    auto far=std::find_if(r->steer.begin(),r->steer.end(),[](auto& x){return x.animal==2;});CHECK(far->direction.x<Fixed{});   // straggler returns
    f[2].alarmed=true;f[2].threat={5_fx,{},{}};r=stepHerds(c,tame,s,2,f,{});for(auto& st:r->steer)CHECK(st.fleeing);       // panic spreads
    f[2].alarmed=false;f[0].alive=false;r=stepHerds(c,tame,s,3,f,{});CHECK(s.herds[1].leader==2);
    TameCommand feed{TameCommandKind::Feed,3,9,50};stepHerds(c,tame,s,4,f,std::span(&feed,1));CHECK(s.bonds[3][9].trust==400);
    stepHerds(c,tame,s,5,f,std::span(&feed,1));CHECK(s.bonds[3][9].trust==400);                                           // cooldown
    stepHerds(c,tame,s,20,f,std::span(&feed,1));stepHerds(c,tame,s,40,f,std::span(&feed,1));CHECK(tamedOwner(s,3)==9);
    roundtrip(s);std::array<int64_t,1> off{1};auto before=s.herds[1].home;
    for(uint32_t t=72000;t<=72000*3;t+=72000)stepHerds(c,tame,s,t,{},{},off);CHECK(s.herds[1].offscreen&&!(s.herds[1].home==before));
}
inline void social(){
    MemoryDefs m{{1,{1,30,6000,3,true}},{2,{2,-30,6000,3,true}},{3,{3,-10,100,1,false}}};RelationConfig c;c.formTicks=10;c.ownedItemUsedMemory=2;SocialState s;
    std::vector<SocialCommand> add{{SocialCommandKind::AddColonist,1},{SocialCommandKind::AddColonist,2},{SocialCommandKind::AddColonist,3},
        {SocialCommandKind::Family,1,2,0,RelSibling},{SocialCommandKind::Remember,1,2,1},{SocialCommandKind::Remember,2,1,1},{SocialCommandKind::Remember,3,0,3}};
    CHECK(stepSocial(c,m,s,1,add));CHECK(opinion(c,m,s,1,2)==50&&s.relations[2][1].bits&RelSibling);CHECK(moodOf(c,m,s,3)==-10);
    std::vector<SocialEvent> all;for(uint32_t t=2;t<=12;++t){auto e=stepSocial(c,m,s,t,{});all.insert(all.end(),e->begin(),e->end());}
    CHECK(std::any_of(all.begin(),all.end(),[](auto& e){return e.kind==SocialEventKind::BecameFriends;}));CHECK(s.relations[1][2].bits&RelFriend);
    SocialCommand propose{SocialCommandKind::Propose,1,2};auto e=stepSocial(c,m,s,13,std::span(&propose,1));CHECK(e->back().kind==SocialEventKind::ProposalRejected);
    std::vector<SocialCommand> more{{SocialCommandKind::Remember,1,2,1},{SocialCommandKind::Remember,2,1,1},{SocialCommandKind::Propose,1,2}};
    e=stepSocial(c,m,s,14,more);CHECK(e->back().kind==SocialEventKind::Partnered&&(s.relations[2][1].bits&RelPartner));
    std::vector<SocialCommand> bed{{SocialCommandKind::Claim,1,0,77},{SocialCommandKind::UseItem,3,0,77}};e=stepSocial(c,m,s,15,bed);
    CHECK(e->back().kind==SocialEventKind::ItemUsedByOther&&opinion(c,m,s,1,3)==-30);
    std::vector<SocialCommand> feud{{SocialCommandKind::Remember,1,3,2},{SocialCommandKind::Remember,3,1,2},{SocialCommandKind::Remember,3,1,2}};stepSocial(c,m,s,16,feud);
    for(uint32_t t=17;t<=30;++t)stepSocial(c,m,s,t,{});CHECK(s.relations[1][3].bits&RelRival);
    roundtrip(s);SocialCommand rm{SocialCommandKind::RemoveColonist,3};stepSocial(c,m,s,31,std::span(&rm,1));CHECK(!s.relations[1].contains(3)&&valid(s));
}
inline void utilities(){
    FluidNetwork n;n.nodes[1]={1,FluidNodeKind::Source,10000,500};n.nodes[2]={2,FluidNodeKind::Junction};n.nodes[3]={3,FluidNodeKind::Sink,0,0,200};
    n.nodes[4]={4,FluidNodeKind::Tank,8000,0,0,1000};n.pipes[1]={1,1,2,1000,1000};n.pipes[2]={2,2,3,1000,200};n.pipes[3]={3,2,4,500,300};CHECK(valid(n));
    FluidState s;FluidReport r;for(uint32_t t=1;t<=20;++t)r=*stepFluid(n,s,t);
    CHECK(r.delivered[3]==200&&r.shortfall[3]==0&&s.tankLevel[4]>0&&s.tankLevel[4]<=1000);
    s.valves[1]=false;for(uint32_t t=21;t<=40;++t)r=*stepFluid(n,s,t);CHECK(r.flow[1]==0);           // source closed: tank feeds the sink
    CHECK(r.flow[3]<0||s.tankLevel.empty());roundtrip(s);
    HeatNetwork h;h.rooms[1]={1,1000};h.rooms[2]={2,1000};h.links[1]={1,1,2,50,false,200};h.links[2]={2,1,0,10};h.devices[1]={1,1,20000,22000};CHECK(valid(h));
    HeatState hs;hs.temperature[1]=0;hs.temperature[2]=0;HeatReport hr;for(uint32_t t=1;t<=600;++t)hr=*stepHeat(h,hs,t,-10000);
    CHECK(hs.temperature[1]>hs.temperature[2]&&hs.temperature[1]<=22000+100);hs.doorOpen[1]=true;auto t2=hs.temperature[2];for(uint32_t t=601;t<=700;++t)stepHeat(h,hs,t,-10000);
    CHECK(hs.temperature[2]>t2);roundtrip(hs);
    RailNetwork rn;rn.segments[1]={1,1,0,20_fx};rn.segments[2]={2,2,5,20_fx};rn.segments[3]={3,3,0,20_fx};rn.segments[4]={4,4,0,20_fx};
    std::map<int64_t,TrainDef> tr{{1,{1,{1,2,3,4},10_fx,2_fx,4_fx,60}},{2,{2,{3,4,1,2},10_fx,2_fx,4_fx,60}}};
    auto rs=placeTrains(rn,tr);CHECK(rs);bool arrived=false,waited=false;
    for(uint32_t t=1;t<=2000;++t){auto x=stepRail(rn,tr,*rs,t,dt);CHECK(x);for(auto& e:x->events){arrived|=e.kind==RailEventKind::Arrived&&e.station==5;waited|=e.kind==RailEventKind::WaitingAtSignal;}
        std::set<int64_t> blocks;for(auto& [id,p]:x->positions)blocks.insert(rn.segments[p.first].block);CHECK(blocks.size()==x->positions.size());}
    CHECK(arrived);roundtrip(*rs);(void)waited;
    std::map<int64_t,TrainDef> clash{{1,{1,{1}}},{2,{2,{1}}}};CHECK(!placeTrains(rn,clash));
}
inline void policyBatch2(){affixes();quests();wounds();herds();social();utilities();policyBatch3();}
