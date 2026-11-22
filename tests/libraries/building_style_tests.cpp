// building-style: the sci-fi style file loads, the dressed mesh matches building-gen (the door corridor is free,
// windows hold only glass), determinism per seed, palettes and seeds vary the look, quarter-turn yaw, style
// proportions, triangle budget and readable style errors. argv[1] = repository root (style files).
#include <aiverselib/systems/BuildingStructure.h>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <stdexcept>
using namespace aiverse;using namespace aiverse::lib::systems;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string(__FILE__)+":"+std::to_string(__LINE__)+": " #x);}while(false)
namespace {
struct Aabb{float lo[3],hi[3];};
Aabb box(const BuildingBox& b,float shrink){Aabb a;Vec3 p[2]={b.min,b.max};
    for(int k=0;k<3;++k){auto get=[&](const Vec3& v){return float((k==0?v.x:k==1?v.y:v.z).toDouble());};a.lo[k]=get(p[0])+shrink;a.hi[k]=get(p[1])-shrink;}return a;}
bool hits(const StyledMesh& m,const Aabb& q,int skipMaterial){
    for(const auto& part:m.parts){
        if(part.material==skipMaterial)continue;
        for(size_t i=0;i+2<part.indices.size();i+=3){
            float lo[3],hi[3];for(int k=0;k<3;++k){lo[k]=1e9f;hi[k]=-1e9f;}
            for(int c=0;c<3;++c){const auto& v=m.vertices[part.indices[i+size_t(c)]];for(int k=0;k<3;++k){lo[k]=std::min(lo[k],v.pos[k]);hi[k]=std::max(hi[k],v.pos[k]);}}
            bool overlap=true;for(int k=0;k<3;++k)overlap&=lo[k]<q.hi[k]&&q.lo[k]<hi[k];
            if(overlap)return true;
        }
    }
    return false;
}
int materialIndex(const BuildingStyle& s,const std::string& id){for(size_t i=0;i<s.materials.size();++i)if(s.materials[i].id==id)return int(i);return -1;}
// Openings stay usable: nothing in the doorway and one metre in front of it; window holes hold only glass.
void checkOpenings(const BuildingDef& d,const BuildingStyle& st,uint64_t seed,Vec3 origin,uint32_t yaw){
    auto m=dressBuilding(d,st,seed,origin,yaw);auto l=generateBuilding(d,seed,origin,yaw);CHECK(l&&!m.vertices.empty());
    int glass=materialIndex(st,"glass"),lit=materialIndex(st,"glassLit");
    bool door=false;
    for(const auto& o:l->openings){
        auto q=box(o.hole,0.02f);
        if(o.door){
            door=true;
            // Stretch the doorway out through the wall to the outside door point (the agent's path in).
            float ox=float(l->doorOutside.x.toDouble()),oz=float(l->doorOutside.z.toDouble());
            q.lo[0]=std::min(q.lo[0],ox);q.hi[0]=std::max(q.hi[0],ox);q.lo[2]=std::min(q.lo[2],oz);q.hi[2]=std::max(q.hi[2],oz);
            CHECK(!hits(m,q,-1));
        }else{
            // Only the panes (one of two glass materials) may sit in a window hole.
            for(const auto& part:m.parts){
                if(part.material==glass||part.material==lit)continue;
                StyledMesh one;one.vertices=m.vertices;one.parts={part};CHECK(!hits(one,q,-1));
            }
        }
    }
    CHECK(door);
}
std::string readFile(const std::string& p){std::ifstream f(p,std::ios::binary);if(!f)throw std::runtime_error("cannot read "+p);std::stringstream s;s<<f.rdbuf();return s.str();}
}
int main(int argc,char** argv){
    try{
        std::string root=argc>1?argv[1]:".";
        // Every shipped style: openings across sizes, floors, seeds and all four quarter turns.
        for(const char* id:{"scifi","modern"}){
            auto style=readBuildingStyle(Json::parse(readFile(root+"/engine/assets/building-styles/"+id+".json")),id);
            CHECK(style.id==id&&style.shell>=0&&!style.palettes.empty()&&style.proportions.window);
            size_t built=0;
            for(uint64_t seed=1;seed<=24;++seed){
                BuildingDef t;t.floors=uint32_t(1+seed%5);t.width=Fixed::fromInt(int64_t(11+seed%12));t.depth=Fixed::fromInt(int64_t(10+seed%7));t.partition=seed%3!=0;
                applyBuildingStyle(t,style);
                if(!generateBuilding(t,seed,{},0))continue;
                checkOpenings(t,style,seed,{Fixed::fromInt(int64_t(seed)*40),{},-20_fx},uint32_t(seed%4));++built;
            }
            CHECK(built>=16);
        }
        auto st=readBuildingStyle(Json::parse(readFile(root+"/engine/assets/building-styles/scifi.json")),"scifi");
        // Style proportions apply when they fit and leave the definition alone when they do not.
        BuildingDef d;d.width=16_fx;d.depth=12_fx;d.floors=4;CHECK(applyBuildingStyle(d,st));
        CHECK(d.windowWidth==st.proportions.windowWidth&&d.floorHeight==st.proportions.floor&&d.floors==st.proportions.maxFloors);
        BuildingDef tiny;tiny.width=6_fx;tiny.depth=6_fx;auto before=tiny;CHECK(!applyBuildingStyle(tiny,st));CHECK(tiny==before);
        // Determinism and variation.
        auto a=dressBuilding(d,st,7),b=dressBuilding(d,st,7);
        CHECK(a.vertices.size()==b.vertices.size()&&a.parts.size()==b.parts.size()&&a.materials==b.materials);
        for(size_t i=0;i<a.vertices.size();++i)for(int k=0;k<3;++k)CHECK(a.vertices[i].pos[k]==b.vertices[i].pos[k]);
        bool differs=false,palette=false;
        for(uint64_t s=8;s<24;++s){auto c=dressBuilding(d,st,s);differs|=c.vertices.size()!=a.vertices.size();palette|=!(c.materials==a.materials);}
        CHECK(differs&&palette);
        // The mesh covers the collision: its bounds hold every building-gen box.
        auto l=*generateBuilding(d,7,{},0);
        for(const auto& bx:l.boxes){auto q=box(bx,0);for(int k=0;k<3;++k)CHECK(q.lo[k]>=a.boundsMin[k]-1e-3f&&q.hi[k]<=a.boundsMax[k]+1e-3f);}
        // Quarter turn: same triangles, footprint extents swap.
        auto r=dressBuilding(d,st,7,{},1);CHECK(r.triangles()==a.triangles());
        CHECK(std::abs((r.boundsMax[0]-r.boundsMin[0])-(a.boundsMax[2]-a.boundsMin[2]))<1e-3f&&std::abs((r.boundsMax[2]-r.boundsMin[2])-(a.boundsMax[0]-a.boundsMin[0]))<1e-3f);
        // Budget: the largest sci-fi building of the street generator stays under 25k triangles.
        BuildingDef big;big.floors=5;big.width=22_fx;big.depth=16_fx;applyBuildingStyle(big,st);auto bm=dressBuilding(big,st,3);CHECK(bm.triangles()>0&&bm.triangles()<25000);
        // A building that does not fit gives an empty mesh.
        CHECK(dressBuilding(tiny,st,1).vertices.empty());CHECK(dressBuilding(d,st,1,{},4).vertices.empty());
        // A minimal style: only the required materials, no decoration - the bare collision boxes.
        auto bare=readBuildingStyle(Json::parse(R"({"id":"bare","materials":{"m":{}},"shell":"m","interior":"m","roof":"m"})"));
        auto bareMesh=dressBuilding(d,bare,7);CHECK(bareMesh.triangles()==l.boxes.size()*12);
        // Errors name the path.
        auto fails=[](const char* t,const char* needle){try{readBuildingStyle(Json::parse(t));}catch(const JsonError& e){
            if(std::string(e.what()).find(needle)!=std::string::npos)return true;std::printf("unexpected error: %s\n",e.what());}return false;};
        CHECK(fails(R"({"id":"x","materials":{"m":{}},"shell":"m","interior":"m","roof":"m","bogus":1})","style: unknown key 'bogus'"));
        CHECK(fails(R"({"id":"x","materials":{"m":{}},"shell":"n","interior":"m","roof":"m"})","style.shell: unknown material 'n'"));
        CHECK(fails(R"({"id":"x","materials":{"m":{}},"interior":"m","roof":"m"})","shell, interior and roof materials are required"));
        CHECK(fails(R"({"id":"x","materials":{"m":{"color":[1,2]}},"shell":"m","interior":"m","roof":"m"})","style.materials.m.color: expected [r, g, b]"));
        CHECK(fails(R"({"id":"x","materials":{"m":{}},"shell":"m","interior":"m","roof":"m","props":[{"kind":"orb","material":"m"}]})","style.props[0].kind: expected box, mast, tank, stack or dish"));
        CHECK(fails(R"({"id":"x","materials":{"m":{}},"shell":"m","interior":"m","roof":"m","roofBlocks":{"material":"m","height":[3,1]}})","style.roofBlocks.height: max below min"));
        // Structures (one armoured hull per building): every kind of the sci-fi style builds, deterministically,
        // its gate points stand outside its collision, a quarter turn keeps the triangles, unknown kinds are empty.
        CHECK(st.structures.size()>=4);
        for(const auto& kind:st.structures){
            for(uint64_t seed=1;seed<=12;++seed){
                auto s1=dressStructure(st,kind.id,seed,{10_fx,{},-5_fx},uint32_t(seed%4)),s2=dressStructure(st,kind.id,seed,{10_fx,{},-5_fx},uint32_t(seed%4));
                CHECK(!s1.mesh.vertices.empty()&&!s1.boxes.empty()&&s1.mesh.vertices.size()==s2.mesh.vertices.size()&&s1.boxes==s2.boxes);
                CHECK(s1.gates.size()==kind.doors&&s1.mesh.triangles()<20000);
                for(const auto& gp:s1.gates)for(const auto& b:s1.boxes)CHECK(!(gp.x>b.min.x&&gp.x<b.max.x&&gp.z>b.min.z&&gp.z<b.max.z));
                CHECK(dressStructure(st,kind.id,seed,{},1).mesh.triangles()==dressStructure(st,kind.id,seed,{},0).mesh.triangles());
            }
        }
        CHECK(dressStructure(st,"nope",1).mesh.vertices.empty());
        CHECK(fails(R"({"id":"x","materials":{"m":{}},"shell":"m","interior":"m","roof":"m","structures":{"a":{}}})","style.structures: needs the structure materials block"));
        std::printf("building-style: sci-fi house %zu triangles, %zu materials\n",a.triangles(),a.parts.size());
    }catch(const std::exception& e){std::fprintf(stderr,"building style tests failed: %s\n",e.what());return 1;}
    std::puts("building-style tests passed");return 0;
}
