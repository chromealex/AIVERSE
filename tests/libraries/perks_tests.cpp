// match-progression: perks/modifier pipeline, xp + level curve + offers, xp pickups, summons, battle deck.
#include <aiverselib/genre/PerksData.h>
#include <aiverselib/genre/PerksConfigured.h>
#include <aiverselib/genre/PerksPersistence.h>
#include <iostream>
#include <stdexcept>
using namespace aiverse;using namespace aiverse::lib::genre;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string(__FILE__)+":"+std::to_string(__LINE__)+": " #x);}while(false)
namespace {
std::string root;
Json file(const std::string& name){return Json::parseFile(root+"/examples/libraries/genre/"+name);}
PerkDefinitions defs(){return readPerkDefinitions(file("perks.json"));}
BattleDeck deck(){return readBattleDeck(file("perks-deck.json"));}
template<class F> std::string jsonError(F f){try{f();}catch(const JsonError& e){return e.what();}return {};}
template<class State> void roundtrip(const State& s){auto bytes=save(s);CHECK(!bytes.empty());State copy;CHECK(load(bytes,copy));CHECK(save(copy)==bytes);CHECK(!load(bytes+"x",copy));CHECK(!load(bytes.substr(0,bytes.size()-1),copy));}
PerkState started(const PerkDefinitions& d,uint64_t seed=7){PerkState s;seedPerks(s,seed);CHECK(addPlayer(d,s,deck()));return s;}

void definitions(){
    auto d=defs();CHECK(d.perks.size()==11&&d.units.size()==5&&d.xpToNext.size()==10&&d.levels.size()==2);
    CHECK(d.perks.at(1).scope==PerkScope::Slot&&d.perks.at(1).effects[0].stat==perk_stat::OutgoingDamage&&d.perks.at(1).effects[0].op==ModOp::Multiply);
    CHECK(d.perks.at(7).upgrades==6&&d.perks.at(9).excludes==std::vector<int64_t>{3}&&d.perks.at(9).hook=="on_burning_hits");
    CHECK(killXp(d,2)==3&&killXp(d,77)==0&&d.levels.at(5)[2].perk==6);
    auto bad=[&](const char* text){return jsonError([&]{readPerkDefinitions(Json::parse(text));});};
    CHECK(bad(R"({"perks":[{"id":1,"wat":1}]})")=="perks.perks[]: unknown key 'wat'");
    CHECK(bad(R"({"perks":[{"id":1,"effects":[{"stat":"luck","value":1}]}]})")=="perks.perk 1.effects[0].stat: unknown stat 'luck'");
    CHECK(bad(R"({"perks":[{"id":1,"requires":[2]}]})")=="perks: perk 1: requires unknown perk 2");
    CHECK(bad(R"({"perks":[{"id":1,"upgrades":2},{"id":2,"upgrades":1}]})")=="perks: perk 1: upgrade chain loops");
    CHECK(bad(R"({"perks":[{"id":1,"scope":"slot"},{"id":2,"upgrades":1}]})")=="perks: perk 2: upgrades must name another perk of the same scope");
    CHECK(bad(R"({"offers":{"default":["recruit",{"type":"recruit","perk":1}]},"perks":[{"id":1}]})").find("recruit variant")!=std::string::npos);
    CHECK(bad(R"({"deck":{"choices":[{"minTech":5,"count":3},{"minTech":0,"count":2}]}})")=="perks: deck.choices: minTech must be strictly increasing");
    CHECK(bad(R"({"perks":[{"id":1,"effects":[{"stat":1,"op":"multiply","value":-5}]}]})")=="perks.perk 1.effects[0].value: must be in 0..1000000");
    auto points=readSummonPoints(file("perks.json"));CHECK(points.size()==2&&points[0].id==1&&points[0].radius==3_fx&&points[0].position.x==10_fx&&points[1].action==SummonAction::RestoreOrRecruit&&!points[1].singleUse);
}
void battleDeck(){
    auto d=defs();auto b=deck();auto known=[&](int64_t id){return d.perks.contains(id);};
    CHECK(b.player==1&&b.slots.size()==5&&!unlocked(b.slots[3])&&!unlocked(b.slots[4])&&unlocked(b.slots[1]));
    CHECK(battleDeckError(d.units,d.deck,b,known).empty()&&deckCost(d.units,b)==10);
    CHECK(deckChoiceCount(d.deck,b,3)==3);auto low=b;low.techLevel=4;CHECK(deckChoiceCount(d.deck,low,3)==2);low.choiceCount=1;CHECK(deckChoiceCount(d.deck,low,3)==1);
    auto e=b;std::swap(e.slots[0].unit,e.slots[1].unit);CHECK(battleDeckError(d.units,d.deck,e,known)=="deck.slots[0]: slot 0 must hold a hero unit");
    e=b;e.slots[2].unit=1;CHECK(battleDeckError(d.units,d.deck,e,known)=="deck.slots[2]: only slot 0 may hold a hero unit");
    e=b;e.slots.push_back({5,3});e.slots.push_back({6,3});CHECK(battleDeckError(d.units,d.deck,e,known).find("more than deck.maxSlots")!=std::string::npos);
    e=b;e.slots.push_back({5,4});CHECK(battleDeckError(d.units,d.deck,e,known)=="deck: cost 13 exceeds budget 12");
    auto rules=d.deck;rules.maxSameUnit=1;rules.budget=0;e=b;e.slots[2].unit=2;CHECK(battleDeckError(d.units,rules,e,known)=="deck.slots[2].unit: more than deck.maxSameUnit copies of unit 2");
    e=b;e.slots[1].perks={99};CHECK(battleDeckError(d.units,d.deck,e,known)=="deck.slots[1].perks: unknown perk 99");
    CHECK(jsonError([]{readBattleDeck(Json::parse(R"({"player":1,"slots":[{"unit":1,"opened":1}]})"));})=="deck.slots[0].opened: expected boolean");
    // Locked slots are not recruitable unless the payload disables enforcement (sandbox / AI catalogs).
    PerkConfig c;auto s=started(d);const auto& p=s.players.at(1);
    CHECK(canRecruit(d,c,p,1)&&canRecruit(d,c,p,2)&&!canRecruit(d,c,p,3)&&!canRecruit(d,c,p,4)&&!canRecruit(d,c,p,0));
    PerkState open;seedPerks(open,1);auto free=b;free.enforceUnlocks=false;CHECK(addPlayer(d,open,free));CHECK(canRecruit(d,c,open.players.at(1),3));
    CHECK(!addPlayer(d,open,free));PerkState unseeded;CHECK(!addPlayer(d,unseeded,b));
}
void statPipeline(){
    auto d=defs();auto s=started(d);auto& p=s.players.at(1);
    // Initial perks: unit 3 (slot 2) carries fire_on_move once, never offered.
    CHECK(p.mods.size()==1&&p.mods[0].perk==10&&p.mods[0].slot==2&&p.mods[0].initial&&p.recruits.at(0)==1);
    CHECK(grantPerk(d,s,1,1,0,5)&&grantPerk(d,s,1,1,0,6)&&grantPerk(d,s,1,6,-1,7));
    CHECK(!grantPerk(d,s,1,1,1,8)); // slot 1 not recruited yet
    std::vector<StatStep> steps;
    CHECK(evaluateStat(d,p,perk_stat::OutgoingDamage,1000,0,0,&steps)==1322&&steps.size()==2&&steps[0].after==1150&&steps[1].after==1322);
    CHECK(evaluateStat(d,p,perk_stat::OutgoingDamage,1000,-1)==1000);       // player-level query ignores slot packages
    CHECK(evaluateStat(d,p,perk_stat::IncomingDamage,1000,3)==900);         // player perks reach every slot
    CHECK(scaleAmount(250,evaluateStat(d,p,perk_stat::IncomingDamage,1000,0))==225&&scaleAmount(UINT32_MAX,2000)==UINT32_MAX&&scaleAmount(7,0)==0);
    // Upgrade chain: armor_2 replaces armor (requires level >= 3 for the choice).
    CHECK(!grantPerk(d,s,1,7,-1,9));p.level=2;CHECK(grantPerk(d,s,1,7,-1,9));CHECK(perkCount(p,6,PerkScope::Player,-1)==0&&evaluateStat(d,p,perk_stat::IncomingDamage,1000,0)==800);
    // Set runs before Add/Multiply, stats clamp, bearer filters.
    PerkDefinitions x=d;x.perks[20]={20,"set","",PerkScope::Player,0,{{perk_stat::IncomingDamage,ModOp::Set,50000,-1,0},{perk_stat::PickupRadius,ModOp::Add,100,-1,7}}};
    CHECK(valid(x));CHECK(grantPerk(x,s,1,20,-1,10));CHECK(evaluateStat(x,p,perk_stat::IncomingDamage,1000)==10000); // 50000*0.8 = 40000, clamped
    CHECK(evaluateStat(x,p,perk_stat::PickupRadius,2500,-1,7)==2600&&evaluateStat(x,p,perk_stat::PickupRadius,2500,-1,3)==2500&&evaluateStat(x,p,perk_stat::PickupRadius,2500)==2500);
    CHECK(evaluateStat(d,p,perk_stat::FireCount,1,0)==1);CHECK(grantPerk(d,s,1,4,0,11));CHECK(evaluateStat(d,p,perk_stat::FireCount,1,0)==2&&evaluateStat(d,p,perk_stat::FireInterval,300,0)==100);
    CHECK(!grantPerk(d,s,1,4,2,12)); // double_shot only for unit 1
    // Max health rescale keeps the fraction on growth, clamps on shrink.
    CHECK(rescaleHealth(50,100,120)==60&&rescaleHealth(100,100,120)==120&&rescaleHealth(90,100,80)==80&&rescaleHealth(0,100,120)==0&&rescaleHealth(70,100,100)==70);
    CHECK(clearPerks(s,1,0)==3&&clearPerks(s,1,-2)==2&&p.mods.size()==1&&p.mods[0].initial); // ids stay monotonic
    CHECK(p.nextOccurrence==8);
}
void experience(){
    auto d=defs();PerkConfig c;auto s=started(d);auto& p=s.players.at(1);
    CHECK(xpToNext(d,c,0)==5&&xpToNext(d,c,9)==50&&!xpToNext(d,c,10));c.xpGrowth=10;CHECK(xpToNext(d,c,10)==60&&xpToNext(d,c,12)==80);c.maxLevel=3;CHECK(!xpToNext(d,c,3));c=PerkConfig{};
    auto ev=grantXp(d,c,s,1,4);CHECK(ev&&ev->size()==1&&p.xp==4&&levelProgressMilli(d,c,p)==800);
    ev=grantXp(d,c,s,1,3);CHECK(ev->size()==2&&(*ev)[1].kind==PerkEventKind::LevelReady&&(*ev)[1].value==1&&availableLevels(d,c,p)==1&&levelProgressMilli(d,c,p)==1000);
    CHECK(grantXp(d,c,s,1,100)&&availableLevels(d,c,p)==6);
    CHECK(!grantXp(d,c,s,1,-1)&&!grantXp(d,c,s,2,1));
    // Pickup bonus (+10%) keeps the fractional remainder.
    CHECK(grantPerk(d,s,1,11,-1,1));p.xp=0;for(int i=0;i<10;++i)CHECK(grantXp(d,c,s,1,1,true));CHECK(p.xp==11&&p.xpRemainderMilli==0);
    CHECK(grantXp(d,c,s,1,1,true)&&p.xp==12&&p.xpRemainderMilli==100);
    // Curve fallback (systems::curve) when no table is given.
    PerkDefinitions cv;cv.xpCurve={{0,10},{10,110}};CHECK(valid(cv)&&xpToNext(cv,c,0)==10&&xpToNext(cv,c,5)==60&&xpToNext(cv,c,40)==110);
}
void offers(){
    auto d=defs();PerkConfig c;c.rerolls=2;auto s=started(d);
    CHECK(!openOffer(d,c,s,1));CHECK(grantXp(d,c,s,1,5));auto ev=openOffer(d,c,s,1);CHECK(ev&&(*ev)[0].kind==PerkEventKind::OfferOpened);
    auto& p=s.players.at(1);
    // Level 1 = recruit only, three positions, only two unlocked recruitable slots: one position is skipped.
    CHECK(p.offer.open&&p.offer.items.size()==2&&p.offer.revision==1);for(const auto& i:p.offer.items)CHECK(i.type==OfferVariant::Recruit&&(i.slot==1||i.slot==2));
    CHECK(p.offer.items[0].slot!=p.offer.items[1].slot);
    CHECK(!openOffer(d,c,s,1));CHECK(!selectOffer(d,c,s,1,0,0,1));CHECK(!selectOffer(d,c,s,1,1,5,1));
    auto rr=rerollOffer(d,c,s,1,1);CHECK(rr&&p.offer.revision==2&&p.offer.rerollsLeft==1&&!rerollOffer(d,c,s,1,1));
    auto chosen=p.offer.items[0];auto sel=selectOffer(d,c,s,1,2,0,3);
    CHECK(sel&&(*sel)[0].kind==PerkEventKind::Recruit&&(*sel)[0].slot==chosen.slot&&p.level==1&&p.xp==0&&!p.offer.open&&p.recruits.at(chosen.slot)==1);
    // Level 2 default variants [recruit, slotPerk, playerPerk]; preferNewRecruitAtLevel=2 offers the slot not yet recruited.
    CHECK(grantXp(d,c,s,1,8+12));auto o2=openOffer(d,c,s,1);CHECK(o2&&p.offer.items.size()==3);
    CHECK(p.offer.items[0].type==OfferVariant::Recruit&&p.offer.items[0].slot==(chosen.slot==1?2:1));
    CHECK(p.offer.items[1].type==OfferVariant::SlotPerk&&p.offer.items[2].type==OfferVariant::PlayerPerk);
    auto rev=p.offer.revision;sel=selectOffer(d,c,s,1,rev,2,4);CHECK(sel&&sel->size()==2&&(*sel)[0].kind==PerkEventKind::PerkSelected&&(*sel)[1].kind==PerkEventKind::OfferOpened);
    CHECK(p.level==2&&p.offer.open&&p.offer.revision==rev+1); // banked level opened the next offer
    // Hooks: a weight hook can veto candidates (here: only slot perks for slot 0).
    PerkHooks only0{[](int64_t,const OfferItem& i,int64_t w){return i.type==OfferVariant::SlotPerk&&i.slot!=0?0:w;}};
    CHECK(closeOffer(s,1)&&!p.offer.open);CHECK(openOffer(d,c,s,1,only0));for(const auto& i:p.offer.items)if(i.type==OfferVariant::SlotPerk)CHECK(i.slot==0);
    // Bot choice: recruit (2) beats perks unless low health picks a defensive perk.
    auto best=botOfferChoice(d,c,p,false);CHECK(best);
    // Level 5: explicit armor perk (6) is reserved first at its position.
    PerkState t=started(d,9);auto& q=t.players.at(1);q.level=4;q.xp=20;CHECK(openOffer(d,c,t,1));
    CHECK(q.offer.items.size()==3&&q.offer.items[2]==(OfferItem{OfferVariant::PlayerPerk,6,-1})&&q.offer.items[0].perk!=6&&q.offer.items[1].perk!=6);
    CHECK(botOfferChoice(d,c,q,true)==2u);
    // Exhausted pools close the offer without spending xp.
    PerkDefinitions empty=d;for(auto& [id,def]:empty.perks)def.offerable=false;empty.levels.clear();empty.defaultVariants={{OfferVariant::PlayerPerk,0}};
    PerkState u=started(empty);u.players.at(1).xp=5;auto e=openOffer(empty,c,u,1);CHECK(e&&(*e)[0].kind==PerkEventKind::OfferEmpty&&!u.players.at(1).offer.open&&u.players.at(1).xp==5);
    // maxSelections: armor (max 1) is never offered again once taken; excludes: burning_hits vs splash on the same slot.
    PerkState m=started(d,3);auto& mp=m.players.at(1);CHECK(grantPerk(d,m,1,6,-1,1)&&!canSelect(d,mp,6,-1));CHECK(grantPerk(d,m,1,3,0,1)&&!canSelect(d,mp,9,0));
    CHECK(canSelect(d,mp,3,0)&&grantPerk(d,m,1,3,0,2)&&grantPerk(d,m,1,3,0,3)&&!canSelect(d,mp,3,0));
}
uint64_t scripted(uint64_t seed){
    auto d=defs();PerkConfig c;c.rerolls=1;PerkState s;seedPerks(s,seed);CHECK(addPlayer(d,s,deck()));
    for(uint32_t tick=1;tick<=40;++tick){
        CHECK(grantXp(d,c,s,1,int64_t(tick%7),true));
        if(!s.players.at(1).offer.open)openOffer(d,c,s,1);
        auto& p=s.players.at(1);
        if(p.offer.open&&tick%3==0){if(tick%6==0&&p.offer.rerollsLeft)CHECK(rerollOffer(d,c,s,1,p.offer.revision));auto i=botOfferChoice(d,c,p,tick%4==0);if(i)CHECK(selectOffer(d,c,s,1,p.offer.revision,*i,tick));}
    }
    roundtrip(s);return progressionHash(s);
}
void determinism(){
    auto a=scripted(11),b=scripted(11),c=scripted(12);CHECK(a==b&&a!=c);
    auto d=defs();CHECK(perkDefinitionsHash(d)==perkDefinitionsHash(defs()));auto e=d;e.perks.at(1).effects[0].value=1160;CHECK(perkDefinitionsHash(d)!=perkDefinitionsHash(e));
    PerkState bad=started(d);bad.players.at(1).mods[0].id=99;CHECK(save(bad).empty());
}
void pickups(){
    PickupConfig c;c.landing=2;c.lifetime=100;c.maxOrbs=3;PickupState s;
    PickupInput in;in.spawns={{0,3,{5_fx,{},{}}},{2,4,{1_fx,{},{}}}};in.collectors={{1,{},{}}};
    auto r=stepPickups(c,s,1,in);CHECK(r&&r->collected.empty()&&s.orbs.size()==2); // landing
    CHECK(!stepPickups(c,s,1,{}));
    r=stepPickups(c,s,3,{{},{{1,{},{}},{2,{10_fx,{},{}}}}});CHECK(r&&r->collected.empty()); // orb 2 belongs to player 2, orb 1 out of reach
    r=stepPickups(c,s,4,{{},{{1,{1_fx,{},{}}},{2,{2_fx,{},{}}}}});CHECK(r&&r->collected.size()==1&&r->collected[0]==std::make_pair(int64_t(2),int64_t(4)));
    r=stepPickups(c,s,5,{{},{{1,{},{},true}}});CHECK(r&&r->collected[0]==std::make_pair(int64_t(1),int64_t(3))&&s.orbs.empty()); // magnet
    // Budget: the fourth spawn merges into the nearest orb of the same owner.
    in={};for(int i=0;i<4;++i)in.spawns.push_back({0,1,{Fixed::fromInt(50+i),{},{}}});
    r=stepPickups(c,s,6,in);CHECK(r&&s.orbs.size()==3&&s.orbs[2].value==2&&r->events.back().kind==PickupEventKind::Merged);
    r=stepPickups(c,s,106,{});CHECK(r&&s.orbs.empty()&&r->events.size()==3&&r->events[0].kind==PickupEventKind::Expired);
    // Homing attraction: grabbed at the pickup radius, flies with acceleration, collected at grabRadius.
    PickupConfig h;h.attractSpeed=Fixed::fromRatio(1,2);h.attractAccel=Fixed::fromRatio(1,4);h.maxSpeed=1_fx;PickupState hs;
    CHECK(stepPickups(h,hs,1,{{{0,7,{2_fx,{},{}}}},{}}));
    r=stepPickups(h,hs,2,{{},{{1,{},{}}}});CHECK(r&&r->events[0].kind==PickupEventKind::Attracted&&hs.orbs[0].position.x==Fixed::fromRatio(3,2));
    r=stepPickups(h,hs,3,{{},{{1,{},{}}}});CHECK(r&&hs.orbs[0].position.x==Fixed::fromRatio(3,4));
    r=stepPickups(h,hs,4,{{},{}});CHECK(r&&r->events[0].kind==PickupEventKind::Released&&hs.orbs[0].target==0);
    r=stepPickups(h,hs,5,{{},{{1,{},{}}}});CHECK(r&&r->collected.size()==1&&hs.orbs.empty());
    roundtrip(s);PickupState rs;CHECK(stepPickups(c,rs,1,{{{0,3,{}}},{}}));roundtrip(rs);
    // Collected xp goes through grantXp with the pickup bonus.
    auto d=defs();PerkState ps=started(d);PerkConfig pc;for(auto [player,xp]:r->collected)CHECK(grantXp(d,pc,ps,player,xp,true));CHECK(ps.players.at(1).xp==7);
    CHECK(!stepPickups(c,s,200,{{{0,0,{}}},{}}));CHECK(!stepPickups(c,s,201,{{},{{1,{},{}},{1,{},{}}}}));
}
void summons(){
    auto d=defs();auto points=readSummonPoints(file("perks.json"));SummonConfig c;c.maxAllies=3;SummonState s;
    CHECK(addSummoner(c,s,1,0)&&!addSummoner(c,s,1,0));
    CHECK(registerAlly(s,{101,1,2,1,0})&&registerAlly(s,{102,1,3,2,0}));
    SummonPresence at1{1,0,{10_fx,{},{}}};
    auto r=stepSummons(c,points,s,1,{{at1},{},{}});CHECK(r&&r->events.empty()); // nothing to restore at a restore point
    CHECK(summonPointView(c,points[0],s,1,1)==SummonView::Blocked);
    r=stepSummons(c,points,s,2,{{},{101},{}});CHECK(r&&r->events[0].kind==SummonEventKind::AllyDied&&aliveAllies(s,1)==1);
    for(uint32_t t=3;t<13;++t)CHECK(stepSummons(c,points,s,t,{{at1},{},{}}));
    int64_t milli=0;CHECK(summonPointView(c,points[0],s,1,13,&milli)==SummonView::Progress&&milli==333);
    r=stepSummons(c,points,s,13,{{},{},{}});CHECK(r&&r->events[0].kind==SummonEventKind::Left&&r->events[1].kind==SummonEventKind::ProgressReset&&s.points[1].progress==0);
    SummonReport last;for(uint32_t t=14;t<44;++t){auto x=stepSummons(c,points,s,t,{{at1},{},{}});CHECK(x);if(!x->requests.empty())last=*x;}
    CHECK(last.requests.size()==1&&last.requests[0].restore==101&&last.requests[0].slot==1&&last.requests[0].unit==2&&s.points[1].pending);
    CHECK(summonPointView(c,points[0],s,1,44)==SummonView::Pending);
    CHECK(confirmSummon(c,points,s,last.requests[0],201,2,1,44));CHECK(!s.allies.contains(101)&&s.allies.at(201).slot==1&&s.points[1].used);
    CHECK(summonPointView(c,points[0],s,1,45)==SummonView::Used);
    // Reusable point with cooldown recruits when nothing is dead; cancel keeps it available.
    SummonPresence at2{1,0,{-10_fx,{},{}}};SummonReport got;
    for(uint32_t t=45;t<300&&got.requests.empty();++t){auto x=stepSummons(c,points,s,t,{{at2},{},{}});CHECK(x);got=*x;}
    CHECK(got.requests.size()==1&&got.requests[0].recruit&&got.requests[0].restore==0);
    CHECK(cancelSummon(c,s,got.requests[0])&&!s.points[2].pending&&!s.points[2].used);
    got={};uint32_t when=0;for(uint32_t t=500;t<800&&got.requests.empty();++t){auto x=stepSummons(c,points,s,t,{{at2},{},{}});CHECK(x);got=*x;when=t;}
    CHECK(confirmSummon(c,points,s,got.requests[0],202,5,4,when,50,false));auto spare=points[1];spare.id=3;CHECK(s.points[2].readyAt==when+60&&summonPointView(c,points[1],s,1,when+1)==SummonView::Cooldown&&summonPointView(c,spare,s,1,when+1)==SummonView::SquadFull);
    // Temporary ally expires and is reported for despawn; non-acquired allies are not restorable.
    r=stepSummons(c,points,s,when+50,{{},{},{}});CHECK(r&&r->despawn==std::vector<int64_t>{202}&&!s.allies.contains(202));
    CHECK(summonPointView(c,points[1],s,1,when+51)==SummonView::Cooldown);
    roundtrip(s);
    // Charges gate points and recharge.
    SummonConfig cc;cc.charges=1;cc.chargeRecharge=10;cc.progressTicks=2;SummonState cs;CHECK(addSummoner(cc,cs,1,0)&&registerAlly(cs,{301,1,2,1,0}));
    std::vector<SummonPointDef> one{{1,0,{},3_fx,0,0,false,SummonAction::Recruit}};
    SummonReport q;for(uint32_t t=1;t<=2;++t)q=*stepSummons(cc,one,cs,t,{{{1,0,{}}},{},{}});
    CHECK(q.requests.size()==1&&cs.players[1].charges==0&&cs.players[1].nextChargeAt==12);
    CHECK(confirmSummon(cc,one,cs,q.requests[0],302,2,1,2));
    CHECK(stepSummons(cc,one,cs,3,{{{1,0,{}}},{},{}})&&cs.points[1].occupant==0&&summonPointView(cc,one[0],cs,1,3)==SummonView::Blocked);
    auto g=stepSummons(cc,one,cs,12,{{},{},{}});CHECK(g&&g->events[0].kind==SummonEventKind::ChargeGained&&cs.players[1].charges==1);
    CHECK(useSummonCharge(cs,1)&&!useSummonCharge(cs,1));
    auto gone=removeSummoner(cs,1);CHECK(gone==(std::vector<int64_t>{301,302})&&cs.allies.empty());
    CHECK(!stepSummons(cc,one,cs,12,{}));
    (void)d;
}
void settings(){
    auto parsed=aiverse::lib::gameplay::readGameplaySettings(file("perks-settings.json"));CHECK(parsed.value&&perkSettingsErrors(*parsed.value).empty());
    auto p=perkConfig(*parsed.value);auto x=pickupConfig(*parsed.value);auto s=summonConfig(*parsed.value);
    CHECK(p.rerolls==1&&p.maxSquad==15&&x.collectRadius==Fixed::fromRatio(5,2)&&x.magnetRadius==30_fx&&x.landing==10&&s.progressTicks==90);
    CHECK(perkConfigError(p).empty()&&pickupConfigError(x).empty()&&summonConfigError(s).empty());
    auto read=[](const char* text){return aiverse::lib::gameplay::readGameplaySettings(Json::parse(text));};
    auto r=read(R"({"schemaVersion":1,"perks":{"offerChoices":9}})");CHECK(!r.value&&r.errors.at(0)=="perks.offerChoices: integer out of range");
    r=read(R"({"schemaVersion":1,"perks":{"pickupAttractSpeedMilli":3000}})");CHECK(!r.value&&r.errors.at(0)=="perks.pickupAttractSpeedMilli: must not exceed pickupMaxSpeedMilli");
    r=read(R"({"schemaVersion":1,"perks":{"summonChargeRechargeTicks":5}})");CHECK(r.value&&perkSettingsErrors(*r.value).at(0)=="perks.summonChargeRechargeTicks: requires summonCharges");
    r=read(R"({"schemaVersion":1,"perks":{"pickupAttractSpeedMilli":100,"pickupGrabRadiusMilli":0}})");CHECK(r.value&&perkSettingsErrors(*r.value).at(0).find("pickupGrabRadiusMilli")!=std::string::npos);
    auto defaults=read(R"({"schemaVersion":1})");CHECK(defaults.value&&perkSettingsErrors(*defaults.value).empty());
    CHECK(progressionConfigHash(perkConfig(*defaults.value),pickupConfig(*defaults.value),summonConfig(*defaults.value))==progressionConfigHash(PerkConfig{},PickupConfig{},SummonConfig{}));
}
}
int main(int argc,char** argv){
    try{CHECK(argc==2);root=argv[1];definitions();battleDeck();statPipeline();experience();offers();determinism();pickups();summons();settings();
        std::cout<<"Match progression: perks, modifier pipeline, xp/levels, offers, xp pickups, summons, battle deck, save/restore: OK\n";}
    catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
