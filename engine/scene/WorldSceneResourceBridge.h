// Concrete bridge: scene dependency -> existing world::WorldScene streamer.
// Reuses WorldScene's AsyncChunkStream, NEREID water/cascades and shared
// ModelTextureCache; does not create another terrain/geometry loader.
// All publish/update/draw/retire methods run on the graphics owner thread.
#pragma once
#include "SceneResourceBackend.h"
#include "engine/world/WorldScene.h"
#include <cstdio>
#include <exception>
#include <functional>
#include <map>
#include <set>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::scene {

class WorldSceneResourceBridge {
public:
    using ManifestReader = std::function<bool(const std::string&,
                                              std::string&, std::string&)>;
    WorldSceneResourceBridge(gfx::Device& device, ManifestReader manifestReader,
                             world::AssetReader contentReader,
                             std::shared_ptr<render::ModelTextureCache> textures = {})
        : state_(std::make_shared<State>(
            device,std::move(manifestReader),std::move(contentReader),
            std::move(textures))) {}

    // Register returned hooks for a SceneResourceKind (typically Terrain).
    // The bridge's shared state is owned by the hooks as well, so the frontend
    // bridge may be moved/destroyed while references remain. Device and
    // external reader contexts must still outlive the resource backend.
    SceneResourceHooks hooks() const {
        const auto state = state_;
        SceneResourceHooks result;
        const ManifestReader read = state->manifestReader;
        result.prepare = [read](const std::string& key, std::string& error)
                             -> ScenePreparedResource {
            if(!read) {error="WorldScene manifest reader not configured";return {};}
            std::string bytes;
            if(!read(key,bytes,error)) {
                if(error.empty())error="WorldScene manifest read failed";
                return {};
            }
            if(bytes.size() > 4 * 1024 * 1024) {
                error="WorldScene manifest too large";
                return {};
            }
            try {
                auto manifest=std::make_shared<Json>(Json::parse(bytes));
                // Validate immutable chunk catalogue on worker; WorldScene
                // constructor will parse it again on the owner thread.
                (void)world::WorldAssets::parse(*manifest);
                return manifest;
            } catch(const std::exception& ex) {
                error=ex.what();
                return {};
            }
        };
        result.publish = [state](const std::string& key,
                                 const ScenePreparedResource& prepared,
                                 std::string& error) {
            if(state->instances.contains(key))return true;
            try {
                auto manifest=std::static_pointer_cast<Json>(prepared);
                if(!manifest) {
                    error="WorldScene manifest payload missing";
                    return false;
                }
                // Reuse the actual WorldScene streaming implementation and
                // original model texture cache, rather than copying assets.
                auto scene=std::make_unique<world::WorldScene>(
                    *manifest,state->contentReader,state->textures);
                state->instances.emplace(key,std::move(scene));
                return true;
            } catch(const std::exception& ex) {
                error=ex.what();
                return false;
            }
        };
        result.retire = [state](const std::string& key) noexcept {
            // SceneResourceHooks::retire must not throw, but
            // WorldScene::shutdown throws when its GPU barrier fails.
            // Detach the instance from the key first (the key can be
            // republished), then park a failed instance so its GPU
            // resources are torn down later instead of leaking/aborting.
            const auto it=state->instances.find(key);
            if(it==state->instances.end())return;
            std::unique_ptr<world::WorldScene> scene=std::move(it->second);
            state->instances.erase(it);
            state->updated.erase(key);
            shutdownOrPark(*state,key,std::move(scene));
        };
        return result;
    }

    // Continue existing WorldScene chunk streaming per frame. Shared scene
    // dependencies are updated once per key and never duplicated per owner.
    void update(std::span<const Vec3> observers,int radius=2) {
        retryParkedShutdowns();
        for(auto& [key,scene] : state_->instances) {
            scene->update(*state_->device,observers,radius);
            state_->updated.insert(key);
        }
    }

    void draw(render::Renderer3D& renderer,world::Cell origin,int cellSize) {
        for(auto& [key,scene] : state_->instances) {
            (void)key;
            scene->draw(renderer,origin,cellSize);
        }
    }

    // WorldScene::status().complete() reports actual selected chunk
    // residency, not just that the wrapper has been published. Hosts may
    // use this as a presentation gate, never as an ECS gameplay decision.
    bool streamingReady(const std::string& key) const {
        const auto it=state_->instances.find(key);
        return it!=state_->instances.end() && state_->updated.contains(key) &&
               it->second->status().complete();
    }

    size_t sceneCount() const {return state_->instances.size();}

    // Retired WorldScenes whose shutdown failed (GPU barrier) are parked
    // here and retried on the owner thread by update() or explicitly.
    // Returns the number of parked scenes still awaiting teardown.
    size_t retryParkedShutdowns() {
        auto parked=std::move(state_->parked);
        state_->parked.clear();
        for(auto& entry : parked)
            shutdownOrPark(*state_,entry.key,std::move(entry.scene));
        return state_->parked.size();
    }
    size_t parkedCount() const {return state_->parked.size();}
    const std::string& lastRetireError() const {return state_->lastRetireError;}
private:
    struct State {
        State(gfx::Device& d, ManifestReader manifest,
              world::AssetReader content,
              std::shared_ptr<render::ModelTextureCache> cache)
            : device(&d),manifestReader(std::move(manifest)),
              contentReader(std::move(content)),textures(std::move(cache)) {}
        gfx::Device* device;
        ManifestReader manifestReader;
        world::AssetReader contentReader;
        std::shared_ptr<render::ModelTextureCache> textures;
        std::map<std::string,std::unique_ptr<world::WorldScene>> instances;
        std::set<std::string> updated;
        struct Parked {
            std::string key;
            std::unique_ptr<world::WorldScene> scene;
        };
        std::vector<Parked> parked;
        std::string lastRetireError;
    };
    static void shutdownOrPark(State& state,const std::string& key,
                               std::unique_ptr<world::WorldScene> scene) noexcept {
        if(!scene)return;
        std::string error;
        try {
            scene->shutdown(*state.device);
            return;
        } catch(const std::exception& ex) {
            error=ex.what();
        } catch(...) {
            error="unknown WorldScene shutdown exception";
        }
        try {
            state.lastRetireError="WorldScene '"+key+"' retire deferred: "+error;
            std::fprintf(stderr,"[scene] %s\n",state.lastRetireError.c_str());
            state.parked.push_back({key,std::move(scene)});
        } catch(...) {
            // Out of memory while parking: the scene is released by its
            // destructor here; nothing else can be done without throwing.
        }
    }
    std::shared_ptr<State> state_;
};

} // namespace aiverse::scene
