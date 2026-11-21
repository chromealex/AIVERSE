// Optional SceneLoadCoordinator dependency adapter that reuses the existing
// world::CollisionStream and its hashed, asynchronous chunk preparation.
// Each source leases a deterministic, disjoint ID namespace from
// SceneCollisionNamespaces; no parallel collision storage/cache is created.
#pragma once
#include "engine/scene/SceneCollisionNamespaces.h"
#include "engine/scene/SceneLoadCoordinator.h"
#include <map>
#include <memory>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>

namespace aiverse::scene {

class SceneCollisionBridge {
public:
    SceneCollisionBridge(world::SpatialWorld& target,
                         SceneCollisionNamespaces& namespaces)
        :target_(target),namespaces_(namespaces){}

    SceneCollisionBridge(const SceneCollisionBridge&)=delete;
    SceneCollisionBridge& operator=(const SceneCollisionBridge&)=delete;

    // Call before any SceneRuntime scene loads. A source refers to a
    // collision WorldAssets manifest (not the SceneAsset manifest).
    // namespaceKey identifies the stable unique scene *instance*.
    // One key per source; never attach the same range twice at once.
    bool define(std::string resourceKey,std::string namespaceKey,
                Json manifest,world::AssetReader reader) {
        if(!SceneAsset::validKey(resourceKey) ||
           !SceneAsset::validKey(namespaceKey) || !reader ||
           sources_.contains(resourceKey) ||
           namespaceKeys_.contains(namespaceKey) ||
           !namespaces_.sealed())
            return false;
        const auto range=namespaces_.find(namespaceKey);
        if(!range)return false;
        try {
            const auto catalog=world::WorldAssets::parse(manifest);
            if(catalog.chunks.size()>range->slotCount)return false;
        }catch(const std::exception&){return false;}
        Sources source{std::move(namespaceKey),std::move(manifest),
                       std::move(reader)};
        namespaceKeys_.insert(source.namespaceKey);
        sources_.emplace(std::move(resourceKey),std::move(source));
        return true;
    }

    // Compose this adapter with other SceneResourceAdapter providers for
    // mixed dependencies (render/audio/physics) in the game host.
    SceneResourceAdapter adapter() {
        return {
            [this](const std::string& key,std::string& error){
                return acquire(key,error);
            },
            [this](const std::string& key){release(key);}
        };
    }

    // Reference counted per key: several SceneLoadCoordinators (or other
    // owners) may share one bridge; each successful acquire() must be
    // matched by one release(). The stream lives until the last release.
    bool acquire(const std::string& key,std::string& error) {
        if(const auto found=resident_.find(key);found!=resident_.end()) {
            ++found->second.refs;
            return true;
        }
        const auto it=sources_.find(key);
        if(it==sources_.end()) {
            error="unknown scene collision source: "+key;
            return false;
        }
        auto lease=namespaces_.acquire(it->second.namespaceKey);
        if(!lease) {
            error="scene collision range already in use: "+key;
            return false;
        }
        Resident record;
        record.lease=std::move(lease);
        try {
            record.stream=std::make_unique<world::CollisionStream>(
                it->second.manifest,it->second.reader,
                target_,record.lease->range());
        } catch(const std::exception& e) {
            error=e.what();
            return false; // Local Resident destructs stream then lease.
        }
        resident_.emplace(key,std::move(record));
        return true;
    }

    void release(const std::string& key) {
        const auto it=resident_.find(key);
        if(it==resident_.end())return;
        if(--it->second.refs>0)return;
        // The stream is destroyed first (removes only its collider IDs),
        // then the move-only namespace lease is returned to the catalog.
        resident_.erase(it);
    }
    size_t leaseCount(const std::string& key)const noexcept {
        const auto it=resident_.find(key);
        return it==resident_.end()?0:it->second.refs;
    }

    // Poll actual collision chunks and publish prepared shapes before
    // Simulation::step. False MUST gate GameModule::readyToStep when any
    // requested gameplay region requires collision that is not resident.
    // These are presentation/host updates, not authoritative ECS commands.
    bool prepareRegions(std::span<const world::Bounds> regions,
                        size_t requests=4,size_t publications=4) {
        bool all=true;
        for(auto& [key,entry]:resident_) {
            (void)key;
            if(!entry.stream->prepareRegions(regions,requests,publications))
                all=false;
        }
        return all;
    }

    bool covers(world::Bounds region)const {
        for(const auto& [key,entry]:resident_) {
            (void)key;
            if(!entry.stream->covers(region))return false;
        }
        return true;
    }
    size_t residentSceneResources()const noexcept{return resident_.size();}
    size_t definedSources()const noexcept{return sources_.size();}

private:
    struct Sources {
        std::string namespaceKey;
        Json manifest;
        world::AssetReader reader;
    };
    struct Resident {
        // Declaration order ensures stream destructor before lease
        // destructor: members are destroyed in reverse declaration order.
        std::optional<SceneCollisionNamespaces::Lease> lease;
        std::unique_ptr<world::CollisionStream> stream;
        size_t refs=1;
    };

    world::SpatialWorld& target_;
    SceneCollisionNamespaces& namespaces_;
    std::map<std::string,Sources> sources_;
    std::set<std::string> namespaceKeys_;
    std::map<std::string,Resident> resident_;
};

} // namespace aiverse::scene
