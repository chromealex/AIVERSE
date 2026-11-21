// generateHumanCharacterLods: index-only chain for skinned character parts.
#include "engine/character/vivid/HumanLod.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
using namespace aiverse;
static void check(bool ok,const char* what){if(!ok){std::fprintf(stderr,"FAIL: %s\n",what);std::exit(1);}}
int main(){
    render::Model m;
    const int n=48;
    for(int y=0;y<=n;++y)for(int x=0;x<=n;++x){render::ModelVertex v;v.pos={float(x)/n,float(y)/n,0.05f*std::sin(float(x)*.4f)*std::cos(float(y)*.3f)};v.nrm={0,0,1};v.joints[0]=0;v.weights[0]=255;m.vertices.push_back(v);}
    for(int y=0;y<n;++y)for(int x=0;x<n;++x){const uint32_t a=uint32_t(y*(n+1)+x),b=a+1,c=a+uint32_t(n+1),d=c+1;for(uint32_t i:{a,c,b,b,c,d})m.indices.push_back(i);}
    render::MeshDef mesh;mesh.name="body";render::Primitive p;p.indexCount=uint32_t(m.indices.size());p.vertexCount=uint32_t(m.vertices.size());p.jointPalette={0};mesh.primitives.push_back(p);
    m.meshes.push_back(mesh);m.nodes.resize(2);m.nodes[0].mesh=0;m.nodes[0].skin=0;render::Skin skin;skin.joints={1};m.skins.push_back(skin);
    const auto vertices=m.vertices.size();
    const size_t added=vivid::generateHumanCharacterLods(m);
    check(added>=1,"no LOD generated for a skinned mesh");
    check(m.vertices.size()==vertices,"LOD must share LOD0 vertices");
    check(m.meshes[0].lods.size()==added,"LOD chain not linked");
    size_t prev=m.meshes[0].primitives[0].indexCount;
    for(const auto& l:m.meshes[0].lods){const auto& q=m.meshes[size_t(l.mesh)].primitives.at(0);
        check(q.indexCount<prev&&q.indexCount%3==0,"LOD not coarser");check(q.jointPalette==std::vector<uint16_t>{0},"joint palette lost");
        for(uint32_t i=0;i<q.indexCount;++i)check(m.indices[q.firstIndex+i]<vertices,"LOD index out of range");prev=q.indexCount;}
    check(vivid::generateHumanCharacterLods(m)==0,"existing chain regenerated");
    std::puts("human_lod ok");
}
