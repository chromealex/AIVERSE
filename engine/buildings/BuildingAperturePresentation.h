#pragma once
// CPU-only movable door, gate and roof presentation for the existing
// Renderer3D owner. No GPU upload and no gameplay/physics authority.
// Geometry is built once; each frame only instance transforms change.
#include "engine/buildings/BuildingApertureFixedTick.h"
#include "engine/buildings/BuildingMaterialAssets.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::building {

struct BuildingApertureVisualAsset {
    std::string socketId;
    std::unique_ptr<render::Model> leaf;
    // The model represents one half of a two-panel opening.
    bool slidesAlongX=true;
    Fixed halfTravel{};
};
struct BuildingApertureLeafPose {
    std::string socketId;
    uint8_t leaf=0;
    Vec3 worldPosition{};
    Fixed opening{};             // visual only; never grants passage
    uint64_t revision=0;
};
struct BuildingApertureVisualFrame {
    uint64_t serverTick=0;
    uint64_t revision=0;
    bool valid=false;
    std::string error;
    std::vector<BuildingApertureLeafPose> leaves;
};

inline std::unique_ptr<render::Model> buildingApertureLeafModel(
    const world::Bounds& bounds,bool slidesAlongX,
    const BuildingStyle& style,const std::string& name){
    const int material=style.structure.plate>=0?
        style.structure.plate:style.shell;
    if(material<0||size_t(material)>=style.materials.size())
        return {};
    const auto dimension=[](Fixed a,Fixed b){
        return float((b-a).toDouble());
    };
    const float width=dimension(bounds.min.x,bounds.max.x);
    const float depth=dimension(bounds.min.z,bounds.max.z);
    const float height=dimension(bounds.min.y,bounds.max.y);
    if(!(width>0.f&&depth>0.f&&height>0.f))return {};
    const float sx=slidesAlongX?width*0.5f:width;
    const float sz=slidesAlongX?depth:depth*0.5f;
    buildingStyleDetail::Mesher mesher(1);
    mesher.box(0,0.f,0.f,0.f,sx,height,sz);
    StyledStructure shell;
    shell.mesh.materials={style.materials[size_t(material)]};
    shell.mesh.vertices=std::move(mesher.v);
    shell.mesh.parts.push_back({0,std::move(mesher.perMaterial[0])});
    shell.mesh.boundsMin[0]=shell.mesh.boundsMin[1]=shell.mesh.boundsMin[2]=0.f;
    shell.mesh.boundsMax[0]=sx;
    shell.mesh.boundsMax[1]=height;
    shell.mesh.boundsMax[2]=sz;
    auto model=runtimeBuildingModel(shell,name);
    model->batching.mergeRigid=false;
    model->batching.autoLod=false;
    model->batching.geometryPoolPolicy=
        render::Model::Batching::GeometryPoolPolicy::Dedicated;
    if(!model->nodes.empty())model->nodes[0].runtimeMutable=true;
    return model;
}

// One instance per scene, one immutable CPU mesh per aperture socket.
// The existing render owner uploads Models once and maintains two node
// transforms per asset; this class never creates GPU resources.
class BuildingAperturePresentation {
public:
    bool initialize(const std::vector<BuildingAperturePose>& poses,
                    const BuildingStyle& style,
                    std::string* error=nullptr){
        if(!entries_.empty()){
            if(error)*error="Aperture presentation is already initialized";
            return false;
        }
        std::map<std::string,Entry> staged;
        for(const auto& pose:poses){
            if(pose.socketId.empty()||staged.contains(pose.socketId)){
                if(error)*error="Invalid/duplicate aperture socket";
                return false;
            }
            // Closed AABB gives the primary sliding axis after the
            // building's quarter-turn yaw has been applied.
            const Fixed dx=pose.bounds.max.x-pose.bounds.min.x;
            const Fixed dz=pose.bounds.max.z-pose.bounds.min.z;
            if(dx.raw<=0||dz.raw<=0){
                if(error)*error="Invalid aperture bounds";
                return false;
            }
            Entry entry;
            entry.pose=pose;
            entry.visualOpening=pose.open?Fixed::fromInt(1):Fixed{};
            entry.asset.socketId=pose.socketId;
            entry.asset.slidesAlongX=dx>=dz;
            entry.asset.halfTravel=(entry.asset.slidesAlongX?dx:dz)/2;
            entry.asset.leaf=buildingApertureLeafModel(
                pose.bounds,entry.asset.slidesAlongX,style,
                "building-aperture-"+pose.socketId);
            if(!entry.asset.leaf){
                if(error)*error="Could not build CPU aperture leaf model";
                return false;
            }
            staged.emplace(pose.socketId,std::move(entry));
        }
        entries_=std::move(staged);
        if(error)error->clear();
        return true;
    }

    // Only successful fixed-tick frames may change presentation targets.
    // Rejected/partial graph publications cannot visually unlock doors.
    bool accept(const BuildingApertureFixedTickFrame& frame,
                std::string* error=nullptr){
        if(!frame.complete||frame.tick<=lastTick_||
           frame.revision<lastRevision_||
           frame.committedPoses.size()!=entries_.size()){
            if(error)*error="Stale, incomplete or incompatible committed frame";
            return false;
        }
        for(const auto& pose:frame.committedPoses){
            const auto it=entries_.find(pose.socketId);
            if(it==entries_.end()||
               !sameBounds(it->second.pose.bounds,pose.bounds)||
               it->second.pose.kind!=pose.kind){
                if(error)*error="Aperture presentation geometry changed; reinitialize assets";
                return false;
            }
        }
        for(const auto& pose:frame.committedPoses)
            entries_.at(pose.socketId).pose=pose;
        lastTick_=frame.tick;
        lastRevision_=frame.revision;
        if(error)error->clear();
        return true;
    }

    // Use this ONLY after the dual-leaf physics owner successfully
    // publishes both collider positions AND all selected graph owners ACK.
    // It keeps visual panels aligned with the same committed substep.
    bool acceptPhysicalStep(const std::vector<BuildingAperturePose>& poses,
                            uint64_t serverTick,uint64_t revision,
                            std::string* error=nullptr){
        if(!serverTick||serverTick<=lastTick_||
           revision<lastRevision_||poses.size()!=entries_.size()){
            if(error)*error="Stale or incompatible physical leaf step";
            return false;
        }
        for(const auto& pose:poses){
            const auto it=entries_.find(pose.socketId);
            if(it==entries_.end()||
               !sameBounds(it->second.pose.bounds,pose.bounds)||
               it->second.pose.kind!=pose.kind||
               pose.physicalPermille>1000){
                if(error)*error="Physical leaf geometry or state changed";
                return false;
            }
        }
        for(const auto& pose:poses){
            auto& e=entries_.at(pose.socketId);
            e.pose=pose;
            e.visualOpening=Fixed::fromRatio(pose.physicalPermille,1000);
        }
        lastTick_=serverTick;lastRevision_=revision;
        if(error)error->clear();
        return true;
    }

    // Render-thread visual interpolation only; dtMs is not simulation time.
    // Opening/closing access is still determined by authoritative colliders.
    BuildingApertureVisualFrame interpolate(uint32_t dtMs,
                                           uint32_t animationMs=350){
        BuildingApertureVisualFrame frame;
        frame.serverTick=lastTick_;frame.revision=lastRevision_;
        if(entries_.empty()||lastTick_==0||animationMs==0){
            frame.error="No committed aperture presentation frame";
            return frame;
        }
        const Fixed alpha=Fixed::fromRatio(
            std::min(dtMs,animationMs),animationMs);
        for(auto& [id,e]:entries_){
            const Fixed target=e.pose.moving?
                Fixed::fromRatio(e.pose.physicalPermille,1000):
                (e.pose.open?Fixed::fromInt(1):Fixed{});
            if(e.visualOpening<target)
                e.visualOpening=fxMin(target,e.visualOpening+alpha);
            else if(e.visualOpening>target)
                e.visualOpening=fxMax(target,e.visualOpening-alpha);
            for(uint8_t side=0;side<2;++side){
                const Fixed shift=e.asset.halfTravel*e.visualOpening;
                Vec3 position=e.pose.bounds.min;
                if(e.asset.slidesAlongX){
                    position.x+=side==0?-shift:e.asset.halfTravel+shift;
                }else{
                    position.z+=side==0?-shift:e.asset.halfTravel+shift;
                }
                frame.leaves.push_back({
                    id,side,position,e.visualOpening,e.pose.revision});
            }
        }
        frame.valid=true;
        return frame;
    }

    // One authored material includes numeric PBR parameters and its
    // matching texture GUIDs. Use this before the render owner uploads.
    bool bindStyleMaterial(const std::string& socketId,
                           const StyleMaterial& style,
                           const BuildingImageByGuid& resolve,
                           std::string* error=nullptr){
        auto it=entries_.find(socketId);
        if(it==entries_.end()||!it->second.asset.leaf){
            if(error)*error="Unknown aperture asset";
            return false;
        }
        return bindBuildingStyleMaterials(*it->second.asset.leaf,
                                          {style},resolve,error);
    }

    // Optional real PBR texture binding. Call only at resource creation
    // BEFORE renderer upload. The existing asset/Renderer3D owner resolves
    // and supplies Image resources (base colour, metallic/roughness,
    // normal, AO, emissive); this module never opens files or decodes
    // textures on the simulation thread. Indices are model-local.
    bool bindPbrTextures(const std::string& socketId,
                         render::Material material,
                         std::vector<render::Image> images,
                         std::string* error=nullptr){
        auto it=entries_.find(socketId);
        if(it==entries_.end()||!it->second.asset.leaf||
           it->second.asset.leaf->materials.size()!=1){
            if(error)*error="Unknown aperture material asset";
            return false;
        }
        const auto valid=[&](int index){
            return index==-1||(index>=0&&size_t(index)<images.size());
        };
        if(!valid(material.baseTex)||!valid(material.mrTex)||
           !valid(material.normalTex)||!valid(material.occlusionTex)||
           !valid(material.emissiveTex)||!valid(material.teamTex)){
            if(error)*error="PBR texture index references missing model image";
            return false;
        }
        for(int index:material.graphTextures)
            if(!valid(index)){
                if(error)*error="ShaderGraph references missing model image";
                return false;
            }
        auto& model=*it->second.asset.leaf;
        model.images=std::move(images);
        model.materials[0]=std::move(material);
        // Existing renderer must invalidate/reupload material resources
        // if host modifies these assets after first GPU upload.
        if(error)error->clear();
        return true;
    }

    // Renderer-resource owner hook, only at spawn/retirement barriers.
    // Never call while active GPU draw packets still borrow these Models.
    template<class Callback>
    bool forEachCpuModel(Callback&& callback){
        for(auto& [id,e]:entries_){
            (void)id;
            if(!e.asset.leaf||!callback(*e.asset.leaf))return false;
        }
        return true;
    }

    // Read-only CPU geometry; render owner handles GPU leases and fences.
    const render::Model* model(const std::string& socketId)const{
        const auto it=entries_.find(socketId);
        return it==entries_.end()?nullptr:it->second.asset.leaf.get();
    }
    void clear(){entries_.clear();lastTick_=lastRevision_=0;}
private:
    static bool sameBounds(const world::Bounds& a,const world::Bounds& b){
        return a.min.x==b.min.x&&a.min.y==b.min.y&&a.min.z==b.min.z&&
               a.max.x==b.max.x&&a.max.y==b.max.y&&a.max.z==b.max.z;
    }
    struct Entry{
        BuildingAperturePose pose;
        BuildingApertureVisualAsset asset;
        Fixed visualOpening{};
    };
    std::map<std::string,Entry> entries_;
    uint64_t lastTick_=0,lastRevision_=0;
};

} // namespace aiverse::building
