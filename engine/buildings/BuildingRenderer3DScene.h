#pragma once
// Genre-free Renderer3D owner for authoritative building apertures.
// The server/Simulation never uploads models or writes renderer state.
// The existing render::RenderDeviceLifetime and gfx::Device own GPU epochs.
#include "engine/buildings/BuildingDualLeafGameSession.h"
#include "engine/buildings/BuildingLiftRenderState.h"
#include "engine/render/Renderer3D.h"
#include "engine/render/RenderDeviceLifetime.h"
#include "engine/world/SpatialWorld.h"
#include <algorithm>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::building {

// One render owner for one already attached dual-leaf scene. The host MUST
// keep the session and gfx::Device alive until shutdown() returns true.
// All methods run on the device's rendering thread. shutdown() must be
// called at the host's GPU frame boundary, OUTSIDE an active render pass.
class BuildingRenderer3DScene {
public:
    enum class Status : uint8_t { Detached, Resident, CleanupRequired };
    BuildingRenderer3DScene()=default;
    BuildingRenderer3DScene(const BuildingRenderer3DScene&)=delete;
    BuildingRenderer3DScene& operator=(const BuildingRenderer3DScene&)=delete;
    ~BuildingRenderer3DScene()=default; // deliberately no unsafe implicit GPU release

    // The caller must have already assigned each socket's combined PBR
    // material+texture GUID image set before calling initialize().
    // instanceNamespace is unique across active WorldScene render owners.
    bool initialize(gfx::Device& device,render::Renderer3D& renderer,
                    render::RenderDeviceLifetime::Lease deviceLease,
                    BuildingDualLeafGameSession& session,
                    uint64_t instanceNamespace,std::string* error=nullptr,
                    render::Model* shell=nullptr,Vec3 shellOrigin={},
                    BuildingLiftRenderState* lifts=nullptr){
        auto fail=[&](const std::string& why){
            if(error)*error=why;
            return false;
        };
        if(status_!=Status::Detached||!session.attached()||
           !instanceNamespace||!deviceLease.validFor(device))
            return fail("Invalid Renderer3D building scene or GPU device generation");
        // Gather every CPU model before ANY upload, so partial-upload
        // failures can be retired without losing ownership of a model.
        std::set<render::Model*> unique;
        const bool gathered=session.forEachCpuModel([&](render::Model& model){
            if(!unique.insert(&model).second||unique.size()>64u)return false;
            models_.push_back(&model);
            return true;
        });
        if(lifts&&!lifts->forEachCpuModel([&](render::Model& model){
            if(!unique.insert(&model).second||unique.size()>192u)return false;
            models_.push_back(&model);return true;
        })){models_.clear();return fail("Invalid lift CPU Models");}
        if(shell&&unique.insert(shell).second)models_.push_back(shell);
        else if(shell){models_.clear();return fail("Duplicate shell Model");}
        if(!gathered||models_.empty()){
            models_.clear();
            return fail("Missing, duplicate or excessive dual-leaf CPU Models");
        }
        device_=&device;
        renderer_=&renderer;
        session_=&session;
        deviceLease_=std::move(deviceLease);
        namespace_=instanceNamespace;
        shell_=shell;shellOrigin_=shellOrigin;lifts_=lifts;
        status_=Status::CleanupRequired;
        // Keep the movable aperture models independent of rigid merge,
        // automatic LOD and pooled GPU geometry. CPU models remain owned
        // by the authoritative presentation while this owner has a lease.
        for(auto* model:models_){
            model->batching.mergeRigid=false;
            model->batching.autoLod=false;
            model->batching.geometryPoolPolicy=
                render::Model::Batching::GeometryPoolPolicy::Dedicated;
            render::prepareModelUpload(*model);
            // The external owner holds every staged Model and safely
            // retires on failed upload, without publishing new images to
            // the shared global texture cache during speculative creation.
            if(!render::uploadModel(device,*model,true,false)){
                const std::string reason="Renderer3D building leaf GPU upload rejected";
                std::string cleanup;
                if(!shutdown(&cleanup))
                    return fail(reason+"; GPU cleanup pending: "+cleanup);
                return fail(reason);
            }
        }
        status_=Status::Resident;
        if(error)error->clear();
        return true;
    }

    // Called from existing WorldScene::draw (after the camera is set).
    // Only *committed* game-authoritative poses flow into ModelDraw; none
    // of the visual interpolation affects physics, navigation or access.
    bool draw(render::Renderer3D& renderer,world::Cell origin,
              int cellSize,uint32_t renderDtMs,
              std::string* error=nullptr){
        auto fail=[&](const std::string& why){
            if(error)*error=why;
            return false;
        };
        if(status_!=Status::Resident||!device_||
           !deviceLease_.validFor(*device_)||
           renderer_!=&renderer||!session_||cellSize<=0)
            return fail("Building render owner not resident or GPU epoch changed");
        std::map<uint64_t,std::pair<std::string,uint8_t>> identities;
        if(shell_){
            // Static geometry was generated in yawed scene-local metres.
            const auto relative=world::relativeTo(shellOrigin_,origin,cellSize);
            const auto id=renderIdentity("",255);
            identities.emplace(id,std::make_pair(std::string{},uint8_t(255)));
            render::ModelDraw command{};
            command.model=shell_;command.persistentId=id;
            command.staticPacketRevision=0;
            command.world=render::mat4::translate({
                float(relative.x.toDouble()),float(relative.y.toDouble()),
                float(relative.z.toDouble())});
            renderer.drawModel(command);
        }
        std::string drawError;
        const bool accepted=session_->submit(
            std::min<uint32_t>(renderDtMs,350u),
            [&](const render::Model& model,const BuildingApertureLeafPose& pose){
                if(pose.leaf>1u)return false;
                const uint64_t id=renderIdentity(pose.socketId,pose.leaf);
                const auto [it,inserted]=identities.emplace(
                    id,std::make_pair(pose.socketId,pose.leaf));
                if(!inserted &&
                   it->second!=std::make_pair(pose.socketId,pose.leaf)){
                    drawError="Building leaf render identity collision";
                    return false;
                }
                const auto relative=world::relativeTo(
                    pose.worldPosition,origin,cellSize);
                render::ModelDraw command{};
                command.model=&model;
                command.persistentId=id;
                // Explicitly disable static packet reuse: moving panels
                // are mutable draw transforms even with stable Model data.
                command.staticPacketRevision=0;
                command.world=render::mat4::translate({
                    float(relative.x.toDouble()),
                    float(relative.y.toDouble()),
                    float(relative.z.toDouble())});
                renderer.drawModel(command);
                return true;
            },&drawError);
        if(!accepted)return fail(drawError.empty()?
            "Renderer3D refused committed aperture pose":drawError);
        if(lifts_&&!lifts_->submit([&](const render::Model& model,
                    const std::string& key,Vec3 position,uint32_t yaw){
            const uint64_t id=renderIdentity(key,254);
            if(!identities.emplace(id,std::make_pair(key,uint8_t(254))).second)return false;
            const auto relative=world::relativeTo(position,origin,cellSize);
            render::ModelDraw command{};
            command.model=&model;command.persistentId=id;command.staticPacketRevision=0;
            // buildingLocalToWorld yaw 1 maps +X to +Z, opposite rotateY.
            command.world=render::mat4::translate({
                float(relative.x.toDouble()),float(relative.y.toDouble()),
                float(relative.z.toDouble())})*
                render::mat4::rotateY(-float(yaw)*1.57079632679f);
            renderer.drawModel(command);return true;
        }))return fail("Renderer3D refused committed lift pose");
        if(error)error->clear();
        return true;
    }

    // Conservative GPU retirement. This intentionally uses a verified
    // full-device idle barrier instead of assuming asynchronous token
    // completion: never release Model pointers while a submitted frame
    // can still refer to them. On failure keep every model attached and
    // return false so host can retry from the next owning frame boundary.
    bool shutdown(std::string* error=nullptr){
        if(status_==Status::Detached){
            if(error)error->clear();
            return true;
        }
        if(!device_||!deviceLease_.validFor(*device_)){
            if(error)*error="GPU device generation unavailable for building retirement";
            return false;
        }
        if(!device_->waitForResourceIdle()){
            if(error)*error="GPU idle barrier refused building resource retirement";
            return false;
        }
        for(auto* model:models_){
            renderer_->invalidateModelCachesFor(model);
            render::releaseModel(*device_,*model);
        }
        models_.clear();
        device_=nullptr;renderer_=nullptr;session_=nullptr;
        namespace_=0;shell_=nullptr;shellOrigin_={};lifts_=nullptr;
        deviceLease_={};
        status_=Status::Detached;
        if(error)error->clear();
        return true;
    }

    Status status() const noexcept{return status_;}
    size_t modelCount() const noexcept{return models_.size();}
private:
    uint64_t renderIdentity(const std::string& socket,uint8_t leaf)const{
        // Stable within one scene instance and generation-independent of
        // rendering order or floating-origin cell. The host reserves a
        // different namespace for each concurrent scene instance.
        uint64_t hash=14695981039346656037ull;
        auto put=[&](uint8_t byte){hash^=byte;hash*=1099511628211ull;};
        for(int i=0;i<8;++i)put(uint8_t(namespace_>>(i*8)));
        for(unsigned char ch:socket)put(ch);
        put(leaf);
        return hash ? hash : 1u;
    }

    Status status_=Status::Detached;
    gfx::Device* device_=nullptr;
    render::Renderer3D* renderer_=nullptr;
    BuildingDualLeafGameSession* session_=nullptr;
    render::RenderDeviceLifetime::Lease deviceLease_;
    uint64_t namespace_=0;
    std::vector<render::Model*> models_;
    render::Model* shell_=nullptr;
    BuildingLiftRenderState* lifts_=nullptr;
    Vec3 shellOrigin_{};
};
} // namespace aiverse::building
