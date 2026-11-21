// SpectraFX graph-v1 host routing: conservative GPU subset or lossless CPU.
// No FPS/RTS imports, RHI mock or real device required for these checks.
#include "engine/fx/SpectraGraphRuntime.h"
#include <iostream>
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

    std::cout<<"PASS SpectraFX graph GPU eligibility, strict CPU preservation and host fallback\n";
}
