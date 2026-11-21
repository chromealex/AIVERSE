// AIVERSE — scene/prefab lifecycle adapter for genre-neutral cosmetic
// presentation ECS attachment instances.
//
// This header deliberately does NOT include SceneLoadCoordinator: the
// multi-world scene loader lives on another feature branch and owns the
// authoritative World. A host binds these methods to its structural scene
// activation/unload callbacks, supplying PRESENTATION Entities only.
#pragma once
#include "EcsAttachmentScene.h"
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>

namespace aiverse::view {

class SceneAttachmentHost final {
public:
    using Snapshot=EcsAttachmentScene::AssetSnapshot;
    using Resolver=EcsAttachmentScene::Resolver;
    using Prepared=ImportedAttachmentAuthoring::Prepared;

    // Native scene instance key; never a raw ECS index. Every object within
    // a scene has a host-supplied persistent ID, independent of Entity slot
    // reuse and render/model asset publication epochs.
    struct Key {
        uint64_t scene=0;
        std::string object;
    };
    struct KeyLess {
        bool operator()(const Key& a,const Key& b) const noexcept {
            if(a.scene!=b.scene)return a.scene<b.scene;
            return a.object<b.object;
        }
    };

    explicit SceneAttachmentHost(World& presentation)
        :presentation_(&presentation){}
    SceneAttachmentHost(const SceneAttachmentHost&)=delete;
    SceneAttachmentHost& operator=(const SceneAttachmentHost&)=delete;

    // Optional binding for a host's native SceneLoadCoordinator::SpawnPrefab
    // adapter. All callbacks must run at the owner structural barrier, AFTER
    // source+child assets have been published and GPU ownership pinned.
    // No worker thread calls activate(); the scene loader's async poll is
    // non-authoritative and must never mutate this presentation World.
    bool activate(Key key,Entity visualParent,
                  const Json& authored,
                  const Snapshot& source,
                  const render::ModelResourceCache& cache,
                  const Resolver& resolveChild,
                  std::string* error=nullptr) {
        if(error)error->clear();
        try {
            if(key.scene==0 || key.object.empty() ||
               key.object.size()>256)
                throw std::invalid_argument(
                    "scene attachment key needs scene ID and stable object name");
            if(!presentation_ || !presentation_->alive(visualParent))
                throw std::invalid_argument(
                    "scene visual parent not alive in presentation World");

            auto found=instances_.find(key);
            if(found!=instances_.end() &&
               found->second.parent!=visualParent) {
                // Scene replacement/rehydration moved the parent into a
                // fresh Entity slot. Stage full new child generation FIRST,
                // keeping old scene visuals until publication succeeds.
                EcsAttachmentScene::Instance replacement;
                if(!EcsAttachmentScene::restore(
                    *presentation_,visualParent,authored,source,cache,
                    resolveChild,replacement,error))
                    return false;
                EcsAttachmentScene::forget(*presentation_,found->second);
                found->second=std::move(replacement);
                return true;
            }

            // Reserve an empty map slot BEFORE any ECS World structural
            // mutation, so a failed map allocation cannot strand live
            // presentation children with no owning instance.
            bool inserted=false;
            if(found==instances_.end()) {
                auto created=instances_.try_emplace(std::move(key));
                found=created.first;
                inserted=created.second;
            }
            if(!EcsAttachmentScene::restore(
                *presentation_,visualParent,authored,source,cache,
                resolveChild,found->second,error)) {
                if(inserted)instances_.erase(found);
                return false;
            }
            return true;
        }catch(const std::exception& ex) {
            if(error)*error=ex.what();
            return false;
        }
    }

    // Detach exactly this scene-owned visual object, not the externally
    // owned visual parent or any authoritative scene Entity.
    bool deactivate(uint64_t scene,const std::string& object) {
        const auto it=instances_.find(Key{scene,object});
        if(it==instances_.end())return false;
        EcsAttachmentScene::forget(*presentation_,it->second);
        instances_.erase(it);
        return true;
    }

    // Called by the native scene loader's unload/replacement barrier AFTER
    // its render scene has stopped producing new draws. CPU Model read
    // leases and GPU fence retirement still belong to the asset host.
    size_t unload(uint64_t scene) {
        size_t count=0;
        for(auto it=instances_.lower_bound(Key{scene,{}});it!=instances_.end()
            && it->first.scene==scene;) {
            EcsAttachmentScene::forget(*presentation_,it->second);
            it=instances_.erase(it);
            ++count;
        }
        return count;
    }
    void clear() {
        for(auto& [key,instance]:instances_) {
            (void)key;
            EcsAttachmentScene::forget(*presentation_,instance);
        }
        instances_.clear();
    }

    bool contains(uint64_t scene,const std::string& object) const {
        return instances_.find(Key{scene,object})!=instances_.end();
    }
    size_t size() const noexcept{return instances_.size();}

    // Stable, pointer-free attachment export for a host scene manifest.
    // Scene/object keys are saved; raw presentation Entity handles are not.
    // The host rebinds each object to a FRESH parent Entity on activation.
    Json saveScene(uint64_t scene) const {
        Json objects=Json::array();
        for(auto it=instances_.lower_bound(Key{scene,{}});it!=instances_.end()
            && it->first.scene==scene;++it) {
            Json entry=Json::object();
            entry.set("object",Json::string(it->first.object));
            entry.set("layout",EcsAttachmentScene::save(it->second));
            objects.push(std::move(entry));
        }
        return objects;
    }

    Json save(uint64_t scene,const std::string& object) const {
        const auto it=instances_.find(Key{scene,object});
        if(it==instances_.end())
            throw std::out_of_range("scene attachment instance not found");
        return EcsAttachmentScene::save(it->second);
    }
    bool prepare(uint64_t scene,const std::string& object,
                 const render::ModelResourceCache& cache,
                 const render::mat4& world,
                 Prepared& out,std::string* error=nullptr) const {
        if(error)error->clear();
        const auto it=instances_.find(Key{scene,object});
        if(it==instances_.end()) {
            if(error)*error="scene attachment instance not found";
            return false;
        }
        return EcsAttachmentScene::prepare(
            *presentation_,it->second,cache,world,out,error);
    }

    // Stable authored identity is resolved anew after EACH scene source
    // reimport/restore; caller never keeps an old ECS slot across reload.
    Entity resolveChild(uint64_t scene,const std::string& object,
                        const std::string& childId) const {
        const auto* instance=find(scene,object);
        return instance
            ?EcsAttachmentScene::resolveChild(*presentation_,*instance,childId)
            :kNullEntity;
    }

    const EcsAttachmentScene::Instance* find(
            uint64_t scene,const std::string& object) const {
        const auto it=instances_.find(Key{scene,object});
        return it==instances_.end()?nullptr:&it->second;
    }

private:
    // Owner-thread only: do not retain this bridge after its World dies.
    // Caller MUST invoke clear() or unload(scene) at the native scene
    // retirement barrier; destructor cannot safely mutate a dead World.
    World* presentation_=nullptr;
    std::map<Key,EcsAttachmentScene::Instance,KeyLess> instances_;
};

} // namespace aiverse::view
