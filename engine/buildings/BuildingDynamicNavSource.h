#pragma once
// Dynamic aperture collision overlay for the existing native nav::Source.
// Derived navigation only. Does not own SpatialWorld, entities or graphs.
// Collision snapshots are copied after authoritative physics commits.
#include "engine/buildings/BuildingNavSource.h"
#include "engine/buildings/BuildingApertureOwner.h"
#include <algorithm>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::building {

class BuildingDynamicNavSource final : public nav::Source {
public:
    // The static source must outlive this view, and may not be rebuilt
    // while a nav::Mesh is refreshing against it.
    explicit BuildingDynamicNavSource(const BuildingNavSource& base)
        :base_(base){}

    // Refuse a mismatch between aperture-owner state and the *actual*
    // registered SpatialWorld collision. On failure, leave the previous
    // committed source snapshot unchanged and return false. Game owner must
    // keep affected nav tiles dirty until reconciliation succeeds.
    bool synchronize(const BuildingApertureOwner& owner,
                     const world::SpatialWorld& spatial,
                     std::string* error=nullptr){
        if(!owner.attached()){
            if(error)*error="Aperture owner is not attached";
            return false;
        }
        const auto ids=owner.ownedColliderIds();
        const auto closed=owner.closedColliderBounds();
        std::vector<world::Bounds> updated;
        updated.reserve(closed.size());
        for(const auto& [id,bounds]:closed){
            if(!ids.contains(id)){
                if(error)*error="Aperture closed collider does not belong to owner";
                return false;
            }
            auto it=spatial.colliders().find(id);
            if(it==spatial.colliders().end()||
               !sameBounds(it->second.bounds,bounds)){
                if(error)*error="Aperture collision is not committed in SpatialWorld";
                return false;
            }
            updated.push_back(bounds);
        }
        for(uint32_t id:ids)
            if(!closed.contains(id)&&spatial.colliders().contains(id)){
                if(error)*error="Opened aperture still has a registered blocking collider";
                return false;
            }
        blockers_=std::move(updated);
        if(error)error->clear();
        return true;
    }

    void spans(int32_t x,int32_t z,std::vector<nav::Span>& out)const override{
        std::vector<nav::Span> original;
        base_.spans(x,z,original);
        for(const auto& span:original)
            if(!blocked(cellCenterX(x),cellCenterZ(z),span.y))
                out.push_back(span);
    }

    // Span indices passed by nav::Mesh refer to the FILTERED lists. Translate
    // them back to their original source index before delegating a link.
    // Otherwise removing one lower floor would connect the wrong upper floor.
    bool link(int32_t x0,int32_t z0,uint32_t i,
              int32_t x1,int32_t z1,uint32_t j)const override{
        const auto a=filteredIndex(x0,z0,i);
        const auto b=filteredIndex(x1,z1,j);
        if(!a||!b)return false;
        if(!base_.link(x0,z0,*a,x1,z1,*b))return false;
        const auto cfg=base_.config();
        const Fixed wx=(cellCenterX(x0)+cellCenterX(x1))/2;
        const Fixed wz=(cellCenterZ(z0)+cellCenterZ(z1))/2;
        (void)cfg;
        // We already validated both standing surfaces. Check the crossing
        // midpoint for a dynamic blocker at the higher surface elevation.
        std::vector<nav::Span> as,bs;
        base_.spans(x0,z0,as);base_.spans(x1,z1,bs);
        return !blocked(wx,wz,fxMax(as[*a].y,bs[*b].y));
    }

    const nav::Config& config()const noexcept{return base_.config();}
    size_t blockerCount()const noexcept{return blockers_.size();}

private:
    static bool sameBounds(const world::Bounds& a,const world::Bounds& b){
        return a.min.x==b.min.x&&a.min.y==b.min.y&&a.min.z==b.min.z&&
               a.max.x==b.max.x&&a.max.y==b.max.y&&a.max.z==b.max.z;
    }
    Fixed cellCenterX(int32_t x)const{
        const auto& c=base_.config();
        return c.originX+c.cell*int64_t(2*int64_t(x)+1)/2;
    }
    Fixed cellCenterZ(int32_t z)const{
        const auto& c=base_.config();
        return c.originZ+c.cell*int64_t(2*int64_t(z)+1)/2;
    }
    bool blocked(Fixed x,Fixed z,Fixed surface)const{
        const Fixed head=surface+base_.standingHeight();
        const Fixed skin=Fixed::fromRatio(1,100);
        for(const auto& box:blockers_){
            if(x<box.min.x||x>=box.max.x||
               z<box.min.z||z>=box.max.z)continue;
            if(box.max.y>surface+skin&&box.min.y<head)return true;
        }
        return false;
    }
    std::optional<uint32_t> filteredIndex(int32_t x,int32_t z,
                                          uint32_t filtered)const{
        std::vector<nav::Span> original;
        base_.spans(x,z,original);
        uint32_t current=0;
        for(uint32_t k=0;k<original.size();++k){
            if(blocked(cellCenterX(x),cellCenterZ(z),original[k].y))
                continue;
            if(current++==filtered)return k;
        }
        return {};
    }
    const BuildingNavSource& base_;
    std::vector<world::Bounds> blockers_;
};

// Register this callback for Navigation on the existing graph-change router.
// Keep the source alive while nav::Mesh uses it. Acknowledgement is only a
// successful overlay snapshot and dirty-tile mark, NOT a completed rebuild.
// The existing nav owner later calls mesh.refresh(source) on its own phase.
inline BuildingGraphChangeRouter::Consumer buildingDynamicNavInvalidator(
    BuildingDynamicNavSource& source,
    const BuildingApertureOwner& apertures,
    const world::SpatialWorld& spatial,nav::Mesh& mesh){
    return [&source,&apertures,&spatial,&mesh](
        const BuildingGraphChange& change){
        if(!mesh.built()||!source.synchronize(apertures,spatial))
            return false;
        markBuildingNavCollisionChanged(mesh,change.bounds);
        return true;
    };
}

} // namespace aiverse::building
