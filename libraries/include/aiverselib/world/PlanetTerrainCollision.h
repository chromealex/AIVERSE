// Native projection of the canonical immutable Surface into the existing
// Box3D Scene. No terrain state or new physics world. Bounded full-body
// or owner-declared streamed residency.
#pragma once
#include "PlanetTerrainResidency.h"
#include "engine/physics/PhysisBox3D.h"
#include <algorithm>
#include <memory>
#include <vector>

namespace aiverse::lib::worldprocess {
class PlanetTerrainCollision {
public:
    size_t count()const{return entries_.size();}
    physics::BodyHandle handle(int face,int x,int y)const{
        for(const auto& e:entries_)if(e.face==face&&e.x==x&&e.y==y)return e.handle;
        return physics::kNoBody;
    }
    bool publish(physics::Scene& scene,std::shared_ptr<const game::Surface> surface,
                 int body,uint64_t revision,PlanetTerrainCollisionConfig config={}){
        if(!surface||!surface->tab||body<0||size_t(body)>=surface->bodies.size())return false;
        const auto& def=surface->bodies[size_t(body)];const int n=def.faceSize;
        if(n<2||n>PlanetTerrainCollisionConfig::maxStreamedFaceSize||
           surface->cubeOf(body).faceSize()!=n)return false;
        const size_t per=size_t(n+1)*size_t(n+1);
        if(def.cornerBase>surface->corners.size()||6*per>surface->corners.size()-def.cornerBase)return false;
        if(scene_&&(scene_!=&scene||body!=body_||surface->tab!=surface_->tab||
           revision<revision_))return false;
        for(const auto& e:entries_)if(!scene.valid(e.handle))return false;
        if(scene_&&revision==revision_&&surface!=surface_&&surface->corners!=surface_->corners)return false;
        const auto requested=planPlanetTerrainTiles(*surface,body,config);
        if(!requested)return false;
        const auto& cube=surface->cubeOf(body);
        std::vector<Entry> next;next.reserve(requested->size());
        std::vector<physics::BodyDesc> descriptions;descriptions.reserve(requested->size());
        std::vector<size_t> changed;changed.reserve(requested->size());
        std::set<physics::BodyHandle> retained;
        constexpr int tile=PlanetTerrainCollisionConfig::tileSize;
        for(const auto& key:*requested){
            const auto [f,x,y]=key;
            Entry e;e.face=f;e.x=x;e.y=y;
            const int w=std::min(tile,n-x),h=std::min(tile,n-y);
            e.heights.reserve(size_t(w+1)*size_t(h+1));
            for(int j=0;j<=h;++j)for(int i=0;i<=w;++i){
                const int cx=x+i,cy=y+j;
                const auto height=surface->corner(body,f,cx,cy);
                const auto radius=def.radius+Fixed::fromRatio(height,16);
                if(radius<=Fixed{}||radius>Fixed::fromInt(1500))return false;
                if(cx==0||cx==n||cy==0||cy==n){
                    int ids[3];const int count=cube.cornerDuplicates(f,cx,cy,ids);
                    for(int k=0;k<count;++k)
                        if(surface->corners[def.cornerBase+size_t(ids[k])]!=height)return false;
                }
                e.heights.push_back(height);
            }
            const size_t index=next.size();
            const auto old=std::lower_bound(entries_.begin(),entries_.end(),key,
                [](const Entry& entry,const PlanetTerrainTileKey& k){
                    return PlanetTerrainTileKey{entry.face,entry.x,entry.y}<k;
                });
            if(old!=entries_.end()&&PlanetTerrainTileKey{old->face,old->x,old->y}==key&&old->heights==e.heights){
                e.handle=old->handle;retained.insert(e.handle);
            }else{
                physics::BodyDesc d;d.motion=physics::Motion::Static;d.shape=physics::ShapeKind::Mesh;
                d.vertices.reserve(e.heights.size());d.indices.reserve(size_t(w)*h*6);
                for(int j=0;j<=h;++j)for(int i=0;i<=w;++i){
                    // CubeSphere gives identical directions for seam aliases.
                    const auto radius=def.radius+Fixed::fromRatio(surface->corner(body,f,x+i,y+j),16);
                    d.vertices.push_back(physics::physisB3(cube.cornerDir(f,x+i,y+j)*radius));
                }
                for(int j=0;j<h;++j)for(int i=0;i<w;++i){
                    const int32_t a=j*(w+1)+i,b=a+1,c=a+w+1,q=c+1;
                    // Face u cross v points inward. Reverse for outward
                    // collision triangles; same diagonal as terrain render.
                    d.indices.insert(d.indices.end(),{a,c,q,a,q,b});
                }
                changed.push_back(index);descriptions.push_back(std::move(d));
            }
            next.push_back(std::move(e));
        }
        if(changed.size()>scene.maxBodies()-scene.bodyCount())return false;
        // All allocating mesh preparation happens before side effects.
        // Retain old colliders until EVERY new collider is successfully made.
        std::vector<physics::BodyHandle> created;created.reserve(changed.size());
        auto discard=[&](){for(auto handle:created)scene.destroy(handle);};
        try{
            for(size_t i=0;i<changed.size();++i){
                const auto handle=scene.create(descriptions[i]);
                if(handle==physics::kNoBody){discard();return false;}
                created.push_back(handle);next[changed[i]].handle=handle;
            }
        }catch(...){discard();throw;}
        // Exclusive serialized scene ownership: all old handles were
        // validated, no query/step is allowed while versions overlap.
        for(const auto& old:entries_)if(!retained.contains(old.handle))scene.destroy(old.handle);
        entries_=std::move(next);surface_=std::move(surface);
        scene_=&scene;body_=body;revision_=revision;return true;
    }
    // Admission for a position and its immediate seam-aware neighbor cells.
    // Not a swept-volume proof: caller must pin the complete query region.
    bool covers(Vec3 position)const{
        if(!scene_||!surface_)return false;
        const int cell=planetTerrainCell(*surface_,body_,position);
        if(cell<0)return false;
        const auto covered=[&](int index){
            if(index<0)return true; // cube-corner missing diagonal
            const auto key=planetTerrainTile(*surface_,body_,index);
            return key&&scene_->valid(handle(key->face,key->x,key->y));
        };
        if(!covered(cell))return false;
        for(int k=0;k<8;++k)if(!covered(surface_->neighbor(cell,k)))return false;
        return true;
    }
    bool reset(physics::Scene& scene){
        if(scene_&&scene_!=&scene)return false;
        for(const auto& e:entries_)if(scene.valid(e.handle))scene.destroy(e.handle);
        entries_.clear();surface_.reset();scene_=nullptr;body_=-1;revision_=0;return true;
    }
private:
    struct Entry {int face=0,x=0,y=0;std::vector<int16_t> heights;physics::BodyHandle handle=physics::kNoBody;};
    std::vector<Entry> entries_;
    std::shared_ptr<const game::Surface> surface_;
    physics::Scene* scene_=nullptr;
    int body_=-1;uint64_t revision_=0;
};
} // namespace aiverse::lib::worldprocess
