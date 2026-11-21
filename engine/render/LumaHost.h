#pragma once
#include "engine/data/Json.h"
#include "engine/render/LumaWeavePostProcess.h"
namespace aiverse::render {
// Explicit native owner. Unsupported auto mode retains the existing LightSystem.
struct LumaHost {
    std::unique_ptr<render::LumaWeavePostProcess> pipeline;
    render::LumaWeaveRegistry sources;
    render::LumaWeaveClusters clusters;
    render::GiGpuBvh geometry;
    render::LumaWeaveQuality quality=render::LumaWeaveQuality::High;
    bool enabled=false,published=false;
    std::string fallback;
    uint64_t composedFrames=0,geometryRevision=1;
    void initialize(gfx::Device& d,const Json& settings,render::PostProcess& downstream,
                    render::LightSystem& legacy,const render::StaticShadowGeometry& mesh,
                    const std::vector<render::PointLight>& lights,bool hdr) {
        using namespace render;
        const auto backend=settings.getStr("backend","auto");
        if(backend!="auto"&&backend!="gpu")throw std::runtime_error("luma backend must be auto or gpu");
        const auto tier=settings.getStr("quality","high");
        if(tier=="balanced")quality=LumaWeaveQuality::Balanced;
        else if(tier=="high")quality=LumaWeaveQuality::High;
        else if(tier=="ultra")quality=LumaWeaveQuality::Ultra;
        else throw std::runtime_error("luma quality must be balanced, high or ultra");
        if(!hdr)fallback="Luma requires HDR; legacy direct fallback";
        else if(d.info().backend!=gfx::Backend::Metal)fallback="Luma GPU backend unavailable; legacy direct fallback";
        else if(mesh.triangles.size()>GiGpuBvh::kMaxTriangles || lights.size()>LumaWeaveRegistry::kMaxSources)
            fallback="Luma GPU capacity exceeded; legacy direct fallback";
        if(!fallback.empty()) {
            if(backend=="gpu")throw std::runtime_error(fallback);
            return;
        }
        std::vector<LumaLight> next;
        auto linear=[](float v){return v<=.04045f?v/12.92f:std::pow((v+.055f)/1.055f,2.4f);};
        for(const auto& point:lights) {
            LumaLight light;light.key=next.size()+1;light.position=point.pos;
            light.color={linear(point.color.x),linear(point.color.y),linear(point.color.z)};
            light.radius=point.radius;light.intensity=point.intensity;
            light.direction=point.dir;light.cosInner=point.cosInner;light.cosOuter=point.cosOuter;
            light.kind=point.cosOuter> -1?LumaLightKind::Spot:LumaLightKind::Point;
            next.push_back(light);
        }
        sources.publish(std::move(next));clusters.rebuild(sources,{});
        GiChunkedGeometryBuilder builder;builder.replace(1,mesh.triangles,mesh.albedos);
        geometry=GiGpuBvh::build(builder.publish());
        pipeline=std::make_unique<LumaWeavePostProcess>(downstream,legacy);
        if(!pipeline->initialize(d))throw std::runtime_error("Luma initialization failed: "+d.lastError());
        pipeline->setEnabled(true);pipeline->setQuality(quality);enabled=true;
    }
    void replaceGeometry(gfx::Device& d,const render::GiChunkedGeometrySnapshot& next,uint64_t revision) {
        if(!enabled || revision<=geometryRevision)return;
        auto bvh=render::GiGpuBvh::build(next); // capacity rejects; never truncate actors
        if(!d.waitForSubmittedGpuWork())throw std::runtime_error("animated Luma GPU fence failed");
        pipeline->pollGpu();
        if(published && !pipeline->publishGeometry(bvh,revision))throw std::runtime_error("animated Luma geometry publication failed");
        geometry=std::move(bvh);geometryRevision=revision;
    }
    void update(gfx::Device& d) {
        if(!enabled)return;
        pipeline->pollGpu();
        // Wait only for ACTUALLY committed work, never a marker behind the
        // current uncommitted frame. This correctness-first host deliberately
        // serializes outstanding Luma compute rather than dropping moving views.
        if(pipeline->screen().busy()) {
            if(!d.waitForSubmittedGpuWork())throw std::runtime_error("Luma prior GPU submission failed");
            pipeline->pollGpu();
            if(pipeline->screen().busy())throw std::runtime_error("Luma pending work did not complete");
        }
        if(!published) {
            if(!pipeline->publishSources(sources,clusters)||!pipeline->publishGeometry(geometry,geometryRevision))
                throw std::runtime_error("Luma source publication failed");
            published=true;
        }
    }
    void presented() {
        if(!enabled)return;
        if(!pipeline->stats().composited)throw std::runtime_error("Luma did not compose the requested view");
        ++composedFrames;
    }
    bool shutdown(gfx::Device& d) {
        if(!pipeline)return true;
        if(!d.waitForSubmittedGpuWork() || !d.waitForResourceIdle())return false;
        pipeline->pollGpu();
        if(!pipeline->drainRetiredAfterGpuIdle() || !pipeline->shutdown())return false;
        pipeline.reset();enabled=false;return true;
    }
};
}
