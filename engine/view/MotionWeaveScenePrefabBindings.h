// AIVERSE MotionWeave — project-supplied prefab -> visual scene bootstrap.
// Pure presentation adapter. The canonical SceneLoadCoordinator remains the
// only authority for prefab spawning, structural commits, Replace and restore.
#pragma once

#include "MotionWeaveSceneAssetBuilder.h"
#include <cstddef>
#include <exception>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::view {

// Bind a project's prefab IDs to visual Model/Graph builders ONCE, before the
// first SceneTicket. Explicitly declare nonvisual prefabs; an unknown prefab
// is an error, never silently treated as an invisible game entity.
//
// Each sealed catalogue belongs to one immutable GPU resource pack and one
// owner-thread GameHost / device epoch. A key must change when its model or
// Graph content changes. The catalogue never owns authoritative ECS state.
class MotionWeaveScenePrefabBindings final {
public:
    using Host=MotionWeaveSceneGameHost;
    using Object=Host::Object;
    using Owner=Host::Owner;
    using Resolve=MotionWeaveSceneAssetBuilder::Resolve;
    using ResolveResident=MotionWeaveSceneAssetBuilder::ResolveResident;

    MotionWeaveScenePrefabBindings()=default;
    MotionWeaveScenePrefabBindings(const MotionWeaveScenePrefabBindings&)=delete;
    MotionWeaveScenePrefabBindings& operator=(
        const MotionWeaveScenePrefabBindings&)=delete;

    bool registerVisual(std::string prefab,Resolve load,
                        ResolveResident reuse,std::string* error=nullptr) {
        if(!load || !reuse)
            return fail(error,"visual prefab needs both fresh and resident resolvers");
        return registerEntry(std::move(prefab),
                             Entry{std::move(load),std::move(reuse),false},
                             error);
    }

    // Markers, triggers and collision-only prefabs must be declared
    // explicitly. They still spawn in canonical authoritative SceneRuntime.
    bool registerNonvisual(std::string prefab,
                           std::string* error=nullptr) {
        return registerEntry(std::move(prefab),Entry{{},{},true},error);
    }

    // Sealing prevents a scene from switching prefab -> Model semantics
    // while a shared ModelAssetProvider is live. Resource key includes
    // content/rig/Graph version and is scoped to the owning RHI generation.
    bool seal(std::string immutablePackKey,std::string* error=nullptr) {
        if(sealed_)return fail(error,"visual prefab registry already sealed");
        if(entries_.empty())return fail(error,"visual prefab registry is empty");
        if(immutablePackKey.empty() || immutablePackKey.size()>256)
            return fail(error,"invalid immutable visual residency key");
        key_=std::move(immutablePackKey);
        sealed_=true;
        if(error)error->clear();
        return true;
    }
    bool sealed() const noexcept{return sealed_;}
    const std::string& residencyKey() const noexcept{return key_;}
    size_t prefabCount() const noexcept{return entries_.size();}

    bool resolveFresh(const scene::SceneObjectAsset& object,
                      render::ModelAssetProvider& provider,
                      gfx::Device& device,std::optional<Object>& out,
                      std::string& error) const {
        out.reset();
        const Entry* entry=lookup(object,error);
        if(!entry)return false;
        if(entry->nonvisual){error.clear();return true;}
        try {
            if(!entry->fresh(object,provider,device,out,error)) {
                out.reset();
                if(error.empty())error="fresh visual prefab resolver rejected";
                return annotate(object,error);
            }
            if(!out) {
                error="visual prefab returned no appearance; declare nonvisual explicitly";
                return annotate(object,error);
            }
            error.clear();
            return true;
        }catch(const std::exception& ex) {
            out.reset();
            error=ex.what();
            return annotate(object,error);
        }catch(...) {
            out.reset();
            error="unknown fresh visual prefab exception";
            return annotate(object,error);
        }
    }

    bool resolveResident(const scene::SceneObjectAsset& object,
                         const render::ModelAssetProvider& provider,
                         std::optional<Object>& out,
                         std::string& error) const {
        out.reset();
        const Entry* entry=lookup(object,error);
        if(!entry)return false;
        if(entry->nonvisual){error.clear();return true;}
        try {
            if(!entry->resident(object,provider,out,error)) {
                out.reset();
                if(error.empty())error="resident visual prefab resolver rejected";
                return annotate(object,error);
            }
            if(!out) {
                error="resident visual prefab returned no appearance";
                return annotate(object,error);
            }
            error.clear();
            return true;
        }catch(const std::exception& ex) {
            out.reset();
            error=ex.what();
            return annotate(object,error);
        }catch(...) {
            out.reset();
            error="unknown resident visual prefab exception";
            return annotate(object,error);
        }
    }

    // Real synchronous structural barrier: NEVER call from the background
    // manifest reader or while recording a GPU frame. The native coordinator
    // owns commit/rollback; this wrapper only provides visual sources.
    bool commitReady(Host& host,scene::SceneTicket ticket,
                     gfx::Device& device,
                     render::RenderDeviceLifetime::Lease lease,
                     render::FileReader reader,Host::Statistics& stats,
                     std::string* error=nullptr) const {
        if(!sealed_) {
            if(error)*error="visual prefab catalogue must be sealed before activation";
            if(host.ready(ticket))(void)host.cancel(ticket);
            return false;
        }
        MotionWeaveSceneAssetBuilder builder(
            device,std::move(lease),std::move(reader),
            [this](const scene::SceneObjectAsset& obj,
                   render::ModelAssetProvider& provider,gfx::Device& gpu,
                   std::optional<Object>& out,std::string& reason) {
                return resolveFresh(obj,provider,gpu,out,reason);
            });
        return host.commitReady(ticket,builder,stats,error);
    }

    // Existing-key scenes use an immutable/read-only resident provider.
    // First-key scenes create one complete native ModelAssetProvider. Missing
    // later-scene assets reject the ticket; never mutate a live shared cache.
    bool commitReadyShared(Host& host,scene::SceneTicket ticket,
                           gfx::Device& device,
                           render::RenderDeviceLifetime::Lease lease,
                           render::FileReader reader,Host::Statistics& stats,
                           std::string* error=nullptr) const {
        if(!sealed_) {
            if(error)*error="visual prefab catalogue must be sealed before activation";
            if(host.ready(ticket))(void)host.cancel(ticket);
            return false;
        }
        MotionWeaveSceneAssetBuilder fresh(
            device,std::move(lease),std::move(reader),
            [this](const scene::SceneObjectAsset& obj,
                   render::ModelAssetProvider& provider,gfx::Device& gpu,
                   std::optional<Object>& out,std::string& reason) {
                return resolveFresh(obj,provider,gpu,out,reason);
            });
        auto build=[this,&fresh](const scene::SceneAsset& manifest,
                                const render::ModelAssetProvider* resident,
                                Owner& next,std::vector<Object>& objects,
                                std::string& reason)->bool {
            if(resident) {
                if(next){reason="resident prefab builder cannot replace provider";return false;}
                return MotionWeaveSceneAssetBuilder::buildResident(
                    manifest,*resident,
                    [this](const scene::SceneObjectAsset& obj,
                           const render::ModelAssetProvider& provider,
                           std::optional<Object>& out,std::string& error) {
                        return resolveResident(obj,provider,out,error);
                    },objects,reason);
            }
            return fresh(manifest,next,objects,reason);
        };
        return host.commitReadyShared(ticket,key_,build,stats,error);
    }

private:
    struct Entry {
        Resolve fresh;
        ResolveResident resident;
        bool nonvisual=false;
    };
    static bool fail(std::string* error,const char* reason) {
        if(error)*error=reason;
        return false;
    }
    static bool annotate(const scene::SceneObjectAsset& obj,
                         std::string& error) {
        error="prefab '"+obj.prefab+"' (scene object "+
              std::to_string(obj.id)+"): "+error;
        return false;
    }
    bool registerEntry(std::string prefab,Entry entry,
                       std::string* error) {
        if(sealed_)return fail(error,"cannot modify a sealed visual prefab catalogue");
        if(!scene::SceneAsset::validKey(prefab))
            return fail(error,"invalid visual prefab key");
        const auto [it,inserted]=entries_.emplace(std::move(prefab),
                                                  std::move(entry));
        (void)it;
        if(!inserted)return fail(error,"duplicate visual prefab registration");
        if(error)error->clear();
        return true;
    }
    const Entry* lookup(const scene::SceneObjectAsset& obj,
                        std::string& error) const {
        if(!sealed_) {
            error="visual prefab catalogue is not sealed";
            annotate(obj,error);
            return nullptr;
        }
        const auto it=entries_.find(obj.prefab);
        if(it==entries_.end()) {
            error="unknown visual prefab (declare it nonvisual if intentional)";
            annotate(obj,error);
            return nullptr;
        }
        return &it->second;
    }
    std::map<std::string,Entry> entries_;
    std::string key_;
    bool sealed_=false;
};

} // namespace aiverse::view
