// SpectraFX graph-v1 host routing: conservative GPU subset or lossless CPU.
// No FPS/RTS imports, RHI mock or real device required for these checks.
#include "engine/fx/SpectraGraphRuntime.h"
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
    }

    std::cout<<"PASS SpectraFX graph GPU eligibility, strict CPU preservation and host fallback\n";
}
