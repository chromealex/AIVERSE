// AIVERSE generic imported child-model attachment authoring.
// Genre-neutral: real cache-resident Model assets, authored appearance
// snapshots, compiled sockets, and short-lived owner-thread read leases.
// Never touches gameplay state, simulation, animation clock or GPU fences.
#pragma once
#include "AppearanceAssetRegistry.h"
#include "AttachmentLayout.h"
#include "engine/render/ModelResourceCache.h"
#include "engine/render/ModelAssetProvider.h"
#include <algorithm>
#include <cstdint>
#include <exception>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::view {

class ImportedAttachmentAuthoring final {
public:
    enum class AnimationOwner : uint8_t {
        ViewEvaluates, // call generic View::update exactly once
        HostPrecomputed // never create a second graph writer
    };
    using Asset = AppearanceAssetRegistry::Asset;
    using Snapshot = AppearanceAssetRegistry::Snapshot;

    // Child appearances are REAL, separately imported, published Models.
    // The host first loads them through ModelAssetProvider/ModelResourceCache,
    // then publishes their material/Model snapshot in AppearanceAssetRegistry.
    // Child graphs are deliberately not evaluated by attachments: animate
    // such a child as its own View::Object with its own independent source ID.
    struct ChildRequest {
        Snapshot appearance;
        std::string socketName;
        int socket=-1;
        render::mat4 local;
        render::mat4 childWorld;
        bool visible=true;
    };
    // Canonical conversion from authored scene/prefab layout to runtime
    // child request. Snapshot/Model ownership is still provided by the
    // native host; layout JSON is strictly CPU authoring data.
    static ChildRequest childFromLayout(
            const AttachmentLayoutItem& authored,Snapshot child) {
        if(!child || authored.model.empty() || authored.socket.empty())
            throw std::invalid_argument(
                "attachment layout needs a resolved child and named socket");
        ChildRequest next;
        next.appearance=std::move(child);
        next.socketName=authored.socket;
        next.local=authored.local;
        next.childWorld=authored.childWorld;
        next.visible=authored.enabled;
        return next;
    }

    struct Compiled {
        struct Child {
            Snapshot appearance;
            std::shared_ptr<const CompiledModelSocket> socketBinding;
            std::string socketName;
            int socket=-1;
            render::mat4 local;
            render::mat4 childWorld;
            bool visible=true;
        };
        Snapshot source;
        std::vector<Child> children;

        bool matches(const Snapshot& current) const noexcept {
            return source && current &&
                source->model && source->modelPublicationGeneration!=0 &&
                source->model==current->model &&
                source->modelPublicationGeneration==
                    current->modelPublicationGeneration &&
                source.get()==current.get();
        }
    };

    // Import a REAL child model through the host's generic resource owner,
    // then publish its immutable static appearance in the common registry.
    // On failure, the caller's previous Snapshot is untouched; old models
    // must be detached before the host invalidates their source in its cache.
    // This is a single-asset helper; use compile() to transactionally author
    // the complete set of already-published children and sockets.
    static bool importChild(render::ModelAssetProvider& provider,
                            gfx::Device& device,
                            AppearanceAssetRegistry& appearances,
                            const std::string& assetKey,
                            const std::string& modelSource,
                            AppearanceAssetRegistry::Request authored,
                            Snapshot& out,
                            const AppearanceAssetRegistry::TextureLoader& textures={},
                            std::string* error=nullptr) {
        if(error)error->clear();
        try {
            if(assetKey.empty() || modelSource.empty())
                throw std::invalid_argument(
                    "imported attachment needs a source path and asset key");
            if(!authored.animationGraph.isNull() || authored.sourceId!=0)
                throw std::invalid_argument(
                    "static attachment cannot publish its own AnimationGraph");
            auto* model=provider.load(device,modelSource);
            if(!model)
                throw std::runtime_error("child Model import/upload failed");
            auto read=provider.resources().acquireModelRead(model);
            if(!read)
                throw std::runtime_error("imported child Model not cache resident");
            authored.model=model;
            if(std::find(authored.sources.begin(),authored.sources.end(),
                         modelSource)==authored.sources.end())
                authored.sources.push_back(modelSource);
            // publish() is last-good transactional: a failed appearance
            // compile never replaces its previously published snapshot.
            if(!appearances.publish(assetKey,authored,textures,{},error))
                return false;
            auto candidate=appearances.find(assetKey);
            if(!candidate || candidate->model!=model ||
               candidate->modelPublicationGeneration==0)
                throw std::runtime_error(
                    "published child appearance has an invalid Model epoch");
            out=std::move(candidate);
            return true;
        }catch(const std::exception& ex) {
            if(error)*error=ex.what();
            return false;
        }
    }

    // A prepared packet and its short-lived CPU read leases must stay on
    // the render/asset owner thread until AFTER synchronous View submission.
    // Destruction releases the leases, allowing the host to invalidate the
    // cache and retire GPU resources at its own safe frame boundary.
    struct Prepared {
        std::vector<render::ModelReadLease> modelReads;
        Snapshot source;
        std::vector<Snapshot> childAppearances;
        Object object;
        Prepared()=default;
        Prepared(const Prepared&)=delete;
        Prepared& operator=(const Prepared&)=delete;
        Prepared(Prepared&&)=default;
        Prepared& operator=(Prepared&&)=default;
    };

    // Never compile against a retired Model. All assets must live in this
    // host's SAME ModelResourceCache, so read leases protect the entire
    // source+child rig transaction. No code is run on animation workers.
    static bool compile(const Snapshot& source,
                        const render::ModelResourceCache& cache,
                        std::span<const ChildRequest> authored,
                        Compiled& out,std::string* error=nullptr) {
        if(error)error->clear();
        try {
            if(!source || !source->model ||
               source->modelPublicationGeneration==0)
                throw std::invalid_argument(
                    "imported attachments require a published source rig");
            auto sourceRead=cache.acquireModelRead(source->model);
            if(!sourceRead)
                throw std::invalid_argument(
                    "source rig has no live Model cache read lease");
            Compiled next;
            next.source=source;
            next.children.reserve(authored.size());
            for(const auto& entry:authored) {
                const auto& child=entry.appearance;
                if(!child || !child->model ||
                   child->modelPublicationGeneration==0)
                    throw std::invalid_argument(
                        "child attachment requires a published Model");
                if(child->graph)
                    throw std::invalid_argument(
                        "animated child requires its own View source, not a static attachment");
                auto childRead=cache.acquireModelRead(child->model);
                if(!childRead)
                    throw std::invalid_argument(
                        "child Model has no live Model cache read lease");
                auto socket=compileModelSocket(
                    *sourceRead.get(),source->modelPublicationGeneration,
                    entry.socketName,entry.socket);
                if(!socket)
                    throw std::invalid_argument(
                        "attachment socket missing or incompatible with published rig");
                next.children.push_back({
                    child,std::move(socket),entry.socketName,entry.socket,
                    entry.local,entry.childWorld,entry.visible
                });
            }
            out=std::move(next);
            return true;
        }catch(const std::exception& ex) {
            if(error)*error=ex.what();
            return false;
        }
    }

    // Make a frame-local packet and pin each borrowed Model in the cache.
    // Optional published LODs are pinned too; all must reside in this cache.
    // The source and child appearance snapshots keep MATERIAL pointer data
    // alive, while ModelReadLease prevents CPU Model eviction until draw.
    // Never submit on a different thread or use this packet after Prepared
    // (and its move-safe leases) has been destroyed.
    static bool prepare(const Compiled& compiled,
                        const render::ModelResourceCache& cache,
                        const render::mat4& world,
                        const Asset::CompiledPresentationBindings* bindings,
                        Prepared& out,
                        AnimationOwner owner=AnimationOwner::ViewEvaluates,
                        std::string* error=nullptr) {
        if(error)error->clear();
        try {
            if(!compiled.source || !compiled.source->model ||
               compiled.source->modelPublicationGeneration==0)
                throw std::invalid_argument(
                    "attachment source snapshot is missing or unpublished");
            Prepared next;
            next.source=compiled.source;
            auto pin=[&](const render::Model* model) {
                if(!model)
                    throw std::invalid_argument("null imported Model");
                auto read=cache.acquireModelRead(model);
                if(!read)
                    throw std::invalid_argument(
                        "imported Model no longer resident in the owner cache");
                next.modelReads.push_back(std::move(read));
            };
            pin(compiled.source->model);
            if(bindings) {
                // Validate source revision before accessing any child or
                // potentially stale visual LOD Model memory.
                if(bindings->sourceModel!=compiled.source->model ||
                   bindings->sourcePublication!=
                       compiled.source->modelPublicationGeneration)
                    throw std::invalid_argument(
                        "compiled LOD configuration source epoch changed");
                for(const auto& lod:bindings->lods)
                    if(lod.model)pin(lod.model);
            }
            std::vector<ModelAttachment> attachments;
            attachments.reserve(compiled.children.size());
            next.childAppearances.reserve(compiled.children.size());
            for(const auto& child:compiled.children) {
                if(!child.appearance || !child.appearance->model ||
                   !child.socketBinding ||
                   !child.socketBinding->matches(
                       *compiled.source->model,
                       compiled.source->modelPublicationGeneration,
                       child.socketName,child.socket))
                    throw std::invalid_argument(
                        "compiled attachment missing or source rig revision changed");
                pin(child.appearance->model);
                Object childPacket;
                if(!child.appearance->packet(child.childWorld,childPacket))
                    throw std::invalid_argument(
                        "child published appearance cannot form a draw packet");
                ModelAttachment a;
                a.draw=childPacket.draw;
                a.socket=child.socket;
                a.socketName=child.socketName;
                a.local=child.local;
                a.visible=child.visible;
                a.compiledSocket=child.socketBinding;
                attachments.push_back(std::move(a));
                next.childAppearances.push_back(child.appearance);
            }
            if(!compiled.source->packetWithBindings(
                   world,attachments,bindings,next.object,error))
                return false;
            // Stage 151 replaces the full authored binding list. For
            // attached child appearances, recover compiled socket identity
            // for entries NOT included in optional LOD/socket bundle.
            for(size_t i=0;i<compiled.children.size();++i) {
                auto& attachment=next.object.attachments[i];
                if(!attachment.compiledSocket)
                    attachment.compiledSocket=
                        compiled.children[i].socketBinding;
            }
            // A host that already advanced MotionWeave must not hand the
            // same graph to another View animation source. Preserve the
            // authoritative host-precomputed Pose/skin output separately.
            if(owner==AnimationOwner::HostPrecomputed)
                next.object.graph.reset();
            else if(next.object.graph && next.object.id==0)
                throw std::invalid_argument(
                    "View-owned animated source requires a stable nonzero ID");
            out=std::move(next);
            return true;
        }catch(const std::exception& ex) {
            if(error)*error=ex.what();
            return false;
        }
    }
};

} // namespace aiverse::view
