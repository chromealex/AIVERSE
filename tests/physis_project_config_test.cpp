#include "engine/physis/PhysisProjectConfig.h"
#include <cstdio>
#include <memory>
using namespace aiverse;
using namespace aiverse::physis;
namespace {
int failed=0;
#define CHECK(c) do { if(!(c)){std::printf("PHYSIS CONFIG FAIL %s:%d %s\n",__FILE__,__LINE__,#c);++failed;} } while(0)

Json authored() {
    return Json::parse(R"({
      "physis":{
        "enabled":true,
        "grid":{"originX":-12,"originZ":5,"width":9,"height":7,
                "cellSizeMeters":100,"seed":44},
        "climate":{"baseTemperature":22.5,"baseHumidity":0.25},
        "enableStorms":true,"enableDisasters":true,
        "enableWindZones":true,"enableGravityFields":true,
        "enableForces":false,"floodMode":"ExternalAuthority"
      }
    })");
}
void optionsAndRejections() {
    CHECK(!optionsFromMap(Json::object()).has_value());
    Json disabled=authored();
    auto config=disabled["physis"];
    config.set("enabled",Json::fromBool(false));
    disabled.set("physis",config);
    CHECK(!optionsFromMap(disabled).has_value());
    const auto opt=optionsFromMap(authored());
    CHECK(opt.has_value());
    if(!opt) return;
    CHECK(opt->climate.originX==-12);
    CHECK(opt->climate.originZ==5);
    CHECK(opt->climate.width==9 && opt->climate.height==7);
    CHECK(opt->climate.cellSizeMeters==100 && opt->climate.seed==44);
    CHECK(opt->climate.baseTemperature==Fixed::fromRatio(45,2));
    CHECK(opt->climate.baseHumidity==Fixed::fromRatio(1,4));
    CHECK(opt->enableStorms && opt->enableDisasters);
    CHECK(opt->enableWindZones && opt->enableGravityFields);
    CHECK(!opt->enableForces);
    CHECK(opt->floodMode==FloodMode::ExternalAuthority);
    Json malformed=authored();
    config=malformed["physis"];
    auto grid=config["grid"];
    grid.set("width",Json::number(int64_t(0)));
    config.set("grid",grid);malformed.set("physis",config);
    bool rejected=false;
    try { (void)optionsFromMap(malformed); }
    catch (const std::out_of_range&) { rejected=true; }
    CHECK(rejected);
    malformed=authored();
    config=malformed["physis"];
    config.set("enableForces",Json::string("true"));
    malformed.set("physis",config);
    rejected=false;
    try { (void)optionsFromMap(malformed); }
    catch (const std::invalid_argument&) { rejected=true; }
    CHECK(rejected);
}
class GenericNativeHost final : public GameModule {
public:
    std::string name() const override { return "physis-config-test"; }
    void setup(Simulation& sim,const Json&,const Json& map) override {
        if(!installFromMap(sim,map)) throw std::runtime_error("PHYSIS not configured");
    }
    void applyCommand(Simulation&,const Command&) override {}
    void hashState(Simulation&,Hasher&) override {}
    int result(Simulation&) override { return 0; }
    bool serializeState(Simulation&,Archive& archive) override { return archive.ok(); }
};
Json authoredFields() {
    Json map = authored();
    Json p = map["physis"];
    p.set("windZones",Json::parse(R"([
      {"id":"wind-z","center":[0,0,0],"radiusMeters":80,"velocity":[6,0,0]},
      {"id":"wind-a","center":[20,0,-20],"radiusMeters":40,"velocity":[0,0,8]}
    ])"));
    p.set("storms",Json::parse(R"([
      {"id":"storm-c","kind":"Tornado","center":[0,0,0],"eyeRadiusMeters":5,
       "outerRadiusMeters":100,"heightMeters":200,"updraft":20}
    ])"));
    p.set("gravityFields",Json::parse(R"([
      {"id":"gravity-b","mode":"Attract","center":[0,50,0],
       "radiusMeters":150,"radialMagnitude":8}
    ])"));
    map.set("physis",p);
    return map;
}

void nativeAuthoredFieldsAndRollback() {
    const Json fixture=authoredFields();
    const auto make=[&]() {
        auto s=std::make_unique<Simulation>(SimConfig{},std::make_unique<GenericNativeHost>());
        s->setup(Json::object(),fixture);
        return s;
    };
    auto a=make(), b=make();
    int aw=0,bw=0,st=0,gr=0;
    a->world().eachExistingConst<WindZone>([&](Entity,const WindZone&){++aw;});
    b->world().eachExistingConst<WindZone>([&](Entity,const WindZone&){++bw;});
    a->world().eachExistingConst<StormEvent>([&](Entity,const StormEvent&){++st;});
    a->world().eachExistingConst<GravityField>([&](Entity,const GravityField&){++gr;});
    CHECK(aw==2 && bw==2 && st==1 && gr==1);
    CHECK(a->checksum()==b->checksum());
    const Vec3 position{0_fx,0_fx,0_fx};
    CHECK(windWithStormsAt(a->world(),position)==
          windWithStormsAt(b->world(),position));
    for(int i=0;i<3;++i){a->step();b->step();}
    CHECK(a->checksum()==b->checksum());
    auto saved=a->serialize();
    auto restored=make();
    std::string error;
    CHECK(restored->deserialize(saved,&error));
    CHECK(restored->checksum()==a->checksum());
    a->step();restored->step();
    CHECK(restored->checksum()==a->checksum());
}

void nativeHostAndSave() {
    auto make = [&]() {
        auto s=std::make_unique<Simulation>(SimConfig{},std::make_unique<GenericNativeHost>());
        s->setup(Json::object(),authored());
        return s;
    };
    auto a=make(),b=make();
    CHECK(a->world().resourceConst<State>()!=nullptr);
    for(int i=0;i<4;++i) { a->step(); b->step(); }
    CHECK(a->checksum()==b->checksum());
    CHECK(a->world().resourceConst<State>()->tick==4);
    const auto saved=a->serialize();
    auto restored=make();
    std::string error;
    CHECK(restored->deserialize(saved,&error));
    CHECK(restored->checksum()==a->checksum());
    a->step(); restored->step();
    CHECK(restored->checksum()==a->checksum());
}
}
int main() {
    optionsAndRejections();nativeHostAndSave();nativeAuthoredFieldsAndRollback();
    std::printf("PHYSIS ProjectConfig: %s\n",failed?"FAIL":"PASS");
    return failed?1:0;
}
