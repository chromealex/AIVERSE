#pragma once
#include "RenderLightingSettings.h"
#include "DynamicGiHost.h"
#include "LumaHost.h"
namespace aiverse::render {
// One presentation owner for editor and other native hosts. Reconfigure between
// GPU frames; caller retains geometry and downstream until shutdown succeeds.
struct RenderLightingRuntime {
    std::unique_ptr<DynamicGiHost> indirect;
    std::unique_ptr<LumaHost> direct;
    void initialize(gfx::Device& device, Renderer3D& renderer, PostProcess& downstream,
                    const Json& input, const StaticShadowGeometry& geometry,
                    const std::vector<PointLight>& sources, float3 center,
                    const Lighting& environment) {
        const Json settings=renderLightingSettings(input);
        if(settings.getStr("indirect")!="off") {
            indirect=std::make_unique<DynamicGiHost>();
            Json cfg=settings;
            cfg.set("backend",settings["indirect"]);
            const int n=settings.getInt("probes"),layers=settings.getInt("layers");
            const float spacing=float(settings["spacing"].toDouble());
            auto xyz=[](float x,float y,float z){Json j=Json::array();for(float v:{x,y,z})j.push(Json::number(double(v),6));return j;};
            cfg.set("dimensions",xyz(float(n),float(layers),float(n)));
            cfg.set("origin",xyz(center.x-(n-1)*spacing*.5f,center.y,center.z-(n-1)*spacing*.5f));
            cfg.set("sunDirection",xyz(environment.sunDir.x,environment.sunDir.y,environment.sunDir.z));
            cfg.set("sunColor",xyz(environment.sunColor.x,environment.sunColor.y,environment.sunColor.z));
            cfg.set("sunIntensity",Json::number(double(environment.sunIntensity),6));
            cfg.set("skyColor",xyz(environment.skyColor.x,environment.skyColor.y,environment.skyColor.z));
            cfg.set("skyIntensity",Json::number(1.0,1));
            indirect->initialize(device,cfg,geometry,sources);
        }
        if(settings.getStr("direct")=="luma") {
            direct=std::make_unique<LumaHost>();
            Json cfg=Json::object();cfg.set("quality",settings["quality"]);
            direct->initialize(device,cfg,downstream,renderer.lights,geometry,sources,true);
        }
    }
    void update(gfx::Device& device,Renderer3D& renderer,float3 focus) {
        renderer.clearDynamicGiOverlay();
        if(direct)direct->update(device);
        if(indirect && indirect->update(device,focus)) {
            if(!indirect->attach(renderer))throw std::runtime_error("GI viewport binding rejected");
        }
    }
    PostProcess* pipeline(PostProcess& downstream) {
        return direct && direct->enabled ? direct->pipeline.get() : &downstream;
    }
    void presented(){if(direct)direct->presented();}
    std::string status() const {
        std::string out="Preview geometry · GI: ";
        out+=indirect?(indirect->useGpu?"GPU":"CPU"):"off";
        if(indirect && !indirect->converged)out+=" (converging)";
        out+=" · Direct: ";out+=direct&&direct->enabled?"Luma":"standard";
        if(indirect&&!indirect->fallback.empty())out+=" · "+indirect->fallback;
        if(direct&&!direct->fallback.empty())out+=" · "+direct->fallback;
        return out;
    }
    bool shutdown(gfx::Device& device,Renderer3D& renderer) {
        renderer.clearDynamicGiOverlay();
        if(direct && !direct->shutdown(device))return false;
        if(indirect && !indirect->shutdownAfterGpuIdle(device,renderer))return false;
        direct.reset();indirect.reset();return true;
    }
};
}
