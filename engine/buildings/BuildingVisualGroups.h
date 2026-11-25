#pragma once
// Shared CPU ownership map for runtime render groups and GLB serialization.
// Atlas remapping is supported only for a mesh with identical part/triangle
// order (the existing BuildingTextureAtlas preserves this contract).
#include "engine/systems/BuildingStructure.h"
#include <map>
#include <stdexcept>
#include <vector>
#include <cstdint>

namespace aiverse::lib::systems {
struct BuildingVisualGroup {
    std::string name;
    std::map<int,std::vector<uint32_t>> indices;
};
inline std::vector<BuildingVisualGroup> buildingVisualGroups(
    const StyledStructure& source,const StyledMesh& output){
    const auto& m=source.mesh;
    if(m.parts.size()!=output.parts.size()||m.materials.size()!=output.materials.size())
        throw std::invalid_argument("Room partition mesh layout changed");
    std::map<std::string,size_t> rooms;
    for(const auto& r:source.roomVisualRanges){
        if(r.roomId.empty()||r.roomId=="building-shell"||!r.vertexCount||
            r.firstVertex>m.vertices.size()||r.vertexCount>m.vertices.size()-r.firstVertex)
            throw std::invalid_argument("Invalid room visual range");
        rooms.try_emplace(r.roomId,0);
    }
    if(rooms.size()>128)throw std::invalid_argument("Room partition budget exceeded");
    std::vector<BuildingVisualGroup> groups(1);groups.front().name="building-shell";
    for(auto& [id,index]:rooms){index=groups.size();groups.push_back({id,{}});}
    std::vector<size_t> owner(m.vertices.size(),0);
    for(const auto& r:source.roomVisualRanges)
        for(size_t i=r.firstVertex;i<size_t(r.firstVertex)+r.vertexCount;++i){
            if(owner[i])throw std::invalid_argument("Overlapping room visual ranges");
            owner[i]=rooms.at(r.roomId);
        }
    for(size_t p=0;p<m.parts.size();++p){
        const auto& a=m.parts[p];const auto& b=output.parts[p];
        if(a.material!=b.material||a.material<0||size_t(a.material)>=m.materials.size()||
            a.indices.size()%3||a.indices.size()!=b.indices.size())
            throw std::invalid_argument("Room partition triangle layout changed");
        for(size_t j=0;j<a.indices.size();j+=3){
            const auto x=a.indices[j],y=a.indices[j+1],z=a.indices[j+2];
            if(x>=owner.size()||y>=owner.size()||z>=owner.size()||
               owner[x]!=owner[y]||owner[x]!=owner[z])
                throw std::invalid_argument("Room triangle crosses visual owners");
            auto& dst=groups[owner[x]].indices[a.material];
            for(size_t k=0;k<3;++k){
                const auto index=b.indices[j+k];
                if(index>=output.vertices.size())throw std::invalid_argument("Invalid room output vertex");
                dst.push_back(index);
            }
        }
    }
    return groups;
}
} // namespace aiverse::lib::systems
