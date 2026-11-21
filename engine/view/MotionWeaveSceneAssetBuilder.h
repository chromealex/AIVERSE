// AIVERSE — concrete genre-neutral SceneAsset -> MotionWeave visual
// builder. Called by MotionWeaveSceneGameHost::commitReady() at the native
// structural owner barrier. SceneObjectAsset::id is canonical visual key.
// All Models/Graphs are cosmetic and owned by ONE new scene provider.
#pragma once
#include "MotionWeaveSceneGameHost.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::view {

class MotionWeaveSceneAssetBuilder final {
public:
    using Host=MotionWeaveSceneGameHost;
    using Owner=Host::Owner;
    using Object=Host::Object;
    // A prefab-specific visual resolver. May use provider.load(device,...)
    // and publish immutable AppearanceAssetRegistry snapshots. Objects
    // WITHOUT a visual representation return std::nullopt.
    //
    // The resolver must NOT change authoritative ECS state, step a GameModule,
    // or retain ModelReadLease beyond this call. All static and independently
    // animated child appearances must be in the supplied provider's cache.
    using Resolve=std::function<bool(
        const scene::SceneObjectAsset&,
        render::ModelAssetProvider&,
        gfx::Device&,
        std::optional<Object>&,
        std::string&)>;

    // Read-only visual resolver for an already resident shared provider.
    // Unlike Resolve, it cannot import, invalidate or upload Models; all
    // referenced appearances must already belong to this cache generation.
    using ResolveResident=std::function<bool(
        const scene::SceneObjectAsset&,
        const render::ModelAssetProvider&,
        std::optional<Object>&,
        std::string&)>;

    // Used by MotionWeaveSceneGameHost::commitReadyShared() when a second
    // scene borrows a GPU provider. Keep the old output unchanged on error.
    static bool buildResident(
            const scene::SceneAsset& manifest,
            const render::ModelAssetProvider& resident,
            const ResolveResident& resolve,
            std::vector<Object>& outObjects,std::string& error) {
        error.clear();
        try {
            if(!resolve)
                throw std::invalid_argument(
                    "shared scene resident visual resolver missing");
            if(manifest.objects.size()>scene::SceneAsset::kMaxObjects)
                throw std::invalid_argument(
                    "shared scene object count exceeds schema limit");
            std::vector<Object> candidates;
            candidates.reserve(manifest.objects.size());
            std::set<uint64_t> seen;
            for(const auto& prefab:manifest.objects) {
                if(prefab.id==0 || !seen.insert(prefab.id).second)
                    throw std::invalid_argument(
                        "shared scene visual object ID zero or duplicated");
                std::optional<Object> visual;
                std::string reason;
                if(!resolve(prefab,resident,visual,reason))
                    throw std::runtime_error(reason.empty()
                        ? "shared scene visual prefab resolution failed"
                        :reason);
                if(!visual)continue;
                const std::string stable=std::to_string(prefab.id);
                if(!visual->objectKey.empty() &&
                   visual->objectKey!=stable)
                    throw std::invalid_argument(
                        "shared scene visual key conflicts with native scene ID");
                visual->objectKey=stable;
                const auto validate=[&](const auto& appearance) {
                    if(!appearance || !appearance->model ||
                       !appearance->modelPublicationGeneration)
                        throw std::invalid_argument(
                            "shared scene appearance publication missing");
                    // The lease verifies native Model identity, not merely
                    // that an immutable Graph shared_ptr is still alive.
                    auto read=resident.resources().acquireModelRead(
                        appearance->model);
                    if(!read)
                        throw std::invalid_argument(
                            "shared scene Model is not resident in selected GPU provider");
                };
                validate(visual->parent);
                for(const auto& [childId,child]:visual->animatedChildren) {
                    if(childId.empty())
                        throw std::invalid_argument(
                            "shared scene animated child ID missing");
                    validate(child);
                }
                candidates.push_back(std::move(*visual));
            }
            outObjects=std::move(candidates);
            return true;
        }catch(const std::exception& ex) {
            error=ex.what();
            return false;
        }catch(...) {
            error="unknown shared scene resident model builder failure";
            return false;
        }
    }

    MotionWeaveSceneAssetBuilder(gfx::Device& device,
                                render::RenderDeviceLifetime::Lease lease,
                                render::FileReader reader,Resolve resolver)
        :device_(device),deviceLease_(std::move(lease)),
         reader_(std::move(reader)),resolve_(std::move(resolver)) {}
    MotionWeaveSceneAssetBuilder(const MotionWeaveSceneAssetBuilder&)=delete;
    MotionWeaveSceneAssetBuilder& operator=(
        const MotionWeaveSceneAssetBuilder&)=delete;

    // The Owner output intentionally survives on builder failure:
    // MotionWeaveSceneCoordinatorAdapter::commitReady() retires even a
    // partially uploaded provider with a true GPU completion token.
    // The output vector remains untouched unless ALL objects are valid.
    bool operator()(const scene::SceneAsset& manifest,
                    Owner& outProvider,
                    std::vector<Object>& outObjects,
                    std::string& error) const {
        error.clear();
        try {
            if(!deviceLease_.validFor(device_))
                throw std::invalid_argument("scene Model GPU epoch expired");
            if(!resolve_)
                throw std::invalid_argument("scene visual resolver missing");
            if(outProvider)
                throw std::invalid_argument(
                    "scene builder output provider already initialized");
            if(manifest.objects.size()>scene::SceneAsset::kMaxObjects)
                throw std::invalid_argument(
                    "scene visual object count exceeds schema limit");

            outProvider=std::make_unique<render::ModelAssetProvider>(reader_);
            outProvider->setGpuDeviceLease(deviceLease_);
            outProvider->resources().bindResourceDevice(device_,deviceLease_);

            std::vector<Object> candidates;
            candidates.reserve(manifest.objects.size());
            std::set<uint64_t> seen;
            for(const auto& prefab:manifest.objects) {
                if(prefab.id==0 || !seen.insert(prefab.id).second)
                    throw std::invalid_argument(
                        "scene visual object ID zero or duplicated");
                std::optional<Object> visual;
                std::string reason;
                if(!resolve_(prefab,*outProvider,device_,visual,reason))
                    throw std::runtime_error(reason.empty()
                        ? "scene visual prefab resolution failed":reason);
                if(!visual)continue; // markers/physics-only entities
                const std::string stable=std::to_string(prefab.id);
                if(!visual->objectKey.empty() &&
                   visual->objectKey!=stable)
                    throw std::invalid_argument(
                        "scene visual key disagrees with canonical SceneObjectAsset ID");
                visual->objectKey=stable; // NEVER use raw Entity::index
                const auto validate=[&](const auto& appearance) {
                    if(!appearance || !appearance->model ||
                       !appearance->modelPublicationGeneration)
                        throw std::invalid_argument(
                            "scene visual appearance publication missing");
                    auto read=outProvider->resources().acquireModelRead(
                        appearance->model);
                    if(!read)
                        throw std::invalid_argument(
                            "scene visual Model not owned by scene provider");
                };
                validate(visual->parent);
                for(const auto& [key,child]:visual->animatedChildren) {
                    if(key.empty())
                        throw std::invalid_argument(
                            "independent Graph child ID missing");
                    validate(child);
                }
                candidates.push_back(std::move(*visual));
            }
            outObjects=std::move(candidates);
            return true;
        }catch(const std::exception& ex) {
            error=ex.what();
            return false;
        }catch(...) {
            error="unknown scene visual Model builder failure";
            return false;
        }
    }

private:
    gfx::Device& device_;
    render::RenderDeviceLifetime::Lease deviceLease_;
    render::FileReader reader_;
    Resolve resolve_;
};

} // namespace aiverse::view
