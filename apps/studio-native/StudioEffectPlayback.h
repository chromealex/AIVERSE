#pragma once
#include "engine/fx/FxModelMesh.h"
#include "engine/fx/ParticleSystem.h"
#include "engine/fx/SpectraSdfFileIO.h"
#include "engine/render/Renderer3D.h"
#include "engine/render/TextureCache.h"
#include <chrono>
#include <filesystem>
#include <fstream>

namespace aiverse::studio {

class StudioEffectPlayback {
    gfx::Device& device_;
    std::filesystem::path project_;
    render::TextureCache textures_;
    fx::ParticleSystem particles_;
    render::Camera camera_;
    fx::FxHandle instance_{};
    int definition_=-1;
    float time_=0,speed_=1,restartAt_=0;
    bool played_=false,loop_=true,moving_=false;
    using Clock=std::chrono::steady_clock;
    Clock::time_point last_{};

    std::filesystem::path asset(const std::string& name) const {
        namespace fs=std::filesystem;
        if(name.empty() || fs::path(name).is_absolute())return {};
        std::error_code ec;
        const auto path=fs::weakly_canonical(project_/name,ec);
        const auto local=path.lexically_relative(project_);
        if(ec || local.empty() || *local.begin()==".." || !fs::is_regular_file(path,ec))return {};
        return path;
    }
public:
    StudioEffectPlayback(gfx::Device& device,render::RenderDeviceLifetime::Lease lease,const std::string& project)
        :device_(device),project_(std::filesystem::canonical(project)){
        const fx::FileReader read=[this](const std::string& name,std::string& out){
            std::ifstream file(asset(name),std::ios::binary);
            if(!file)return false;
            out.assign(std::istreambuf_iterator<char>(file),{});
            return !file.bad();
        };
        textures_.init(device);
        particles_.setFileReader(read);
        particles_.setMeshLoader([read](const auto& name,auto& out){return fx::meshFromModelFile(name,read,out);});
        particles_.setTextureLoader([this](const std::string& name,fx::FxTexture& out){
            if(asset(name).empty())return false;
            const auto ref=textures_.resolve(name,false,project_.string()+"/");
            if(!ref.valid())return false;
            out={ref.tex,ref.u0,ref.v0,ref.u1,ref.v1};return true;
        });
        particles_.setGpuDeviceLease(std::move(lease));
        if(!particles_.init(&device))throw std::runtime_error(particles_.lastError());
        particles_.setQuality(fx::FxQuality::High);
        camera_.target={0,1,0};camera_.distance=10;camera_.pitch=.42f;camera_.yaw=.6f;
    }
    ~StudioEffectPlayback(){particles_.shutdown();textures_.shutdown();}

    Json command(const Json& p){
        if(p.has("source")){
            fx::EffectDef def;
            std::vector<std::string> errors;
            if(!fx::parseEffect(Json::parse(p.getStr("source")),"Preview",def,errors,fx::projectSdfReader(project_)) || def.emitters.empty())
                throw std::runtime_error(errors.empty()?"The effect needs an emitter":errors.front());
            if(!device_.waitForResourceIdle())throw std::runtime_error("Particle preview GPU is busy");
            particles_.clear();
            definition_=particles_.define("_preview",def);
            time_=restartAt_=0;played_=false;instance_={};last_={};
        }
        if(p.has("speed"))speed_=float(std::clamp(p["speed"].toDouble(1),.1,2.));
        if(p.has("quality"))particles_.setQuality(fx::FxQuality(std::clamp(p.getInt("quality"),0,3)));
        if(p.has("loop"))loop_=p.getBool("loop");
        if(p.has("moving"))moving_=p.getBool("moving");
        Json result=Json::object();
        result.set("particles",Json::number(int64_t(particles_.stats().particles)));
        result.set("drawCalls",Json::number(int64_t(particles_.stats().drawCalls)));
        result.set("time",Json::number(double(time_)));
        return result;
    }
    void draw(render::Renderer3D& renderer,int width,int height){
        if(definition_<0)return;
        const auto now=Clock::now();
        const float dt=last_==Clock::time_point{}?0:std::min(.1f,std::chrono::duration<float>(now-last_).count())*speed_;
        last_=now;time_+=dt;
        renderer.beginScene(camera_,width,height);
        for(int i=-5;i<=5;++i){
            const auto color=render::rgba(61,74,90);
            renderer.line({float(i),0,-5},{float(i),0,5},color);
            renderer.line({-5,0,float(i)},{5,0,float(i)},color);
        }
        particles_.setView(camera_,renderer.viewProj(),width,height);
        const float angle=time_*1.6f;
        const fx::float3 pos=moving_?fx::float3{3*std::cos(angle),1.2f,3*std::sin(angle)}:fx::float3{0,.5f,0};
        const fx::float3 dir=moving_?fx::float3{-std::sin(angle),0,std::cos(angle)}:fx::float3{0,1,0};
        if(particles_.playing(instance_))particles_.move(instance_,pos,dir);
        else if(!played_ || (loop_ && time_>=restartAt_)){
            instance_=particles_.play(definition_,pos,dir);
            const auto def=particles_.def(definition_);
            restartAt_=time_+(def&&!def->looping()?std::min(8.f,def->lifetime()+.4f):.5f);
            played_=true;
        }
        particles_.update(dt);
        gfx::PassDesc pass;pass.clearColor[0]=.07f;pass.clearColor[1]=.09f;pass.clearColor[2]=.12f;
        renderer.render(pass);
        particles_.render({renderer.lighting.fogColor,0});
    }
};
}
