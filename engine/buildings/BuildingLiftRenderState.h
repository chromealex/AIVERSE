#pragma once
// CPU asset owner and immutable committed presentation snapshot. The
// render thread never reads mutable ECS or circulation controllers.
#include "engine/buildings/BuildingLiftPresentation.h"
#include "engine/buildings/BuildingMaterialAssets.h"
#include <mutex>
#include <string>
#include <utility>

namespace aiverse::building {
class BuildingLiftRenderState {
public:
    bool initialize(const BuildingCirculationHost& host,const BuildingStyle& style,
                    const BuildingImageByGuid& resolver,std::string* error=nullptr){
        std::lock_guard<std::mutex> lock(mutex_);
        if(!assets_.platforms.empty()||!assets_.leaves.empty())return false;
        try{
            auto staged=makeBuildingLiftVisualAssets(host,style);
            const int material=style.structure.plate>=0?style.structure.plate:style.shell;
            if(material<0||size_t(material)>=style.materials.size())return false;
            for(auto& [id,model]:staged.platforms){
                (void)id;
                if(!model||!bindBuildingStyleMaterials(*model,{style.materials[size_t(material)]},resolver,error))
                    return false;
            }
            for(auto& [id,leaf]:staged.leaves){
                (void)id;
                if(!leaf.model||!bindBuildingStyleMaterials(*leaf.model,{style.materials[size_t(material)]},resolver,error))
                    return false;
            }
            assets_=std::move(staged);
            return true;
        }catch(const std::exception& e){if(error)*error=e.what();return false;}
    }
    bool publish(BuildingLiftPresentationFrame frame){
        if(!frame.valid)return false;
        std::lock_guard<std::mutex> lock(mutex_);
        frame_=std::move(frame);return true;
    }
    template<class Callback> bool forEachCpuModel(Callback&& callback){
        std::lock_guard<std::mutex> lock(mutex_);
        for(auto& [id,model]:assets_.platforms){(void)id;if(!model||!callback(*model))return false;}
        for(auto& [id,leaf]:assets_.leaves){(void)id;if(!leaf.model||!callback(*leaf.model))return false;}
        return true;
    }
    // Stable string identities are in a separate renderer identity class
    // from normal aperture sockets. Geometry remains at local origin.
    template<class Callback> bool submit(Callback&& callback)const{
        std::lock_guard<std::mutex> lock(mutex_);
        if(!frame_.valid)return false;
        for(const auto& pose:frame_.platforms){
            auto it=assets_.platforms.find(pose.feature);
            if(it==assets_.platforms.end()||!it->second||
               !callback(*it->second,"lift-platform/"+std::to_string(pose.feature),
                         pose.worldPosition,pose.yaw))return false;
        }
        for(const auto& pose:frame_.doorLeaves){
            auto it=assets_.leaves.find(pose.key.feature);
            if(it==assets_.leaves.end()||!it->second.model||
               !callback(*it->second.model,"lift-door/"+std::to_string(pose.key.feature)+"/"+
                         std::to_string(pose.key.floor)+(pose.left?"/left":"/right"),
                         pose.worldPosition,pose.yaw))return false;
        }
        return true;
    }
    // Host must retire every GPU lease first; keep frame if only graphics
    // is retired so it can be re-uploaded without reading mutable ECS.
    void clearAssets(){
        std::lock_guard<std::mutex> lock(mutex_);assets_={};
    }
    void clear(){
        std::lock_guard<std::mutex> lock(mutex_);assets_={};frame_={};
    }
private:
    mutable std::mutex mutex_;
    BuildingLiftVisualAssets assets_;
    BuildingLiftPresentationFrame frame_;
};
}
