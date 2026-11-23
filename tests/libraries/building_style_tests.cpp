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
        // Stage 3: functional geometry comes from bounded declarative forms.
        // Every role has some authored silhouette components, and turning them
        // off removes real geometry/collision, not merely changing materials.
        for(const auto& kind:st.structures){
            CHECK(!kind.forms.empty());
            auto onlyCore=st;
            for(auto& entry:onlyCore.structures)if(entry.id==kind.id)entry.forms.clear();
            auto withForms=dressStructure(st,kind.id,5);
            auto core=dressStructure(onlyCore,kind.id,5);
            CHECK(withForms.mesh.vertices.size()>core.mesh.vertices.size());
            CHECK(withForms.mesh.triangles()>core.mesh.triangles());
            CHECK(withForms.boxes.size()>core.boxes.size());
            CHECK(withForms.mesh.triangles()<20000);
            for(uint64_t seed:{1u,5u,19u})for(uint32_t turn=0;turn<4;++turn){
                auto one=dressStructure(st,kind.id,seed,{},turn,StructureSize{28.f,15.f,24.f});
                auto two=dressStructure(st,kind.id,seed,{},turn,StructureSize{28.f,15.f,24.f});
                CHECK(one.mesh.vertices.size()==two.mesh.vertices.size()&&one.mesh.parts.size()==two.mesh.parts.size());
                CHECK(one.boxes==two.boxes&&one.gates==two.gates);
                CHECK(one.mesh.triangles()<20000);
                for(size_t i=0;i<one.mesh.vertices.size();++i){
                    for(int j=0;j<3;++j)CHECK(std::abs(one.mesh.vertices[i].pos[j]-two.mesh.vertices[i].pos[j])<1e-6f);
                }
            }
        }
        // Curved primitives: each adds geometry; the arch must preserve its
        // open central corridor in coarse collision (no one giant box).
        bool curvedKinds[3]{false,false,false};
        for(const auto& k:st.structures)for(const auto& v:k.forms){
            if(v.kind==StructureForm::Kind::Arch)curvedKinds[0]=true;
            if(v.kind==StructureForm::Kind::Dome)curvedKinds[1]=true;
            if(v.kind==StructureForm::Kind::SweptHull)curvedKinds[2]=true;
        }
        for(bool exists:curvedKinds)CHECK(exists);
        {
            auto archStyle=st;
            auto* kinds=&archStyle.structures;
            for(auto& kind:*kinds)if(kind.id=="depot"){
                kind.forms.clear();
                StructureForm arch;arch.kind=StructureForm::Kind::Arch;
                arch.material=archStyle.shell;arch.x=0.5f;arch.z=1.35f;
                arch.width=0.60f;arch.height=1.3f;arch.depth=0.18f;
                arch.level=0.0f;kind.forms.push_back(arch);
            }
            auto withArch=dressStructure(archStyle,"depot",9);
            auto without=st;
            for(auto& kind:without.structures)if(kind.id=="depot")kind.forms.clear();
            auto plain=dressStructure(without,"depot",9);
            CHECK(withArch.mesh.triangles()>plain.mesh.triangles());
            CHECK(withArch.boxes.size()>plain.boxes.size()+4);
            // The arch must not obstruct its geometric centre at human height.
            // This isolates the module in front of the depot (beyond the pad).
            const float zCenter=1.35f*8.f;
            const float yClear=0.35f+0.8f;
            const float xCenter=4.f;
            for(const auto& collision:withArch.boxes){
                const auto b=box(collision,0);
                CHECK(!(xCenter>b.lo[0]&&xCenter<b.hi[0]&&
                        yClear>b.lo[1]&&yClear<b.hi[1]&&
                        zCenter>b.lo[2]&&zCenter<b.hi[2]));
            }
            for(const auto& v:withArch.mesh.vertices)
                for(int a=0;a<3;++a)CHECK(std::isfinite(v.pos[a])&&std::isfinite(v.nrm[a]));
        }
        // A very low building is rejected if the final envelope would crush
        // the visual front gate below clearance (unsized legacy stays unchanged).
        CHECK(dressStructure(st,"command",13,{},0,StructureSize{24.f,5.f,18.f}).mesh.vertices.empty());
        // The four primitive kinds must all be exercised in shipped data.
        bool found[7]={false,false,false,false,false,false,false};
        for(const auto& kind:st.structures)for(const auto& form:kind.forms)
            found[int(form.kind)]=true;
        for(bool exists:found)CHECK(exists);
        const auto withShape=R"({"id":"x","materials":{"m":{}},"shell":"m","interior":"m","roof":"m","structure":{"hull":"m","dark":"m"},"structures":{"x":{"forms":[{"kind":"silo","material":"m","at":[0.5,0.5],"size":[0.2,0.6,0.2],"level":0.3}]}}})";
        auto fromData=readBuildingStyle(Json::parse(withShape));
        CHECK(fromData.structureKind("x")&&fromData.structureKind("x")->forms.size()==1);
        CHECK(dressStructure(fromData,"x",1).mesh.triangles()>0);
        auto invalidForm=[](const std::string& form,const std::string& why) {
            const std::string json="{\"id\":\"x\",\"materials\":{\"m\":{}},\"shell\":\"m\",\"interior\":\"m\",\"roof\":\"m\",\"structure\":{\"hull\":\"m\",\"dark\":\"m\"},\"structures\":{\"x\":{\"forms\":["+form+"]}}}";
            try{(void)readBuildingStyle(Json::parse(json));}
            catch(const JsonError& e){return std::string(e.what()).find(why)!=std::string::npos;}
            return false;
        };
        CHECK(invalidForm(R"({"kind":"dome","material":"m","at":[0.5,0.5],"size":[0.2,0.6,0.2],"chance":-0.1})","outside supported bounds"));
        CHECK(invalidForm(R"({"kind":"orb","material":"m","at":[0.5,0.5],"size":[0.2,0.6,0.2]})",".kind: expected"));
        CHECK(invalidForm(R"({"kind":"silo","material":"unknown","at":[0.5,0.5],"size":[0.2,0.6,0.2]})",".material: unknown"));
        CHECK(invalidForm(R"({"kind":"silo","material":"m","at":[0.5],"size":[0.2,0.6,0.2]})",".at: expected array"));
        CHECK(invalidForm(R"({"kind":"silo","material":"m","at":[0.5,0.5],"size":[0.2,0.6,0.2],"chance":1.2})","outside supported bounds"));
        CHECK(invalidForm(R"({"kind":"silo","material":"m","at":[0.5,0.5],"size":[0.2,0.6,0.2],"repeat":2.5})",".repeat: expected integer"));
        CHECK(invalidForm(R"({"kind":"silo","material":"m","at":[0.5,0.5],"size":[0.2,0.6,0.2],"repeat":8,"step":[0.8,0]})",".step: repeated form escapes"));
        CHECK(dressStructure(st,"nope",1).mesh.vertices.empty());
        // Data-driven silhouettes distinguish functions without hardcoded game genres.
        CHECK(st.structureKind("barracks")->massing==StructureMassing::Bastion);
        CHECK(st.structureKind("command")->massing==StructureMassing::Spire);
        CHECK(st.structureKind("factory")->massing==StructureMassing::TwinBay);
        CHECK(st.structureKind("depot")->massing==StructureMassing::Terraced);
        const StructureSize dims{24.f,12.f,18.f}; // W, H, D in metres
        for(const auto& kind:st.structures)for(uint32_t turn=0;turn<4;++turn){
            const Vec3 at{10_fx,2_fx,-5_fx};
            auto a=dressStructure(st,kind.id,42,at,turn,dims);
            auto b=dressStructure(st,kind.id,42,at,turn,dims);
            CHECK(!a.mesh.vertices.empty()&&a.mesh.triangles()==b.mesh.triangles());
            CHECK(a.boxes==b.boxes&&a.gates==b.gates);
            const float want[3]={turn%2?18.f:24.f,12.f,turn%2?24.f:18.f};
            const float base[3]={10.f,2.f,-5.f};
            for(int axis=0;axis<3;++axis){
                CHECK(std::abs(a.mesh.boundsMin[axis]-base[axis])<0.002f);
                CHECK(std::abs(a.mesh.boundsMax[axis]-base[axis]-want[axis])<0.002f);
            }
            for(const auto& vertex:a.mesh.vertices)for(int axis=0;axis<3;++axis)
                CHECK(vertex.pos[axis]>=base[axis]-0.002f&&vertex.pos[axis]<=base[axis]+want[axis]+0.002f);
            for(const auto& bx:a.boxes){
                auto bb=box(bx,0.f);
                for(int axis=0;axis<3;++axis)
                    CHECK(bb.lo[axis]>=base[axis]-0.002f&&bb.hi[axis]<=base[axis]+want[axis]+0.002f);
            }
        }
        auto small=dressStructure(st,"factory",42,{},0,StructureSize{12.f,10.f,12.f});
        auto large=dressStructure(st,"factory",42,{},0,StructureSize{40.f,12.f,30.f});
        CHECK(!small.mesh.vertices.empty()&&!large.mesh.vertices.empty());
        CHECK(small.mesh.boundsMax[0]!=large.mesh.boundsMax[0]);
        for(const auto bad:{StructureSize{3,10,12},StructureSize{12,3,12},
                            StructureSize{12,10,3},StructureSize{12,10,0},
                            StructureSize{12,10,999}})
            CHECK(dressStructure(st,"factory",1,{},0,bad).mesh.vertices.empty());
        CHECK(fails(R"({"id":"x","materials":{"m":{}},"shell":"m","interior":"m","roof":"m","structure":{"hull":"m","dark":"m"},"structures":{"x":{"massing":"orb"}}})","style.structures.x.massing: expected"));

        CHECK(fails(R"({"id":"x","materials":{"m":{}},"shell":"m","interior":"m","roof":"m","structures":{"a":{}}})","style.structures: needs the structure materials block"));
        std::printf("building-style: sci-fi house %zu triangles, %zu materials\n",a.triangles(),a.parts.size());
    }catch(const std::exception& e){std::fprintf(stderr,"building style tests failed: %s\n",e.what());return 1;}
    std::puts("building-style tests passed");return 0;
}
