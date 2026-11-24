// SpectraFX graph-v1 host routing: conservative GPU subset or lossless CPU.
// No FPS/RTS imports, RHI mock or real device required for these checks.
#include "engine/fx/SpectraGraphRuntime.h"
#include "engine/fx/SpectraGpuPoolWorld.h"
#include "engine/world/SpectraPhysisSnapshot.h"
#include "engine/world/SpectraWaterSnapshot.h"
#include "engine/world/SpectraLiveFields.h"
#include "engine/world/SpectraTerrainProviders.h"
#include <array>
#include <cmath>
#include <iostream>
#include <string_view>
#include <stdexcept>
#include <string>
#include <vector>

using namespace aiverse;
namespace sp = aiverse::spectra;

static void require(bool ok,const char* why) {
    if(!ok)throw std::runtime_error(why);
}

static Json graph(bool extra=false) {
    Json g=Json::parse(R"JSON({
      "version":1,
      "nodes":[
        {"id":"spawn","type":"Spawn","properties":{"burst":5}},
        {"id":"initialize","type":"Initialize","properties":{
            "life":0.8,"speed":2,"size":0.35,"spread":0,"gravity":[0,-5,0],"drag":0.2}},
        {"id":"output","type":"SpriteOutput","properties":{
            "texture":"builtin:soft","blend":"additive","align":"camera"}}
      ],
      "links":[{"from":"spawn","to":"initialize"},{"from":"initialize","to":"output"}],
      "outputs":["output"]
    })JSON");
    if(extra)
        g.find("nodes")->at(1).find("properties")->set("noise",
            Json::parse(R"({"strength":2,"frequency":1})"));
    return g;
}

int main() {
    fx::EffectDef basic;
    std::vector<std::string> errors;
    require(fx::compileSpectraGraph(graph(),basic,errors),"baseline graph must compile");

    sp::GraphRuntimeOptions options;
    options.capacity=128;
    auto eligibility=sp::inspectGraphGpuEligibility(basic,options);
    require(eligibility.eligible, "simple sprite point graph should pass GPU eligibility");
    require(eligibility.gpu.burst==5 && eligibility.gpu.capacity==128,
            "GPU emitter config must mirror compiled graph");
    options.gpuIndirect=true;
    eligibility=sp::inspectGraphGpuEligibility(basic,options);
    require(eligibility.eligible && eligibility.gpu.gpuIndirect,
            "opt-in GPU-authored indirect preference was not forwarded");
    options.gpuIndirect=false;

    auto alternate=basic;
    alternate.emitters[0].spread=30;
    require(!sp::inspectGraphGpuEligibility(alternate,options).eligible,
            "spread would silently change particle directions");
    alternate=basic;
    alternate.emitters[0].life={0.6f,1.f};
    require(!sp::inspectGraphGpuEligibility(alternate,options).eligible,
            "random lifetime silently mapped to GPU constant");
    alternate=basic;
    alternate.emitters[0].texture="builtin:spark";
    require(!sp::inspectGraphGpuEligibility(alternate,options).eligible,
            "texture change silently replaced by soft sprite");
    alternate=basic;
    alternate.emitters[0].blend=fx::FxBlend::Alpha;
    require(!sp::inspectGraphGpuEligibility(alternate,options).eligible,
            "alpha blend incorrectly classified as additive");
    alternate=basic;
    alternate.emitters[0].color={{0,{1,0,0,1}},{1,{1,0,0,0}}};
    require(!sp::inspectGraphGpuEligibility(alternate,options).eligible,
            "custom color gradient silently dropped");
    alternate=basic;
    alternate.emitters[0].trail.enabled=true;
    require(!sp::inspectGraphGpuEligibility(alternate,options).eligible,
            "GPU accepted unsupported ribbons/trails");
    alternate=basic;
    alternate.emitters[0].noise.strength={1,1};
    require(!sp::inspectGraphGpuEligibility(alternate,options).eligible,
            "GPU accepted unsupported noise");
    alternate=basic;
    alternate.emitters[0].subEmitters.push_back({fx::FxSubEvent::Death,"sparks",1});
    require(!sp::inspectGraphGpuEligibility(alternate,options).eligible,
            "GPU accepted sub-emitter callbacks");
    alternate=basic;
    alternate.forceFields.push_back({});
    require(!sp::inspectGraphGpuEligibility(alternate,options).eligible,
            "GPU accepted unimplemented forceFields");
    alternate=basic;
    alternate.emitters.push_back(basic.emitters[0]);
    require(!sp::inspectGraphGpuEligibility(alternate,options).eligible,
            "multi-emitter graph was silently flattened");

    options.capacity=4; // burst requires five slots
    require(!sp::inspectGraphGpuEligibility(basic,options).eligible,
            "unsafe GPU pool size accepted");
    options.capacity=128;
    fx::EffectDef complex;
    require(fx::compileSpectraGraph(graph(true),complex,errors),
            "complex CPU graph should still compile");
    require(!sp::inspectGraphGpuEligibility(complex,options).eligible,
            "noise graph should not attempt GPU compute");

    sp::GraphRuntime runtime;
    require(runtime.initGraph(nullptr,graph(),Json::object(),options),
            "eligible graph must fall back to CPU without a graphics Device");
    require(runtime.backend()==sp::ComputeBackend::Cpu && !runtime.gpuActive(),
            "null device was not routed to CPU");
    require(runtime.burstAt({0,2,0}),"graph CPU burst failed");
    require(runtime.step(0.1f),"graph CPU step failed");
    std::vector<fx::FxParticleInfo> snapshot;
    runtime.cpuFallback().snapshot(snapshot);
    require(snapshot.size()==5,"graph CPU burst lost particles");
    require(snapshot[0].pos.y>1.f,"graph CPU particle movement incorrect");
    runtime.clear();
    runtime.cpuFallback().snapshot(snapshot);
    require(snapshot.empty(),"graph clear should drop live CPU particles");
    runtime.shutdown();

    require(runtime.initEffect(nullptr,complex,options),
            "complex graph must retain its original full CPU effects");
    require(runtime.backend()==sp::ComputeBackend::Cpu,"complex graph used GPU");
    require(runtime.burstAt({0,0,0}),"complex graph CPU spawn failed");
    runtime.step(0.05f);
    runtime.cpuFallback().snapshot(snapshot);
    require(snapshot.size()==5,"complex graph lost CPU emission");
    runtime.shutdown();

    options.preference=sp::ComputePreference::RequireGpu;
    require(!runtime.initGraph(nullptr,graph(),Json::object(),options),
            "RequireGpu silently degraded to CPU");
    require(!runtime.reason().empty(),"RequireGpu must explain failure");
    require(!runtime.initEffect(nullptr,complex,options),
            "RequireGpu accepted unsupported noise effect");
    require(runtime.reason().find("noise")!=std::string::npos ||
            runtime.reason().find("module")!=std::string::npos ||
            runtime.reason().find("effect")!=std::string::npos,
            "RequireGpu missing unsupported graph diagnostics");

    options.preference=sp::ComputePreference::CpuOnly;
    require(runtime.initEffect(nullptr,basic,options),
            "CpuOnly mode must accept simple graph");
    require(runtime.backend()==sp::ComputeBackend::Cpu,"CpuOnly mode ignored");
    runtime.shutdown();

    // Graph-v2 read-only Data Interface: a producer's sampled visual wind
    // is snapshotted into an existing param slot exactly once per burst.
    {
        Json authored=Json::parse(R"JSON({
          "version":2,
          "instanceParameters":{"wind_lift":{"slot":0,"default":1,"min":0,"max":20}},
          "dataInterfaces":{"wind_lift":{"provider":"weather","channel":"up",
            "scale":1.5,"offset":0.5,"fallback":3,"required":true}},
          "nodes":[
            {"id":"spawn","type":"Spawn","properties":{"burst":1}},
            {"id":"init","type":"Initialize","properties":{
              "life":2,"speed":0,"size":0.2,"gravity":[0,0,0]}},
            {"id":"lift","type":"DataInterface","properties":{"name":"wind_lift"}},
            {"id":"program","type":"ParticleProgramModule","properties":{
              "version":1,"spawn":[{"target":"velocity.y",
                "value":{"$runtime":"lift"}}]}},
            {"id":"out","type":"SpriteOutput","properties":{
              "texture":"builtin:soft","blend":"additive"}}
          ],
          "links":[{"from":"spawn","to":"init"},{"from":"init","to":"program"},
                   {"from":"program","to":"out"}],
          "outputs":["out"]
        })JSON");
        fx::EffectDef graphEffect;
        std::vector<std::string> diagnostics;
        require(fx::compileSpectraGraph(authored,graphEffect,diagnostics),
                "typed DataInterface graph should compile to existing particle VM");
        require(!graphEffect.emitters[0].particleProgram.isNull(),
                "DataInterface node must lower to particle VM, not constant-fold");

        sp::InstanceParameterTable definitions;
        require(sp::loadInstanceParameterTable(authored["instanceParameters"],
                                               definitions,diagnostics),
                "instance parameter table valid");
        sp::DataInterfaceBindings bindings;
        require(sp::loadDataInterfaceBindings(authored["dataInterfaces"],
                                               definitions,bindings,diagnostics) &&
                bindings.size()==1,
                "Data Interface schema is recognized");
        sp::DataInterfaceRegistry provider;
        require(provider.bind("weather",[](std::string_view channel,
                                            render::float3 p,float& value) {
            if(channel!="up")return false;
            value=p.x+2.0f;
            return true;
        }),"register weather presentation snapshot provider");
        std::array<float,8> oldValues{};
        oldValues[0]=7;
        std::array<float,8> sampled=oldValues;
        std::string dataError;
        sp::DataInterfaceSampleStats dataStats;
        require(sp::sampleDataInterfaces(bindings,definitions,&provider,
                 {0,0,0},oldValues,sampled,dataError,&dataStats) &&
                 std::abs(sampled[0]-3.5f)<1e-5f &&
                 dataStats.resolved==1,
                "transformed environment scalar assigned to named parameter slot");
        oldValues[0]=19;
        sampled=oldValues;
        require(!sp::sampleDataInterfaces(bindings,definitions,nullptr,
                 {0,0,0},oldValues,sampled,dataError,&dataStats) &&
                 sampled==oldValues && dataStats.failedRequired==1,
                "missing required source blocks burst without changing parameters");
        Json optional=authored;
        optional.find("dataInterfaces")->find("wind_lift")->set("required",Json::fromBool(false));
        require(sp::loadDataInterfaceBindings(optional["dataInterfaces"],
                definitions,bindings,diagnostics) &&
                sp::sampleDataInterfaces(bindings,definitions,nullptr,
                {0,0,0},oldValues,sampled,dataError,&dataStats) &&
                sampled[0]==3 && dataStats.fallbacks==1,
                "optional missing Data Interface uses authored fallback");

        auto malformed=authored;
        malformed.find("dataInterfaces")->find("wind_lift")->set(
            "provider",Json::string("../unsafe"));
        require(!fx::compileSpectraGraph(malformed,graphEffect,diagnostics),
                "unsafe data provider identifier rejected at graph compile time");
        malformed=authored;
        malformed.find("dataInterfaces")->set("other",authored["dataInterfaces"]["wind_lift"]);
        require(!fx::compileSpectraGraph(malformed,graphEffect,diagnostics),
                "undeclared instance parameter cannot receive Data Interface binding");
        malformed=authored;
        malformed.find("dataInterfaces")->find("wind_lift")->set(
            "fallback",Json::number(40.0));
        require(!fx::compileSpectraGraph(malformed,graphEffect,diagnostics),
                "out-of-range fallback rejected before runtime allocation");
        malformed=authored;
        malformed.find("dataInterfaces")->erase("wind_lift");
        require(!fx::compileSpectraGraph(malformed,graphEffect,diagnostics),
                "DataInterface node requires matching authoring declaration");

        sp::GraphRuntime windRuntime;
        sp::GraphRuntimeOptions cpuOptions;
        cpuOptions.preference=sp::ComputePreference::CpuOnly;
        require(windRuntime.initGraph(nullptr,authored,Json::object(),cpuOptions),
                "Data Interface graph initializes with headless CPU backend");
        require(windRuntime.dataInterfaceBindings().size()==1,
                "graph runtime stores authoring bindings");
        require(!windRuntime.burstAt({0,0,0}),
                "required Data Interface rejects an unbound runtime burst");
        windRuntime.setDataInterfaceRegistry(&provider);
        require(windRuntime.burstAt({0,0,0}) &&
                windRuntime.dataInterfaceStats().resolved==1,
                "Data Interface sampled during first particle burst");
        windRuntime.step(0.1f);
        std::vector<fx::FxParticleInfo> visualParticles;
        windRuntime.cpuFallback().snapshot(visualParticles);
        require(visualParticles.size()==1 && visualParticles[0].pos.y>0.1f,
                "first burst gets dynamic wind into particle velocity");
        require(windRuntime.burstAt({4,0,0}),"second burst samples new position");
        windRuntime.step(0.1f);
        windRuntime.cpuFallback().snapshot(visualParticles);
        require(visualParticles.size()==2,
                "second burst preserves first particles in CPU backend");
        float leftY=-1,rightY=-1;
        for(const auto& p:visualParticles) {
            if(p.pos.x<2)leftY=p.pos.y;
            else rightY=p.pos.y;
        }
        require(leftY>0 && rightY>0 && rightY>leftY/2,
                "new position samples a distinct provider value without recompile");
        windRuntime.shutdown();
        require(windRuntime.dataInterfaceBindings().empty(),
                "runtime teardown removes all graph-owned bindings");

        sp::BatchWorld shared;
        require(shared.init(nullptr),"shared CPU world starts headless");
        require(windRuntime.initGraph(shared,authored,Json::object(),cpuOptions),
                "Data Interface graph initializes in shared CPU world");
        windRuntime.setDataInterfaceRegistry(&provider);
        require(windRuntime.burstAt({0,0,0}),"shared world reads Data Interface");
        shared.update(0.1f);
        shared.particles().snapshot(visualParticles);
        require(visualParticles.size()==1,
                "shared batching preserves Data Interface-driven particle");
        windRuntime.shutdown();
        shared.shutdown();

        // Pool graph declarations retain Data Interfaces even when physical
        // effect definitions are deduplicated. Reject before birth, and keep
        // per-source base values separate from sampled birth values.
        sp::GpuPoolWorld pool;
        require(pool.init(nullptr),"headless pooled world");
        require(pool.defineGraph("required_weather",authored,Json::object(),cpuOptions),
                "pooled weather graph");
        auto fallbackGraph=authored;
        fallbackGraph.find("dataInterfaces")->find("wind_lift")->set("required",Json::fromBool(false));
        require(pool.defineGraph("optional_weather",fallbackGraph,Json::object(),cpuOptions),
                "pooled optional graph");
        const auto recovery=pool.exportRecoveryManifest();
        require(recovery.effects.size()==2 && recovery.effects[0].dataBindings.size()==1 &&
                recovery.effects[1].dataBindings.size()==1,"recovery preserves provider declarations");
        render::Camera camera{{0,0,0},8.f,0.5f,0.5f};
        const auto view=fx::SpectraFxView::fromCamera(camera,camera.proj(1.f,false)*camera.view(),128,128);
        require(pool.beginPresentationFrame(view),"begin pooled data frame");
        require(!pool.burst("required_weather",{0,0,0}) &&
                pool.dataInterfaceStats().failedRequired==1,"missing required pooled data rejects birth");
        require(pool.burst("optional_weather",{0,0,0}) &&
                pool.dataInterfaceStats().fallbacks==1,"optional pooled data uses authored fallback");
        require(pool.step(0.1f),"step pooled fallback");
        pool.endPresentationFrame();
        require(pool.setDataInterfaceRegistry(&provider),"bind pooled provider");
        const auto source=pool.createSource("required_weather");
        require(source.valid() && pool.setSourceNamedParameter(source,"wind_lift",19.f),"pooled source defaults");
        require(pool.beginPresentationFrame(view),"begin pooled source frame");
        require(!pool.setDataInterfaceRegistry(nullptr),"registry cannot change midframe");
        require(pool.emitSource(source,{4,0,0}) && pool.dataInterfaceStats().resolved==1,
                "pooled source samples real data before spawn");
        require(pool.step(0.1f),"step pooled provider");
        pool.endPresentationFrame();
        pool.cpuBatchWorld().particles().snapshot(visualParticles);
        require(visualParticles.size()==2,"required failed birth had no particles");
        float sourceY=-1;
        for(const auto& p:visualParticles)if(p.pos.x>2)sourceY=p.pos.y;
        require(sourceY>0.1f,"sampled scalar reaches pooled CPU VM");
        pool.shutdown();

    }

    // Real PHYSIS committed snapshot, not a synthetic weather callback.
    {
        World climate;
        world::SpectraPhysisRegion region;
        region.origin={-50,0,-50};
        region.spacing=100;
        region.dimensions={2,1,2};
        require(!world::SpectraPhysisSnapshot::capture(climate,region),"uninitialized PHYSIS unavailable");
        physis::Settings settings;
        settings.originX=-1;settings.originZ=-1;
        settings.width=2;settings.height=2;settings.cellSizeMeters=100;
        physis::initialize(climate,settings);
        physis::initializeDisasters(climate);
        require(physis::ignite(climate,-1,-1),"PHYSIS fire fixture");
        const auto e=climate.create();
        physis::WindZone zone;
        zone.center={Fixed::fromInt(-50),Fixed{},Fixed::fromInt(-50)};
        zone.radiusMeters=Fixed::fromInt(40);
        zone.velocity={Fixed::fromInt(15),Fixed{},Fixed{}};
        climate.add<physis::WindZone>(e,zone);
        auto snap=world::SpectraPhysisSnapshot::capture(climate,region);
        require(snap && snap->size()==4 && snap->tick()==0,"committed weather region");
        sp::DataInterfaceRegistry registry;
        require(world::publishSpectraPhysis(registry,snap),"publish immutable PHYSIS snapshot");
        float value=-1;
        require(registry.sample("physis","wind_x",{-50,0,-50},value) &&
                std::abs(value-15)<1e-5f,"negative-coordinate wind");
        require(registry.sample("physis","fire",{-50,0,-50},value) && value==1,
                "real PHYSIS hazard");
        require(!registry.sample("physis","wind_x",{1000,0,0},value),"outside coverage unavailable");
        require(!registry.sample("physis","unknown",{-50,0,-50},value),"unknown channel unavailable");
        auto invalid=region;invalid.dimensions={64,64,64};
        require(!world::SpectraPhysisSnapshot::capture(climate,invalid),"bounded region budget");
        climate=World{};
        snap.reset();
        require(registry.sample("physis","fire",{-50,0,-50},value) && value==1,
                "snapshot survives authoritative world destruction");
        require(world::publishSpectraPhysis(registry,{}) &&
                !registry.sample("physis","fire",{-50,0,-50},value),"unload removes provider");
    }

    {
        World water;
        lib::nereid::WaterBodyComponent body;
        body.footprint=lib::nereid::WaterFootprint::Infinite;
        body.surfaceY=Fixed::fromInt(5);body.bottomY=Fixed::fromInt(-5);
        water.add<lib::nereid::WaterBodyComponent>(water.create(),body);
        world::SpectraFieldRegion region;region.origin={2,3,0};region.spacing=1;
        world::SpectraLiveFields live;
        auto first=live.publish(water,1,7,region,world::SpectraWaterGeometry::Plane);
        require(first && live.publish(water,1,7,region,world::SpectraWaterGeometry::Plane)==first,
                "same committed tick and region reuse immutable registry");
        auto replacement=live.publish(water,2,7,region,world::SpectraWaterGeometry::Plane);
        require(replacement && replacement!=first,"same tick replacement refreshes registry");
        float liveValue=-1;
        require(replacement->sample("nereid","depth",{2,3,0},liveValue)&&liveValue==2,
                "live registry contains canonical water depth");
        require(!replacement->sample("physis","wind_x",{2,3,0},liveValue),"absent PHYSIS stays unavailable");
        auto invalidRegion=region;invalidRegion.spacing=0;
        require(!live.publish(water,2,8,invalidRegion,world::SpectraWaterGeometry::Plane)&&!live.current(),
                "invalid publication clears current registry");
        require(first->sample("nereid","depth",{2,3,0},liveValue)&&liveValue==2,
                "old leased registry remains immutable");
        auto plane=world::SpectraWaterSnapshot::capture(water,region,world::SpectraWaterGeometry::Plane,7);
        sp::DataInterfaceRegistry registry;
        require(plane && plane->tick()==7 && world::publishSpectraWater(registry,plane),"water snapshot");
        float value=-1;
        require(registry.sample("nereid","depth",{2,3,0},value)&&value==2,"plane depth");
        require(!registry.sample("nereid","depth",{2,4,0},value),"outside snapshot");
        lib::nereid::WaterSphereComponent sphere;
        sphere.surfaceRadius=Fixed::fromInt(10);sphere.floorRadius=Fixed::fromInt(5);
        water.add<lib::nereid::WaterSphereComponent>(water.create(),sphere);
        region.origin={8,0,0};
        auto radial=world::SpectraWaterSnapshot::capture(water,region,world::SpectraWaterGeometry::Sphere,8);
        require(radial && world::publishSpectraWater(registry,radial),"sphere publication");
        require(registry.sample("nereid","depth",{8,0,0},value)&&value==2,"radial depth");
        require(registry.sample("nereid","up_x",{8,0,0},value)&&value==1,"radial up");
        const Vec3 anchor{Fixed::fromInt(8192),Fixed{},Fixed{}};
        sphere.center=anchor;
        water.add<lib::nereid::WaterSphereComponent>(water.create(),sphere);
        auto other=sphere;other.center={Fixed::fromInt(12288),Fixed{},Fixed{}};
        other.priority=100;
        water.add<lib::nereid::WaterSphereComponent>(water.create(),other);
        auto local=live.publish(water,3,9,region,world::SpectraWaterGeometry::Sphere,Fixed{},anchor,anchor);
        require(local && local->sample("nereid","depth",{8,0,0},value)&&value==2,
                "body-local depth ignores other high-priority planets");
        require(local->sample("nereid","surface_x",{8,0,0},value)&&value==10,
                "water surface point translated to body-local space");
        require(world::publishSpectraWater(registry,{})&&!registry.sample("nereid","depth",{8,0,0},value),"water unload");
        auto height=std::make_shared<terraweave::AuthoredWorldHeightSnapshot>(10);
        height->setTile(world::Cell{0,0,0},{2,{200,200,200,200}});
        require(world::publishSpectraTerrain(registry,height),"terrain publication");
        require(registry.sample("terraweave","clearance",{2,5,3},value)&&value==3,"terrain clearance");
        require(!registry.sample("terraweave","height",{-5,0,0},value),"missing terrain tile");
        world::SpectraPlanetTerrain planet;
        planet.baseRadiusMeters=10;planet.terrain.baseHeightCm=200;planet.terrain.reliefCm=0;
        require(world::publishSpectraPlanetTerrain(registry,planet),"planet terrain");
        require(registry.sample("terraweave","clearance",{15,0,0},value)&&value==3,"radial terrain clearance");
        require(registry.sample("terraweave","up_x",{15,0,0},value)&&value==1,"radial terrain up");
        require(!registry.sample("terraweave","height",{0,0,0},value),"undefined planet centre");
    }
    std::cout<<"PASS SpectraFX graph GPU eligibility, strict CPU preservation and host fallback\n";
}
