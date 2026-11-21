// AIVERSE LENS: owned per-ECS-camera PostFx and ScreenLighting.
// No genre dependencies. Each camera has separate TAA/SSR history, exposure,
// jitter and intermediate textures. The host supplies settings and shaders.
// Call shutdownAfterGpuIdle() only after queue completion, before Device dies.
// A CameraLensComponent with driveExposure / driveDepthOfField sets that
// camera's PRISMA manual EV / defocus override (presentation only; the
// authored autoExposure / depthOfField effects must exist in the settings).
#pragma once
#include "CameraLensPhysics.h"
#include "CameraTemporalPolicy.h"
#include "ScreenLighting.h"
#include "Prisma.h"
#include <cstdint>
#include <memory>
#include <unordered_map>

namespace aiverse::render {
class CameraPostFxPool {
    struct Entry {
        Prisma post;
        ScreenLighting screen;
        CameraTemporalSignature signature{};
        bool initialized=false;
        bool hasScreen=false;
        bool failed=false;
        // Last physical-lens values pushed into `post` (changes only, so
        // exposure/motion history is not reset every frame).
        bool lensExposure=false, lensDof=false;
        float lensEV=0;
        Prisma::DepthOfFieldOverride lensDofValues{};
    };
    gfx::Device* device_=nullptr;
    const World* world_=nullptr;
    PostFxSettings settings_{};
    PostFxQuality quality_=PostFxQuality::Medium;
    bool temporalEnabled_=false;
    Prisma::ImageLoader loader_{};
    std::unordered_map<uint64_t,std::unique_ptr<Entry>> entries_;
public:
    bool init(gfx::Device& device) {
        if(device_ && device_!=&device)return false;
        device_=&device;return true;
    }
    void configure(PostFxSettings settings,PostFxQuality quality,
                   bool enableTemporal=false) {
        settings_=std::move(settings);
        quality_=quality;
        temporalEnabled_=enableTemporal;
        for(auto& [id,e]:entries_) {
            (void)id;
            if(e->initialized) {
                e->post.setSettings(settings_);
                e->post.setQuality(quality_);
                e->screen.setEnabled(enableTemporal);
                e->screen.reset();
            }
        }
    }
    void setImageLoader(Prisma::ImageLoader loader) {
        loader_=std::move(loader);
        for(auto& [id,e]:entries_)if(e->initialized)e->post.setImageLoader(loader_);
    }
    void bindWorld(const World& world) {
        if(world_ && world_!=&world)
            for(auto& [id,e]:entries_) {
                (void)id;e->screen.reset();e->post.resetUnderwater();
                e->signature={};clearLens(*e);
            }
        world_=&world;
        for(auto& it:entries_)if(!world.alive(Entity::fromPacked(it.first))) {
            it.second->screen.reset();
            it.second->post.resetUnderwater();
            it.second->signature={};
            clearLens(*it.second);
        }
    }
    // Returns transient jittered view copy and tracks history invalidation
    // for this camera. Missing PostFX resources return an unchanged view.
    Camera prepare(const EcsCameraView& view,int width,int height) {
        Camera camera=view.camera;
        if(!device_ || width<=0 || height<=0)return camera;
        auto& entry=entryFor(view.entity.packed());
        if(!ensure(entry))return camera;
        const auto sig=cameraSignature(view,width,height);
        if(invalidateCameraHistory(entry.signature,sig))entry.screen.reset();
        entry.signature=sig;
        applyLens(entry,view,height);
        if(temporalEnabled_)return entry.screen.jittered(camera,width,height);
        return camera;
    }
    PostProcess* get(Entity entity) {
        auto it=entries_.find(entity.packed());
        if(it==entries_.end() || !it->second->initialized)return nullptr;
        if(temporalEnabled_ && it->second->hasScreen)
            return &it->second->screen;
        return &it->second->post;
    }
    void resetHistory(Entity entity) {
        auto it=entries_.find(entity.packed());
        if(it!=entries_.end()) {
            it->second->screen.reset();it->second->signature={};
        }
    }
    // A camera entity can be forgotten once all pending GPU submissions have
    // finished; otherwise leave resource ownership intact and reset history.
    void invalidate(Entity entity) {resetHistory(entity);}
    size_t cameraCount()const{return entries_.size();}
    void shutdownAfterGpuIdle() {
        if(!device_)return;
        for(auto& [id,e]:entries_) {
            (void)id;
            if(e->hasScreen)e->screen.shutdown();
            if(e->initialized)e->post.shutdown();
        }
        entries_.clear();world_=nullptr;device_=nullptr;
    }
    // Diagnostics: what the lens currently drives for this camera.
    struct LensState { bool exposure=false, depthOfField=false; float ev=0;
                       Prisma::DepthOfFieldOverride dof{}; };
    LensState lensState(Entity entity)const {
        auto it=entries_.find(entity.packed());
        if(it==entries_.end())return {};
        const Entry& e=*it->second;
        return {e.lensExposure,e.lensDof,e.lensEV,e.lensDofValues};
    }
private:
    static bool sameDof(const Prisma::DepthOfFieldOverride& a,
                        const Prisma::DepthOfFieldOverride& b) {
        return a.enabled==b.enabled&&a.focusDistance==b.focusDistance&&
               a.focusRangeMeters==b.focusRangeMeters&&a.maxBlur==b.maxBlur;
    }
    void clearLens(Entry& e) {
        if(!e.initialized){e.lensExposure=e.lensDof=false;return;}
        const uint64_t id=e.post.viewId();
        if(e.lensExposure)e.post.clearViewManualExposure(id);
        if(e.lensDof)e.post.clearViewDepthOfField(id);
        e.lensExposure=e.lensDof=false;
    }
    void applyLens(Entry& e,const EcsCameraView& view,int height) {
        const CameraLensComponent* lens=world_&&world_->alive(view.entity)?
            world_->tryGetConst<CameraLensComponent>(view.entity):nullptr;
        const uint64_t id=e.post.viewId();
        const CameraLensValues v=lens?cameraLensValues(*lens):CameraLensValues{};
        const bool valid=lens&&cameraLensValuesValid(v);
        if(lens&&lens->driveExposure&&valid) {
            const float ev=float(cameraLensExposureStops(v,lens->exposureCompensation.toDouble()));
            if(!e.lensExposure||e.lensEV!=ev) {
                e.post.setViewManualExposure(id,true,ev);
                e.lensExposure=true;e.lensEV=ev;
            }
        } else if(e.lensExposure) {
            e.post.clearViewManualExposure(id);e.lensExposure=false;
        }
        if(lens&&lens->driveDepthOfField) {
            Prisma::DepthOfFieldOverride d{};
            const auto pass=valid?cameraLensDofPass(cameraLensDepthOfField(v,height)):
                                  CameraDofPassValues{};
            if(pass.valid) {
                d.focusDistance=pass.focusDistance;
                d.focusRangeMeters=pass.focusRange;
                d.maxBlur=pass.maxBlurPx;
            } else d.enabled=false; // an invalid lens never blurs with stale focus
            if(!e.lensDof||!sameDof(e.lensDofValues,d)) {
                e.post.setViewDepthOfField(id,d);
                e.lensDof=true;e.lensDofValues=d;
            }
        } else if(e.lensDof) {
            e.post.clearViewDepthOfField(id);e.lensDof=false;
        }
    }
    Entry& entryFor(uint64_t entity) {
        auto& ptr=entries_[entity];
        if(!ptr)ptr=std::make_unique<Entry>();
        return *ptr;
    }
    bool ensure(Entry& e) {
        if(e.failed)return false;
        if(!e.initialized) {
            if(!e.post.init(*device_)){e.post.shutdown();e.failed=true;return false;}
            e.initialized=true;
            e.post.setSettings(settings_);e.post.setQuality(quality_);
            if(loader_)e.post.setImageLoader(loader_);
        }
        if(temporalEnabled_ && !e.hasScreen) {
            if(!e.screen.init(*device_,e.post)) {
                e.screen.shutdown();e.failed=true;return false;
            }
            e.hasScreen=true;
        }
        if(e.hasScreen)e.screen.setEnabled(temporalEnabled_);
        return true;
    }
};
} // namespace aiverse::render
