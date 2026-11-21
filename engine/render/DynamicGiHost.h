#pragma once
#include "engine/data/Json.h"
#include "engine/render/GiMainCpuBridge.h"
#include "engine/render/GiMainGpuBridge.h"
#include "engine/render/Renderer3D.h"
#include <algorithm>
#include <chrono>
#include <map>
namespace aiverse::render {
// Native host example: immutable opaque geometry captured BEFORE GPU upload,
// explicit indirect-only sources, bounded work before render, per-view binding.
struct DynamicGiHost {
    render::GiMainCpuBridge cpu;
    render::GiMainGpuBridge gpu;
    bool enabled=false,useGpu=false,published=false,converged=false;
    std::string fallback;
    std::shared_ptr<const render::GiChunkedGeometrySnapshot> scene;
    std::vector<render::GiMovingLight> lights;
    render::GiEnvironmentLight environment;
    std::chrono::steady_clock::time_point started;
    double convergenceMs=0;
    uint64_t geometryRevision=1;
    static render::float3 vector(const Json& j) {
        if(!j.isArray() || j.size()!=3)throw std::runtime_error("dynamicGI requires xyz vectors");
        render::float3 v{float(j[0].toDouble()),float(j[1].toDouble()),float(j[2].toDouble())};
        if(!std::isfinite(v.x)||!std::isfinite(v.y)||!std::isfinite(v.z))throw std::runtime_error("dynamicGI nonfinite vector");
        return v;
    }
    void initialize(gfx::Device& d,const Json& settings,render::StaticShadowGeometry geometry,
                    const std::vector<render::PointLight>& sources) {
        using namespace render;
        GiMainCpuConfig cfg;cfg.origin=vector(settings["origin"]);
        const auto dims=vector(settings["dimensions"]);
        for(float n:{dims.x,dims.y,dims.z})if(n<1||n>256||n!=std::floor(n))throw std::runtime_error("dynamicGI dimensions must be integers 1..256");
        cfg.nx=int(dims.x);cfg.ny=int(dims.y);cfg.nz=int(dims.z);
        cfg.spacing=float(settings["spacing"].toDouble(1));
        const double samples=settings["samples"].toDouble(32);
        if(!std::isfinite(samples)||samples<8||samples>128||samples!=std::floor(samples))throw std::runtime_error("dynamicGI samples must be integer 8..128");
        cfg.samples=int(samples);cfg.traceDistance=float(settings["distance"].toDouble(12));
        cfg.cpuBudgetMs=settings["cpuBudgetMs"].toDouble(2);
        const double cascades=settings["cascades"].toDouble(1);
        if(!std::isfinite(cascades)||cascades<1||cascades>GiMainGpuBridge::kMaxCascades||cascades!=std::floor(cascades))
            throw std::runtime_error("dynamicGI cascades must be integer 1..3");
        cfg.cascades=int(cascades);
        // Spatial chunks (one per occupied cell of about one trace distance):
        // the GPU bridge keeps only cells a GI ray can reach resident, so a
        // world larger than the GPU triangle budget can still run on the GPU.
        const float cell=std::max(cfg.traceDistance,cfg.spacing);
        if(!std::isfinite(cell) || cell<=0)throw std::runtime_error("dynamicGI distance/spacing must be positive");
        std::map<uint64_t,std::vector<StaticShadowGeometry::Triangle>> cells;
        for(const auto& t:geometry.triangles) {
            const float3 c=(t.a+t.b+t.c)*(1.0f/3.0f);
            auto axis=[&](float v)->uint64_t {
                const double k=std::floor(double(v)/double(cell));
                return uint64_t(int64_t(std::clamp(std::isfinite(k)?k:0.0,-1048575.0,1048575.0))+1048576)&0x1FFFFF;
            };
            cells[(axis(c.x)<<42)|(axis(c.y)<<21)|axis(c.z)].push_back(t);
        }
        GiChunkedGeometryBuilder builder;
        for(auto& [key,chunk]:cells)builder.replace(key+1,std::move(chunk),geometry.albedos);
        scene=std::make_shared<GiChunkedGeometrySnapshot>(builder.publish());
        auto decode=[](float v){return v<=.04045f?v/12.92f:std::pow((v+.055f)/1.055f,2.4f);};
        for(const auto& source:sources) {
            GiMovingLight l;l.key=lights.size()+1;l.position=source.pos;l.radius=source.radius;l.intensity=source.intensity;
            l.color={decode(source.color.x),decode(source.color.y),decode(source.color.z)};
            l.direction=source.dir;l.cosInner=source.cosInner;l.cosOuter=source.cosOuter;lights.push_back(l);
        }
        if(settings.has("sunDirection"))environment.sunDirection=normalize(vector(settings["sunDirection"]));
        if(settings.has("sunColor"))environment.sunColor=vector(settings["sunColor"]);
        if(settings.has("skyColor"))environment.skyColor=vector(settings["skyColor"]);
        environment.sunIntensity=float(settings["sunIntensity"].toDouble(0));
        environment.skyIntensity=float(settings["skyIntensity"].toDouble(0));
        if(!environment.valid())throw std::runtime_error("invalid dynamicGI indirect environment");
        const auto backend=settings.getStr("backend","auto");
        if(backend!="auto"&&backend!="cpu"&&backend!="gpu")throw std::runtime_error("dynamicGI backend must be auto, cpu or gpu");
        float lightReach=GiMainGpuBridge::kDefaultLightReach;
        for(const auto& l:lights)if(std::isfinite(l.radius))lightReach=std::max(lightReach,l.radius);
        // Only geometry a GI ray can reach must fit the GPU budget (residency).
        const bool fits=GiMainGpuBridge::planResidency(*scene,cfg,lightReach).admitted &&
            lights.size()<=GiGpuSmallScene::kMaxLights && cfg.samples<=GiGpuSmallScene::kMaxSamples;
        if(backend!="cpu") {
            if(!fits)fallback="GPU capacity exceeded by geometry within GI reach or by lights; full scene retained on CPU";
            else if(!d.supportsCompute())fallback="device has no compute; using CPU";
            else if(!(useGpu=gpu.initialize(d,cfg,lightReach)))fallback="GPU initialization rejected; using CPU";
            if(!useGpu && backend=="gpu")throw std::runtime_error(fallback);
        }
        if(!useGpu) {
            if(cfg.cascades!=1)fallback+=(fallback.empty()?"":"; ")+std::string("CPU GI uses one cascade");
            cfg.cascades=1;
            if(!cpu.initialize(d,cfg))throw std::runtime_error("dynamicGI CPU initialization failed");
        }
        enabled=true;
    }
    void replaceGeometry(gfx::Device& d,std::shared_ptr<const render::GiChunkedGeometrySnapshot> next,uint64_t revision) {
        if(!enabled || revision<=geometryRevision)return;
        if(!published){scene=std::move(next);geometryRevision=revision;return;}
        if(useGpu) {
            if(!d.waitForSubmittedGpuWork())throw std::runtime_error("animated GI GPU fence failed");
            gpu.poll();
            if(!gpu.publishGeometry(next,revision))throw std::runtime_error(gpu.diagnostic());
        } else if(!cpu.publishGeometry(next,revision))throw std::runtime_error("animated GI CPU geometry failed");
        scene=std::move(next);geometryRevision=revision;converged=false;
        if(published)started=std::chrono::steady_clock::now();
    }
    bool update(gfx::Device& d,render::float3 focus) {
        if(!enabled)return true;
        if(!d.gpuHealthy())throw std::runtime_error("dynamicGI device lost");
        if(!published) {
            started=std::chrono::steady_clock::now();
            const bool ok=useGpu?(gpu.publishGeometry(scene,geometryRevision)&&gpu.publishLights(lights)&&gpu.publishEnvironment(environment)):
                (cpu.publishGeometry(scene,geometryRevision)&&cpu.publishLights(lights)&&cpu.publishEnvironment(environment));
            if(!ok)throw std::runtime_error("dynamicGI source publication failed");
            published=true;
        }
        if(useGpu){gpu.updateFrame(focus);converged=gpu.canPresent();}
        else {const auto stats=cpu.updateFrame(focus);if(!stats.canPresent)throw std::runtime_error("dynamicGI CPU update failed");converged=stats.pending==0;}
        if(!converged && std::chrono::steady_clock::now()-started>std::chrono::seconds(60))throw std::runtime_error("dynamicGI convergence timeout");
        if(converged && !convergenceMs)convergenceMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
        return converged;
    }
    bool attach(render::Renderer3D& r) {
        return !enabled || (converged && (useGpu?r.setDynamicGiOverlay(gpu):r.setDynamicGiOverlay(cpu)));
    }
    bool shutdownAfterGpuIdle(gfx::Device& d,render::Renderer3D& r) {
        r.clearDynamicGiOverlay();
        if(!enabled && !scene)return true;
        if(!d.waitForResourceIdle())return false;
        const bool ok=gpu.shutdownAfterGpuIdle() && cpu.shutdownAfterGpuIdle();
        scene.reset();enabled=false;return ok;
    }
};
}
