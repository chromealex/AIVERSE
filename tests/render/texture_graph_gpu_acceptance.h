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
inline int textureGraphGpuAcceptance(gfx::Device& dev,gfx::Backend backend,bool offscreen=false){
    render::RenderDeviceLifetime lifetime(dev);
    render::texture_graph::TextureGraphGpuOwner owner;
    const std::string project="texture-graph-rhi-fixture";
    const char* expected=backend==gfx::Backend::Metal?"metal":"vulkan";
    graph_gpu_profile::Capture profile;
    auto require=[](bool ok,const char* what){if(!ok)throw std::runtime_error(what);};
    auto beginFrame=[&]{if(offscreen)require(dev.beginOffscreenFrame(),"offscreen frame rejected");else dev.beginFrame(64,64);};
    auto frame=[&]{
        beginFrame();
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
        // Vulkan scope contract: no outside-frame start, no nesting,
        // no completion until actual submission; an abandoned query is invalid.
        if(backend==gfx::Backend::Vulkan){
            constexpr uint64_t firstTag=0x12345678,abandonedTag=0x12345679;
            require(!dev.beginGpuScope(firstTag),"scope began outside frame");
            beginFrame();
            const bool supported=dev.beginGpuScope(firstTag);
            if(supported){
                const bool nested=dev.beginGpuScope(abandonedTag);
                dev.endGpuScope();
                const bool abandoned=dev.beginGpuScope(abandonedTag);
                const auto premature=dev.takeGpuScopeTimings();
                dev.endFrame();require(dev.waitIdle(),"scope contract fence failed");
                require(!nested&&abandoned&&premature.empty(),"scope nesting or premature delivery");
                bool firstSeen=false,abandonedSeen=false;
                for(const auto& timing:dev.takeGpuScopeTimings()){
                    if(timing.tag==firstTag)firstSeen=true;
                    if(timing.tag==abandonedTag){abandonedSeen=true;require(!timing.valid,"unbalanced scope reported valid");}
                }
                require(firstSeen&&abandonedSeen&&dev.takeGpuScopeTimings().empty(),"scope completion identity or duplicate delivery");
            }else{
                dev.endFrame();require(dev.waitIdle(),"unsupported scope frame failed");
                std::printf("Vulkan queue does not admit timestamp scopes\n");
            }
        }
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
        // Exercise neighborhood filters, restricted arithmetic and embedded image
        // source on the production CPU-reference-checked materialized GPU path.
        auto filters=Json::parse(R"JSON({"graph":{"version":1,"size":64,"nodes":[
            {"id":"g","op":"gradient","inputs":[],"properties":{"axis":"x"}},
            {"id":"c","op":"constant","inputs":[],"properties":{"value":0.75}},
            {"id":"b","op":"blur","inputs":["g"],"properties":{"radius":2}},
            {"id":"e","op":"erosion","inputs":["b"],"properties":{"radius":1}},
            {"id":"w","op":"warp","inputs":["e","c","g"],"properties":{"amount":4}},
            {"id":"d","op":"distort","inputs":["w","c"],"properties":{"amount":3}},
            {"id":"f","op":"formula","inputs":["d","c"],"properties":{"expression":"a b * 0.5 +"}},
            {"id":"i","op":"image","inputs":[],"properties":{"width":2,"height":2,"rgb":"ff000000ff000000ffffffff"}}
        ],"outputs":{"height":"f","color":"i"}},"requireGpu":true,"bakeSet":true,"fusePointwise":true})JSON");
        const auto filterId=owner.start(filters,project).getStr("request");drain();
        const auto fs=owner.status();
        require(fs.getStr("request")==filterId&&fs.getStr("state")=="ready"&&fs.getStr("backend")==expected,"native filter / formula / image GPU acceptance failed");
        require(fs["completedOutputs"].toDouble()==2,"missing filter or image result");
        for(const auto& row:fs["outputs"].items())require(row.getStr("backend")==expected,"filter or image CPU fallback");
        std::printf("TextureGraph filter/formula/image parity accepted: %s\n",expected);
        // Larger immutable RGB input must use data, not source-code literals.
        auto buffered=Json::parse(R"JSON({"graph":{"version":1,"size":64,"nodes":[{"id":"i","op":"image","inputs":[],"properties":{"width":64,"height":64}}],"outputs":{"color":"i"}},"requireGpu":true,"bakeSet":true,"materialized":true})JSON");
        std::string rgb;rgb.reserve(64*64*6);for(int i=0;i<64*64;++i)rgb+=i%2?"ff0000":"0000ff";
        buffered.member("graph").member("nodes").at(0).member("properties").set("rgb",Json::string(rgb));
        auto bufferedProgram=render::texture_graph::compile(buffered["graph"]);
        auto bufferedSources=render::texture_graph::gpu::compileMaterializedDag(bufferedProgram,{0},64);
        require(bufferedSources.imageSourceBytes==64*64*4&&bufferedSources.nodes[0].msl.find("switch(index)")==std::string::npos&&bufferedSources.nodes[0].msl.find("input0[index]")!=std::string::npos,"RGB source not budgeted or still shader literals");
        const auto bufferedId=owner.start(buffered,project).getStr("request");drain();
        const auto bs=owner.status();
        require(bs.getStr("request")==bufferedId&&bs.getStr("state")=="ready"&&bs.getStr("backend")==expected,"buffer-backed RGB GPU acceptance failed");
        // Native host resolver supplies original external pixels with a small graph.
        auto external=Json::parse(R"JSON({"graph":{"version":1,"size":64,"nodes":[{"id":"asset","op":"imageAsset","inputs":[],"properties":{"asset":"content/source.png","revision":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"}}],"outputs":{"color":"asset"}},"requireGpu":true,"bakeSet":true,"materialized":true})JSON");
        const render::texture_graph::ImageResolver resolveImage=[](const std::string& path,const std::string& revision,size_t budget){
            if(path!="content/source.png"||revision!=std::string(64,'a')||budget<512u*512u*4u)throw std::invalid_argument("fixture source mismatch");
            render::texture_graph::ImageSource source;source.width=source.height=512;
            source.rgb.resize(512u*512u);for(size_t i=0;i<source.rgb.size();++i)source.rgb[i]=(i/512)%16<8?0xff0000u:0x0000ffu;
            return source;
        };
        bool missingResolver=false;try{(void)render::texture_graph::compile(external["graph"]);}catch(const std::invalid_argument&){missingResolver=true;}
        require(missingResolver,"external image silently resolved without host");
        const auto externalProgram=render::texture_graph::compile(external["graph"],Json::object(),resolveImage);
        const auto externalSources=render::texture_graph::gpu::compileMaterializedDag(externalProgram,{0},64);
        require(externalSources.imageSourceBytes==512u*512u*4u,"external source bytes not admitted");
        const auto externalId=owner.start(external,project,lifetime.lease(),resolveImage).getStr("request");drain();
        const auto externalStatus=owner.status();
        require(externalStatus.getStr("request")==externalId&&externalStatus.getStr("state")=="ready"&&externalStatus.getStr("backend")==expected,"external image GPU parity failed");
        // External channel nodes use the same immutable raster budget and a scalar GPU slot.
        auto channel=external;
        channel.member("graph").member("nodes").at(0).set("op",Json::string("imageAssetChannel"));
        channel.member("graph").member("nodes").at(0).member("properties").set("channel",Json::string("alpha"));
        channel.member("graph").member("nodes").at(0).member("properties").set("revision",Json::string(std::string(64,'c')));
        channel.member("graph").set("outputs",Json::parse(R"({"mask":"asset"})"));
        const render::texture_graph::ImageResolver rgbaSource=[](const std::string&,const std::string&,size_t){
            render::texture_graph::ImageSource s;s.width=2;s.height=1;s.hasAlpha=true;s.rgb={0x00112233u,0x80445566u};return s;
        };
        const auto cp=render::texture_graph::compile(channel["graph"],Json::object(),rgbaSource);
        const auto ci=render::texture_graph::evaluate(cp,0);
        require(ci.rgba[0]==0&&ci.rgba[32*4]==128&&ci.rgba[32*4+3]==255,"external alpha mask CPU mismatch");
        const auto channelId=owner.start(channel,project,lifetime.lease(),rgbaSource).getStr("request");drain();
        const auto channelStatus=owner.status();
        require(channelStatus.getStr("request")==channelId&&channelStatus.getStr("state")=="ready"&&channelStatus.getStr("backend")==expected,"external alpha mask GPU parity failed");
        // HDR source stays floating-point until explicit input mapping.
        auto hdrAsset=external;
        hdrAsset.member("graph").member("nodes").at(0).member("properties").set("revision",Json::string(std::string(64,'d')));
        hdrAsset.member("graph").member("nodes").at(0).member("properties").set("hdrExposure",Json::number(int64_t(-1)));
        const render::texture_graph::ImageResolver hdrSource=[](const std::string&,const std::string&,size_t){
            render::texture_graph::ImageSource s;s.width=2;s.height=1;s.hasAlpha=true;s.rgba32f={4.f,2.f,-1.f,1.f,0.5f,0.25f,8.f,0.5f};return s;
        };
        const auto hp=render::texture_graph::compile(hdrAsset["graph"],Json::object(),hdrSource);
        const auto hi=render::texture_graph::evaluate(hp,0);
        require(render::texture_graph::imageSourceBytes(hp)==32&&hi.rgba[0]==170&&hi.rgba[1]==128&&hi.rgba[2]==0,"HDR exposure/mapping CPU mismatch");
        const auto hs=render::texture_graph::gpu::compileMaterializedDag(hp,{0},64);
        require(hs.imageSourceBytes==32&&hs.nodes[0].msl.find("float4 raw=input0[index]")!=std::string::npos,"HDR source GPU buffer layout mismatch");
        const auto hdrId=owner.start(hdrAsset,project,lifetime.lease(),hdrSource).getStr("request");drain();
        const auto hdrStatus=owner.status();
        require(hdrStatus.getStr("request")==hdrId&&hdrStatus.getStr("state")=="ready"&&hdrStatus.getStr("backend")==expected,"HDR source GPU parity failed");
        // NPOT mip chain, trilinear interpolation and exact decoded storage accounting.
        auto mipAsset=external;
        auto& mipProps=mipAsset.member("graph").member("nodes").at(0).member("properties");
        mipProps.set("revision",Json::string(std::string(64,'e')));
        mipProps.set("mipMode",Json::string("explicit"));mipProps.set("mipLevel",Json::number(1.5));mipProps.set("filter",Json::string("trilinear"));
        const render::texture_graph::ImageResolver mipResolver=[](const std::string&,const std::string&,size_t){
            render::texture_graph::ImageSource s;s.width=3;s.height=1;s.hasAlpha=true;s.rgb={0xff000000u,0xff000000u,0xffff0000u};return s;
        };
        const auto mipProgram=render::texture_graph::compile(mipAsset["graph"],Json::object(),mipResolver);
        const auto mipImage=render::texture_graph::evaluate(mipProgram,0);
        require(render::texture_graph::imageSourceBytes(mipProgram)==24&&mipProgram.nodes[0].image->mips.size()==3&&mipImage.rgba[0]==64&&mipImage.rgba[63*4]==192,"NPOT trilinear mip CPU mismatch");
        const auto mipSources=render::texture_graph::gpu::compileMaterializedDag(mipProgram,{0},64);
        require(mipSources.imageSourceBytes==24&&mipSources.nodes[0].msl.find("mix(mipA,mipB,")!=std::string::npos,"mip chain GPU layout mismatch");
        const auto mipId=owner.start(mipAsset,project,lifetime.lease(),mipResolver).getStr("request");drain();
        const auto mipStatus=owner.status();
        require(mipStatus.getStr("request")==mipId&&mipStatus.getStr("state")=="ready"&&mipStatus.getStr("backend")==expected,"trilinear mip GPU parity failed");
        // Authored floor-halved NPOT mip layout must not be replaced by box-derived levels.
        auto authoredAsset=mipAsset;
        auto& authoredProps=authoredAsset.member("graph").member("nodes").at(0).member("properties");
        authoredProps.set("revision",Json::string(std::string(64,'f')));
        authoredProps.set("mipLevel",Json::number(1.0));
        const render::texture_graph::ImageResolver authoredResolver=[](const std::string&,const std::string&,size_t){
            render::texture_graph::ImageSource s;s.width=3;s.height=1;s.hasAlpha=true;s.authoredMips=true;
            s.mips={{3,1,0},{1,1,3}};s.rgb={0xffff0000u,0xffff0000u,0xffff0000u,0xff00ff00u};return s;
        };
        const auto authoredProgram=render::texture_graph::compile(authoredAsset["graph"],Json::object(),authoredResolver);
        const auto authoredImage=render::texture_graph::evaluate(authoredProgram,0);
        require(render::texture_graph::imageSourceBytes(authoredProgram)==16&&authoredImage.rgba[0]==0&&authoredImage.rgba[1]==255,"authored mip content was not preserved");
        const auto authoredId=owner.start(authoredAsset,project,lifetime.lease(),authoredResolver).getStr("request");drain();
        const auto authoredStatus=owner.status();
        require(authoredStatus.getStr("request")==authoredId&&authoredStatus.getStr("state")=="ready"&&authoredStatus.getStr("backend")==expected,"authored mip GPU parity failed");
        // Typed custom expressions: same validated instruction stream on CPU and GPU.
        auto computed=Json::parse(R"JSON({"graph":{"version":1,"size":64,"nodes":[
          {"id":"a","op":"constant","inputs":[],"properties":{"value":0.25}},
          {"id":"b","op":"constant","inputs":[],"properties":{"value":0.75}},
          {"id":"c","op":"constant","inputs":[],"properties":{"value":0.5}},
          {"id":"red","op":"color","inputs":[],"properties":{"value":[1,0,0]}},
          {"id":"blue","op":"color","inputs":[],"properties":{"value":[0,0,1]}},
          {"id":"s","op":"customScalar","inputs":["a","b","c"],"properties":{"code":"a b c mix p0 * p1 + sat","p0":0.5,"p1":0.25}},
          {"id":"rgb","op":"customColor","inputs":["red","blue","s"],"properties":{"red":"ar br c mix","green":"u p2 *","blue":"ab bb c mix","p2":0.5}}
        ],"outputs":{"height":"s","color":"rgb"}},"requireGpu":true,"bakeSet":true,"materialized":true,"fusePointwise":true})JSON");
        const auto customId=owner.start(computed,project,lifetime.lease()).getStr("request");drain();
        const auto customStatus=owner.status();
        require(customStatus.getStr("request")==customId&&customStatus.getStr("state")=="ready"&&customStatus.getStr("backend")==expected,"custom compute GPU parity failed");
        // Typed resource reads use materialized input fields, not fused point samples.
        auto resources=Json::parse(R"JSON({"graph":{"version":1,"size":32,"nodes":[
          {"id":"ramp","op":"gradient","inputs":[],"properties":{"axis":"x"}},
          {"id":"zero","op":"constant","inputs":[],"properties":{"value":0}},
          {"id":"red","op":"color","inputs":[],"properties":{"value":[1,0,0]}},
          {"id":"blue","op":"color","inputs":[],"properties":{"value":[0,0,1]}},
          {"id":"mirror","op":"customScalar","inputs":["ramp","zero","zero"],"properties":{"code":"1 u - v sampleA"}},
          {"id":"edge","op":"customScalar","inputs":["ramp","zero","zero"],"properties":{"code":"0 v sampleA","resourceFilter":"bilinear","resourceAddress":"repeat"}},
          {"id":"rgb","op":"customColor","inputs":["red","blue","mirror"],"properties":{"red":"u v sampleAr","green":"u v sampleC","blue":"u v sampleBb"}}
        ],"outputs":{"height":"mirror","mask":"edge","color":"rgb"}},"requireGpu":true,"bakeSet":true,"materialized":true,"fusePointwise":true})JSON");
        const auto resourceProgram=render::texture_graph::compile(resources["graph"]);
        const auto mirrorIndex=resourceProgram.outputs.at("height");
        const auto mirrorPixels=render::texture_graph::evaluate(resourceProgram,mirrorIndex);
        const auto edgePixels=render::texture_graph::evaluate(resourceProgram,resourceProgram.outputs.at("mask"));
        require(mirrorPixels.rgba[0]==251&&mirrorPixels.rgba[31*4]==4&&edgePixels.rgba[0]==128,"resource sampling CPU expected values");
        require(!render::texture_graph::gpu::fusionPointwise(resourceProgram.nodes[mirrorIndex]),"resource sampling incorrectly fused");
        auto invalidResource=resources["graph"];
        invalidResource.member("nodes").at(4).member("properties").set("code",Json::string("u v sampleAr"));
        bool badResource=false;try{(void)render::texture_graph::compile(invalidResource);}catch(const std::invalid_argument&){badResource=true;}
        require(badResource,"invalid scalar resource channel accepted");
        const auto resourceId=owner.start(resources,project,lifetime.lease()).getStr("request");drain();
        const auto resourceStatus=owner.status();
        require(resourceStatus.getStr("request")==resourceId&&resourceStatus.getStr("state")=="ready"&&resourceStatus.getStr("backend")==expected,"custom resource GPU parity failed");
        // Named declarations and independent float data sources reuse the typed slots.
        auto named=Json::parse(R"JSON({"graph":{"version":1,"size":32,"nodes":[
          {"id":"table","op":"bufferScalar","inputs":[],"properties":{"values":[0,0.25,0.5,1]}},
          {"id":"palette","op":"bufferColor","inputs":[],"properties":{"values":[[1,0,0],[0,1,0]]}},
          {"id":"zero","op":"constant","inputs":[],"properties":{"value":0}},
          {"id":"scaled","op":"customScalar","inputs":["table","zero","zero"],"properties":{"resources":{"curve":{"slot":0,"kind":"scalar"}},"uniforms":{"gain":{"type":"float","value":0.5}},"code":"u v sample:curve param:gain *"}},
          {"id":"rgb","op":"customColor","inputs":["palette","palette","scaled"],"properties":{"resources":{"palette":{"slot":0,"kind":"color"},"mask":{"slot":2,"kind":"scalar"}},"uniforms":{"enabled":{"type":"bool","value":true},"two":{"type":"int","value":2}},"red":"u v sample:palette.r param:enabled *","green":"u v sample:palette.g","blue":"u v sample:mask param:two *"}}
        ],"outputs":{"height":"scaled","color":"rgb"}},"requireGpu":true,"bakeSet":true,"materialized":true,"fusePointwise":true})JSON");
        const auto namedProgram=render::texture_graph::compile(named["graph"]);
        const auto namedPixels=render::texture_graph::evaluate(namedProgram,namedProgram.outputs.at("height"));
        require(namedPixels.rgba[0]==0&&namedPixels.rgba[31*4]==128&&render::texture_graph::imageSourceBytes(namedProgram)==96,"named buffer/uniform CPU mismatch");
        auto wrongKind=named["graph"];
        wrongKind.member("nodes").at(3).member("properties").member("resources").member("curve").set("kind",Json::string("color"));
        bool rejectedKind=false;try{(void)render::texture_graph::compile(wrongKind);}catch(const std::invalid_argument&){rejectedKind=true;}
        require(rejectedKind,"named resource type mismatch accepted");
        const auto namedId=owner.start(named,project,lifetime.lease()).getStr("request");drain();
        const auto namedStatus=owner.status();
        require(namedStatus.getStr("request")==namedId&&namedStatus.getStr("state")=="ready"&&namedStatus.getStr("backend")==expected,"named resource GPU parity failed");
        // HDR values remain signed float through arithmetic, with explicit tone map.
        auto hdrGraph=Json::parse(R"JSON({"graph":{"version":1,"size":16,"nodes":[
          {"id":"light","op":"hdrConstant","inputs":[],"properties":{"radiance":[4,2,-1]}},
          {"id":"gain","op":"hdrExposure","inputs":["light"],"properties":{"ev":1}},
          {"id":"sum","op":"hdrAdd","inputs":["gain","light"]},
          {"id":"display","op":"hdrToneMap","inputs":["sum"],"properties":{"ev":-1,"mapping":"reinhard"}}
        ],"outputs":{"radiance":"sum","color":"display"}},"requireGpu":true,"materialized":true,"fusePointwise":true,"output":"radiance","resolution":16})JSON");
        const auto hdrProgram=render::texture_graph::compile(hdrGraph["graph"]);
        const auto rawHdr=render::texture_graph::evaluateRadiance(hdrProgram,hdrProgram.outputs.at("radiance"));
        require(rawHdr[0]==12&&rawHdr[1]==6&&rawHdr[2]==-3&&rawHdr[3]==1,"linear HDR values clipped");
        const auto hdrDisplay=render::texture_graph::evaluate(hdrProgram,hdrProgram.outputs.at("color"));
        require(hdrDisplay.rgba[0]==219&&hdrDisplay.rgba[1]==191&&hdrDisplay.rgba[2]==0,"HDR tone mapping mismatch");
        const auto hdrPreviewId=owner.start(hdrGraph,project,lifetime.lease()).getStr("request");drain();
        const auto hdrPreviewStatus=owner.status();
        require(hdrPreviewStatus.getStr("request")==hdrPreviewId&&hdrPreviewStatus.getStr("state")=="ready"&&hdrPreviewStatus.getStr("backend")==expected,"HDR materialized preview parity failed");
        auto hdrBake=hdrGraph;hdrBake.set("bake",Json::fromBool(true));
        const auto rawId=owner.start(hdrBake,project,lifetime.lease()).getStr("request");drain();
        const auto rawStatus=owner.status();
        require(rawStatus.getStr("request")==rawId&&rawStatus.getStr("state")=="ready"&&rawStatus.getStr("backend")==expected&&rawStatus.getStr("resultFormat")=="rgba32f-linear"&&rawStatus.getInt("receivedBytes")==16*16*16,"raw GPU HDR readback failed");
        const auto rawGpu=owner.readAcceptedRadiance(project,rawId);
        require(rawGpu.size()==rawHdr.size()&&std::abs(rawGpu[0]-12.f)<.001f&&std::abs(rawGpu[1]-6.f)<.001f&&std::abs(rawGpu[2]+3.f)<.001f&&rawGpu[3]==1.f,"raw GPU HDR values lost");
        bool staleHdr=false;try{(void)owner.readAcceptedRadiance(project,"stale");}catch(const std::runtime_error&){staleHdr=true;}
        require(staleHdr,"stale HDR request accepted");
        cancel(rawId);drain();
        // Native function calls lower into ordinary typed DAG nodes. Two calls
        // share a definition but use independent parameter values and public IDs.
        auto functions=Json::parse(R"JSON({"graph":{"version":1,"size":32,
          "functions":{"scale":{"inputs":[{"id":"x","type":"scalar"}],"nodes":[
            {"id":"factor","op":"constant","inputs":[],"properties":{"value":0.5}},
            {"id":"product","op":"multiply","inputs":["x","factor"]}],
            "outputs":{"value":"product"},"parameters":{"gain":{"default":0.5,"bindings":[{"node":"factor","property":"value"}]}}}},
          "nodes":[{"id":"source","op":"constant","inputs":[],"properties":{"value":0.8}},
            {"id":"first","op":"function","inputs":["source"],"properties":{"function":"scale","output":"value","arguments":{"gain":0.25}}},
            {"id":"second","op":"function","inputs":["source"],"properties":{"function":"scale","output":"value","arguments":{"gain":0.75}}}],
          "outputs":{"height":"first","roughness":"second"}},"requireGpu":true,"bakeSet":true,"materialized":true})JSON");
        const auto program=render::texture_graph::compile(functions["graph"]);
        require(program.nodes.size()==5&&program.nodes[program.outputs.at("height")].id=="first"&&program.nodes[program.outputs.at("roughness")].id=="second","function call identities lost");
        const auto firstPixels=render::texture_graph::evaluate(program,program.outputs.at("height"));
        const auto secondPixels=render::texture_graph::evaluate(program,program.outputs.at("roughness"));
        require(firstPixels.rgba[0]==51&&secondPixels.rgba[0]==153,"function instance parameters not independent");
        auto invalid=functions["graph"];
        invalid.member("functions").member("scale").member("inputs").at(0).set("type",Json::string("color"));
        bool invalidFunctionRejected=false;
        try{(void)render::texture_graph::compile(invalid);}catch(const std::invalid_argument&){invalidFunctionRejected=true;}
        require(invalidFunctionRejected,"function type mismatch accepted");
        const auto functionId=owner.start(functions,project).getStr("request");drain();
        const auto fnStatus=owner.status();
        require(fnStatus.getStr("request")==functionId&&fnStatus.getStr("state")=="ready"&&fnStatus.getStr("backend")==expected&&fnStatus["completedOutputs"].toDouble()==2,"native function GPU acceptance failed");
        for(const auto& row:fnStatus["outputs"].items())require(row.getStr("backend")==expected,"native function CPU fallback");
        std::printf("TextureGraph function instance parity accepted: %s\\n",expected);
        // Nested calls retain the public result ID and forward independent instance parameters.
        auto nested=functions;
        nested.member("graph").member("functions").set("wrapper",Json::parse(R"JSON({
          "inputs":[{"id":"x","type":"scalar"}],
          "nodes":[{"id":"nested","op":"function","inputs":["x"],"properties":{"function":"scale","output":"value","arguments":{}}}],
          "outputs":{"value":"nested"},
          "parameters":{"gain":{"default":0.5,"bindings":[{"node":"nested","property":"arguments.gain"}]}}
        })JSON"));
        for(size_t i=1;i<=2;++i)nested.member("graph").member("nodes").at(i).member("properties").set("function",Json::string("wrapper"));
        const auto np=render::texture_graph::compile(nested["graph"]);
        require(np.nodes.size()==5&&np.nodes[np.outputs.at("height")].id=="first"&&render::texture_graph::evaluate(np,np.outputs.at("height")).rgba[0]==51&&render::texture_graph::evaluate(np,np.outputs.at("roughness")).rgba[0]==153,"nested function instance mismatch");
        auto recursion=nested["graph"];
        recursion.member("functions").member("wrapper").member("nodes").at(0).member("properties").set("function",Json::string("wrapper"));
        bool rejectedRecursion=false;try{(void)render::texture_graph::compile(recursion);}catch(const std::invalid_argument&){rejectedRecursion=true;}
        require(rejectedRecursion,"recursive texture function accepted");
        const auto nestedId=owner.start(nested,project).getStr("request");drain();
        const auto nestedStatus=owner.status();
        require(nestedStatus.getStr("request")==nestedId&&nestedStatus.getStr("state")=="ready"&&nestedStatus.getStr("backend")==expected,"nested function GPU parity failed");
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
        // A pinned external image is eligible for incremental execution.
        auto extInc=external;extInc.set("incremental",Json::fromBool(true));
        auto runExternal=[&](const Json& input,const render::texture_graph::ImageResolver& resolve,int dispatched,int reused){
            owner.start(input,project,lifetime.lease(),resolve);drain();const auto s=owner.status();
            require(s.getStr("state")=="ready"&&s.getStr("backend")==expected,"external incremental GPU request failed");
            require(s["materializedNodePasses"].toDouble()==dispatched&&s["incrementalReusedNodes"].toDouble()==reused,"incorrect external image incremental reuse");
        };
        runExternal(extInc,resolveImage,1,0);runExternal(extInc,resolveImage,0,1);
        auto revised=extInc;revised.member("graph").member("nodes").at(0).member("properties").set("revision",Json::string(std::string(64,'b')));
        const render::texture_graph::ImageResolver resolveRevised=[](const std::string& path,const std::string& revision,size_t budget){
            if(path!="content/source.png"||revision!=std::string(64,'b')||budget<512u*512u*4u)throw std::invalid_argument("fixture revision mismatch");
            render::texture_graph::ImageSource source;source.width=source.height=512;source.rgb.assign(512u*512u,0x00ff00u);return source;
        };
        runExternal(revised,resolveRevised,1,0);
        cancel(owner.status().getStr("request"));drain();
        // Snapshot must survive resolver lifetime and not reread a changed file.
        int resolutions=0;bool sourceAvailable=true;
        const render::texture_graph::ImageResolver queueResolver=[&](const std::string& path,const std::string& revision,size_t budget){
            ++resolutions;if(!sourceAvailable)throw std::runtime_error("fixture source removed after enqueue");
            return resolveImage(path,revision,budget);
        };
        const auto externalTicket=owner.enqueue(extInc,project,lifetime.lease(),3,queueResolver).getStr("ticket");
        const auto removedTicket=owner.enqueue(extInc,project,lifetime.lease(),0,queueResolver).getStr("ticket");
        require(resolutions==2&&owner.queueStatus()["pendingImageBytes"].toDouble()==2*512*512*4,"queued source snapshot accounting failed");
        owner.cancelTicket(removedTicket,project);
        require(owner.queueStatus()["pendingImageBytes"].toDouble()==512*512*4,"cancelled queue snapshot retained");
        sourceAvailable=false;
        frame();require(owner.queueStatus().getStr("activeTicket")==externalTicket,"external queue promotion failed");drain();
        require(resolutions==2&&owner.queueStatus()["pendingImageBytes"].toDouble()==0&&owner.status().getStr("state")=="ready"&&owner.status().getStr("backend")==expected,"queued external snapshot lost or reread");
        owner.releaseTicket(externalTicket,project,true);drain();

        // Real GPU candidates adopt into an actual immutable appearance.
        // Keep the old snapshot pinned across a partial publication.
        render::Model materialModel;materialModel.materials.resize(1);
        render::ModelImageRollbackPool rollback;
        struct ModelCleanup {
            gfx::Device& dev;render::Model& model;
            ~ModelCleanup(){if(dev.waitIdle())render::releaseModel(dev,model);}
        } cleanup{dev,materialModel};
        view::AppearanceAssetRegistry registry;
        view::AppearanceAssetRegistry::Asset asset;asset.model=&materialModel;asset.materials=materialModel.materials;
        require(registry.publishPrepared("fixture",asset),"fixture appearance publication failed");
        auto live=registry.find("fixture");
        opts.incremental=true;opts.incrementalOutputs=true;opts.fusePointwise=true;
        auto stamp=[&]{return view::LiveAppearanceMaterialTarget{live->modelPublicationGeneration,live->version,0};};
        auto startTarget=[&](const Json& graph){
            return owner.startResidentMaterial(graph,Json::object(),project,lifetime.lease(),opts,stamp()).getStr("request");
        };
        auto applyTarget=[&](const std::string& id,const view::LiveAppearanceMaterialTarget& target){
            return owner.publishCompletedGpuSnapshot(project,id,live,target,materialModel,dev,lifetime.lease(),rollback);
        };
        const auto full=startTarget(residentGraph);drain();
        require(owner.status()["materializedPackPasses"].toDouble()==2&&applyTarget(full,stamp()),"initial targeted GPU set failed");
        const auto pinned=live;
        const auto aoIndex=live->materials[0].occlusionTex,normalIndex=live->materials[0].normalTex;
        require(aoIndex>=0&&normalIndex>=0&&materialModel.images.size()==2,"initial texture bindings missing");
        const auto aoHandle=materialModel.images[size_t(aoIndex)].tex;
        auto altered=Json::parse(R"JSON({"version":1,"size":32,"nodes":[
            {"id":"h","op":"gradient","inputs":[],"properties":{"axis":"x"}},
            {"id":"n","op":"normal","inputs":["h"],"properties":{"strength":8}}
        ],"outputs":{"ao":"h","height":"h","normal":"n"}})JSON");
        const auto partial=startTarget(altered);drain();
        auto partialStatus=owner.status();
        require(partialStatus["incrementalReusedOutputs"].size()==1&&partialStatus["materializedPackPasses"].toDouble()==1,
            "unchanged output regenerated");
        auto stale=stamp();++stale.version;
        require(!applyTarget(partial,stale)&&applyTarget(partial,stamp()),"stale or partial publication mismatch");
        require(materialModel.images.size()==3&&live->materials[0].occlusionTex==aoIndex&&
            materialModel.images[size_t(aoIndex)].tex==aoHandle&&live->materials[0].normalTex!=normalIndex&&
            pinned->materials[0].normalTex==normalIndex,"partial publication changed old or retained bindings");
        const auto revision=live->version;const auto noChange=startTarget(altered);
        require(owner.status().getBool("incrementalOutputNoop")&&owner.status().getBool("gpuTextureSetReady")&&
            owner.status()["materializedPackPasses"].toDouble()==0,"unchanged set encoded GPU work");
        require(applyTarget(noChange,stamp())&&live->version==revision&&materialModel.images.size()==3,"no-op mutated appearance");
        // The index, name, texture handle and publication stamps are unchanged,
        // but the material pin does not describe that generation. The receipt
        // must refuse even the no-op path and require a fresh generation.
        const auto generationRequest=startTarget(altered);
        require(owner.status().getBool("incrementalOutputNoop"),"generation fixture requires cached outputs");
        const auto validSnapshot=live;
        auto mismatched=std::make_shared<view::AppearanceAssetRegistry::Asset>(*live);
        // AO is channel 3; corrupt only the retained AO pin.
        mismatched->materials[0].proceduralGraphPins=render::withGraphMaterialPin(
            mismatched->materials[0].proceduralGraphPins,3,std::make_shared<const render::ProceduralImagePin>());
        live=mismatched;
        require(!applyTarget(generationRequest,stamp()),"stale material generation accepted");
        live=validSnapshot;
        // Failed publication leaves the request owned by the GPU owner.
        // Explicitly cancel before starting the required full regeneration.
        cancel(generationRequest);drain();
        const auto regenerate=startTarget(altered);drain();
        require(!owner.status().getBool("incrementalOutputNoop")&&
            owner.status()["materializedPackPasses"].toDouble()==2&&applyTarget(regenerate,stamp()),
            "generation refusal did not regenerate the full output set");
        registry.clear();live.reset();
        require(rollback.drain(dev),"fixture image rollback drain failed");

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
        // A new capture selects detail rather than nested aggregate timers.
        profile.start(dev.info(),project,true);
        auto detail=fusedInc;detail.set("incremental",Json::fromBool(false));
        const auto detailedId=owner.start(detail,project,lifetime.lease()).getStr("request");
        drain();
        require(owner.status().getStr("state")=="ready"&&owner.status()["materializedNodePasses"].toDouble()==3,"detailed capture changed execution");
        const auto detailed=profile.report();bool group=false,fusedGroup=false,pack=false;
        for(const auto& row:detailed["samples"].items()){
            require(row.getStr("request")==detailedId,"detailed capture request mismatch");
            const auto scope=row.getStr("scope");
            require(scope!="texture-graph-materialized-dispatch","nested aggregate scope in detail mode");
            if(scope=="texture-graph-materialized-group"){
                group=true;const auto& members=row["groupMembers"];
                require(members.isArray()&&members.size()>0&&members.size()<=8&&
                    members[members.size()-1].str()==row.getStr("groupRoot"),"group membership evidence invalid");
                fusedGroup|=members.size()>1;
            }else if(scope=="texture-graph-output-pack")pack=true;
        }
        std::printf("TextureGraph region scope evidence: %s\n",detailed.dump(-1).c_str());
        if(const auto* strict=std::getenv("AIVERSE_TEXTURE_GRAPH_REQUIRE_GPU_TIMINGS");strict&&std::string_view(strict)=="1")
            require(group&&fusedGroup&&pack,"strict detailed GPU timing evidence missing");
        result=0;
    }catch(const std::exception& e){std::fprintf(stderr,"FAIL texture graph RHI: %s\n",e.what());}
    if(dev.waitIdle())owner.shutdownAfterIdle(dev);
    else{std::fprintf(stderr,"FAIL texture graph RHI: shutdown idle failed\n");result=1;}
    if(!result)std::printf("PASS texture graph %s: parity, shared scratch, multi-block readback, cancellation, timeout, recovery and native fence-only resident lifecycle\n",expected);
    return result;
}
} // namespace aiverse::tests
