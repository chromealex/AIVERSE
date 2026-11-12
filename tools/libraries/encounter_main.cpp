// aiverse_encounter: validate boss-encounters data and run the headless hero-vs-boss reference fight.
//   aiverse_encounter validate <encounter.json> [--json]
//   aiverse_encounter simulate <encounter.json> [--boss ID] [--seed N] [--ticks N] [--reaction N] [--json] [--quiet]
// Exit codes: 0 ok (simulate: boss defeated), 1 invalid data / usage, 2 fight lost or timed out.
#include <aiverselib/genre/EncounterScenario.h>
#include "engine/data/Json.h"
#include <cstdlib>
#include <iostream>
#include <string>
using namespace aiverse;using namespace aiverse::lib::genre;
static int usage(){std::cerr<<"usage: aiverse_encounter validate|simulate <encounter.json> [--boss ID] [--seed N] [--ticks N] [--reaction N] [--json] [--quiet]\n";return 1;}
static std::string hex(uint64_t v){static const char* d="0123456789abcdef";std::string s(16,'0');for(int i=15;i>=0;--i){s[size_t(i)]=d[v&15];v>>=4;}return s;}
int main(int argc,char** argv){
    if(argc<3)return usage();
    std::string mode=argv[1],path=argv[2];bool json=false,quiet=false;ScenarioConfig cfg;uint64_t seed=1;
    for(int i=3;i<argc;++i){
        std::string a=argv[i];auto next=[&]()->const char*{return i+1<argc?argv[++i]:nullptr;};
        if(a=="--json")json=true;else if(a=="--quiet")quiet=true;
        else if(a=="--boss"||a=="--seed"||a=="--ticks"||a=="--reaction"){
            auto v=next();if(!v)return usage();auto n=std::strtoll(v,nullptr,10);if(n<0)return usage();
            if(a=="--boss")cfg.boss=n;else if(a=="--seed")seed=uint64_t(n);else if(a=="--ticks")cfg.maxTicks=uint32_t(n);else cfg.reaction=uint32_t(n);
        }else return usage();
    }
    EncounterData data;Json source;
    try{source=Json::parseFile(path);data=readEncounterData(source);}
    catch(const std::exception& e){
        if(json){auto o=Json::object();o.set("ok",Json::fromBool(false));o.set("error",Json::string(e.what()));std::cout<<o.dump(-1)<<"\n";}
        else std::cerr<<path<<": "<<e.what()<<"\n";
        return 1;
    }
    if(mode=="validate"){
        if(json){
            auto o=Json::object();o.set("ok",Json::fromBool(true));o.set("statuses",Json::number(int64_t(data.statuses.size())));
            o.set("attacks",Json::number(int64_t(data.attacks.size())));o.set("bosses",Json::number(int64_t(data.bosses.size())));
            o.set("windows",Json::number(int64_t(data.schedule.windows.size())));o.set("hash",Json::string(hex(encounterDataHash(source))));
            std::cout<<o.dump(-1)<<"\n";
        }else std::cout<<"ok: "<<data.statuses.size()<<" statuses, "<<data.attacks.size()<<" attacks, "<<data.bosses.size()<<" bosses, "
            <<data.schedule.windows.size()<<" windows, hash "<<hex(encounterDataHash(source))<<"\n";
        return 0;
    }
    if(mode!="simulate")return usage();
    if(!cfg.addAttack){for(const auto& [id,d]:data.attacks)if(d.area.radius<=2_fx&&d.area.shape==AreaShape::Cone)cfg.addAttack=id;}
    cfg.log=!quiet;
    auto r=runBossScenario(data,cfg,seed);
    if(json){
        auto o=Json::object();o.set("ok",Json::fromBool(true));
        o.set("result",Json::string(r.bossDefeated?"boss defeated":r.heroDefeated?"hero defeated":"timed out"));
        for(auto [k,v]:std::initializer_list<std::pair<const char*,int64_t>>{{"ticks",r.ticks},{"heroHp",r.heroHp},{"bossHp",r.bossHp},{"phases",r.phases},{"attacks",r.attacks},
            {"strikes",r.strikes},{"heroHits",r.heroHits},{"dodged",r.dodged},{"addsSpawned",r.addsSpawned},{"addsKilled",r.addsKilled},{"statusesApplied",r.statusesApplied},{"burnTicks",r.burnTicks}})
            o.set(k,Json::number(v));
        o.set("enraged",Json::fromBool(r.enraged));o.set("hash",Json::string(hex(r.hash)));
        if(!quiet){auto l=Json::array();for(const auto& s:r.log)l.push(Json::string(s));o.set("log",l);}
        std::cout<<o.dump(-1)<<"\n";
    }else{
        for(const auto& s:r.log)std::cout<<s<<"\n";
        std::cout<<(r.bossDefeated?"boss defeated":r.heroDefeated?"hero defeated":"timed out")<<" at tick "<<r.ticks<<": hero hp "<<r.heroHp<<", boss hp "<<r.bossHp
            <<", phases "<<r.phases<<", attacks "<<r.attacks<<", hits "<<r.heroHits<<", dodged "<<r.dodged<<", adds "<<r.addsKilled<<"/"<<r.addsSpawned
            <<", statuses "<<r.statusesApplied<<", burn ticks "<<r.burnTicks<<(r.enraged?", enraged":"")<<", hash "<<hex(r.hash)<<"\n";
    }
    return r.bossDefeated?0:2;
}
