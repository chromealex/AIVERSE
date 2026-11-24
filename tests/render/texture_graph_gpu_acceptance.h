// Exercise the production native preview owner with a real RHI, not a simulated backend.
#pragma once
#include "engine/render/graph/TextureGraphGpuOwner.h"
#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <thread>

namespace aiverse::tests {
inline int textureGraphGpuAcceptance(gfx::Device& dev,gfx::Backend backend){
    render::texture_graph::TextureGraphGpuOwner owner;
    const std::string project="texture-graph-rhi-fixture";
    const char* expected=backend==gfx::Backend::Metal?"metal":"vulkan";
    auto require=[](bool ok,const char* what){if(!ok)throw std::runtime_error(what);};
    auto frame=[&]{
        dev.beginFrame(64,64);
        try{owner.tick(dev,project);}catch(...){dev.endFrame();throw;}
        dev.endFrame();
        // Deliberate test-only barrier; owner itself remains asynchronous.
        require(dev.waitIdle(),"GPU frame idle failed");
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
        require(dev.gpuHealthy(),"device unhealthy");
        result=0;
    }catch(const std::exception& e){std::fprintf(stderr,"FAIL texture graph RHI: %s\n",e.what());}
    if(dev.waitIdle())owner.shutdownAfterIdle(dev);
    else{std::fprintf(stderr,"FAIL texture graph RHI: shutdown idle failed\n");result=1;}
    if(!result)std::printf("PASS texture graph %s: parity, shared scratch, multi-block readback, cancellation, timeout and recovery\n",expected);
    return result;
}
} // namespace aiverse::tests
