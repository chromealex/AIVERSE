// Genre-neutral, owner-thread appearance publication with pinned immutable
// snapshots. Rendering and AnimationGraph clocks remain in a single View.
#pragma once
#include "AppearanceAuthoringService.h"
#include "AppearanceChoiceAuthoring.h"
#include "ModelPublicationEpoch.h"
#include "engine/render/RenderDeviceLifetime.h"
#include "engine/render/ModelReadLease.h"
#include "engine/render/graph/TextureGraphMaterialBinding.h"
#include <algorithm>
#include <cstdint>
#include <cmath>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace aiverse::view {

class AppearanceAssetRegistry final {
public:
    using Warning=AppearanceAuthoringService::Warning;
    using TextureLoader=AppearanceAuthoringService::TextureLoader;

    struct Request {
        // Model and texture indices remain host-owned. The registry owns only
        // material overrides, graph programs and snapshot metadata.
        const render::Model* model=nullptr;
        Json materialLibrary=Json::object();
        std::vector<Json> materialOverrides; // by imported model material index
        // Optional shared rule keys: materials("*"/named), assign(node glob), variant.
        // Exact same CPU policy is used by the RTS legacy adapter and native FPS.
        Json appearance;
        std::vector<int> nodeMaterial;
        Json animationGraph; // null: no graph, string: external asset, object: inline
        render::FileReader graphReader;
        render::AnimationOperations operations;
        render::AnimationTraits traits;
        render::AnimationGraphs namedGraphs;
        render::AnimationParameters parameters;
        std::vector<std::string> sources; // include material-library and model paths
        render::GeometryPolicy geometry{};
        render::float4 tint{1,1,1,1};
        render::float3 team{1,1,1};
        int variant=-1;
        uint32_t flags=0;
        uint64_t sourceId=0; // mandatory for an animated source
        uint64_t graphRevision=0; // increment for changed in-memory graph inputs
        uint64_t modelRevision=0; // host increments when the Model is re-imported at a reused address
    };

    struct Asset final {
        // A shared Asset pins the previous material+graph snapshot across a
        // transaction. It does NOT extend the host Model/GPU Device lifetime.
        const render::Model* model=nullptr;
        std::vector<render::Material> materials;
        std::vector<int> nodeMaterial;
        std::shared_ptr<const render::AnimationGraph> graph;
        render::AnimationParameters parameters;
        render::GeometryPolicy geometry{};
        render::float4 tint{1,1,1,1};
        render::float3 team{1,1,1};
        int variant=-1;
        uint32_t flags=0;
        uint64_t sourceId=0;
        uint64_t generation=0; // changes only when graph/model/source identity changes
        uint64_t version=0;    // changes on every successful publication
        uint64_t graphRevision=0;
        uint64_t modelRevision=0;
        std::string modelSource;
        std::string graphDefinition;
        std::string graphSource;
        std::vector<std::string> sources;
        // Globally unique publication identity for the borrowed Model
        // appearance, safe even when a Model address or registry is reused.
        uint64_t modelPublicationGeneration=0;

        bool packet(const render::mat4& world, Object& out) const {
            AppearanceSpec spec;
            spec.model=model;
            spec.materials=&materials;
            spec.nodeMaterial=&nodeMaterial;
            spec.geometry=geometry;
            spec.tint=tint;
            spec.team=team;
            spec.variant=variant;
            spec.flags=flags;
            spec.graph=graph;
            spec.parameters=parameters;
            spec.sourceId=sourceId;
            spec.generation=generation;
            spec.modelPublicationGeneration=modelPublicationGeneration;
            return buildAppearancePacket(spec,world,out);
        }

        // Native C++ authoring helper: compile a visual LOD against two
        // already published, immutable snapshots. Both revisions must be
        // nonzero and are checked again by View::submitOne on every draw.
        // On failure, out is unchanged, preserving a host's last-good LOD.
        // This pins the plan, NOT the borrowed Models or their GPU buffers.
        bool compileVisualLod(const Asset& visual,float minDistance,
                              const ModelPoseRetarget& authored,
                              ModelLod& out,std::string* error=nullptr) const {
            if(error)error->clear();
            try {
                if(!model || !visual.model ||
                   !modelPublicationGeneration ||
                   !visual.modelPublicationGeneration)
                    throw std::invalid_argument(
                        "compiled LOD needs two published Model snapshots");
                if(!std::isfinite(minDistance) || minDistance<=0.f)
                    throw std::invalid_argument(
                        "compiled LOD distance must be finite and positive");
                auto plan=compileModelPoseRetarget(
                    *model,*visual.model,authored,
                    modelPublicationGeneration,
                    visual.modelPublicationGeneration);
                if(!plan) {
                    if(error) {
                        const auto report=describeModelPoseRetarget(
                            *model,*visual.model,authored,
                            modelPublicationGeneration,
                            visual.modelPublicationGeneration);
                        *error=std::string(modelPoseRetargetIssueName(report.issue))+
                            " (visual node "+std::to_string(report.targetNode)+")";
                    }
                    return false;
                }
                ModelLod candidate;
                candidate.model=visual.model;
                candidate.minDistance=minDistance;
                candidate.compiledRetarget=std::move(plan);
                candidate.visualModelGeneration=
                    visual.modelPublicationGeneration;
                out=std::move(candidate);
                return true;
            }catch(const std::exception& ex) {
                if(error)*error=ex.what();
                return false;
            }
        }

        // Generic authoring requests. Published appearance snapshots remain
        // host-owned; these descriptions are used only during compilation.
        struct VisualLodRequest {
            const Asset* visual=nullptr;
            float minDistance=0.f;
            // Explicit map for complex rigs; null + matchByName enables
            // generic importer-name matching, never index guessing.
            std::shared_ptr<const ModelPoseRetarget> mapping;
            bool matchByName=false;
        };
        struct SocketBindingRequest {
            size_t attachmentIndex=0;
            std::string socketName;
            int socket=-1;
        };
        struct CompiledPresentationBindings {
            struct SocketBinding {
                size_t attachmentIndex=0;
                std::string socketName;
                int socket=-1;
                std::shared_ptr<const CompiledModelSocket> compiled;
            };
            const render::Model* sourceModel=nullptr; // comparison only
            uint64_t sourcePublication=0;
            std::vector<ModelLod> lods;
            std::vector<SocketBinding> sockets;

            // Transactional packet application. No graph evaluation, GPU
            // allocation, old Model dereference or ownership transfer.
            // All authored selectors and source publication must match.
            bool apply(Object& packet) const {
                if(!sourceModel || !sourcePublication ||
                   animationSourceModel(packet)!=sourceModel ||
                   packet.animationModelGeneration!=sourcePublication)
                    return false;
                for(const auto& binding:sockets) {
                    if(!binding.compiled ||
                       binding.attachmentIndex>=packet.attachments.size())
                        return false;
                    const auto& attachment=packet.attachments[binding.attachmentIndex];
                    if(attachment.socketName!=binding.socketName ||
                       attachment.socket!=binding.socket ||
                       binding.compiled->rig!=sourceModel ||
                       binding.compiled->publication!=sourcePublication ||
                       binding.compiled->socketName!=binding.socketName ||
                       binding.compiled->authoredSocket!=binding.socket)
                        return false;
                }
                Object next=packet;
                next.lods=lods;
                // Replace the entire authored socket layout: never leave
                // a compiled binding from an earlier publication behind.
                for(auto& attachment:next.attachments)
                    attachment.compiledSocket.reset();
                for(const auto& binding:sockets)
                    next.attachments[binding.attachmentIndex].compiledSocket=
                        binding.compiled;
                packet=std::move(next);
                return true;
            }
        };

        // Compile a whole authored visual configuration atomically against
        // this already-published immutable source asset. If any LOD or
        // socket fails, out is untouched (the prior last-good configuration
        // remains usable). No render-thread or gameplay state is changed.
        bool compilePresentationBindings(
                std::span<const VisualLodRequest> authoredLods,
                std::span<const SocketBindingRequest> authoredSockets,
                CompiledPresentationBindings& out,
                std::string* error=nullptr) const {
            if(error)error->clear();
            try {
                if(!model || modelPublicationGeneration==0)
                    throw std::invalid_argument(
                        "presentation bindings need a published source Model");
                CompiledPresentationBindings candidate;
                candidate.sourceModel=model;
                candidate.sourcePublication=modelPublicationGeneration;
                candidate.lods.reserve(authoredLods.size());
                float previousDistance=0.f;
                for(const auto& authored:authoredLods) {
                    if(!authored.visual || !authored.visual->model ||
                       !std::isfinite(authored.minDistance) ||
                       authored.minDistance<=previousDistance)
                        throw std::invalid_argument(
                            "visual LOD needs a published Model and strictly increasing distance");
                    auto mapping=authored.mapping;
                    if(!mapping) {
                        if(!authored.matchByName)
                            throw std::invalid_argument(
                                "visual LOD needs explicit map or opt-in named matching");
                        mapping=std::make_shared<const ModelPoseRetarget>(
                            makeNamedModelPoseRetarget(*model,*authored.visual->model));
                    }
                    ModelLod lod;
                    if(!compileVisualLod(*authored.visual,authored.minDistance,
                                         *mapping,lod,error))
                        return false;
                    lod.retarget=std::move(mapping);
                    candidate.lods.push_back(std::move(lod));
                    previousDistance=authored.minDistance;
                }
                candidate.sockets.reserve(authoredSockets.size());
                std::unordered_set<size_t> seen;
                for(const auto& authored:authoredSockets) {
                    if(!seen.insert(authored.attachmentIndex).second)
                        throw std::invalid_argument(
                            "duplicate attachment binding index");
                    auto compiled=compileModelSocket(
                        *model,modelPublicationGeneration,
                        authored.socketName,authored.socket);
                    if(!compiled)
                        throw std::invalid_argument(
                            "invalid attachment socket or unpublished rig");
                    candidate.sockets.push_back({
                        authored.attachmentIndex,authored.socketName,
                        authored.socket,std::move(compiled)});
                }
                out=std::move(candidate);
                return true;
            }catch(const std::exception& ex) {
                if(error)*error=ex.what();
                return false;
            }
        }

        // One native packet entry point for an already-published appearance,
        // optional compiled LODs/sockets and host-authored child attachments.
        // A caller with its own presentation animation owner must clear
        // packet.graph before submitting a PRECOMPUTED pose to generic View.
        // Failure leaves the previously valid output packet untouched.
        bool packetWithBindings(
                const render::mat4& world,
                std::span<const ModelAttachment> attachments,
                const CompiledPresentationBindings* bindings,
                Object& out,std::string* error=nullptr) const {
            if(error)error->clear();
            try {
                Object next;
                if(!packet(world,next)) {
                    if(error)*error="appearance has no published Model";
                    return false;
                }
                next.attachments.assign(attachments.begin(),attachments.end());
                if(bindings && !bindings->apply(next)) {
                    if(error)*error="compiled appearance binding revision or attachment mismatch";
                    return false;
                }
                out=std::move(next);
                return true;
            }catch(const std::exception& ex) {
                if(error)*error=ex.what();
                return false;
            }
        }
    };

    using Snapshot=std::shared_ptr<const Asset>;

    Snapshot find(const std::string& key) const {
        const auto it=assets_.find(key);
        return it==assets_.end()?Snapshot{}:it->second;
    }
    bool dirty(const std::string& key) const noexcept {
        return dirty_.contains(key);
    }
    size_t size() const noexcept{return assets_.size();}

    // Marks dependents dirty without deleting their last working appearance.
    // The host re-reads sources and calls publish at a safe frame boundary.
    // A changed graph file also forces a new graph generation even if the
    // authored reference string is identical.
    size_t invalidateSources(const std::vector<std::string>& sources) {
        std::unordered_set<std::string> changed(sources.begin(),sources.end());
        size_t count=0;
        for(const auto& [key,asset]:assets_) {
            bool affected=false;
            for(const auto& source:asset->sources)
                if(changed.contains(source)){affected=true;break;}
            if(!affected)continue;
            if(dirty_.insert(key).second)++count;
            const bool changedModel=!asset->modelSource.empty() &&
                changed.contains(asset->modelSource);
            const bool changedGraph=!asset->graphSource.empty() &&
                changed.contains(asset->graphSource);
            if(changedModel)dirtyModel_.insert(key);
            if(changedModel || changedGraph)dirtyGraph_.insert(key);
        }
        return count;
    }

    // Strict publish transaction: program validation, material preparation
    // and graph compilation must ALL succeed before the live map is swapped.
    // On failure the old snapshot/dirty flag/graph clock survive unchanged.
    // TextureLoader is a host resolver: if it allocates GPU textures, its
    // rollback/retirement responsibility remains with the host.
    bool publish(const std::string& key,const Request& request,
                 const TextureLoader& textureLoader={},
                 const Warning& warn={},std::string* error=nullptr) {
        if(error)error->clear();
        try {
            if(key.empty() || !request.model)
                throw std::invalid_argument("appearance needs key and model");
            if(request.materialOverrides.size()>request.model->materials.size())
                throw std::invalid_argument("appearance material override index out of range");
            if(!request.animationGraph.isNull() && !request.sourceId)
                throw std::invalid_argument("animated appearance requires stable sourceId");
            if(versionSerial_==std::numeric_limits<uint64_t>::max())
                throw std::overflow_error("appearance version overflow");

            auto next=std::make_shared<Asset>();
            next->model=request.model;
            next->materials=request.model->materials;
            next->nodeMaterial=request.nodeMaterial;
            next->geometry=request.geometry;
            next->tint=request.tint;
            next->team=request.team;
            next->variant=request.variant;
            next->flags=request.flags;
            next->sourceId=request.sourceId;
            next->parameters=request.parameters;
            next->graphRevision=request.graphRevision;
            next->modelRevision=request.modelRevision;
            next->modelSource=request.model->path;
            next->graphDefinition=request.animationGraph.isNull()?
                std::string{}:request.animationGraph.dump(0);
            if(request.animationGraph.isString())
                next->graphSource=request.animationGraph.str();
            next->sources=request.sources;
            if(!next->modelSource.empty() &&
               std::find(next->sources.begin(),next->sources.end(),
                         next->modelSource)==next->sources.end())
                next->sources.push_back(next->modelSource);
            if(!next->graphSource.empty() &&
               std::find(next->sources.begin(),next->sources.end(),
                         next->graphSource)==next->sources.end())
                next->sources.push_back(next->graphSource);

            const auto applyStrict=[&](render::Material& material,const Json& ref) {
                if(ref.isNull())return;
                const Json resolved=AppearanceAuthoringService::resolveMaterial(
                    request.materialLibrary,ref,warn);
                // Precompile a shaderGraph before the best-effort material API
                // can discard a broken graph; this is a CPU transaction only.
                if(resolved.has("shaderGraph"))
                    (void)render::graph::compile(resolved["shaderGraph"],"material");
                AppearanceAuthoringService::applyMaterial(
                    material,request.materialLibrary,ref,textureLoader,warn);
            };
            if(request.appearance.isObject()) {
                const auto choice=AppearanceChoiceAuthoring::resolve(
                    *request.model,request.appearance,warn);
                for(size_t i=0;i<choice.materialLayers.size();++i)
                    for(const auto& layer:choice.materialLayers[i])
                        applyStrict(next->materials[i],layer);
                for(const auto& extra:choice.extraMaterials) {
                    render::Material material;
                    material.name=extra.name;
                    applyStrict(material,extra.reference);
                    next->materials.push_back(std::move(material));
                }
                if(request.nodeMaterial.empty())
                    next->nodeMaterial=choice.nodeMaterial;
                if(request.variant<0)
                    next->variant=choice.variant;
            }
            for(size_t i=0;i<request.materialOverrides.size();++i)
                applyStrict(next->materials[i],request.materialOverrides[i]);

            const auto previous=find(key);
            if(!request.animationGraph.isNull()) {
                const bool reuse=previous && previous->graph &&
                    previous->model==request.model &&
                    previous->sourceId==request.sourceId &&
                    previous->graphDefinition==next->graphDefinition &&
                    previous->graphRevision==request.graphRevision &&
                    previous->modelRevision==request.modelRevision &&
                    !dirtyGraph_.contains(key);
                if(reuse) {
                    next->graph=previous->graph;
                    next->generation=previous->generation;
                } else {
                    next->graph=AppearanceAuthoringService::compileGraph(
                        *request.model,request.animationGraph,request.graphReader,
                        request.operations,request.traits,request.namedGraphs);
                    if(!next->graph)
                        throw std::runtime_error("animation graph compilation returned null");
                    if(animationSerial_==std::numeric_limits<uint64_t>::max())
                        throw std::overflow_error("animation generation overflow");
                    next->generation=animationSerial_+1;
                }
            }

            next->version=versionSerial_+1;
            // Appearance/material/graph edits are NOT new Model assets.
            // Keep retarget maps valid if both the host Model revision and
            // borrowed Model identity are unchanged. Source invalidation
            // explicitly marks in-place reimports even with pointer reuse.
            const bool sameModel=previous &&
                previous->model==next->model &&
                previous->modelRevision==next->modelRevision &&
                previous->modelSource==next->modelSource &&
                !dirtyModel_.contains(key) &&
                previous->modelPublicationGeneration!=0;
            next->modelPublicationGeneration=sameModel
                ?previous->modelPublicationGeneration
                :ModelPublicationEpoch::next();
            // Commit only after every potentially throwing preparation step.
            assets_.insert_or_assign(key,std::move(next));
            ++versionSerial_;
            if(assets_.at(key)->graph)
                animationSerial_=std::max(animationSerial_,assets_.at(key)->generation);
            dirty_.erase(key);
            dirtyGraph_.erase(key);
            dirtyModel_.erase(key);
            return true;
        }catch(const std::exception& ex) {
            if(error)*error=ex.what();
            return false;
        }
    }

    // Adopt an ALREADY compiled/validated host appearance without reloading
    // textures or recompiling its AnimationGraph. Useful when a native host
    // staged multiple RTS/scene choices and must publish them as one outer
    // frame-boundary transaction. Does not own Model/GPU resources or clocks.
    bool publishPrepared(const std::string& key,const Asset& prepared,
                         std::string* error=nullptr) {
        if(error)error->clear();
        try {
            if(key.empty() || !prepared.model)
                throw std::invalid_argument("prepared appearance needs key/model");
            if(prepared.graph && !prepared.sourceId)
                throw std::invalid_argument("prepared animated appearance needs sourceId");
            if(prepared.materials.size()<prepared.model->materials.size())
                throw std::invalid_argument("prepared materials missing base entries");
            for(const int index:prepared.nodeMaterial)
                if(index < -1 || (index>=0 &&
                    size_t(index)>=prepared.materials.size()))
                    throw std::invalid_argument("prepared node material index invalid");
            if(versionSerial_==std::numeric_limits<uint64_t>::max())
                throw std::overflow_error("prepared appearance version overflow");
            const auto old=find(key);
            auto next=std::make_shared<Asset>(prepared);
            next->version=versionSerial_+1;
            const bool sameModel=old &&
                old->model==next->model &&
                old->modelRevision==next->modelRevision &&
                old->modelSource==next->modelSource &&
                !dirtyModel_.contains(key) &&
                old->modelPublicationGeneration!=0;
            next->modelPublicationGeneration=sameModel
                ?old->modelPublicationGeneration
                :ModelPublicationEpoch::next();
            if(next->graph) {
                if(old && old->graph==next->graph &&
                   old->model==next->model &&
                   old->sourceId==next->sourceId &&
                   old->modelRevision==next->modelRevision &&
                   !dirtyGraph_.contains(key) &&
                   !dirtyModel_.contains(key))
                    next->generation=old->generation;
                else {
                    if(animationSerial_==std::numeric_limits<uint64_t>::max())
                        throw std::overflow_error("prepared graph generation overflow");
                    next->generation=animationSerial_+1;
                }
            }else next->generation=0;
            assets_.insert_or_assign(key,std::move(next));
            ++versionSerial_;
            if(assets_.at(key)->graph)
                animationSerial_=std::max(animationSerial_,
                                          assets_.at(key)->generation);
            dirty_.erase(key);dirtyGraph_.erase(key);dirtyModel_.erase(key);
            return true;
        }catch(const std::exception& ex) {
            if(error)*error=ex.what();
            return false;
        }
    }

    // Native host only: at a joined frame boundary, with the mutable Model
    // still owned by the host and no readers touching its image vector.
    // Existing snapshots keep their original image indices. New textures are
    // append-only until the Model is retired by its existing resource owner.
    // The per-model cap bounds repeated edits without invalidating old pins.
    static constexpr size_t maxProceduralImages=24;
    bool publishTextureGraphMaterial(const std::string& key,uint64_t expectedVersion,
            uint64_t expectedModelPublication,size_t materialIndex,
            render::Model& model,gfx::Device& device,
            const render::RenderDeviceLifetime::Lease& lease,
            render::ModelImageRollbackPool& rollback,
            const render::texture_graph::Program& program,
            const std::vector<render::Image>& images,
            std::string_view generation,std::string* error=nullptr) {
        if(error)error->clear();
        try {
            const auto it=assets_.find(key);
            if(it==assets_.end()||dirty(key))
                throw std::invalid_argument("appearance missing or dirty");
            const auto old=it->second;
            if(!expectedVersion||old->version!=expectedVersion||
               !expectedModelPublication||old->modelPublicationGeneration!=expectedModelPublication||
               old->model!=&model||materialIndex>=old->materials.size())
                throw std::invalid_argument("appearance publication identity changed");
            if(!lease.validFor(device)||!device.gpuHealthy())
                throw std::runtime_error("appearance graphics owner unavailable");
            if(versionSerial_==std::numeric_limits<uint64_t>::max())
                throw std::overflow_error("appearance version overflow");
            auto plan=render::texture_graph::prepareMaterialImages(program,images,generation);
            size_t retained=0;
            for(const auto& image:model.images)
                if(image.name.rfind("#texture-graph/",0)==0)++retained;
            if(retained>maxProceduralImages||plan.entries.size()>maxProceduralImages-retained)
                throw std::runtime_error("procedural image budget exhausted; retire/reload Model through owner");
            // Preallocate the replacement snapshot before GPU allocation.
            auto next=std::make_shared<Asset>(*old);
            next->version=versionSerial_+1;
            render::ModelImageUploadTransaction transaction(device,rollback,lease);
            next->materials[materialIndex]=render::texture_graph::stageMaterialImages(
                transaction,model,old->materials[materialIndex],std::move(plan));
            if(!lease.validFor(device)||!device.gpuHealthy())
                throw std::runtime_error("appearance graphics owner lost during staging");
            // No external callbacks between the image commit and publishing
            // the preallocated snapshot. Old indices and graph epoch survive.
            transaction.commit();
            model.markMaterialChanged();
            it->second=std::move(next);
            ++versionSerial_;
            return true;
        }catch(const std::exception& ex) {
            if(error)*error=ex.what();
            return false;
        }
    }

    void erase(const std::string& key) {
        assets_.erase(key);
        dirty_.erase(key);
        dirtyGraph_.erase(key);
        dirtyModel_.erase(key);
    }
    void clear() {
        assets_.clear();dirty_.clear();dirtyGraph_.clear();dirtyModel_.clear();
        // Serial counters intentionally survive clear(), preventing an old
        // animated source from aliasing a new generation in a live View.
    }
private:
    // Mutation is confined to the owning presentation/render thread. Stable
    // shared snapshots may be read by consumers that hold their own handles.
    std::unordered_map<std::string,Snapshot> assets_;
    std::unordered_set<std::string> dirty_,dirtyGraph_,dirtyModel_;
    uint64_t versionSerial_=0,animationSerial_=0;
};

// Retains published appearance/material/graph snapshots across one
// View::update() + synchronous View::submit() transaction. Native and
// cinematic hosts may construct a batch while the asset registry is
// hot reloading other keys. Pointer fields in ModelDraw refer into the
// pinned snapshots, not the registry's replaceable current entries.
//
// Important: snapshots own ONLY appearance CPU values + shared Graph,
// NOT a Model or its GPU resources. The host still retains each borrowed
// Model and its Device epoch through the animation worker join and draw.
class PinnedAppearanceBatch final {
public:
    using Snapshot=AppearanceAssetRegistry::Snapshot;
    void reserve(size_t count) {
        // Reserve first for stable, failure-safe packet assembly.
        pins_.reserve(count);
        packets_.reserve(count);
    }
    bool add(Snapshot asset,const render::mat4& world) {
        if(!asset)return false;
        Object packet;
        if(!asset->packet(world,packet))return false;
        pins_.push_back(std::move(asset));
        try {
            packets_.push_back(std::move(packet));
        }catch(...) {
            pins_.pop_back();
            throw;
        }
        return true;
    }

    // Optional STRONG CPU ownership for a host whose Model is managed by
    // shared_ptr. The exact pointer must match the borrowed published Asset.
    // The shared_ptr's custom deleter must NEVER destroy in-flight GPU
    // handles; GPU completion/retirement remains owned by the renderer host.
    //
    // ModelResourceCache currently stores unique_ptr<Model> and cannot
    // produce this strong pin without a separate owner transaction.
    // Such cache-backed hosts should keep using add() plus their explicit
    // cache detach/retire boundary; do not fabricate non-owning aliases.
    bool addOwned(Snapshot asset,const render::mat4& world,
                  std::shared_ptr<const render::Model> modelOwner) {
        if(!asset || !modelOwner || asset->model!=modelOwner.get())
            return false;
        // Thousands of actors may share one Model. Keep ONE strong CPU pin
        // per distinct Model pointer, not one shared_ptr ref per instance.
        const bool newPin=std::none_of(modelPins_.begin(),modelPins_.end(),
            [&](const auto& existing){return existing.get()==modelOwner.get();});
        if(newPin)modelPins_.push_back(std::move(modelOwner));
        try {
            if(add(std::move(asset),world))return true;
            if(newPin)modelPins_.pop_back();
            return false;
        }catch(...) {
            if(newPin)modelPins_.pop_back();
            throw;
        }
    }
    // An alternative for native ModelResourceCache's UNIQUE Model ownership:
    // acquire an actual cache read lease from ModelResourceCache on its
    // owner thread, then transfer the lease into this batch. The lease
    // blocks cache eviction/replacement until clear(); it does NOT own GPU
    // handles or change ModelMap's unique_ptr semantics.
    bool addCacheOwned(Snapshot asset,const render::mat4& world,
                       render::ModelReadLease lease) {
        if(!asset || !lease || asset->model!=lease.get())return false;
        const auto* raw=lease.get();
        const bool newPin=std::none_of(cacheModelPins_.begin(),cacheModelPins_.end(),
            [&](const auto& existing){return existing.get()==raw;});
        if(newPin)cacheModelPins_.push_back(std::move(lease));
        try {
            if(add(std::move(asset),world))return true;
            if(newPin)cacheModelPins_.pop_back();
            return false;
        }catch(...) {
            if(newPin)cacheModelPins_.pop_back();
            throw;
        }
    }
    // Check a new authored LOD/attachment before accepting a cache
    // lease. Do not acquire leases for arbitrary unused cache Models.
    bool pinCacheRelatedModel(render::ModelReadLease lease) {
        if(!lease)return false;
        const auto* raw=lease.get();
        bool referenced=false;
        for(const auto& packet:packets_) {
            if(packet.draw.model==raw ||
               animationSourceModel(packet)==raw)
                {referenced=true;break;}
            for(const auto& lod:packet.lods)
                if(lod.model==raw){referenced=true;break;}
            if(referenced)break;
            for(const auto& child:packet.attachments)
                if(child.draw.model==raw){referenced=true;break;}
            if(referenced)break;
        }
        if(!referenced)return false;
        for(const auto& existing:cacheModelPins_)
            if(existing.get()==raw)return true;
        cacheModelPins_.push_back(std::move(lease));
        return true;
    }

    // Additional owned LOD, animation-source or attachment Models can be
    // pinned after editing packets. Reject unrelated pointers, so the
    // owner check is auditable rather than a decorative shared_ptr.
    bool pinRelatedModel(std::shared_ptr<const render::Model> owner) {
        if(!owner)return false;
        const auto* model=owner.get();
        bool referenced=false;
        for(const auto& packet:packets_) {
            if(packet.draw.model==model ||
               animationSourceModel(packet)==model)
                {referenced=true;break;}
            for(const auto& lod:packet.lods)
                if(lod.model==model){referenced=true;break;}
            if(referenced)break;
            for(const auto& attachment:packet.attachments)
                if(attachment.draw.model==model){referenced=true;break;}
            if(referenced)break;
        }
        if(!referenced)return false;
        for(const auto& pin:modelPins_)
            if(pin.get()==model)return true;
        modelPins_.push_back(std::move(owner));
        return true;
    }
    // Strict opt-in validation, BEFORE any View::update/submit. This does
    // not inspect RHI Device state or guarantee that Model GPU handles
    // outlive an async submission: the host still owns those fences.
    size_t missingModelPins() const noexcept {
        const auto held=[this](const render::Model* model) {
            if(!model)return true; // unloaded optional visual LOD
            for(const auto& pin:modelPins_)
                if(pin.get()==model)return true;
            for(const auto& lease:cacheModelPins_)
                if(lease.get()==model)return true;
            return false;
        };
        size_t missing=0;
        for(const auto& packet:packets_) {
            if(!held(packet.draw.model))++missing;
            const auto* rig=animationSourceModel(packet);
            if(rig!=packet.draw.model && !held(rig))++missing;
            for(const auto& lod:packet.lods)
                if(!held(lod.model))++missing;
            for(const auto& attachment:packet.attachments)
                if(!held(attachment.draw.model))++missing;
        }
        return missing;
    }
    bool allModelsPinned() const noexcept {
        return missingModelPins()==0;
    }
    void requireModelPins() const {
        if(!allModelsPinned())
            throw std::logic_error(
                "PinnedAppearanceBatch: missing CPU Model owners for one or more draws");
    }
    // Opt-in strict path for native shared-ownership clients. Call AFTER
    // constructing all optional LOD and attachment packets, and use the
    // returned span for both update() and synchronous submit() before clear().
    std::span<const Object> validatedSpan() const {
        requireModelPins();
        return span();
    }
    // Optional GPU owner-epoch preflight for native hosts that keep
    // a RenderDeviceLifetime lease. Checks thread + Device generation,
    // but DOES NOT retire or pin GPU buffer/image handles. A successful
    // lease check is NOT proof that a queued draw has finished on GPU.
    std::span<const Object> validatedSpan(
            gfx::Device& device,
            const render::RenderDeviceLifetime::Lease& lease) const {
        if(!lease.validFor(device))
            throw std::logic_error(
                "PinnedAppearanceBatch: expired or foreign GPU Device epoch");
        return validatedSpan();
    }
    size_t modelPinCount() const noexcept{
        return modelPins_.size()+cacheModelPins_.size();
    }
    size_t cacheReadLeaseCount() const noexcept{return cacheModelPins_.size();}
    size_t size() const noexcept{return packets_.size();}
    bool empty() const noexcept{return packets_.empty();}
    const std::vector<Object>& objects() const noexcept {return packets_;}
    std::span<const Object> span() const noexcept {
        return {packets_.data(),packets_.size()};
    }
    // Modify model LODs, attachments or significance AFTER constructing the
    // batch, not while View::update / Render3D submission is in progress.
    // References to packet elements may be invalidated by subsequent add().
    Object& edit(size_t index) {return packets_.at(index);}
    void clear() noexcept {
        // End of the host's joined update/render transaction only.
        packets_.clear();
        pins_.clear();
        // Releasing model CPU owners does NOT release any in-flight GPU
        // allocations. Shared model deleters must cooperate with the
        // host's separately fenced GPU retirement queue.
        modelPins_.clear();
        // Release read leases LAST, after packet+snapshot pointers cease
        // being referenced. Host may now evict/retire unique-owned Models.
        cacheModelPins_.clear();
    }
private:
    std::vector<Snapshot> pins_;
    std::vector<Object> packets_;
    std::vector<std::shared_ptr<const render::Model>> modelPins_;
    std::vector<render::ModelReadLease> cacheModelPins_;
};

} // namespace aiverse::view
