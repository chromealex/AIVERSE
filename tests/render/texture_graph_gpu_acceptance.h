// Exercise the production native preview owner with a real RHI, not a simulated backend.
#pragma once
#include "engine/render/graph/TextureGraphGpuOwner.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <stdexcept>
#include <thread>

namespace aiverse::tests {
inline int textureGraphGpuAcceptance(gfx::Device& dev,gfx::Backend backend){
    render::RenderDeviceLifetime lifetime(dev);
    render::texture_graph::TextureGraphGpuOwner owner;
    const std::string project="texture-graph-rhi-fixture";
    const char* expected=backend==gfx::Backend::Metal?"metal":"vulkan";
    graph_gpu_profile::Capture profile;
    auto require=[](bool ok,const char* what){if(!ok)throw std::runtime_error(what);};
    auto frame=[&]{
        dev.beginFrame(64,64);
        try{owner.tick(dev,project,&profile);}catch(...){dev.endFrame();throw;}
        dev.endFrame();
        // Deliberate test-only barrier; owner itself remains asynchronous.
        require(dev.waitIdle(),"GPU frame idle failed");
        // Test owns this Device exclusively: only completed real scopes enter the capture.
        for(const auto& timing:dev.takeGpuScopeTimings())profile.complete(timing,dev.frameSerial());
        profile.expire(dev.frameSerial());
    };
    auto drain=[&]{
        for(int i=0;i<128;++i){
            if(!owner.status().getBool("busy"))return;
            frame();
        }
        throw std::runtime_error("request did not drain within 128 frames");
    };
    auto cancel=[&](const std::string& request){
        Json p=Json::object();p.set("request",Json::string(request));
        return owner.cancel(p,project);
    };
    auto request=Json::parse(R"JSON({"graph":{"version":1,"size":256,"nodes":[
        {"id":"h","op":"gradient","inputs":[],"properties":{"axis":"x"}},
        {"id":"m","op":"constant","inputs":[],"properties":{"value":0.25}},
        {"id":"n","op":"normal","inputs":["h"],"properties":{"strength":4}}
    ],"outputs":{"height":"h","metallic":"m","normal":"n","roughness":"h"}},
    "bakeSet":true,"requireGpu":true,"timeoutMs":60000})JSON");
    int result=1;
    try{
        require(dev.info().backend==backend&&dev.supportsCompute()&&dev.gpuHealthy(),"requested compute backend unavailable");
        std::printf("TextureGraph RHI: %s / %s\n",expected,dev.info().name.c_str());
        profile.start(dev.info(),project);
        auto complete=[&]{
            const auto id=owner.start(request,project).getStr("request");
            drain();
            const auto s=owner.status();
            std::printf("request=%s %s\n",id.c_str(),s.dump(-1).c_str());
            require(s.getStr("request")==id&&s.getStr("state")=="ready","set did not complete");
            require(s.getStr("backend")==expected,"GPU fallback or backend mismatch");
            require(s["completedOutputs"].toDouble()==4&&s["totalOutputs"].toDouble()==4,"missing set outputs");
            require(s["sharedScratchHits"].toDouble()==1,"normal did not reuse scalar scratch");
            require(s["reusedOutputs"].toDouble()==1,"duplicate output not reused");
            require(s["encodedPasses"].toDouble()==1,"normal producer was dispatched again");
            require(s["readbackBlocks"].toDouble()==4&&s["receivedBytes"].toDouble()==262144,"multi-block readback missing");
            require(s["retainedBufferBytes"].toDouble()<=524288,"scratch exceeds two-buffer budget");
            const auto& outputs=s["outputs"];
            require(outputs.size()==4,"missing backend provenance");
            for(const auto& row:outputs.items())require(row.getStr("backend")==expected,"output did not pass GPU CPU-reference acceptance");
            require(!s.getBool("published"),"unexpected publication");
            return id;
        };
        const auto first=complete();
        const auto second=complete(); // new immutable request; previous pixels cannot be accepted
        require(first!=second,"request identity reused");
        bool staleRejected=false;
        try{cancel(first);}catch(const std::exception&){staleRejected=true;}
        require(staleRejected,"stale cancel accepted");
        auto queued=owner.start(request,project).getStr("request");
        require(cancel(queued).getStr("state")=="cancelled","queued cancellation failed");
        require(!owner.status().getBool("busy"),"queued cancel left busy owner");
        auto submitted=owner.start(request,project).getStr("request");
        frame();
        require(owner.status().getStr("state")=="pending"&&owner.status().getBool("busy"),"no real submitted request to cancel");
        const auto cancelled=cancel(submitted);
        require(cancelled.getStr("state")=="cancelled"&&cancelled.getBool("draining"),"submitted cancellation did not retain retirement");
        drain();
        require(owner.status().getStr("state")=="cancelled"&&owner.status()["completedOutputs"].toDouble()==0,"cancelled results revived");
        auto shortRequest=request;shortRequest.set("timeoutMs",Json::number(int64_t(1000)));
        owner.start(shortRequest,project);
        std::this_thread::sleep_for(std::chrono::milliseconds(1100));
        frame();drain();
        require(owner.status().getStr("state")=="failed"&&owner.status()["completedOutputs"].toDouble()==0,"queued deadline failed");
        complete(); // new request after cancellation and timeout
        // Exercise the public native material API with the same RHI and owner.
        // A missing lease must not enqueue work or replace the last result.
        auto residentGraph=Json::parse(R"JSON({"version":1,"size":32,"nodes":[
            {"id":"h","op":"gradient","inputs":[],"properties":{"axis":"x"}},
            {"id":"n","op":"normal","inputs":["h"],"properties":{"strength":4}}
        ],"outputs":{"ao":"h","height":"h","normal":"n"}})JSON");
        using render::texture_graph::ResidentAcceptance;
        using render::texture_graph::ResidentMaterialOptions;
        ResidentMaterialOptions opts;opts.acceptance=ResidentAcceptance::FenceOnly;opts.mipmaps=false;
        bool missingLeaseRejected=false;
        try{owner.startResidentMaterial(residentGraph,Json::object(),project,{},opts);}
        catch(const std::exception&){missingLeaseRejected=true;}
        require(missingLeaseRejected,"native resident request accepted missing lease");
        require(dev.supportsStorageBufferTextureCopy(),"GPU texture copy support unavailable");
        auto startResident=[&]{
            return owner.startResidentMaterial(residentGraph,Json::object(),project,lifetime.lease(),opts).getStr("request");
        };
        const auto resident=startResident();
        frame();
        require(owner.status().getStr("state")=="pending"&&!owner.status().getBool("gpuTextureSetReady"),
            "resident set exposed before fence acceptance");
        drain();
        const auto ready=owner.status();
        require(ready.getStr("request")==resident&&ready.getStr("state")=="ready"&&ready.getStr("backend")==expected,
            "native resident request not GPU-ready");
        require(ready.getBool("gpuTextureSetReady")&&!ready.getBool("fileExportAvailable")&&
            ready.getStr("gpuTextureAcceptance")=="fence_only","resident acceptance/export state incorrect");
        require(ready["completedOutputs"].toDouble()==2&&ready["totalOutputs"].toDouble()==2&&
            ready["gpuTextureAccepted"].toDouble()==2,"nonbindable map was not filtered or material output missing");
        require(ready["gpuTextureValidationBytes"].toDouble()==0&&ready["receivedBytes"].toDouble()==0&&
            ready["readbackBlocks"].toDouble()==0,"fence-only request reported readback");
        cancel(resident);drain();
        const auto residentCancelled=startResident();
        frame();
        require(cancel(residentCancelled).getBool("draining"),"resident submission did not retain fence retirement");
        drain();
        require(owner.status().getStr("state")=="cancelled"&&!owner.status().getBool("gpuTextureSetReady"),
            "cancelled resident set became publishable");
        auto inc=Json::parse(R"JSON({"graph":{"version":1,"size":32,"nodes":[
            {"id":"a","op":"constant","inputs":[],"properties":{"value":0.25}},
            {"id":"b","op":"constant","inputs":[],"properties":{"value":0.5}},
            {"id":"n","op":"normal","inputs":["a"],"properties":{"strength":4}}
        ],"outputs":{"height":"a","ao":"b","normal":"n"}},"bakeSet":true,"materialized":true,"incremental":true})JSON");
        auto runIncremental=[&](const Json& input,int dispatched,int reused){
            owner.start(input,project,lifetime.lease());drain();const auto s=owner.status();
            require(s.getStr("state")=="ready"&&s.getStr("backend")==expected,"incremental GPU request failed");
            require(s["materializedNodePasses"].toDouble()==dispatched&&s["incrementalReusedNodes"].toDouble()==reused,"incorrect incremental reuse");
        };
        runIncremental(inc,3,0);runIncremental(inc,0,3);
        auto modified=Json::parse(inc.dump(-1).replace(inc.dump(-1).find("0.25"),4,"0.75"));
        runIncremental(modified,2,1);
        cancel(owner.status().getStr("request"));drain();frame(); // cold fused layout
        auto fusedInc=Json::parse(R"JSON({"graph":{"version":1,"size":32,"nodes":[
            {"id":"a","op":"constant","inputs":[],"properties":{"value":0.25}},
            {"id":"i","op":"invert","inputs":["a"]},
            {"id":"b","op":"constant","inputs":[],"properties":{"value":0.5}},
            {"id":"n","op":"normal","inputs":["i"],"properties":{"strength":4}}
        ],"outputs":{"height":"i","ao":"b","normal":"n"}},"bakeSet":true,"materialized":true,"incremental":true,"fusePointwise":true})JSON");
        runIncremental(fusedInc,3,0);runIncremental(fusedInc,0,3);
        const auto fusedStatus=owner.status();
        require(fusedStatus["fusionElidedPasses"].toDouble()==1&&fusedStatus["incrementalReusedPasses"].toDouble()==3,"combined fusion/incremental counters incorrect");
        auto fusedModified=Json::parse(fusedInc.dump(-1).replace(fusedInc.dump(-1).find("0.25"),4,"0.75"));
        runIncremental(fusedModified,2,1);
        cancel(owner.status().getStr("request"));drain();
        const auto low=owner.enqueue(inc,project,lifetime.lease(),0).getStr("ticket");
        const auto high=owner.enqueue(modified,project,lifetime.lease(),3).getStr("ticket");
        frame();require(owner.queueStatus().getStr("activeTicket")==high,"priority promotion failed");drain();
        require(owner.queueStatus().getStr("activeTicket")==high,"ready result overwritten");
        owner.releaseTicket(high,project,true);drain();frame();
        require(owner.queueStatus().getStr("activeTicket")==low,"next ticket not promoted");drain();
        owner.releaseTicket(low,project,true);drain();
        require(owner.queueStatus().getStr("activeTicket").empty(),"queue result not released");
        require(dev.gpuHealthy(),"device unhealthy");
        const auto report=profile.report();
        std::set<std::string> scopes;
        for(const auto& row:report["samples"].items()){
            require(view::parseAppearanceIdentity(row.getStr("request"))!=0&&!row.getStr("output").empty(),
                "GPU profile lost texture request identity");
            require(row.getStr("view")=="0"&&row["milliseconds"].toDouble()>0,"invalid texture scope sample");
            scopes.insert(row.getStr("scope"));
        }
        std::printf("TextureGraph GPU scope evidence: %s\n",report.dump(-1).c_str());
        if(const auto* strict=std::getenv("AIVERSE_TEXTURE_GRAPH_REQUIRE_GPU_TIMINGS");strict&&std::string_view(strict)=="1"){
            require(scopes.contains("texture-graph-fused-dispatch")&&scopes.contains("texture-graph-materialized-dispatch")&&
                scopes.contains("texture-graph-texture-copy"),"strict GPU scope evidence missing");
        }else if(scopes.empty())std::printf("GPU scope timing unavailable; no GPU timing evidence\n");
        result=0;
    }catch(const std::exception& e){std::fprintf(stderr,"FAIL texture graph RHI: %s\n",e.what());}
    if(dev.waitIdle())owner.shutdownAfterIdle(dev);
    else{std::fprintf(stderr,"FAIL texture graph RHI: shutdown idle failed\n");result=1;}
    if(!result)std::printf("PASS texture graph %s: parity, shared scratch, multi-block readback, cancellation, timeout, recovery and native fence-only resident lifecycle\n",expected);
    return result;
}
} // namespace aiverse::tests
