#pragma once
#include "StudioPreviewParticles.h"
namespace aiverse::studio {
class StudioEffectPlayback : private StudioPreviewParticles {
    render::Camera camera_;
    fx::FxHandle instance_{};
    int definition_=-1;
    float time_=0,speed_=1,restartAt_=0;
    bool played_=false,loop_=true,moving_=false;
    using Clock=std::chrono::steady_clock;
    Clock::time_point last_{};

    void resetCamera(){
        camera_.target={0,1,0};camera_.distance=10;camera_.pitch=.42f;camera_.yaw=.6f;
    }
public:
    StudioEffectPlayback(gfx::Device& device,render::RenderDeviceLifetime::Lease lease,const std::string& project)
        :StudioPreviewParticles(device,std::move(lease),project){resetCamera();}
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
        if(p.has("quality"))particles_.setQuality(fx::FxQuality(std::clamp<int64_t>(p.getInt("quality"),0,3)));
        if(p.has("loop"))loop_=p.getBool("loop");
        if(p.has("moving"))moving_=p.getBool("moving");
        if(p.getBool("resetCamera"))resetCamera();
        if(p.has("orbitX")||p.has("orbitY")){
            camera_.yaw-=float(std::clamp(p["orbitX"].toDouble(),-250.,250.))*.01f;
            camera_.pitch=std::clamp(camera_.pitch+float(std::clamp(p["orbitY"].toDouble(),-250.,250.))*.01f,.05f,1.5f);
        }
        if(p.has("zoom")){
            const auto steps=std::clamp(p["zoom"].toDouble(),-50.,50.);
            camera_.distance=std::clamp(camera_.distance*float(std::pow(.9,steps)),2.f,60.f);
        }
        Json result=Json::object();
        result.set("yaw",Json::number(double(camera_.yaw)));
        result.set("pitch",Json::number(double(camera_.pitch)));
        result.set("distance",Json::number(double(camera_.distance)));
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
