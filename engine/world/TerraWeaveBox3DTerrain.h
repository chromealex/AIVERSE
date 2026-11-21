// TerraWeave -> optional Box3D authoritative terrain collider adapter.
// Reuses the exact triangle list baked for render::Model and SpatialWorld.
// Does not alter NEREID water, ECS scheduling, or graphics Device state.
#pragma once
#include "engine/world/TerraWeaveTerrainPatch.h"
#include "engine/physics/Scene.h"
#include <cstdint>
#include <limits>
#include <map>
#include <stdexcept>
#include <tuple>
#include <utility>
#include <vector>

namespace aiverse::terraweave {

inline physics::BodyDesc makeTerrainBox3DBody(
        const TerrainPatch& patch,uint32_t userData=0) {
    if(patch.resolution<2 || patch.resolution>129 ||
       patch.cellSizeMetres<16 || patch.cellSizeMetres>4096 ||
       patch.collision.size()!=size_t(patch.resolution-1u)*
                               size_t(patch.resolution-1u)*2u)
        throw std::invalid_argument("TerraWeave Box3D terrain topology");
    physics::BodyDesc body;
    body.motion=physics::Motion::Static;
    body.shape=physics::ShapeKind::Mesh;
    body.category=physics::kCategoryStatic;
    body.userData=userData;
    const int64_t x=int64_t(patch.cell.x)*patch.cellSizeMetres;
    const int64_t z=int64_t(patch.cell.z)*patch.cellSizeMetres;
    if(x<-100000 || x>100000-patch.cellSizeMetres ||
       z<-100000 || z>100000-patch.cellSizeMetres)
        throw std::out_of_range("TerraWeave Box3D terrain outside physics scene");
    body.position={float(x),0,float(z)};
    const Vec3 origin{
        Fixed::fromInt(x),{},Fixed::fromInt(z)
    };
    std::map<std::tuple<int64_t,int64_t,int64_t>,int32_t> indices;
    body.vertices.reserve(size_t(patch.resolution)*patch.resolution);
    body.indices.reserve(patch.collision.size()*3u);
    for(const auto& triangle:patch.collision) {
        for(const auto& p:{triangle.a,triangle.b,triangle.c}) {
            const Vec3 local=p-origin;
            const auto key=std::make_tuple(
                local.x.raw,local.y.raw,local.z.raw);
            const auto it=indices.find(key);
            int32_t vertex=0;
            if(it==indices.end()) {
                if(body.vertices.size()>=65536u)
                    throw std::length_error("TerraWeave Box3D mesh vertex budget");
                vertex=static_cast<int32_t>(body.vertices.size());
                body.vertices.push_back({
                    float(local.x.toDouble()),
                    float(local.y.toDouble()),
                    float(local.z.toDouble())
                });
                indices.emplace(key,vertex);
            }else vertex=it->second;
            body.indices.push_back(vertex);
        }
    }
    return body;
}

// One authoritative Box3D scene body per pinned terrain tile. The replacement
// static collider is CREATED before the old one is removed. If creation
// rejects an invalid/oversized mesh, the previous ground remains usable.
// Call on the Box3D scene owner thread BETWEEN physics::Scene::step() calls.
// Revisions are ordered per tile: a patch baked from an OLDER soil revision
// than the resident body is rejected (returns false, scene untouched), so a
// late/out-of-order bake cannot replace newer ground. After a rollback/load
// that legitimately lowers the soil revision, call unload()/shutdown() first.
class TerrainBox3DPublisher {
public:
    size_t residentTiles()const{return bodies_.size();}
    physics::BodyHandle body(world::Cell cell)const {
        const auto it=bodies_.find(cell);
        return it==bodies_.end()?physics::kNoBody:it->second.body;
    }
    uint64_t revision(world::Cell cell)const {
        const auto it=bodies_.find(cell);
        return it==bodies_.end()?0:it->second.revision;
    }
    bool publish(physics::Scene& scene,const TerrainPatch& patch,
                 uint32_t userData=0){
        if(bodies_.size()>=256u && !bodies_.contains(patch.cell))
            throw std::length_error("TerraWeave Box3D resident tile budget");
        if(const auto current=bodies_.find(patch.cell);
           current!=bodies_.end() &&
           patch.sourceRevision<current->second.revision)
            return false; // stale bake: keep the newer resident collider
        const auto description=makeTerrainBox3DBody(patch,userData);
        const auto created=scene.create(description);
        if(created==physics::kNoBody)return false;
        // Keep the old collider alive until the new one exists and the
        // registry slot can be allocated without throwing.
        try {
            auto [it,inserted]=bodies_.try_emplace(
                patch.cell,Entry{created,patch.sourceRevision});
            if(!inserted){
                const auto old=it->second.body;
                it->second={created,patch.sourceRevision};
                if(scene.valid(old))scene.destroy(old);
            }
        }catch(...){
            scene.destroy(created);
            throw;
        }
        return true;
    }
    void unload(physics::Scene& scene,world::Cell cell){
        const auto it=bodies_.find(cell);
        if(it==bodies_.end())return;
        if(scene.valid(it->second.body))scene.destroy(it->second.body);
        bodies_.erase(it);
    }
    void shutdown(physics::Scene& scene){
        for(const auto& [cell,entry]:bodies_){
            (void)cell;
            if(scene.valid(entry.body))scene.destroy(entry.body);
        }
        bodies_.clear();
    }
private:
    struct Entry {
        physics::BodyHandle body=physics::kNoBody;
        uint64_t revision=0;
    };
    std::map<world::Cell,Entry> bodies_;
};

} // namespace aiverse::terraweave
