// Same VFX presenter consumes data-only ECS components in unrelated game styles.
#include <aiverselib/mechanics/SpectraFX.h>
#include <aiverselib/mechanics/SpectraSourceMirror.h>
#include "engine/fx/SpectraScenePoolEmission.h"
#include "engine/view/SpectraSocketPose.h"
#include <algorithm>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

using namespace aiverse;
namespace lg = aiverse::lib::mechanics;
static void check(bool ok,const char* why) {
    if (!ok) throw std::runtime_error(why);
}
static fx::EffectDef continuous() {
    fx::EffectDef out;
    std::vector<std::string> errors;
    check(fx::parseEffect(Json::parse(R"JSON({"emitters":[{
      "texture":"builtin:soft", "loop":true,"duration":0.5,"rate":10,
      "speed":0,"life":0.25,"size":0.1
    }]})JSON"),"test",out,errors),"continuous fx fixture");
    return out;
}
static fx::EffectDef oneShot() {
    fx::EffectDef out;
    std::vector<std::string> errors;
    check(fx::parseEffect(Json::parse(R"JSON({"emitters":[{
      "texture":"builtin:spark", "burst":3,
      "speed":0,"life":0.5,"size":0.1
    }]})JSON"),"test",out,errors),"oneshot fx fixture");
    return out;
}
struct SpaceUnit {fx::float3 pos{2,3,4};};
struct RacingVehicle {fx::float3 pos{5,6,7};};

template <typename Kind>
static void scenario() {
    fx::ParticleSystem ps;
    ps.setQuality(fx::FxQuality::High);
    check(ps.init(nullptr),"headless backend");
    check(ps.define("engine_fire",continuous())>=0,"define continuous");
    check(ps.define("impact_flash",oneShot())>=0,"define event effect");
    World world;
    Entity source=world.create();
    world.add<Kind>(source);
    spectra::SourceComponent fxSource;
    fxSource.effect="engine_fire";
    fxSource.killOnDetach=true;
    world.add<spectra::SourceComponent>(source,fxSource);
    spectra::PresentationSystem presenter(ps);
    auto pose=[](const World& w,Entity e,fx::float3& pos,fx::float3& dir) {
        if (const Kind* kind=w.tryGetConst<Kind>(e)) {
            pos=kind->pos;
            dir={0,1,0};
            return true;
        }
        return false;
    };
    presenter.sync(world,pose);
    check(presenter.activeAttachments()==1,"attached emitter absent");
    ps.update(0.1f);
    check(ps.stats().particles>0,"no continuous particles");
    presenter.sync(world,pose);
    check(presenter.activeAttachments()==1,"duplicate attached emitter");

    SimEvent hit;
    hit.type=72;
    hit.pos={1_fx,2_fx,3_fx};
    const std::map<uint16_t,spectra::EventBinding> bindings{{72,{"impact_flash",1_fx}}};
    // Check the effect's own particles. An unsigned 'before-20'
    // underflows for the small headless continuous emitter in this test.
    const int impactId=ps.effect("impact_flash");
    check(impactId>=0,"impact effect ID missing");
    std::vector<fx::FxParticleInfo> particles;
    auto impactCount=[&]() {
        ps.snapshot(particles);
        return std::count_if(particles.begin(),particles.end(),
            [impactId](const fx::FxParticleInfo& p) {
                return p.effect==impactId;
            });
    };
    presenter.consumeTickEvents(40,{hit},bindings);
    ps.update(0.1f);
    check(impactCount()==3,"gameplay event must spawn three impact particles");
    presenter.consumeTickEvents(40,{hit},bindings);
    check(presenter.lastPresentedTick()==40,"duplicate tick changed cursor");
    ps.update(0.1f);
    check(impactCount()==3,"duplicate tick must not spawn another impact burst");

    world.destroy(source);
    Entity recycled=world.create();
    check(recycled.index==source.index && recycled.gen!=source.gen,"generation reuse fixture");
    world.add<Kind>(recycled);
    world.add<spectra::SourceComponent>(recycled,fxSource);
    presenter.sync(world,pose);
    check(presenter.activeAttachments()==1,"recycled entity attached twice");
    presenter.reset();
    check(presenter.activeAttachments()==0,"reset retained renderer handles");
    ps.snapshot(particles);
    check(particles.empty(),"reset must kill attached and recent one-shot particles");
    presenter.consumeTickEvents(39,{hit},bindings);
    check(presenter.lastPresentedTick()==39,"rollback reset did not clear event cursor");
}
static void mirrorScenario(){
    fx::ParticleSystem ps;
    check(ps.init(nullptr),"mirror headless init");
    check(ps.define("flame",continuous())>=0,"mirror effect define");
    lg::SpectraSourceMirror mirror;
    lg::SpectraSourceComponent source;source.effect="flame";source.killOnDetach=true;
    lg::SpectraPresentationSystem presenter(ps);
    mirror.sync({{"a",source,{1,2,3}},{"b",source,{4,5,6}}});
    presenter.sync(mirror.world(),lg::SpectraSourceMirror::resolve);
    check(presenter.activeAttachments()==2,"mirror must keep independent instances");
    mirror.sync({{"a",source,{8,2,3}}});
    presenter.sync(mirror.world(),lg::SpectraSourceMirror::resolve);
    check(mirror.size()==1&&presenter.activeAttachments()==1,"mirror failed to retire removed source");
    mirror.clear();
    presenter.sync(mirror.world(),lg::SpectraSourceMirror::resolve);
    check(presenter.activeAttachments()==0,"mirror clear retained attachments");
}
static void poolPolicyScenario(){
    spectra::PoolContinuousEmission policy;
    check(!spectra::readScenePoolEmission(Json::object(),policy),"legacy graph silently opted into pooled bursts");
    const auto graph=Json::parse(R"({"scenePoolEmission":{"burstsPerSecond":8,"maxBurstsPerFrame":2}})");
    check(spectra::readScenePoolEmission(graph,policy)&&policy.burstsPerSecond==8&&policy.maxBurstsPerFrame==2,"explicit pool policy missing");
    bool rejected=false;
    try{spectra::readScenePoolEmission(Json::parse(R"({"scenePoolEmission":{"burstsPerSecond":-1}})"),policy);}
    catch(const std::exception&){rejected=true;}
    check(rejected,"invalid pooled policy accepted");
    auto terrain=Json::parse(R"({"dataInterfaces":{"height":{"provider":"terraweave"}}})");
    check(spectra::scenePoolDataCoverageCompatible(terrain,policy),"rate births use exact source position");
    policy.metresPerBurst=1.f;
    check(!spectra::scenePoolDataCoverageCompatible(terrain,policy),"distance terrain births require continuous coverage");
    auto weather=Json::parse(R"({"dataInterfaces":{"wind":{"provider":"physis"}}})");
    check(spectra::scenePoolDataCoverageCompatible(weather,policy),"weather has spatial coverage");
}
static void parameterScenario(){
    lg::SpectraSourceComponent a,b;
    a.effect=b.effect="flame";
    a.parameters["lift"]=2_fx;b.parameters["lift"]=5_fx;
    spectra::InstanceParameterTable schema{{"lift",{0,1,0,10}}};
    std::array<float,8> av{},bv{};
    check(lg::resolveSpectraSourceParameters(a,schema,av)&&av[0]==2,"first instance override");
    check(lg::resolveSpectraSourceParameters(b,schema,bv)&&bv[0]==5,"second instance override");
    a.parameters.clear();
    check(lg::resolveSpectraSourceParameters(a,schema,av)&&av[0]==1,"graph default");
    a.parameters["lift"]=11_fx;
    check(!lg::resolveSpectraSourceParameters(a,schema,av),"out of range accepted");
    const auto roundtrip=lg::readSpectraSourceDocument(lg::writeSpectraSourceDocument(b));
    check(roundtrip.parameters==b.parameters,"parameter document roundtrip");
}
static void dataScenario(){
    fx::ParticleSystem ps;
    ps.setQuality(fx::FxQuality::High); // Assert exact authored emission counts.
    check(ps.init(nullptr),"data scenario headless");
    auto def=continuous();
    check(ps.define("weather_loop",def)>=0,"weather loop definition");
    check(ps.define("weather_event",oneShot())>=0,"weather event definition");
    spectra::PresentationSystem presenter(ps);
    const auto graph=Json::parse(R"({"instanceParameters":{"lift":{"slot":0,"default":1,"min":0,"max":20}},"dataInterfaces":{"lift":{"provider":"weather","channel":"up","required":true}}})");
    std::vector<std::string> errors;
    check(presenter.bindGraphDataInterfaces("weather_loop",graph,errors)&&
          presenter.bindGraphDataInterfaces("weather_event",graph,errors),"declare required data");
    lg::SpectraSourceMirror mirror;
    lg::SpectraSourceComponent source;source.effect="weather_loop";source.killOnDetach=true;
    mirror.sync({{"a",source,{1,2,3}}});
    presenter.sync(mirror.world(),lg::SpectraSourceMirror::resolve);
    check(presenter.activeAttachments()==0&&!presenter.dataInterfaceError().empty(),"missing provider prevents attachment");
    spectra::DataInterfaceRegistry registry;
    float lift=2;
    check(registry.bind("weather",[&lift](std::string_view,render::float3,float& v){v=lift;return true;}),"bind weather");
    presenter.setDataInterfaceRegistry(&registry);
    presenter.sync(mirror.world(),lg::SpectraSourceMirror::resolve);
    ps.update(0.1f);
    check(presenter.activeAttachments()==1&&ps.stats().particles>0,"provider enables attachment");
    lift=3;
    presenter.sync(mirror.world(),lg::SpectraSourceMirror::resolve);
    check(presenter.activeAttachments()==1&&ps.stats().particles>0,"sample change does not kill prior particles");
    SimEvent hit;hit.type=72;
    std::map<uint16_t,spectra::EventBinding> events{{72,{"weather_event",1_fx}}};
    presenter.consumeTickEvents(1,{hit},events);
    ps.update(0.1f);
    const auto eventId=ps.effect("weather_event");
    std::vector<fx::FxParticleInfo> rows;
    ps.snapshot(rows);
    check(std::count_if(rows.begin(),rows.end(),[eventId](const auto& p){return p.effect==eventId;})==3,"sampled event birth");
    registry.clear();
    presenter.sync(mirror.world(),lg::SpectraSourceMirror::resolve);
    check(presenter.activeAttachments()==0,"required data loss stops attachment");
    presenter.reset();
    ps.snapshot(rows);
    check(rows.empty(),"reset removes recent events");
}
static void socketScenario(){
    render::Model rig;
    rig.nodes.resize(1);
    rig.bindWorld={render::mat4::translate({99,0,0})};
    view::CompiledModelSocket binding{&rig,7,"muzzle",-1,0};
    std::array<render::mat4,1> pose{render::mat4::translate({2,0,0})};
    spectra::SocketEmissionPose result;
    check(spectra::resolveSocketEmissionPose(rig,7,binding,render::mat4::translate({10,0,0}),pose,
          render::mat4::translate({0,3,0}),{0,1,0},result),"evaluated socket pose");
    check(result.position.x==12&&result.position.y==3&&result.direction.y==1,"socket transform composition");
    check(!spectra::resolveSocketEmissionPose(rig,8,binding,{},pose,{},{0,1,0},result),"stale model epoch");
    check(!spectra::resolveSocketEmissionPose(rig,7,binding,{},{},{},{0,1,0},result),"no implicit bind fallback");
    check(spectra::resolveSocketEmissionPose(rig,7,binding,{},{},{},{0,1,0},result,true)&&result.position.x==99,"explicit bind fallback");
}
int main() {
    dataScenario();
    socketScenario();
    parameterScenario();
    poolPolicyScenario();
    mirrorScenario();
    scenario<SpaceUnit>();
    scenario<RacingVehicle>();
    std::cout << "PASS vfx_ecs_presentation_test (two genres, attaches, events, generation and reset)\n";
}
