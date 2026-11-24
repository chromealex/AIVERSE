// Host-managed AnimationGraph hot reload with last-good fallback.
// No global singleton, no mutation of simulation or compiled graph.
#pragma once
#include "engine/animation/EcsAnimationSystems.h"
#include "engine/render/AnimationAsset.h"
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <string>
#include <utility>
namespace aiverse::lib::animation {
struct AnimationGraphKeyComponent { std::string asset; };
class AnimationHotReloadRegistry {
public:
    struct Entry {
        const render::Model* model=nullptr;
        std::shared_ptr<const render::AnimationGraph> graph;
        uint64_t revision=0;
        std::string error;
        std::filesystem::file_time_type stamp{};
        bool hasStamp=false;
    };
    bool compile(const std::string& key,const render::Model& model,
                 const Json& definition,uint64_t revision) {
        if(key.empty() || key.size()>128)return false;
        auto& entry=cache_[key];
        if(entry.graph && revision<=entry.revision && entry.model==&model)
            return false;
        try {
            auto compiled=render::AnimationGraph::compile(model,definition);
            entry.model=&model;entry.graph=std::move(compiled);
            entry.revision=revision;entry.error.clear();
            return true;
        }catch(const std::exception& e){
            entry.error=e.what(); // last known working version remains active
            return false;
        }
    }
    // Optional host polling. Content changes are compiled transactionally.
    // An invalid edit keeps previous graph and remains visible as an error.
    bool pollFile(const std::string& key,const render::Model& model,
                  const std::filesystem::path& file) {
        std::error_code ec;
        const auto stamp=std::filesystem::last_write_time(file,ec);
        if(ec){cache_[key].error=ec.message();return false;}
        auto& entry=cache_[key];
        if(entry.hasStamp && entry.stamp==stamp && entry.model==&model)
            return false;
        std::ifstream stream(file,std::ios::binary);
        if(!stream){entry.error="cannot read animation asset";return false;}
        std::string data((std::istreambuf_iterator<char>(stream)),{});
        if(data.size()>256*1024){entry.error="asset exceeds 256 KiB";return false;}
        try {
            const Json definition=render::parseAnimationAsset(data);
            const bool changed=compile(key,model,definition,entry.revision+1);
            entry.stamp=stamp;entry.hasStamp=true;
            return changed;
        }catch(const std::exception& e){
            entry.error=e.what();
            entry.stamp=stamp;entry.hasStamp=true;
            return false;
        }
    }
    const Entry* lookup(const std::string& key)const {
        const auto it=cache_.find(key);
        return it==cache_.end()?nullptr:&it->second;
    }
    // Host invokes this after detaching every presentation source that
    // borrows model, before releasing its GPU/CPU model allocations.
    // It also drops the last-good graph to prevent use-after-free if a
    // host allocator later reuses the same Model pointer address.
    size_t forgetModel(const render::Model* model) {
        if(!model)return 0;
        size_t removed=0;
        for(auto it=cache_.begin();it!=cache_.end();) {
            if(it->second.model==model) {
                it=cache_.erase(it);
                ++removed;
            } else ++it;
        }
        return removed;
    }
    // Call on owning render thread after file IO/compilation and before
    // animation update. Existing players restart only after graph swap;
    // stale runtime instances cannot outlive the asset's host model.
    size_t apply(World& presentation)const {
        auto* assets=presentation.prepareExistingWrite<AnimationAssetComponent>();
        if(!assets)return 0;
        size_t updated=0;
        for(size_t i=0;i<assets->size();++i){
            const auto entity=assets->entityAt(i);
            if(!presentation.alive(entity))continue;
            const auto* key=static_cast<const World&>(presentation)
                .tryGetConst<AnimationGraphKeyComponent>(entity);
            if(!key)continue;
            const auto* entry=lookup(key->asset);
            if(!entry || !entry->graph)continue;
            auto& asset=assets->dataAt(i);
            if(asset.model!=entry->model || asset.graph==entry->graph)continue;
            asset.graph=entry->graph;
            // A graph revision invalidates node state/clip clocks even when
            // its host Model pointer is unchanged; sync() can apply later,
            // but a direct registry.apply must be safe on its own.
            if(auto* player=presentation.tryGet<AnimationPlayerComponent>(entity))
                player->restart=true;
            if(auto* slots=presentation.tryGet<AnimationMontageSlotsComponent>(entity)) {
                slots->players.clear();
                slots->events.clear();
            }
            if(auto* events=presentation.tryGet<AnimationGraphNotifiesComponent>(entity))
                events->events.clear();
            ++updated;
        }
        return updated;
    }
private:
    std::map<std::string,Entry> cache_;
};
} // namespace aiverse::lib::animation
