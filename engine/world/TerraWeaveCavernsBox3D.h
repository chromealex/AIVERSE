#pragma once
// Optional Box3D integration for the same authoritative cut triangles
// as Caverns Resident / SpatialWorld. This does not own simulation ticks.
#include "engine/render/TerraWeaveCavernsResident.h"
#include "engine/physics/Scene.h"
#include <map>
#include <tuple>
namespace aiverse::terraweave::caverns {
inline physics::BodyDesc makeCavernsBox3DBody(const CutNativePayload& payload,
                                               int32_t tileSizeMetres,uint32_t userData=0) {
    if(payload.collision.empty()||payload.collision.size()>65534||
       tileSizeMetres<16||tileSizeMetres>4096)
        throw std::invalid_argument("Caverns Box3D payload");
    const int64_t ox=int64_t(payload.cell.x)*tileSizeMetres;
    const int64_t oz=int64_t(payload.cell.z)*tileSizeMetres;
    if(ox< -100000||oz< -100000||ox>100000-tileSizeMetres||
       oz>100000-tileSizeMetres)
        throw std::out_of_range("Caverns Box3D extent");
    physics::BodyDesc body;body.motion=physics::Motion::Static;
    body.shape=physics::ShapeKind::Mesh;
    body.category=physics::kCategoryStatic;body.userData=userData;
    body.position={float(ox),0,float(oz)};
    const Vec3 origin{Fixed::fromInt(ox),{},Fixed::fromInt(oz)};
    std::map<std::tuple<int64_t,int64_t,int64_t>,int32_t> positions;
    body.indices.reserve(payload.collision.size()*3);
    for(const auto& t:payload.collision) {
        if(!world::SpatialWorld::validTriangle(t))
            throw std::invalid_argument("Caverns invalid Box3D triangle");
        for(const Vec3 p:{t.a,t.b,t.c}) {
            const auto v=p-origin;
            auto key=std::make_tuple(v.x.raw,v.y.raw,v.z.raw);
            auto it=positions.find(key);
            int32_t index;
            if(it==positions.end()){
                if(body.vertices.size()>=65536)
                    throw std::length_error("Caverns Box3D vertex budget");
                index=int32_t(body.vertices.size());
                positions.emplace(key,index);
                body.vertices.push_back({float(v.x.toDouble()),
                    float(v.y.toDouble()),float(v.z.toDouble())});
            }else index=it->second;
            body.indices.push_back(index);
        }
    }
    return body;
}
// Box3D Scene owner thread, strictly between steps; host must suppress
// original terrain Box3D body for this tile. No implicit double physics.
class CavernsBox3DPublisher {
    struct Entry {physics::BodyHandle body=physics::kNoBody;uint64_t revision=0;};
    std::map<world::Cell,Entry> bodies_;
public:
    physics::BodyHandle body(world::Cell cell)const{
        const auto it=bodies_.find(cell);
        return it==bodies_.end()?physics::kNoBody:it->second.body;
    }
    bool publish(physics::Scene& scene,const CutNativePayload& payload,
                 int32_t tileSizeMetres,uint32_t userData=0){
        auto old=bodies_.find(payload.cell);
        if(old!=bodies_.end()&&payload.revision<old->second.revision)return false;
        if(old==bodies_.end()&&bodies_.size()>=256)
            throw std::length_error("Caverns Box3D resident tile budget");
        auto desc=makeCavernsBox3DBody(payload,tileSizeMetres,userData);
        const auto created=scene.create(desc);
        if(created==physics::kNoBody)return false;
        try{
            auto [it,inserted]=bodies_.try_emplace(payload.cell,Entry{created,payload.revision});
            if(!inserted){
                const auto previous=it->second.body;
                it->second={created,payload.revision};
                if(scene.valid(previous))scene.destroy(previous);
            }
        }catch(...){scene.destroy(created);throw;}
        return true;
    }
    void unload(physics::Scene& scene,world::Cell cell){
        const auto it=bodies_.find(cell);
        if(it==bodies_.end())return;
        if(scene.valid(it->second.body))scene.destroy(it->second.body);
        bodies_.erase(it);
    }
    void shutdown(physics::Scene& scene){
        for(const auto& [cell,e]:bodies_){
            (void)cell;
            if(scene.valid(e.body))scene.destroy(e.body);
        }
        bodies_.clear();
    }
};
} // namespace aiverse::terraweave::caverns
