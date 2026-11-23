#pragma once
// Opt-in REAL local-loopback FPS host for generated procedural building
// sockets: authoritative ECS/ActorHost fixed tick, same SpatialWorld,
// client prediction collider mirror and Renderer3D ModelDraw submission.
// This is NOT the remote-network snapshot protocol.
#include "engine/actor/ActorBuildingApertures.h"
#include "engine/net/authority/Session.h"
#include "engine/render/Renderer3D.h"
#include "engine/render/Model.h"
#include <aiverselib/systems/BuildingStyle.h>
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::client {

class FpsBuildingDoorScene {
public:
    using Read=std::function<bool(const std::string&,std::string&)>;
    // The optional map["buildingDoors"] declaration carries the actual
    // generated structure-graph socket IDs and closed-world AABBs. Native
    // game authoring must export these IDs when placing a procedural GLB.
    // IDs are in the reserved 0x60000000..0x6fffffff range.
    bool attach(net::authority::Server& server,
                net::authority::Client& client,
                const Json& scene,
                const Read& read,gfx::Device& device,
                std::string* error=nullptr){
        if(active_||!scene.isObject()||!read){
            if(error)*error="Invalid FPS building door scene";
            return false;
        }
        const auto styleId=scene.getStr("style");
        if(styleId.empty()||styleId.size()>48||
           !std::all_of(styleId.begin(),styleId.end(),[](char c){
             return (c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-';
           })){
            if(error)*error="Unsupported authored building style ID";
            return false;
        }
        std::string styleText;
        const std::string stylePath="engine/assets/building-styles/"+styleId+".json";
        if(!read(stylePath,styleText)){
            if(error)*error="Building style not present in packaged assets";
            return false;
        }
        lib::systems::BuildingStyle style;
        try{style=lib::systems::readBuildingStyle(Json::parse(styleText));}
        catch(const std::exception& e){if(error)*error=e.what();return false;}
        if(style.id!=styleId){
            if(error)*error="Style file ID mismatched";return false;
        }
        const auto& sockets=scene["sockets"];
        if(!sockets.isArray()||sockets.size()==0||sockets.size()>32){
            if(error)*error="FPS building doors require 1..32 authored sockets";
            return false;
        }
        const uint64_t sceneId=uint64_t(scene.getInt("sceneId",0));
        if(!sceneId){
            if(error)*error="FPS building scene ID must be nonzero";return false;
        }
        const auto vec=[](const Json& value,Vec3& out){
            if(!value.isArray()||value.size()!=3)return false;
            for(size_t i=0;i<3;++i)
                if(!value[i].isNumber()||
                   !std::isfinite(value[i].toDouble())||
                   std::abs(value[i].toDouble())>100000.)
                    return false;
            out={value[0].fx(),value[1].fx(),value[2].fx()};
            return true;
        };
        std::vector<building::BuildingAperturePose> poses;
        std::map<std::string,building::BuildingDualLeafIds> colliderIds;
        for(const auto& socket:sockets.items()){
            if(!socket.isObject()||!socket["id"].isString()){
                if(error)*error="Invalid authored building socket";return false;
            }
            const std::string id=socket.getStr("id");
            if(id.empty()||id.size()>128||colliderIds.contains(id)){
                if(error)*error="Duplicate/invalid generated socket ID";
                return false;
            }
            const auto first=socket.getInt("firstCollider",0);
            const auto second=socket.getInt("secondCollider",0);
            if(first<0x60000000||first>0x6fffffff||
               second<0x60000000||second>0x6fffffff||
               first==second){
                if(error)*error="Collider IDs must be distinct and reserved";
                return false;
            }
            building::BuildingAperturePose pose;
            pose.socketId=id;
            const auto feature=socket.getInt("featureIndex",-1);
            if(feature<0||feature>100000){
                if(error)*error="Invalid authored aperture feature ID";
                return false;
            }
            pose.featureIndex=uint32_t(feature);
            if(!vec(socket["min"],pose.bounds.min)||
               !vec(socket["max"],pose.bounds.max)||
               !(pose.bounds.min.x<pose.bounds.max.x&&
                 pose.bounds.min.y<pose.bounds.max.y&&
                 pose.bounds.min.z<pose.bounds.max.z)){
                if(error)*error="Invalid generated aperture closed bounds";
                return false;
            }
            pose.open=socket.getBool("open",false);
            pose.physicalPermille=pose.open?1000:0;
            pose.kind=lib::systems::BuildingFeatureKind::PersonnelDoor;
            poses.push_back(std::move(pose));
            colliderIds.emplace(id,building::BuildingDualLeafIds{
                uint32_t(first),uint32_t(second)});
        }
        const auto all=std::make_shared<actor::ActorBuildingApertures>();
        if(!all->attach(server.simulation(),server.module(),
                        sceneId,poses,colliderIds,style,error))return false;
        // Packaged games can provide their stable GUID->content file map.
        // Never guess source paths from GUIDs or silently omit PBR maps.
        const auto& texturePaths=scene["textureAssets"];
        auto resolve=[&](const std::string& guid,render::Image& image,
                         std::string& issue){
            if(!texturePaths.isObject()||
               !texturePaths[guid].isString()){
                issue="Missing packaged PBR texture GUID mapping";
                return false;
            }
            const std::string path=texturePaths[guid].str();
            if(path.rfind("content/",0)!=0||path.find("..")!=std::string::npos||
               path.find('\\')!=std::string::npos){
                issue="Unsafe packaged texture path";
                return false;
            }
            std::string bytes;
            if(!read(path,bytes)||bytes.size()>16u*1024u*1024u||
               !render::decodeImage(bytes,image)){
                issue="Packaged building PBR image cannot be decoded";
                return false;
            }
            return true;
        };
        const int material=style.structure.plate>=0?
            style.structure.plate:style.shell;
        if(material<0||size_t(material)>=style.materials.size()){
            if(error)*error="Building has no movable-leaf material";
            all->detach();
            return false;
        }
        for(const auto& pose:poses)
            if(!all->bindMaterial(pose.socketId,
                    style.materials[size_t(material)],resolve,error)){
                all->detach();
                return false;
            }
        // Upload once in the real existing Renderer3D Device owner. A
        // failed upload cannot leave the new session live.
        bool uploaded=all->forEachCpuModel([&](render::Model& model){
            model.batching.mergeRigid=false;
            model.batching.autoLod=false;
            model.batching.allowGeometryPool=false;
            render::prepareModelUpload(model);
            return render::uploadModel(device,model,true);
        });
        if(!uploaded){
            device.waitForResourceIdle();
            all->forEachCpuModel([&](render::Model& model){
                render::releaseModel(device,model);return true;
            });
            all->detach();
            if(error)*error="FPS building leaf GPU asset upload failed";
            return false;
        }
        for(uint32_t id:all->physics().ownedColliderIds())
            if(client.localBuildingPredictionCollision().colliders().contains(id)){
                device.waitForResourceIdle();
                all->forEachCpuModel([&](render::Model& model){
                    render::releaseModel(device,model);return true;
                });
                all->detach();
                if(error)*error="FPS prediction collision ID already owned";
                return false;
            }
        host_=std::move(all);
        server.module().setBuildingApertures(host_);
        client_=&client;
        active_=true;
        syncPrediction();
        if(error)error->clear();
        return true;
    }

    // LOCAL loopback: never accept untrusted remote door state. This mirror
    // copies only committed server leaf collision into FPS client prediction.
    void syncPrediction(){
        if(!active_||!client_||!host_)return;
        const auto closed=host_->physics().closedColliderBounds();
        for(uint32_t id:host_->physics().ownedColliderIds()){
            client_->localBuildingPredictionCollision().remove(id);
            if(auto it=closed.find(id);it!=closed.end())
                (void)client_->localBuildingPredictionCollision().add({id,it->second});
        }
    }

    bool submit(render::Renderer3D& renderer,world::Cell origin,
                uint32_t renderMs,std::string* error=nullptr){
        if(!active_||!host_)return true;
        return host_->submit(renderMs,[&](const render::Model& model,
                                          const building::BuildingApertureLeafPose& pose){
            render::ModelDraw draw{};
            draw.model=&model;
            draw.world=render::mat4::translate({
                float(world::relativeTo(pose.worldPosition,origin,32).x.toDouble()),
                float(world::relativeTo(pose.worldPosition,origin,32).y.toDouble()),
                float(world::relativeTo(pose.worldPosition,origin,32).z.toDouble())});
            // Renderer source-packet caching disabled for moving leaves.
            draw.staticPacketRevision=0;
            renderer.drawModel(draw);
            return true;
        },error);
    }

    void detach(net::authority::Server* server,
                gfx::Device& device,render::Renderer3D& renderer){
        if(!active_||!host_)return;
        // GPU draws and model packets may still borrow the leaf pointer.
        device.waitForResourceIdle();
        host_->forEachCpuModel([&](render::Model& model){
            renderer.invalidateModelCachesFor(&model);
            render::releaseModel(device,model);
            return true;
        });
        if(server&&server->module().buildingApertures()==host_)
            server->module().setBuildingApertures({});
        if(client_)
            for(uint32_t id:host_->physics().ownedColliderIds())
                client_->localBuildingPredictionCollision().remove(id);
        host_->detach();
        host_.reset();client_=nullptr;active_=false;
    }

    bool active()const{return active_;}
private:
    std::shared_ptr<actor::ActorBuildingApertures> host_;
    net::authority::Client* client_=nullptr;
    bool active_=false;
};

} // namespace aiverse::client
