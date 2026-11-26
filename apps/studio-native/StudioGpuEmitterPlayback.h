#pragma once
#include "apps/editor/core/SpectraFxGpuPreviewPolicy.h"
#include "engine/fx/SpectraFxSceneHost.h"
#include "engine/render/Renderer3D.h"
#include <chrono>
#include <filesystem>
#include <fstream>

namespace aiverse::studio {

class StudioGpuEmitterPlayback {
    gfx::Device& device_;
    std::filesystem::path project_;
    fx::SpectraFxSceneHost host_;
    editor::SpectraFxGpuPreviewPolicy policy_;
    render::Camera camera_{{0,.8f,0},9,.45f,.7f};
    std::string source_, error_;
    uint64_t revision_=0;
    bool ready_=false;
    double time_=0;

    bool read(const std::string& name,std::string& output) const {
        if(name=="fx/gpu/routes.json"){
            output=R"({"version":1,"routes":[{"eventType":1,"asset":"studio_preview","ownPlayerOnly":true}]})";
            return true;
        }
        if(name=="fx/gpu/studio_preview.gpu.json"){output=source_;return true;}
        namespace fs=std::filesystem;
        if(fs::path(name).is_absolute())return false;
        const auto path=fs::weakly_canonical(project_/name);
        const auto rel=path.lexically_relative(project_);
        if(rel.empty()||*rel.begin()=="..")return false;
        std::ifstream input(path,std::ios::binary);
        if(!input)return false;
        output.assign(std::istreambuf_iterator<char>(input),{});
        return !input.bad();
    }
    void publish(const std::string& text){
        if(text==source_ && ready_)return;
        source_=text;ready_=false;time_=0;++revision_;
        policy_.invalidDraft();host_.shutdown();
        fx::SpectraFxHybridEmitter::Config config;
        if(!fx::SpectraFxGpuEmitterAsset::readText(text,config,&error_))return;
        ready_=host_.init(device_,[this](const auto& path,auto& out){return read(path,out);},project_.string()+"/");
        error_=ready_?"":host_.lastError();
    }
public:
    StudioGpuEmitterPlayback(gfx::Device& device,const std::string& project)
        :device_(device),project_(std::filesystem::weakly_canonical(project)){}
    ~StudioGpuEmitterPlayback(){host_.shutdown();}

    Json command(const Json& p){
        if(p["source"].isString())publish(p.getStr("source"));
        if(p.getBool("restart"))policy_.requestRestart();
        if(p.has("paused")&&p.getBool("paused")!=policy_.paused())policy_.togglePause();
        if(p.getBool("step"))policy_.requestStep();
        if(p.getBool("resetCamera"))camera_={{0,.8f,0},9,.45f,.7f};
        camera_.yaw-=float(std::clamp(p["orbitX"].toDouble(),-250.,250.))*.01f;
        camera_.pitch=std::clamp(camera_.pitch+float(std::clamp(p["orbitY"].toDouble(),-250.,250.))*.01f,.05f,1.5f);
        camera_.distance=std::clamp(camera_.distance*float(std::pow(.9,std::clamp(p["zoom"].toDouble(),-50.,50.))),2.f,60.f);
        auto result=Json::object();
        result.set("paused",Json::fromBool(policy_.paused()));result.set("time",Json::number(time_,8));
        result.set("error",Json::string(error_));result.set("distance",Json::number(double(camera_.distance)));
        result.set("yaw",Json::number(double(camera_.yaw)));result.set("pitch",Json::number(double(camera_.pitch)));
        const auto* playback=host_.playback();
        if(playback){
            const auto stats=playback->backendSnapshot();
            result.set("gpu",Json::number(int64_t(stats.dedicatedGpu)));
            result.set("cpu",Json::number(int64_t(stats.dedicatedCpu+stats.dedicatedCpuTwin)));
        }
        return result;
    }
    void draw(render::Renderer3D& renderer,int width,int height){
        if(ready_&&policy_.needsStart(revision_)&&host_.resetScene()){
            fx::SpectraFxCue cue{};cue.id=1;cue.key=1;cue.action=fx::SpectraFxCue::Action::Play;
            cue.position={0,.7f,0};cue.direction={0,1,0};
            if(host_.dispatch(cue)){policy_.didStart(revision_);time_=0;error_.clear();}
            else{policy_.didFail(revision_);error_="Emitter unavailable on this device";}
        }
        const bool active=ready_&&policy_.canDraw(revision_);
        const auto now=std::chrono::steady_clock::now().time_since_epoch();
        const float dt=policy_.advance(std::chrono::duration<double>(now).count(),active);
        renderer.beginScene(camera_,width,height);
        const auto view=fx::SpectraFxView::fromCamera(camera_,renderer.viewProj(),width,height);
        bool drawParticles=false;
        if(active){
            time_+=dt;host_.prepareGpu(dt,view);host_.prepareCpu(dt,view);
            drawParticles=host_.prepareRenderView(view);
            if(!drawParticles)error_="Could not prepare emitter view";
        }
        for(int i=-5;i<=5;++i){
            const auto color=render::rgba(61,74,90);
            renderer.line({float(i),0,-5},{float(i),0,5},color);
            renderer.line({-5,0,float(i)},{5,0,float(i)},color);
        }
        gfx::PassDesc pass;pass.clearColor[0]=.07f;pass.clearColor[1]=.09f;pass.clearColor[2]=.12f;
        renderer.render(pass);
        if(drawParticles)host_.render(renderer.lighting,view);
    }
};
}
