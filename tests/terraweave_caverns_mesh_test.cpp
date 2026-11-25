#include "engine/world/TerraWeaveCavernsVolumeSnapshot.h"
#include <map>
#include <cassert>
#include <iostream>
using namespace aiverse::terraweave::caverns;
static void portalSurfaceWinding() {
    CaveVolumeSnapshot snapshot;
    snapshot.settings.tunnelChance=snapshot.settings.chamberChance=0;
    snapshot.settings.minYcm=-790;snapshot.settings.maxYcm=-100;
    snapshot.stamps.push_back({1,{0,-500,0},220,CaveStampMode::Carve});
    snapshot.entrances.push_back({1,0,0,0,-400,125});
    canonicalizeCaveSnapshot(snapshot);
    using Point=std::array<int,3>;
    struct Incidence {int uses=0,direction=0;};
    std::map<std::pair<Point,Point>,Incidence> edges;
    for(int x=-1;x<=0;++x)for(int y=-1;y<=0;++y)for(int z=-1;z<=0;++z){
        const auto mesh=generateCaveSnapshotMesh(snapshot,{x,y,z},16,50);
        for(size_t i=0;i<mesh.indices.size();i+=3){
            Point points[3];
            for(int j=0;j<3;++j){
                const auto v=mesh.vertices[mesh.indices[i+j]];
                points[j]={800*x+int(std::lround(v.x*100)),800*y+int(std::lround(v.y*100)),800*z+int(std::lround(v.z*100))};
            }
            for(int j=0;j<3;++j){
                const auto& a=points[j];const auto& b=points[(j+1)%3];
                auto& edge=edges[std::minmax(a,b)];
                ++edge.uses;edge.direction+=a<b?1:-1;
            }
        }
    }
    assert(!edges.empty());
    for(const auto& [key,edge]:edges)assert(edge.uses<=2 && (edge.uses==1||edge.direction==0));
}
int main(){
    portalSurfaceWinding();
    Settings s;s.cellCm=2000;s.tunnelRadiusCm=500;s.chamberRadiusCm=850;
    s.chamberChance=1000;s.tunnelChance=1000;s.minYcm=-30000;s.maxYcm=-100;
    auto a=generateCaveMesh(s,{0,-1,0},12,200);
    auto b=generateCaveMesh(s,{0,-1,0},12,200);
    assert(!a.vertices.empty());assert(a.vertices.size()==b.vertices.size());
    assert(a.indices==b.indices);
    for(size_t i=0;i<a.vertices.size();++i){
        assert(a.vertices[i].x==b.vertices[i].x);
        assert(a.vertices[i].y==b.vertices[i].y);
        assert(a.vertices[i].z==b.vertices[i].z);
    }
    assert(a.indices.size()%3==0);
    assert(caveFieldCm(s,{0,100,0})<0);
    s.chamberChance=0;s.tunnelChance=0;
    assert(generateCaveMesh(s,{0,-1,0},8,200).indices.empty());
    std::cout<<"caverns mesh OK triangles="<<a.indices.size()/3<<"\n";
}
