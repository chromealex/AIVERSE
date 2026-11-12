// aiverse_progression: headless match-progression run (bot hero, kills -> xp orbs -> pickups -> level-up offers ->
// bot choices -> recruits as allies -> deaths -> summon circle restores) over the example definitions.
// --verify replays the run twice and once through a mid-match save/restore and requires identical checksums.
//   aiverse_progression [--root DIR] [--defs F] [--deck F] [--settings F] [--seed N] [--ticks N] [--verify] [--json]
#include <aiverselib/genre/PerksData.h>
#include <aiverselib/genre/PerksConfigured.h>
#include <aiverselib/genre/PerksPersistence.h>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
using namespace aiverse;using namespace aiverse::lib::genre;
namespace {
struct Run {
    PerkDefinitions defs;std::vector<SummonPointDef> points;PerkConfig perk;PickupConfig pick;SummonConfig summon;BattleDeck deck;
    PerkState perks;PickupState orbs;SummonState allies;Random world;int64_t nextEntity=1000;
    uint32_t kills=0,collected=0,recruits=0,summons=0,choices=0,deaths=0;
};
bool begin(Run& r,uint64_t seed){
    seedPerks(r.perks,seed);r.world.reseed(seed,0x44454d4full);
    if(!addPlayer(r.defs,r.perks,r.deck))return false;
    return addSummoner(r.summon,r.allies,r.deck.player,0);
}
Vec3 heroAt(uint32_t tick){int64_t phase=int64_t(tick/8%80);int64_t x=phase<40?phase-20:60-phase;return {Fixed::fromInt(x)/2,{},{}};}
bool step(Run& r,uint32_t tick){
    const int64_t player=r.deck.player;auto hero=heroAt(tick);auto& p=r.perks.players.at(player);
    PickupInput in;
    if(tick%15==0){auto kind=tick%600==0?9:int64_t(1+r.world.below(2));++r.kills;
        in.spawns.push_back({0,killXp(r.defs,kind),{hero.x+Fixed::fromInt(int64_t(r.world.below(9))-4),{},Fixed::fromInt(int64_t(r.world.below(9))-4)}});}
    auto radius=Fixed::fromRatio(evaluateStat(r.defs,p,perk_stat::PickupRadius,r.pick.collectRadius.raw*1000/Fixed::kOne),1000);
    in.collectors.push_back({player,hero,radius,tick%900==0});
    auto got=stepPickups(r.pick,r.orbs,tick,in);if(!got)return false;
    for(auto [who,xp]:got->collected){++r.collected;if(!grantXp(r.defs,r.perk,r.perks,who,xp,true))return false;}
    if(!p.offer.open)openOffer(r.defs,r.perk,r.perks,player);
    if(p.offer.open&&tick%30==0)if(auto i=botOfferChoice(r.defs,r.perk,p,tick%300<60)){
        auto ev=selectOffer(r.defs,r.perk,r.perks,player,p.offer.revision,*i,tick);if(!ev)return false;++r.choices;
        for(const auto& e:*ev)if(e.kind==PerkEventKind::Recruit){++r.recruits;
            if(!registerAlly(r.allies,{r.nextEntity++,player,p.deck.slots[std::size_t(e.slot)].unit,e.slot,tick}))return false;}
    }
    SummonInput si;si.players.push_back({player,0,hero,!p.offer.open});
    if(tick%200==0)for(const auto& [id,a]:r.allies.allies)if(a.alive){si.died.push_back(id);++r.deaths;break;}
    auto sr=stepSummons(r.summon,r.points,r.allies,tick,si);if(!sr)return false;
    for(const auto& q:sr->requests){
        auto slot=q.restore?q.slot:1;auto unit=q.restore?q.unit:p.deck.slots[1].unit;
        if(!confirmSummon(r.summon,r.points,r.allies,q,r.nextEntity++,unit,slot,tick))return false;
        ++r.summons;
    }
    return true;
}
uint64_t checksum(const Run& r){Hasher h;h.u64(progressionHash(r.perks));h.u64(progressionHash(r.orbs));h.u64(progressionHash(r.allies));h.u64(r.world.state());return h.value();}
std::string arg(int argc,char** argv,const std::string& name,const std::string& def){for(int i=1;i+1<argc;++i)if(argv[i]==name)return argv[i+1];return def;}
bool flag(int argc,char** argv,const std::string& name){for(int i=1;i<argc;++i)if(argv[i]==name)return true;return false;}
}
int main(int argc,char** argv){
    try{
        auto root=arg(argc,argv,"--root",".");auto ex=root+"/examples/libraries/genre/";
        auto defsJson=Json::parseFile(arg(argc,argv,"--defs",ex+"perks.json"));
        auto settings=aiverse::lib::gameplay::readGameplaySettings(Json::parseFile(arg(argc,argv,"--settings",ex+"perks-settings.json")));
        if(!settings.value){for(auto& e:settings.errors)std::cerr<<e<<'\n';return 2;}
        if(auto e=perkSettingsErrors(*settings.value);!e.empty()){for(auto& x:e)std::cerr<<x<<'\n';return 2;}
        Run base;base.defs=readPerkDefinitions(defsJson);base.points=readSummonPoints(defsJson);
        base.perk=perkConfig(*settings.value);base.pick=pickupConfig(*settings.value);base.summon=summonConfig(*settings.value);
        base.deck=readBattleDeck(Json::parseFile(arg(argc,argv,"--deck",ex+"perks-deck.json")));
        if(auto e=battleDeckError(base.defs.units,base.defs.deck,base.deck,[&](int64_t id){return base.defs.perks.contains(id);});!e.empty()){std::cerr<<e<<'\n';return 2;}
        auto seed=uint64_t(std::strtoull(arg(argc,argv,"--seed","3").c_str(),nullptr,10));auto ticks=uint32_t(std::strtoul(arg(argc,argv,"--ticks","5400").c_str(),nullptr,10));
        auto play=[&](Run& r,uint32_t from,uint32_t to){for(uint32_t t=from;t<=to;++t)if(!step(r,t)){std::cerr<<"step failed at tick "<<t<<'\n';return false;}return true;};
        Run a=base;if(!begin(a,seed)||!play(a,1,ticks))return 1;
        const auto& p=a.perks.players.at(a.deck.player);bool ok=true;
        if(flag(argc,argv,"--verify")){
            Run b=base;if(!begin(b,seed)||!play(b,1,ticks))return 1;
            Run c=base;if(!begin(c,seed)||!play(c,1,ticks/2))return 1;
            Run d=base;auto s1=save(c.perks),s2=save(c.orbs),s3=save(c.allies);
            ok=!s1.empty()&&!s2.empty()&&!s3.empty()&&load(s1,d.perks)&&load(s2,d.orbs)&&load(s3,d.allies);
            d.world=c.world;d.nextEntity=c.nextEntity;d.kills=c.kills;d.collected=c.collected;d.recruits=c.recruits;d.summons=c.summons;d.choices=c.choices;d.deaths=c.deaths;
            ok=ok&&play(d,ticks/2+1,ticks)&&checksum(a)==checksum(b)&&checksum(a)==checksum(d);
        }
        std::string perksTaken;for(const auto& o:p.mods){if(!perksTaken.empty())perksTaken+=",";perksTaken+=a.defs.perks.at(o.perk).key;}
        char hex[17];std::snprintf(hex,sizeof hex,"%016llx",static_cast<unsigned long long>(checksum(a)));
        if(flag(argc,argv,"--json"))
            std::cout<<"{\"ticks\":"<<ticks<<",\"seed\":"<<seed<<",\"level\":"<<p.level<<",\"xp\":"<<p.xp<<",\"kills\":"<<a.kills<<",\"orbsCollected\":"<<a.collected
                <<",\"choices\":"<<a.choices<<",\"recruits\":"<<a.recruits<<",\"deaths\":"<<a.deaths<<",\"summons\":"<<a.summons<<",\"alliesAlive\":"<<aliveAllies(a.allies,a.deck.player)
                <<",\"perks\":\""<<perksTaken<<"\",\"checksum\":\""<<hex<<"\",\"verified\":"<<(ok?"true":"false")<<"}\n";
        else std::cout<<"progression: ticks="<<ticks<<" seed="<<seed<<" level="<<p.level<<" xp="<<p.xp<<" kills="<<a.kills<<" orbs="<<a.collected<<" choices="<<a.choices
            <<" recruits="<<a.recruits<<" deaths="<<a.deaths<<" summons="<<a.summons<<" allies="<<aliveAllies(a.allies,a.deck.player)<<"\nperks: "<<perksTaken<<"\nchecksum "<<hex
            <<(flag(argc,argv,"--verify")?(ok?" (replay + save/restore identical)":" MISMATCH"):"")<<'\n';
        return ok?0:1;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}
}
