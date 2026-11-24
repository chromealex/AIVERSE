#pragma once
#include "engine/buildings/BuildingMaterialAssets.h"
#include "engine/systems/BuildingInstances.h"
#include <set>
#include <map>
namespace aiverse::building {
using namespace lib::systems;
// Each triangle belongs wholly to the shell or to one authored interior
// item. Split index streams, share the original vertices and materials.
// No extra geometry, collision, or duplicated textures.
inline std::unique_ptr<render::Model> runtimeRoomPartitionedBuildingModel(
    const StyledStructure& building,const BuildingImageByGuid& resolve,
    const std::string& name,std::string* error=nullptr,BuildingSurfaceCache* surfaces=nullptr){
    // This optional representation uses per-room frustum culling instead
    // of the monolithic authored LOD chain.
    auto base=building;base.visualLods.clear();
    auto model=runtimeTexturedBuildingModel(base,resolve,name,error,surfaces);
    if(!model||building.roomVisualRanges.empty())return model;
    std::map<std::string,size_t> roomIndex;
    for(const auto& r:building.roomVisualRanges)roomIndex.try_emplace(r.roomId,0);
    size_t next=1;for(auto& [id,index]:roomIndex){(void)id;index=next++;}
    if(next>129)throw std::invalid_argument("Room partition budget exceeded");
    std::vector<size_t> owner(building.mesh.vertices.size(),0);
    for(const auto& r:building.roomVisualRanges){
        if(r.roomId.empty()||r.firstVertex>owner.size()||r.vertexCount>owner.size()-r.firstVertex)
            throw std::invalid_argument("Invalid room visual range");
        for(size_t i=r.firstVertex;i<size_t(r.firstVertex)+r.vertexCount;++i){
            if(owner[i])throw std::invalid_argument("Overlapping room visual ranges");
            owner[i]=roomIndex.at(r.roomId);
        }
    }
    std::vector<std::map<int,std::vector<uint32_t>>> groups(next);
    for(const auto& p:building.mesh.parts)for(size_t i=0;i<p.indices.size();i+=3){
        const auto a=p.indices[i],b=p.indices[i+1],c=p.indices[i+2];
        if(a>=owner.size()||b>=owner.size()||c>=owner.size()||owner[a]!=owner[b]||owner[a]!=owner[c])
            throw std::invalid_argument("Room triangle crosses visual owners");
        auto& dst=groups[owner[a]][p.material];dst.insert(dst.end(),{a,b,c});
    }
    model->meshes.clear();model->indices.clear();model->nodes.clear();
    auto append=[&](size_t i,const std::string& id){
        if(groups[i].empty())return;
        render::MeshDef mesh;mesh.name=id;
        for(const auto& [material,indices]:groups[i]){
            render::Primitive p;p.material=material;p.firstIndex=uint32_t(model->indices.size());
            p.indexCount=uint32_t(indices.size());p.firstVertex=0;p.vertexCount=uint32_t(model->vertices.size());
            model->indices.insert(model->indices.end(),indices.begin(),indices.end());mesh.primitives.push_back(std::move(p));
        }
        render::Node node;node.name=id;node.mesh=int(model->meshes.size());
        model->meshes.push_back(std::move(mesh));model->nodes.push_back(std::move(node));
    };
    append(0,"building-shell");
    for(const auto& [id,index]:roomIndex)append(index,id);
    for(size_t i=0;i<building.lights.size();++i){
        const auto& l=building.lights[i];render::Node node;node.name=l.id;node.light=int(i);
        node.t={l.position[0],l.position[1],l.position[2]};model->nodes.push_back(std::move(node));
    }
    model->batching.mergeRigid=false;model->batching.autoLod=false;model->finalize();
    return model;
}
struct BuildingPortalPlane {std::array<double,3> n;double d=0;};
using PortalPoint=std::array<double,3>;
inline double portalDot(PortalPoint a,PortalPoint b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
inline PortalPoint portalSub(PortalPoint a,PortalPoint b){return {a[0]-b[0],a[1]-b[1],a[2]-b[2]};}
inline PortalPoint portalCross(PortalPoint a,PortalPoint b){return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};}
struct BuildingPortalVisibility {bool conservativeFallback=true;std::set<std::string> visibleRooms;};
// Interior-only portal traversal. Exterior openings, vertical routes,
// damage, balconies, non-orthogonal shells or camera ambiguity can expose
// another path not represented by horizontal room edges: fail open.
inline BuildingPortalVisibility buildingPortalVisibility(const BuildingStructureGraph& g,PortalPoint eye,size_t budget=512){
    auto all=[&](){BuildingPortalVisibility v;for(const auto& r:g.rooms)v.visibleRooms.insert(r.id);return v;};
    if(!g||g.rooms.empty()||!g.sockets.empty()||!g.removedDamage.empty()||!g.visualPrisms.empty()||
       !g.balconies.empty()||!g.terraces.empty()||!budget)return all();
    for(double x:eye)if(!std::isfinite(x))return all();
    std::string start;std::map<std::string,const BuildingGraphRoom*> rooms;
    for(const auto& r:g.rooms){
        if(!rooms.emplace(r.id,&r).second)return all();
        if(eye[0]>r.x0+.36&&eye[0]<r.x1-.36&&eye[1]>r.y0+.01&&eye[1]<r.y1-.01&&eye[2]>r.z0+.36&&eye[2]<r.z1-.36){
            if(!start.empty())return all();start=r.id;
        }
    }
    if(start.empty())return all();
    for(const auto& c:g.connections)if(c.kind!=BuildingConnectionKind::Passage||!rooms.contains(c.from)||!rooms.contains(c.to)||
        (c.id.rfind("passage/x/",0)!=0&&c.id.rfind("passage/z/",0)!=0)||c.width<=0||c.height<=0)return all();
    struct State {std::string room;std::set<std::string> path;std::vector<BuildingPortalPlane> planes;};
    std::vector<State> queue{{start,{start},{}}};BuildingPortalVisibility result;result.conservativeFallback=false;
    for(size_t i=0;i<queue.size();++i){
        if(queue.size()>budget)return all();
        const auto state=queue[i];result.visibleRooms.insert(state.room);
        for(const auto& e:g.connections){
            if(e.from!=state.room&&e.to!=state.room)continue;
            const auto& dest=e.from==state.room?e.to:e.from;
            if(state.path.contains(dest))continue;
            const bool xPlane=e.id.rfind("passage/x/",0)==0;
            const double half=e.width*.5+.15,bottom=e.y-.15,top=e.y+e.height+.15;
            const double axis=xPlane?e.x:e.z;
            if(std::abs((xPlane?eye[0]:eye[2])-axis)<.15)return all();
            std::array<PortalPoint,4> corners;
            if(xPlane)corners={PortalPoint{axis,bottom,e.z-half},{axis,bottom,e.z+half},{axis,top,e.z+half},{axis,top,e.z-half}};
            else corners={PortalPoint{e.x-half,bottom,axis},{e.x+half,bottom,axis},{e.x+half,top,axis},{e.x-half,top,axis}};
            bool outside=false;
            for(const auto& p:state.planes)if(std::all_of(corners.begin(),corners.end(),[&](auto c){return portalDot(p.n,c)+p.d<-.02;})){outside=true;break;}
            if(outside)continue;
            auto child=state;child.room=dest;child.path.insert(dest);
            PortalPoint center{e.x,(bottom+top)*.5,e.z};
            for(size_t j=0;j<4;++j){
                auto normal=portalCross(portalSub(corners[j],eye),portalSub(corners[(j+1)%4],eye));
                const double len=std::sqrt(portalDot(normal,normal));if(len<1e-8)return all();
                for(auto& n:normal)n/=len;
                if(portalDot(normal,portalSub(center,eye))<0)for(auto& n:normal)n=-n;
                child.planes.push_back({normal,-portalDot(normal,eye)});
            }
            queue.push_back(std::move(child));
        }
    }
    return result;
}
// All-pass mask: use ONLY if the host explicitly certifies this view has
// no auxiliary shadow/reflection/capture requirements. Otherwise use the
// normal Renderer3D per-node frustum culling without a mask.
inline std::vector<uint8_t> buildingRoomNodeMask(const render::Model& model,const BuildingPortalVisibility& visibility){
    std::vector<uint8_t> mask(model.nodes.size(),1);
    if(visibility.conservativeFallback)return mask;
    for(size_t i=0;i<model.nodes.size();++i){
        const auto& n=model.nodes[i];if(n.mesh>=0&&n.name.rfind("room/",0)==0&&!visibility.visibleRooms.contains(n.name))mask[i]=0;
    }
    return mask;
}
} // namespace aiverse::building
