// SpectraFX graph-v1 host routing: conservative GPU subset or lossless CPU.
// No FPS/RTS imports, RHI mock or real device required for these checks.
#include "engine/fx/SpectraGraphRuntime.h"
#include "engine/bake/MeshDistanceGrid.h"
#include "engine/fx/SpectraSdfBinary.h"
#include "engine/fx/SpectraVolumeGraph.h"
#include "engine/fx/SpectraStaticSphereCollision.h"
#include "engine/fx/SpectraTerrainCollision.h"
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
    {
        auto authored=graph();authored.set("version",Json::number(int64_t(2)));
        auto* output=authored.find("nodes")->at(2).find("type");
        *output=Json::string("VolumetricParticleOutput");
        authored.find("nodes")->at(2).set("properties",Json::parse(
            R"({"shape":"sphere","density":2,"steps":12,"depthAware":true})"));
        spectra::volume::PresentationGraph compiled;
        require(spectra::volume::compilePresentationGraph(authored,Json::object(),compiled,errors)&&
                compiled.particles&&!compiled.volume&&compiled.particleMaterial.enabled&&
                compiled.particleMaterial.depthAware,"volumetric particle graph must retain its GPU material");
        auto invalid=authored;
        invalid.find("nodes")->at(2).find("properties")->set("steps",Json::number(int64_t(1)));
        require(!spectra::volume::compilePresentationGraph(invalid,Json::object(),compiled,errors),
                "invalid volumetric ray-step budget must fail validation");
    }

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

    auto display=basic;
    display.emitters[0].blendSpace=fx::FxBlendSpace::Display;
    const auto displayEligibility=sp::inspectGraphGpuEligibility(display,options);
    require(!displayEligibility.eligible && displayEligibility.reason.find("display-space")!=std::string::npos,
            "display-space request must not silently use linear GPU blending");

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

    alternate=basic;
    alternate.emitters[0].drag={0,0};
    auto& sphereCollision=alternate.emitters[0].collision;
    sphereCollision.enabled=true;sphereCollision.ground=false;
    sphereCollision.staticSphereRadius=1;sphereCollision.staticSphereCenter={2,0,0};
    sphereCollision.radius=0.5f;
    const auto sphereEligibility=sp::inspectGraphGpuEligibility(alternate,options);
    require(sphereEligibility.eligible&&sphereEligibility.gpu.staticSphereRadius==1&&
            sphereEligibility.gpu.staticSphereCenter.x==2,"analytic sphere GPU eligibility");
    sphereCollision.scene=true;
    require(!sp::inspectGraphGpuEligibility(alternate,options).eligible,"host scene collision remains CPU");
    sphereCollision.scene=false;sphereCollision.lifeLoss=0.1f;
    require(!sp::inspectGraphGpuEligibility(alternate,options).eligible,"unsupported collision life loss rejected");
    {
        render::float3 end{5,0,0},velocity{60,0,0};
        require(fx::resolveStaticSphere({-1,0,0},end,velocity,{2,0,0},1.25f,1,0)&&
                std::abs(end.x-0.749f)<0.002f&&velocity.x<0,
                "analytic sphere swept response");
    }

    {
        auto volume=basic;auto& e=volume.emitters[0];
        e.drag={0,0};e.collision.enabled=true;e.collision.ground=false;
        auto& sdf=e.collision.staticSdf;
        sdf.origin={-2,-2,-2};sdf.dimension=5;sdf.spacing=1;
        for(unsigned z=0;z<5;++z)for(unsigned y=0;y<5;++y)for(unsigned x=0;x<5;++x)
            sdf.samples.push_back(float(y)-2);
        require(sdf.valid(),"valid static SDF volume");
        auto eligible=sp::inspectGraphGpuEligibility(volume,options);
        require(eligible.eligible&&eligible.gpu.staticSdf.samples.size()==125,"SDF graph GPU eligibility");
        fx::EffectDef decoded;
        require(fx::parseEffect(fx::effectToJson(volume),"sdf",decoded,errors)&&
                decoded.emitters[0].collision.staticSdf.samples==sdf.samples,"SDF roundtrip");
        render::float3 pos{0,-1,0},vel{0,-20,0};
        require(fx::resolveStaticSdf(sdf,{0,1,0},pos,vel,.25f,1,0)&&
                std::abs(pos.y-.25f)<.01f&&vel.y>0,"sampled SDF sweep");
        // Expanded dense lattice keeps the same JSON and GPU source ABI.
        sdf.dimension=32;sdf.spacing=.125f;sdf.origin={-2,-2,-2};sdf.samples.clear();
        for(unsigned z=0;z<32;++z)for(unsigned y=0;y<32;++y)for(unsigned x=0;x<32;++x)
            sdf.samples.push_back(float(y)*sdf.spacing-2);
        require(sdf.valid()&&sdf.samples.size()==32768,"32 cubed SDF valid");
        float distance=0;render::float3 gradient{};
        require(sdf.sample({0,.25f,0},distance,gradient)&&
                std::abs(distance-.25f)<1e-5f&&std::abs(gradient.y-1)<1e-5f,
                "32 cubed SDF interpolation");
        eligible=sp::inspectGraphGpuEligibility(volume,options);
        require(eligible.eligible&&eligible.gpu.staticSdf.samples.size()==32768,
                "32 cubed GPU eligibility");
        require(fx::parseEffect(fx::effectToJson(volume),"sdf32",decoded,errors)&&
                decoded.emitters[0].collision.staticSdf.samples==sdf.samples,
                "32 cubed SDF roundtrip");
        std::string bytes,error;
        require(fx::encodeSdfBinary(sdf,bytes)&&bytes.size()==131108,"dense SDF binary size");
        fx::StaticSdfVolume restored;
        require(fx::decodeSdfBinary(bytes,restored,&error)&&restored.samples==sdf.samples&&
                restored.dimension==32&&restored.spacing==sdf.spacing,"lossless SDF binary roundtrip");
        auto corrupt=bytes;corrupt[40]^=1;
        require(!fx::decodeSdfBinary(corrupt,restored,&error)&&restored.samples==sdf.samples,
                "SDF corrupt checksum and atomic output");
        require(!fx::decodeSdfBinary(std::string_view(bytes).substr(0,bytes.size()-1),restored),
                "SDF truncated rejected");
        corrupt=bytes;corrupt[8]=33;
        require(!fx::decodeSdfBinary(corrupt,restored),"SDF oversized dimension rejected");
        corrupt=bytes;corrupt[4]=2;
        require(!fx::decodeSdfBinary(corrupt,restored),"SDF version rejected");
        sdf.samples.pop_back();
        require(!sp::inspectGraphGpuEligibility(volume,options).eligible,"invalid SDF rejected");
        require(!fx::encodeSdfBinary(sdf,bytes)&&bytes.size()==131108,"invalid encode preserves output");
        // High-resolution dense data is binary-only, with exact samples
        // and a bounded external reference in the authored JSON.
        for(uint32_t n:{64u,128u}){
            sdf.dimension=n;sdf.origin={-2,-2,-2};sdf.spacing=.0625f;
            sdf.samples.resize(size_t(n)*n*n);
            for(uint32_t z=0;z<n;++z)for(uint32_t y=0;y<n;++y)for(uint32_t x=0;x<n;++x)
                sdf.samples[(size_t(z)*n+y)*n+x]=float(y)*sdf.spacing-2;
            require(sdf.valid()&&fx::encodeSdfBinary(sdf,bytes)&&
                    bytes.size()==36+4*size_t(n)*n*n&&uint8_t(bytes[4])==2,
                    "high-resolution SDFB v2 encode");
            require(fx::decodeSdfBinary(bytes,restored,&error)&&restored.sameSamples(sdf),
                    "high-resolution exact binary roundtrip");
            float distance=0;render::float3 gradient{};
            require(restored.sample({0,.25f,0},distance,gradient)&&
                    std::abs(distance-.25f)<1e-5f&&std::abs(gradient.y-1)<1e-5f,
                    "high-resolution native sampling");
            require(sp::inspectGraphGpuEligibility(volume,options).eligible,
                    "high-resolution GPU schema eligibility");
            bool exportRejected=false;
            try{(void)fx::effectToJson(volume);}catch(const std::runtime_error&){exportRejected=true;}
            require(exportRejected,"unlinked dense export rejected");
            e.collision.staticSdfAsset="content/test.sdfbin";
            const auto external=fx::effectToJson(volume);
            require(external.dump(-1).size()<4096,"compact effect serialization");
            require(!fx::parseEffect(external,"noReader",decoded,errors),"external requires reader");
            require(fx::parseEffect(external,"highResolution",decoded,errors,
                [&](const std::string& path,std::string& out){
                    if(path!="content/test.sdfbin")return false;out=bytes;return true;
                })&&decoded.emitters[0].collision.staticSdf.sameSamples(sdf),
                "external high-resolution effect roundtrip");
            e.collision.staticSdfAsset.clear();
        }
        sdf.dimension=129;
        require(!sdf.valid()&&!fx::encodeSdfBinary(sdf,bytes),
                "oversized native volume rejected");
        auto inlineTooLarge=Json::parse(R"({"emitters":[{"burst":1,"collision":{"ground":false,"staticSdf":{"origin":[0,0,0],"spacing":1,"dimension":64,"samples":[]}}}]})");
        require(!fx::parseEffect(inlineTooLarge,"oversizedInline",decoded,errors),
                "high-resolution inline JSON rejected");
    }
    {
        sp::ComputeEmitterDesc desc;
        desc.depthCollision=true;
        require(!sp::validComputeEmitter(desc),"depth must not silently fall back to CPU");
        desc.backend=sp::ComputePreference::RequireGpu;
        require(sp::validComputeEmitter(desc),"explicit native depth config");
        desc.staticSphereRadius=1;
        require(!sp::validComputeEmitter(desc),"incompatible depth and sphere rejected");
        sp::DepthCollisionSnapshot invalid;
        require(!invalid.valid(),"unpublished depth rejected");
    }
    {
        std::vector<uint8_t> bytes(sp::kGpuCollisionEventBytes,0);
        auto put=[&](size_t i,uint32_t v){std::memcpy(bytes.data()+4*i,&v,4);};
        auto putFloat=[&](size_t i,float f){uint32_t v;std::memcpy(&v,&f,4);put(i,v);};
        put(1,1);putFloat(2,2);putFloat(5,1);put(8,2);put(9,7);
        sp::GpuCollisionBatch batch;
        require(sp::decodeGpuCollisionBatch(bytes,8,batch)&&batch.events.size()==1&&
                batch.events[0].normal.x==1&&batch.events[0].kind==sp::GpuCollisionKind::Sdf,
                "bounded collision record decode");
        fx::SpectraFxEmitterEventRouter router;
        require(sp::publishGpuCollisionBatch(batch,1,router)==1&&router.pending()==1,
                "visual event router publication");
        put(9,8);
        require(!sp::decodeGpuCollisionBatch(bytes,8,batch),"out of range slot rejected");
    }
    {
        auto parent=basic;
        auto& emitter=parent.emitters[0];
        emitter.drag={0,0};
        emitter.collision.enabled=true;emitter.collision.ground=false;
        emitter.collision.staticSphereRadius=1;
        emitter.subEmitters.push_back({fx::FxSubEvent::Collision,"sparks",1});
        auto poolOptions=options;poolOptions.routePoolCollisionChildren=true;
        std::string child;
        require(!sp::inspectGraphGpuEligibility(parent,poolOptions).eligible,
                "dedicated graph does not silently strip collision child");
        auto eligibility=sp::inspectPoolGraphGpuEligibility(parent,poolOptions,&child);
        require(eligibility.eligible&&eligibility.gpu.reportCollisionEvents&&child=="sparks",
                "pooled collision child opt-in");
        require(parent.emitters[0].subEmitters.size()==1,"CPU fallback retains authored child");
        emitter.subEmitters[0].chance=.5f;
        require(!sp::inspectPoolGraphGpuEligibility(parent,poolOptions,&child).eligible&&child.empty(),
                "unsupported chance remains CPU");
    }
    {
        const std::vector<render::float3> p{
            {-1,-1,-1},{1,-1,-1},{1,1,-1},{-1,1,-1},
            {-1,-1,1},{1,-1,1},{1,1,1},{-1,1,1}};
        const std::vector<uint32_t> indices{
            0,2,1,0,3,2,4,5,6,4,6,7,0,1,5,0,5,4,
            3,7,6,3,6,2,0,4,7,0,7,3,1,2,6,1,6,5};
        bake::MeshDistanceGrid grid;std::string error;
        require(bake::bakeMeshDistanceGrid(p,indices,7,render::mat4{},grid,error),"closed cube SDF bake");
        const auto at=[&](unsigned x,unsigned y,unsigned z){return grid.samples[(z*7+y)*7+x];};
        require(std::abs(at(3,3,3)+1.f)<1e-4f&&std::abs(at(6,3,3)-.5f)<1e-4f,
                "signed cube distances");
        auto open=indices;open.resize(open.size()-3);
        require(!bake::bakeMeshDistanceGrid(p,open,7,render::mat4{},grid,error)&&grid.samples.size()==343,
                "open mesh rejected without mutating previous result");
        auto reversed=indices;std::swap(reversed[0],reversed[1]);
        require(!bake::bakeMeshDistanceGrid(p,reversed,7,render::mat4{},grid,error),
                "inconsistent winding rejected");
        require(bake::bakeMeshDistanceGrid(p,indices,32,render::mat4{},grid,error)&&
                grid.samples.size()==32768,"32 cubed closed cube bake");
        require(grid.samples[(16u*32+16)*32+16]<-.9f,"high resolution signed interior");
        require(!bake::bakeMeshDistanceGrid(p,indices,129,render::mat4{},grid,error)&&
                grid.samples.size()==32768,"unsupported dimension rejected without mutation");
        const auto previous=grid.samples;
        bake::MeshDistanceQueryStats stats;
        require(!bake::bakeMeshDistanceGrid(p,indices,32,render::mat4{},grid,error,&stats,10)&&
                error.find("work budget")!=std::string::npos&&grid.samples==previous&&stats.work()==10,
                "measured traversal budget preserves previous result");
        // Compare the accelerated grid to the original exhaustive double
        // distance and solid-angle winding on a nested reversed shell.
        auto shell=p;auto shellIndices=indices;
        for(auto v:p)shell.push_back(v*.5f);
        for(size_t i=0;i<indices.size();i+=3){
            shellIndices.push_back(indices[i]+8);shellIndices.push_back(indices[i+2]+8);
            shellIndices.push_back(indices[i+1]+8);
        }
        require(bake::bakeMeshDistanceGrid(shell,shellIndices,11,render::mat4{},grid,error,&stats),
                "nested shell bake");
        using namespace bake::distance_detail;
        std::vector<Tri> reference;
        for(size_t i=0;i<shellIndices.size();i+=3)
            reference.push_back({d(shell[shellIndices[i]]),d(shell[shellIndices[i+1]]),d(shell[shellIndices[i+2]])});
        const double eps=std::max(1e-9,double(grid.spacing)*1e-7);
        for(uint32_t z=0;z<11;++z)for(uint32_t y=0;y<11;++y)for(uint32_t x=0;x<11;++x){
            const auto q=d(grid.origin)+D3{double(x)*grid.spacing,double(y)*grid.spacing,double(z)*grid.spacing};
            double best=std::numeric_limits<double>::infinity(),angle=0;
            for(const auto& t:reference)best=std::min(best,distance2(q,t));
            double expected=std::sqrt(best);
            if(expected<=eps)expected=0;
            else{
                for(const auto& t:reference)angle+=solidAngle(q,t);
                if(std::abs(angle/12.56637061435917295385)>0.5)expected=-expected;
            }
            require(std::abs(grid.samples[(z*11+y)*11+x]-expected)<1e-5,
                    "accelerated distance and winding match exhaustive reference");
        }
        require(grid.samples[(5*11+5)*11+5]>.49f,"opposite winding creates cavity");
        // 64 separated closed cubes = 768 triangles; exceeds old 512
        // triangle limit for a 32 cubed lattice without changing resolution.
        std::vector<render::float3> many;
        std::vector<uint32_t> manyIndices;
        for(unsigned z=0;z<4;++z)for(unsigned y=0;y<4;++y)for(unsigned x=0;x<4;++x){
            const uint32_t offset=uint32_t(many.size());
            const render::float3 center{float(x)*2,float(y)*2,float(z)*2};
            for(auto v:p)many.push_back(center+v*.4f);
            for(auto index:indices)manyIndices.push_back(offset+index);
        }
        require(bake::bakeMeshDistanceGrid(many,manyIndices,32,render::mat4{},grid,error,&stats)&&
                grid.samples.size()==32768&&stats.work()<=bake::kMeshDistanceMaxWork,
                "768 triangle accelerated bake above old product cap");
        require(stats.distanceTests+stats.rayTests+stats.solidAngleTests<2ull*32768*768,
                "accelerated triangle query count below exhaustive reference");
        require(std::any_of(grid.samples.begin(),grid.samples.end(),[](float f){return f<-.1f;}),
                "disjoint solids have negative interior");
    }
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
    {
        auto registry=std::make_shared<fx::SceneCollisionRegistry>();
        require(registry->bind(1,fx::makeSceneSdfSweep([](render::float3 p,float& d){
            d=render::length(p)-1.f;return true;
        })),"sphere SDF");
        fx::SceneCollisionHit hit;
        fx::SceneCollisionStats stats;
        require(registry->sweep({{-3,0,0},{3,0,0},0.25f},hit,stats)&&
                std::abs(hit.fraction-(1.75f/6.f))<0.002f&&hit.normal.x<-.99f,
                "swept sphere prevents tunnelling");
        fx::ParticleSystem ps;
        require(ps.init(nullptr),"scene collision CPU init");
        fx::EffectDef effect;
        std::vector<std::string> errors;
        require(fx::parseEffect(Json::parse(R"({"emitters":[{"burst":1,"speed":60,"life":2,"size":0.2,"spread":0,"gravity":[0,0,0],"collision":{"ground":false,"scene":true,"bounce":1}}]})"),"sweep",effect,errors),"authored scene collision");
        require(effect.emitters[0].collision.scene,"scene flag");
        const auto roundtrip=fx::effectToJson(effect);
        fx::EffectDef restored;
        require(fx::parseEffect(roundtrip,"roundtrip",restored,errors)&&restored.emitters[0].collision.scene,"scene collision roundtrip");
        ps.setSceneCollisionRegistry(registry);
        const int id=ps.define("sweep",effect);
        require(id>=0&&ps.play(id,{-3,0,0},{1,0,0}).valid(),"scene collision particle birth");
        ps.update(.1f);
        std::vector<fx::FxParticleInfo> particles;
        ps.snapshot(particles);
        require(particles.size()==1&&particles[0].pos.x<0&&particles[0].vel.x<0&&
                ps.sceneCollisionStats().hits==1,"actual CPU particle swept collision");
        ps.shutdown();
    }
    {
        auto plane=fx::makeTerrainSweep([](render::float3 p,fx::TerrainCollisionSample& s){
            s.clearance=p.y;s.up={0,1,0};return true;
        });
        fx::SceneCollisionHit hit;
        require(plane({{0,3,0},{0,-3,0},0.5f},hit)&&
                std::abs(hit.fraction-2.5f/6.f)<0.001f,"swept terrain radius");
        auto planet=fx::makeTerrainSweep([](render::float3 p,fx::TerrainCollisionSample& s){
            const float length=render::length(p);
            if(length<=1e-5f)return false;
            s.clearance=length-10.f;s.up=p*(1.f/length);return true;
        });
        require(planet({{13,0,0},{8,0,0},0.5f},hit)&&
                std::abs(hit.fraction-0.5f)<0.001f&&hit.normal.x>.99f,"radial terrain sweep");
    }
    {
        namespace vol=sp::volume;
        vol::Config config;config.dimension=8;config.origin={-1,-1,-1};config.cellSize=.25f;
        config.pressureIterations=6;config.gravity={0,0,0};config.smokeDecay=0;
        config.temperatureDecay=0;config.fuelDecay=0;config.velocityDecay=0;
        config.burnRate=6;config.ignition=.1f;
        vol::GasGrid volume;
        require(volume.init(config)&&volume.cells().size()==512,"voxel gas allocation");
        vol::Source source;source.position={0,0,0};source.radius=.7f;
        source.smoke=2;source.temperature=4;source.fuel=3;source.acceleration={0,0,0};
        require(volume.inject(source,.5f),"volumetric impulse");
        const auto initial=volume.stats();const auto bytes=volume.memoryBytes();
        require(initial.density>0&&initial.temperature>0&&initial.fuel>0,"injected real density and fuel");
        require(volume.step()&&volume.tick()==1&&volume.memoryBytes()==bytes,"fixed gas step");
        const auto after=volume.stats();
        require(after.fuel<initial.fuel&&after.density>initial.density&&after.temperature>initial.temperature,"combustion produces smoke and heat");
        vol::Cell sample;
        require(volume.sample({0,0,0},sample)&&sample.density>0,"trilinear volume sample");
        require(!volume.sample({3,0,0},sample),"outside volume is not clamped by public sampler");
        vol::RayResult ray,clipped;
        require(volume.march({0,0,-3},{0,0,1},8,vol::Material{},ray)&&ray.alpha>0&&ray.radiance.x>0,"volumetric ray integrates real field");
        require(volume.march({0,0,-3},{0,0,1},1,vol::Material{},clipped)&&clipped.alpha==0,"opaque distance clips volume");
        fx::StaticSdfVolume obstacle;obstacle.dimension=5;obstacle.origin={-2,-2,-2};obstacle.spacing=1;
        for(unsigned z=0;z<5;++z)for(unsigned y=0;y<5;++y)for(unsigned x=0;x<5;++x)
            obstacle.samples.push_back(float(x)-2);
        require(volume.setObstacle(obstacle)&&volume.stats().solidCells==256,"existing SDF becomes solid mask");
        require(volume.setSources({source})&&volume.step(),"continuous volumetric source");
        for(size_t i=0;i<volume.cells().size();++i)if(volume.solidMask()[i])
            require(volume.cells()[i].density==0&&volume.cells()[i].fuel==0,"solid cells remain empty");
        require(volume.setGravity({-9.81f,0,0})&&volume.setWind({0,0,1})&&volume.step(),"world-vector gravity and wind");
        require(volume.stats().peakSpeed<=config.maxVelocity+.001f,"bounded velocity");
        auto invalid=config;invalid.dimension=vol::kMaxDimension+1;
        require(!volume.init(invalid)&&volume.cells().size()==512,"invalid config preserves existing grid");
        vol::Asset asset;asset.simulation=config;asset.sources={source};
        asset.material.smokeDetail=.8f;asset.material.emberIntensity=1.5f;
        vol::AuthoredLight fireLight;fireLight.position={.2f,.4f,0};fireLight.color={1,.3f,.08f};
        fireLight.radius=3;fireLight.intensity=12;asset.lights.push_back(fireLight);
        vol::Asset decoded;std::string why;
        require(vol::readAsset(vol::writeAsset(asset),decoded,why)&&decoded.sources.size()==1&&
                std::abs(decoded.material.smokeDetail-.8f)<.0001f&&
                std::abs(decoded.material.emberIntensity-1.5f)<.0001f&&decoded.lights.size()==1&&
                std::abs(decoded.lights[0].intensity-12)<.0001f,"volume asset material and light roundtrip");
        vol::GasGrid second;require(vol::initAsset(decoded,second,why)&&second.step()&&second.stats().density>0,"native asset runtime");
        auto malformed=vol::writeAsset(asset);malformed.set("unknown",Json::number(int64_t(1)));
        require(!vol::readAsset(malformed,decoded,why)&&decoded.sources.size()==1,"strict asset rejection");
        malformed=vol::writeAsset(asset);malformed.find("material")->set("emberIntensity",Json::number(11.0));
        require(!vol::readAsset(malformed,decoded,why)&&decoded.sources.size()==1,"invalid ember intensity rejected");
        malformed=vol::writeAsset(asset);malformed.find("lights")->itemsMut()[0].set("radius",Json::number(-1.0));
        require(!vol::readAsset(malformed,decoded,why)&&decoded.lights.size()==1,"invalid volume light rejected atomically");
        volume.clear();require(volume.stats().density==0&&volume.tick()==0&&volume.stats().solidCells==256,"clear transient field retains obstacle");
    }
    {
        namespace vol=sp::volume;
        auto pure=Json::parse(R"({"version":2,"nodes":[],"links":[],"outputs":[]})");
        std::string error;vol::PresentationGraph compiled;
        auto trail=pure;
        require(vol::appendPreset(trail,"trail",error)&&vol::compilePresentationGraph(trail,Json::object(),compiled,errors)&&
                compiled.volumeAsset.attachment=="trail"&&compiled.volumeAsset.sources.size()==1,"moving trail graph preset");
        require(vol::appendPreset(pure,"fire",error)&&vol::compilePresentationGraph(pure,Json::object(),compiled,errors)&&
                compiled.volume&&!compiled.particles&&compiled.volumeAsset.sources.size()==1,"volume graph compiler");
        require(!fx::compileSpectraGraph(pure,Json::object(),basic,errors),"legacy particle compiler rejects volume");
        auto broken=pure;broken.set("links",Json::array());
        require(!vol::compilePresentationGraph(broken,Json::object(),compiled,errors)&&compiled.volume,"disconnected graph rejected atomically");
        auto hybrid=graph();hybrid.set("version",Json::number(int64_t(2)));
        // Existing v1 fixture already has a compatible exec chain and output.
        if(!hybrid.has("outputs")){auto out=Json::array();out.push(Json::string("output"));hybrid.set("outputs",out);}
        require(vol::appendPreset(hybrid,"smoke",error)&&vol::compilePresentationGraph(hybrid,Json::object(),compiled,errors)&&
                compiled.volume&&compiled.particles&&!compiled.particleEffect.emitters.empty(),"hybrid graph compiler");
        Json cross=Json::object();cross.set("from",Json::string("spawn"));cross.set("to",Json::string("volume_source"));
        hybrid.find("links")->push(cross);
        require(!vol::compilePresentationGraph(hybrid,Json::object(),compiled,errors),"cross-domain execution rejected");
    }
    {
        namespace vol=sp::volume;
        auto explosion=Json::parse(R"({"version":2,"nodes":[],"links":[],"outputs":[]})");
        std::string error;vol::PresentationGraph compiled;
        require(vol::appendPreset(explosion,"explosion",error)&&vol::compilePresentationGraph(explosion,Json::object(),compiled,errors),"explosion graph");
        require(compiled.volumeAsset.impulse&&compiled.volumeAsset.sources.empty()&&
            compiled.volumeAsset.impulse->radialAcceleration>0,"one-shot radial explosion");
        vol::Asset restored;
        require(vol::readAsset(vol::writeAsset(compiled.volumeAsset),restored,error)&&restored.impulse&&
            restored.impulse->radialAcceleration==35,"impulse asset roundtrip");
        restored.simulation.dimension=8;restored.simulation.cellSize=.5f;
        vol::GasGrid grid;require(vol::initAsset(restored,grid,error)&&grid.stats().density>0,"CPU impulse injection");
        auto invalid=vol::writeAsset(restored);invalid.set("impulseSeconds",Json::number(2.0));
        require(!vol::readAsset(invalid,restored,error),"invalid impulse duration rejected");
    }
    std::cout<<"PASS SpectraFX graph GPU eligibility, strict CPU preservation and host fallback\n";
}
