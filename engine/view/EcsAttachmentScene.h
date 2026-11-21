// AIVERSE — genre-neutral presentation-ECS prefab/scene attachment bridge.
//
// This World is cosmetic presentation only; no authoritative simulation,
// physics, damage, network or genre mechanics. The host owns CPU Models and
// GPU fences. An animated source has exactly one pose writer; children
// in this bridge are STATIC socket attachments, not graph players.
#pragma once
#include "AttachmentLayout.h"
#include "ImportedAttachmentAuthoring.h"
#include "engine/core/ECS.h"
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::view {

struct SceneAttachmentRootComponent {
    uint64_t instanceRevision=0;
    uint64_t modelPublicationGeneration=0;
    uint32_t authoredChildren=0;
};
struct SceneAttachmentChildComponent {
    Entity parent{};
    uint64_t instanceRevision=0;
    uint32_t authoredIndex=0;
    std::string modelSource;
    std::string socketName;
    bool enabled=false;
    render::mat4 local{};
    render::mat4 childWorld{};
    // Stable authored prefab child key (may be empty in legacy layouts).
    std::string authoredId;
    bool animated=false;
    std::string animationGraph; // authored key, no Graph pointer
};

// One explicit presentation World source/root instance. It retains
// appearance metadata + compiled sockets, NOT Model or GPU ownership.
// Call forget() on the owner thread before destroying this World or freeing
// the corresponding ModelResourceCache. Never save raw Entity handles.
class EcsAttachmentScene final {
public:
    using AssetSnapshot=AppearanceAssetRegistry::Snapshot;
    using Resolver=std::function<AssetSnapshot(const std::string&)>;

    struct Instance {
        Entity parent{};
        uint64_t revision=0;
        AttachmentLayout document;
        ImportedAttachmentAuthoring::Compiled bindings;
        std::vector<Entity> children; // authored order, includes disabled
        bool active() const noexcept{return revision!=0;}
    };

    static constexpr size_t kMaxAuthoredChildren=4096;

    // Transactional replacement/restore for an already-live parent Entity.
    // All external asset/bone validation completes before touching World.
    // On failed imports, invalid sockets, duplicate parent ownership or
    // allocation errors, previous World children/root and Instance survive.
    static bool publish(
            World& presentation,
            Entity parent,
            const AttachmentLayout& layout,
            const AssetSnapshot& source,
            const render::ModelResourceCache& cache,
            const Resolver& resolveChild,
            Instance& out,
            std::string* error=nullptr) {
        if(error)error->clear();
        std::vector<Entity> created;
        try {
            if(!presentation.alive(parent))
                throw std::invalid_argument(
                    "attachment parent is not alive in presentation World");
            if(layout.attachments.size()>kMaxAuthoredChildren)
                throw std::invalid_argument(
                    "too many authored presentation attachment children");
            if(!resolveChild)
                throw std::invalid_argument("attachment resolver missing");
            if(!source || !source->model ||
               source->modelPublicationGeneration==0)
                throw std::invalid_argument(
                    "attachment source Model is not published");
            // Also validates all matrices, paths and socket names.
            (void)AttachmentLayoutCodec::write(layout);
            const auto* previous=presentation.tryGetConst<
                SceneAttachmentRootComponent>(parent);
            if(previous &&
               (previous->instanceRevision!=out.revision ||
                !out.active() || out.parent!=parent))
                throw std::logic_error(
                    "attachment parent belongs to a different scene instance");
            if(out.active() &&
               (!previous || out.parent!=parent ||
                previous->instanceRevision!=out.revision))
                throw std::logic_error("attachment scene owner lost its root");

            std::vector<ImportedAttachmentAuthoring::ChildRequest> requests;
            requests.reserve(layout.attachments.size());
            for(const auto& child:layout.attachments) {
                // An animated child owns its OWN Graph/pose source and must
                // never be included in a parent-pose static socket packet.
                if(!child.enabled || child.animated)continue;
                const auto resolved=resolveChild(child.model);
                if(!resolved)
                    throw std::invalid_argument(
                        "unresolved scene attachment child: "+child.model);
                requests.push_back(
                    ImportedAttachmentAuthoring::childFromLayout(
                        child,resolved));
            }
            ImportedAttachmentAuthoring::Compiled compiled;
            if(!ImportedAttachmentAuthoring::compile(
                source,cache,requests,compiled,error))
                return false;

            Instance candidate;
            candidate.parent=parent;
            candidate.revision=nextRevision();
            candidate.document=layout;
            candidate.bindings=std::move(compiled);
            candidate.children.reserve(layout.attachments.size());
            // World structural writes begin only after COMPLETE resolver,
            // Model read-lease, socket and document preflight succeeded.
            for(size_t i=0;i<layout.attachments.size();++i) {
                const auto& child=layout.attachments[i];
                Entity entity=presentation.create();
                created.push_back(entity);
                presentation.add<SceneAttachmentChildComponent>(
                    entity,SceneAttachmentChildComponent{
                        parent,candidate.revision,uint32_t(i),
                        child.model,child.socket,child.enabled,
                        child.local,child.childWorld,child.id,
                        child.animated,child.animationGraph});
                candidate.children.push_back(entity);
            }
            // The source Entity is externally owned and is NEVER destroyed.
            // Replacing its root component is the final ECS commit point.
            presentation.add<SceneAttachmentRootComponent>(
                parent,SceneAttachmentRootComponent{
                    candidate.revision,source->modelPublicationGeneration,
                    uint32_t(candidate.children.size())});

            // These moves are nonthrowing for standard allocator instances.
            // Old child handles are generation checked before deletion.
            Instance old=std::move(out);
            out=std::move(candidate);
            created.clear();
            destroyOwnedChildren(presentation,old);
            return true;
        }catch(const std::exception& ex) {
            // Destroy only children allocated during this failed attempt;
            // the existing parent/root and prior attachments are preserved.
            for(const auto entity:created)
                if(presentation.alive(entity))
                    presentation.destroy(entity);
            if(error)*error=ex.what();
            return false;
        }
    }

    // Rehydrate ECS children from a pointer-free authored prefab/scene
    // document. New generation-specific Entity handles are created rather
    // than trusting stale serialized IDs from a different World.
    static bool restore(World& presentation,Entity parent,
                        const Json& serialized,
                        const AssetSnapshot& source,
                        const render::ModelResourceCache& cache,
                        const Resolver& resolver,
                        Instance& out,std::string* error=nullptr) {
        if(error)error->clear();
        try {
            AttachmentLayout layout;
            AttachmentLayoutCodec::read(serialized,layout);
            return publish(presentation,parent,layout,source,
                           cache,resolver,out,error);
        }catch(const std::exception& ex) {
            if(error)*error=ex.what();
            return false;
        }
    }

    static Json save(const Instance& instance) {
        return AttachmentLayoutCodec::write(instance.document);
    }

    // Read-only ECS preflight before preparing any GPU-facing packet.
    // Fails closed on destroyed/reused parent or child Entity handles,
    // another instance's root, or a newer Model publication epoch.
    static bool valid(const World& presentation,
                      const Instance& instance) {
        if(!instance.active() || !presentation.alive(instance.parent) ||
           instance.children.size()!=instance.document.attachments.size())
            return false;
        const auto* root=presentation.tryGetConst<
            SceneAttachmentRootComponent>(instance.parent);
        if(!root || root->instanceRevision!=instance.revision ||
           !instance.bindings.source || !instance.bindings.source->model ||
           root->modelPublicationGeneration!=
               instance.bindings.source->modelPublicationGeneration ||
           root->authoredChildren!=instance.children.size())
            return false;
        for(size_t i=0;i<instance.children.size();++i) {
            const auto entity=instance.children[i];
            const auto* child=presentation.tryGetConst<
                SceneAttachmentChildComponent>(entity);
            const auto& authored=instance.document.attachments[i];
            if(!child || child->parent!=instance.parent ||
               child->instanceRevision!=instance.revision ||
               child->authoredIndex!=i ||
               child->modelSource!=authored.model ||
               child->socketName!=authored.socket ||
               child->authoredId!=authored.id ||
               child->animated!=authored.animated ||
               child->animationGraph!=authored.animationGraph ||
               child->enabled!=authored.enabled ||
               !sameMatrix(child->local,authored.local) ||
               !sameMatrix(child->childWorld,authored.childWorld))
                return false;
        }
        return true;
    }

    // Resolve a STABLE authored child key to the current generation of an
    // ECS Entity. Never serialize or cache the returned Entity across an
    // asset reload: publication recreates child entities transactionally.
    static Entity resolveChild(const World& presentation,
                               const Instance& instance,
                               const std::string& authoredId) {
        if(authoredId.empty() || !valid(presentation,instance))
            return kNullEntity;
        for(size_t i=0;i<instance.children.size();++i) {
            if(instance.document.attachments[i].id==authoredId)
                return instance.children[i];
        }
        return kNullEntity;
    }

    // The existing host Animator/Graph is the ONLY source of this pose.
    // A move-only Prepared keeps ModelReadLeases alive through the caller's
    // synchronous View/Renderer3D submission. No second Graph is started.
    static bool prepare(const World& presentation,
                        const Instance& instance,
                        const render::ModelResourceCache& cache,
                        const render::mat4& world,
                        ImportedAttachmentAuthoring::Prepared& out,
                        std::string* error=nullptr) {
        if(error)error->clear();
        if(!valid(presentation,instance)) {
            if(error)*error=
                "scene attachment root or child identity is stale";
            return false;
        }
        return ImportedAttachmentAuthoring::prepare(
            instance.bindings,cache,world,nullptr,out,
            ImportedAttachmentAuthoring::AnimationOwner::HostPrecomputed,
            error);
    }

    // Destroy only the child entities this instance actually created.
    // Generation/revision guards prevent deleting an unrelated entity after
    // the ECS allocator reuses an old slot. The caller owns the parent.
    static void forget(World& presentation,Instance& instance) {
        if(!instance.active())return;
        if(presentation.alive(instance.parent)) {
            const auto* root=presentation.tryGetConst<
                SceneAttachmentRootComponent>(instance.parent);
            if(root && root->instanceRevision==instance.revision)
                presentation.remove<SceneAttachmentRootComponent>(
                    instance.parent);
        }
        destroyOwnedChildren(presentation,instance);
        instance={};
    }

private:
    static bool sameMatrix(const render::mat4& a,
                           const render::mat4& b) noexcept {
        for(size_t i=0;i<16;++i)
            if(a.m[i]!=b.m[i])return false;
        return true;
    }
    static uint64_t nextRevision() {
        static std::atomic<uint64_t> next{1};
        const uint64_t revision=next.fetch_add(
            1,std::memory_order_relaxed);
        if(revision==0 || revision==std::numeric_limits<uint64_t>::max())
            throw std::overflow_error(
                "presentation attachment revision exhausted");
        return revision;
    }
    static void destroyOwnedChildren(World& presentation,
                                     const Instance& old) {
        for(const auto entity:old.children) {
            const auto* child=presentation.tryGetConst<
                SceneAttachmentChildComponent>(entity);
            if(child && child->parent==old.parent &&
               child->instanceRevision==old.revision)
                presentation.destroy(entity);
        }
    }
};

} // namespace aiverse::view
