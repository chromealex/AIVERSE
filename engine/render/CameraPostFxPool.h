// AIVERSE LENS: owned per-ECS-camera PostFx and ScreenLighting.
// No genre dependencies. Each camera has separate TAA/SSR history, exposure,
// jitter and intermediate textures. The host supplies settings and shaders.
// Call shutdownAfterGpuIdle() only after queue completion, before Device dies.
#pragma once
#include "CameraTemporalPolicy.h"
#include "ScreenLighting.h"
#include <cstdint>
#include <memory>
#include <unordered_map>

namespace aiverse::render {
class CameraPostFxPool {
    struct Entry {
        PostFx post;
        ScreenLighting screen;
        CameraTemporalSignature signature{};
        bool initialized=false;
        bool hasScreen=false;
        bool failed=false;
    };
    gfx::Device* device_=nullptr;
    const World* world_=nullptr;
    PostFxSettings settings_{};
    PostFxQuality quality_=PostFxQuality::Medium;
    bool temporalEnabled_=false;
    PostFx::ImageLoader loader_{};
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
    void setImageLoader(PostFx::ImageLoader loader) {
        loader_=std::move(loader);
        for(auto& [id,e]:entries_)if(e->initialized)e->post.setImageLoader(loader_);
    }
    void bindWorld(const World& world) {
        if(world_ && world_!=&world)
            for(auto& [id,e]:entries_) {
                (void)id;e->screen.reset();e->post.resetUnderwater();
                e->signature={};
            }
        world_=&world;
        for(auto& it:entries_)if(!world.alive(Entity::fromPacked(it.first))) {
            it.second->screen.reset();
            it.second->post.resetUnderwater();
            it.second->signature={};
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
private:
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
