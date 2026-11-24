#include "engine/world/TerraWeaveCavernsLodPolicy.h"
#include <cassert>
#include <iostream>
using namespace aiverse::terraweave::caverns;
static CaveMesh mesh(ChunkKey3 key,unsigned axis,bool low,bool split,bool divergent=false){
    CaveMesh m;m.key=key;
    // Physical span 400 cm, chunk-local coordinates are in metres.
    auto vertex=[](float x,float y,float z){
        MeshVertex v;v.x=x;v.y=y;v.z=z;return v;
    };
    if(axis==0){
        if(low)m.vertices={vertex(4,0,0),vertex(4,4,0),vertex(3,0,0)};
        else if(split)m.vertices={vertex(0,0,0),vertex(0,2,divergent?.25f:0),vertex(0,4,0),vertex(1,0,0)};
        else m.vertices={vertex(0,0,0),vertex(0,4,0),vertex(1,0,0)};
    }else if(axis==1){
        if(low)m.vertices={vertex(0,4,0),vertex(4,4,0),vertex(0,3,0)};
        else if(split)m.vertices={vertex(0,0,0),vertex(2,0,divergent?.25f:0),vertex(4,0,0),vertex(0,1,0)};
        else m.vertices={vertex(0,0,0),vertex(4,0,0),vertex(0,1,0)};
    }else{
        if(low)m.vertices={vertex(0,0,4),vertex(4,0,4),vertex(0,0,3)};
        else if(split)m.vertices={vertex(0,0,0),vertex(2,divergent?.25f:0,0),vertex(4,0,0),vertex(0,0,1)};
        else m.vertices={vertex(0,0,0),vertex(4,0,0),vertex(0,0,1)};
    }
    m.indices=split&&!low?std::vector<uint32_t>{0,1,3,1,2,3}:
                            std::vector<uint32_t>{0,1,2};
    return m;
}
int main(){
    for(unsigned axis=0;axis<3;++axis){
        ChunkKey3 lowKey{-3,-3,-3},highKey=lowKey;
        if(axis==0)++highKey.x;
        if(axis==1)++highKey.y;
        if(axis==2)++highKey.z;
        // Supported coarse-low/fine-high pair in both argument orders.
        const auto coarse=mesh(lowKey,axis,true,false);
        const auto fine=mesh(highKey,axis,false,true);
        auto result=resolveCaveFaceLod({&coarse,2,200,&fine,4,100});
        assert(result.decision==CaveFaceDecision::Subdivided);
        assert(result.firstMesh.indices.size()==6);
        assert(result.commonSpacingCm==100);
        auto reversed=resolveCaveFaceLod({&fine,4,100,&coarse,2,200});
        assert(reversed.decision==CaveFaceDecision::Subdivided);
        assert(reversed.secondMesh.indices.size()==6);
        // Divergent contours are refused; source meshes remain unchanged.
        const auto different=mesh(highKey,axis,false,true,true);
        auto fallback=resolveCaveFaceLod({&coarse,2,200,&different,4,100});
        assert(fallback.decision==CaveFaceDecision::RegenerateAtCommonLod);
        assert(fallback.commonResolution==4);
        assert(fallback.firstMesh.indices==coarse.indices);
        // Fine on negative side, coarse on positive side: safe fallback.
        const auto fineLow=mesh(lowKey,axis,true,false);
        const auto coarseHigh=mesh(highKey,axis,false,false);
        auto reverseFace=resolveCaveFaceLod({&fineLow,4,100,&coarseHigh,2,200});
        assert(reverseFace.commonSpacingCm==100);
        // This fixture can have matching border edges, otherwise fallback.
        assert(reverseFace.decision==CaveFaceDecision::ExactMatch ||
               reverseFace.decision==CaveFaceDecision::RegenerateAtCommonLod);
    }
    CaveFaceLease lease{{-1,0,0},{0,0,0},11,12,CaveFaceDecision::ExactMatch,4,100};
    assert(caveFaceLeaseCurrent(lease,11,12));
    assert(!caveFaceLeaseCurrent(lease,11,13));
    std::cout<<"caverns stage15 six-direction face policy OK\n";
}
