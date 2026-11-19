#include "engine/physis/PhysisProjectConfig.h"
#include <cstdio>
#include <stdexcept>
using namespace aiverse;
using namespace aiverse::physis;

namespace {
int failures=0;
#define CHECK(v) do { if(!(v)){std::printf("PHYSIS FIELDS FAIL %s:%d %s\n",__FILE__,__LINE__,#v);++failures;} } while(0)

Json configured() {
    return Json::parse(R"({
      "physis":{
        "enabled":true,
        "grid":{"originX":-2,"originZ":-2,"width":4,"height":4,
                "cellSizeMeters":100,"seed":1},
        "climate":{"baseTemperature":15,"baseHumidity":0.5},
        "enableWindZones":true,"enableGravityFields":true,"enableStorms":true,
        "windZones":[
          {"id":"wind-z","center":[0,0,0],"radiusMeters":40,"velocity":[7,0,0]},
          {"id":"wind-a","center":[10,0,-10],"radiusMeters":70,
           "velocity":[0,3,5],"gustAmplitude":2,"seed":13}
        ],
        "storms":[{"id":"storm-b","kind":"Tornado","center":[0,0,0],
          "eyeRadiusMeters":8,"outerRadiusMeters":80,"heightMeters":200,
          "tangentialWind":40,"updraft":30}],
        "gravityFields":[{"id":"gravity-c","mode":"Uniform",
          "center":[0,0,0],"radiusMeters":100,
          "acceleration":[0,-2,0],"overrideBaseline":true}]
      }
    })");
}
void parserAndStableOrder() {
    const Json map=configured();
    const auto opts=optionsFromMap(map);
    CHECK(opts.has_value());
    if(!opts) return;
    const auto fields=parseFieldAssets(map["physis"],*opts);
    CHECK(fields.size()==4);
    if(fields.size()!=4)return;
    CHECK(fields[0].id=="gravity-c");
    CHECK(fields[1].id=="storm-b");
    CHECK(fields[2].id=="wind-a");
    CHECK(fields[3].id=="wind-z");
    World a,b;
    initialize(a,opts->climate);
    initialize(b,opts->climate);
    spawnFieldAssets(a,fields);
    spawnFieldAssets(b,fields);
    int aw=0,bw=0,storm=0,gravity=0;
    a.eachExistingConst<WindZone>([&](Entity,const WindZone&){++aw;});
    b.eachExistingConst<WindZone>([&](Entity,const WindZone&){++bw;});
    a.eachExistingConst<StormEvent>([&](Entity,const StormEvent&){++storm;});
    a.eachExistingConst<GravityField>([&](Entity,const GravityField&){++gravity;});
    CHECK(aw==2 && bw==2);
    CHECK(storm==1 && gravity==1);
    CHECK(windAt(a,{0_fx,0_fx,0_fx})==windAt(b,{0_fx,0_fx,0_fx}));
    CHECK(gravityAt(a,{0_fx,0_fx,0_fx})==gravityAt(b,{0_fx,0_fx,0_fx}));
    CHECK(stormsAt(a,{0_fx,0_fx,0_fx},0).wind==
          stormsAt(b,{0_fx,0_fx,0_fx},0).wind);
}
void rejectInvalidAssetsBeforeWorldMutation() {
    Json map=configured();
    auto opts=optionsFromMap(map);
    CHECK(opts.has_value());
    if(!opts)return;
    auto conf=map["physis"];
    auto winds=conf["windZones"];
    auto first=winds[0];
    first.set("id",Json::string("gravity-c"));
    winds.at(0)=first;
    conf.set("windZones",winds);
    bool rejected=false;
    try { (void)parseFieldAssets(conf,*opts); }
    catch(const std::invalid_argument&){rejected=true;}
    CHECK(rejected);
    conf=map["physis"];
    conf.set("enableStorms",Json::fromBool(false));
    rejected=false;
    try { (void)parseFieldAssets(conf,*opts); }
    catch(const std::invalid_argument&){rejected=true;}
    // Note: opts is from original map, use matching disabled flag.
    InstallOptions noStorm=*opts;noStorm.enableStorms=false;
    rejected=false;
    try { (void)parseFieldAssets(conf,noStorm); }
    catch(const std::invalid_argument&){rejected=true;}
    CHECK(rejected);

    auto invalid=map;
    conf=invalid["physis"];
    winds=conf["windZones"];
    first=winds[0];
    first.set("radiusMeters",Json::number(int64_t(-1)));
    winds.at(0)=first;
    conf.set("windZones",winds);
    invalid.set("physis",conf);
    rejected=false;
    try { (void)parseFieldAssets(invalid["physis"],*opts); }
    catch(const std::invalid_argument&){rejected=true;}
    CHECK(rejected);
}
}
int main(){parserAndStableOrder();rejectInvalidAssetsBeforeWorldMutation();
std::printf("PHYSIS Fields: %s\n",failures?"FAIL":"PASS");return failures?1:0;}
