#pragma once
// Building styles: the look of a generated building (building-gen) as data. A style file says which
// materials the building uses and how its outside is dressed - wall cladding panels, floor bands with light
// strips, a plinth, corner pillars, window and door frames with glass, a parapet, roof blocks and roof props.
// The code knows only these generic elements; a setting (sci-fi, medieval, wasteland, ...) is a style file,
// so a new setting is new data, not new code. dressBuilding() turns the same definition + seed that
// building-gen uses for collision into a presentation mesh that matches it: every window, the door and the
// walls are exactly where the collision has them, so what the player sees is what they can walk through.
//
// Presentation only (float); the decoration is still deterministic per definition + style + seed (engine
// Random), so every client builds the same mesh. Output: vertices (position, normal, world-metre UV) and one
// index list per used material, ready for a Model or a glTF export (tools/buildings).
#include "BuildingGen.h"
#include "BuildingSurfacePattern.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <string>
#include <vector>
namespace aiverse::lib::systems {
using StyleRgb=std::array<float,3>;
struct StyleTextureGuidSlots {
    std::string baseColor,metallicRoughness,normal,occlusion,emissive;
    bool operator==(const StyleTextureGuidSlots&)const=default;
};
struct StyleMaterial{
    std::string id;StyleRgb color{0.5f,0.5f,0.5f};float metallic=0,roughness=0.8f;
    StyleRgb emissive{0,0,0};float emissiveStrength=1;
    StyleTextureGuidSlots textures;
    std::array<float,2> uvScale{1.f,1.f},uvOffset{0.f,0.f};
    float uvRotation=0.f; // radians, shared by all PBR texture channels
    float normalScale=1.f,occlusionStrength=1.f;
    // Append new fields to preserve positional aggregate initializers across
    // native tools and existing game clients.
    std::string surfacePattern; uint32_t surfaceSeed=0;
    bool operator==(const StyleMaterial&)const=default;
};
struct StyleRange{float lo=0,hi=0;float pick(Random& g)const{return lo+(hi-lo)*float(g.unit().toDouble());}};
struct StyleLight{int material=-1;float height=0.06f,chance=0;};   // an emissive strip; material -1 = none
struct BuildingStyleProp{enum Kind{Box,Mast,Tank,Stack,Dish}kind=Box;int material=-1;uint32_t min=0,max=0;StyleRange x{0.6f,1.2f},y{0.4f,1},z{0.6f,1.2f};StyleLight tip;};
// A whole structure (RTS-style building: one armoured hull, not floors): sizes in metres, fractions of the hull.
// Shared geometric grammar, orthogonal to setting and gameplay role.
enum class StructureMassing : uint8_t { Bastion, Spire, TwinBay, Terraced };
// Large authored shapes, in footprint-relative units. Materials come from the
// style palette; no engine/gameplay function name appears in this geometry API.
struct StructureForm {
    enum class Kind : uint8_t { Pod, Silo, Pylon, Gantry, Arch, Dome, SweptHull };
    Kind kind=Kind::Pod;
    float x=0.5f,z=0.5f,width=0.25f,height=0.25f,depth=0.25f;
    float level=0.8f,taper=0.1f,chance=1.f;
    float stepX=0.f,stepZ=0.f;
    uint32_t repeat=1;
    int material=-1;
};
struct StructureKind{
    std::string id;StructureMassing massing=StructureMassing::Bastion;float width=12,depth=12;StyleRange height{6,8};float taper=0.15f,chamfer=1.6f,roofRise=0.7f,teamChance=0.25f;uint32_t bands=3;
    uint32_t modulesMin=0,modulesMax=0;StyleRange moduleWidth{0.25f,0.4f},moduleDepth{0.4f,0.7f},moduleHeight{0.45f,0.7f};
    float towerChance=0;StyleRange towerWidth{0.25f,0.4f},towerDepth{0.25f,0.4f},towerHeight{2,3.5f};
    float doorWidth=0.4f,doorHeight=0.55f;uint32_t doors=1;bool legs=false;uint32_t thrusters=0;float pad=1.2f;
    std::vector<BuildingStyleProp> props;
    std::vector<StructureForm> forms; // major structure composition, before small props
};
struct BuildingStyle{
    std::string id,name;
    std::vector<StyleMaterial> materials;
    std::vector<std::map<int,StyleRgb>> palettes;   // seeded colour sets: material -> colour (and emissive for lights)
    int shell=-1,interior=-1,roof=-1;               // collision boxes: outer walls, everything inside, the roof slab
    struct Panels{int material=-1,accent=-1;float width=2,height=0,gap=0.05f,accentChance=0;StyleRange depth{0.03f,0.06f};}panels;
    struct Base{int material=-1;float height=0.6f,depth=0.15f;}base;
    struct Band{int material=-1;float height=0.3f,depth=0.12f;StyleLight light;}band;
    struct Corners{int material=-1;float width=0.5f,depth=0.12f,chamfer=0;}corners;   // chamfer > 0: heavy cut-corner blocks
    struct Window{int frame=-1,glass=-1,lit=-1;float width=0.1f,depth=0.1f,litChance=0,hood=0;StyleLight strip;}window;   // hood > 0: armoured lid over each window
    struct Door{int frame=-1;float width=0.15f,depth=0.15f;StyleLight light;int canopy=-1;float canopyDepth=1.2f,canopyHeight=0.2f;}door;
    struct Parapet{int material=-1;float height=0.7f,width=0.3f;StyleLight light;}parapet;
    struct RoofBlocks{int material=-1;uint32_t min=0,max=0;StyleRange width{0.2f,0.4f},depth{0.2f,0.4f},height{1.5f,3};StyleLight light;}roofBlocks;
    using Prop=BuildingStyleProp;
    std::vector<Prop> props;
    struct StructureMaterials{int hull=-1,plate=-1,team=-1,dark=-1,pad=-1,glow=-1,beacon=-1,exhaust=-1,window=-1;}structure;
    std::vector<StructureKind> structures;
    const StructureKind* structureKind(const std::string& id)const{for(const auto& k:structures)if(k.id==id)return &k;return nullptr;}
    struct Crown{int material=-1;float height=0.8f,out=0.6f;StyleLight light;}crown;   // slanted visor round the top, mitred corners
    struct Skirt{int material=-1;float height=2,out=1,plate=2.4f,gap=0.08f;}skirt;   // sloped armour round the ground floor
    struct Buttresses{int material=-1;float width=0.7f,out=1.1f,height=0.5f,chance=1;}buttresses;   // wedges between window columns; height = fraction of the walls
    struct Pipes{int material=-1,bracket=-1;float radius=0.14f,chance=0.3f;}pipes;   // down pipes between window columns, bent over the parapet
    struct Hazard{int material=-1,back=-1;float stripe=0.3f,height=0.4f;}hazard;   // diagonal stripes over the door and on its canopy
    struct Ribs{int material=-1;float width=0.18f,depth=0.25f,chance=0;StyleLight light;}ribs;            // vertical fins between window columns
    struct Sign{int material=-1,frame=-1;float chance=0,width=0.6f,depth=0.45f;StyleRange height{0.4f,0.7f};}sign;   // vertical light sign on a corner
    // Proportions a setting implies (ribbon windows, tall floors); applyBuildingStyle() writes them into a BuildingDef.
    struct Proportions{bool window=false,door=false,floorHeight=false;uint32_t minFloors=1,maxFloors=10;Fixed windowWidth,windowHeight,windowSill,windowSpacing,doorWidth,doorHeight,floor;}proportions;
};
// Writes the style's proportions into the definition; keeps the definition unchanged when the result would not
// fit (the caller's footprint and agent win). Returns whether the style's proportions were applied.
inline bool applyBuildingStyle(BuildingDef& d,const BuildingStyle& s){
    auto t=d;const auto& p=s.proportions;
    if(p.window){t.windowWidth=p.windowWidth;t.windowHeight=p.windowHeight;t.windowSill=p.windowSill;t.windowSpacing=p.windowSpacing;}
    if(p.door){t.doorWidth=p.doorWidth;t.doorHeight=p.doorHeight;}
    t.floors=std::clamp(t.floors,p.minFloors,p.maxFloors);
    if(p.floorHeight)t.floorHeight=p.floor;
    if(!valid(t))return false;d=t;return true;
}
struct StyledVertex{float pos[3],nrm[3],uv[2];};
struct StyledPart{int material=0;std::vector<uint32_t> indices;};
struct StyledMesh{
    std::vector<StyledVertex> vertices;std::vector<StyledPart> parts;std::vector<StyleMaterial> materials;   // materials after the palette
    float boundsMin[3]{0,0,0},boundsMax[3]{0,0,0};
    size_t triangles()const{size_t n=0;for(const auto& p:parts)n+=p.indices.size()/3;return n;}
};
namespace buildingStyleDetail {
struct Mesher{
    std::vector<std::vector<uint32_t>> perMaterial;std::vector<StyledVertex> v;
    explicit Mesher(size_t materials):perMaterial(materials){}
    // Any planar quad (slanted visors, chamfers); `out` picks the side the normal faces. Both sides are emitted
    // when twoSided (thin sheets seen from below).
    void quad(int mat,std::array<std::array<float,3>,4> p,std::array<float,3> out,bool twoSided=false){
        if(mat<0||size_t(mat)>=perMaterial.size())return;
        std::array<float,3> a{p[1][0]-p[0][0],p[1][1]-p[0][1],p[1][2]-p[0][2]},b{p[2][0]-p[0][0],p[2][1]-p[0][1],p[2][2]-p[0][2]};
        std::array<float,3> n{a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};float l=std::sqrt(n[0]*n[0]+n[1]*n[1]+n[2]*n[2]);if(l<1e-8f)return;
        for(auto& c:n)c/=l;
        if(n[0]*out[0]+n[1]*out[1]+n[2]*out[2]<0){std::swap(p[1],p[3]);for(auto& c:n)c=-c;}
        for(int side=0;side<(twoSided?2:1);++side){
            auto base=uint32_t(v.size());
            for(auto& q:p){StyledVertex s{{q[0],q[1],q[2]},{n[0],n[1],n[2]},{0,0}};
                if(std::fabs(n[1])>0.7f){s.uv[0]=q[0];s.uv[1]=q[2];}else if(std::fabs(n[0])>std::fabs(n[2])){s.uv[0]=q[2];s.uv[1]=-q[1];}else{s.uv[0]=q[0];s.uv[1]=-q[1];}
                v.push_back(s);}
            auto& o=perMaterial[size_t(mat)];for(uint32_t i:{0u,1u,2u,0u,2u,3u})o.push_back(base+i);
            std::swap(p[1],p[3]);for(auto& c:n)c=-c;
        }
    }
    // Axis-aligned box in building-local metres. Faces get UVs projected in metres (tiling textures line up).
    using P3=std::array<float,3>;
    // Convex planar polygon (fan); `out` picks the side the normal faces.
    void poly(int mat,std::vector<P3> p,P3 out){
        if(mat<0||size_t(mat)>=perMaterial.size()||p.size()<3)return;
        P3 n{0,0,0};for(size_t i=0;i<p.size();++i){const auto& a=p[i];const auto& b=p[(i+1)%p.size()];n[0]+=(a[1]-b[1])*(a[2]+b[2]);n[1]+=(a[2]-b[2])*(a[0]+b[0]);n[2]+=(a[0]-b[0])*(a[1]+b[1]);}
        float l=std::sqrt(n[0]*n[0]+n[1]*n[1]+n[2]*n[2]);if(l<1e-8f)return;for(auto& c:n)c/=l;
        if(n[0]*out[0]+n[1]*out[1]+n[2]*out[2]<0){std::reverse(p.begin(),p.end());for(auto& c:n)c=-c;}
        auto base=uint32_t(v.size());
        for(const auto& q:p){StyledVertex s{{q[0],q[1],q[2]},{n[0],n[1],n[2]},{0,0}};
            if(std::fabs(n[1])>0.7f){s.uv[0]=q[0];s.uv[1]=q[2];}else if(std::fabs(n[0])>std::fabs(n[2])){s.uv[0]=q[2];s.uv[1]=-q[1];}else{s.uv[0]=q[0];s.uv[1]=-q[1];}
            v.push_back(s);}
        auto& o=perMaterial[size_t(mat)];for(uint32_t i=1;i+1<p.size();++i){o.push_back(base);o.push_back(base+i);o.push_back(base+i+1);}
    }
    // Convex prism between two matching rings (caps included unless open): chamfered blocks, cylinders,
    // sloped skirts with mitred ends. Normals face away from the centre of the solid.
    void prism(int mat,const std::vector<P3>& a,const std::vector<P3>& b,bool caps=true,bool smooth=false){
        if(mat<0||a.size()<3||a.size()!=b.size())return;
        P3 c{0,0,0};for(const auto& q:a)for(int k=0;k<3;++k)c[k]+=q[k];for(const auto& q:b)for(int k=0;k<3;++k)c[k]+=q[k];for(auto& x:c)x/=float(a.size()*2);
        auto away=[&](const std::vector<P3>& pts){P3 m{0,0,0};for(const auto& q:pts)for(int k=0;k<3;++k)m[k]+=q[k]/float(pts.size());return P3{m[0]-c[0],m[1]-c[1],m[2]-c[2]};};
        size_t n=a.size();
        for(size_t i=0;i<n;++i){size_t j=(i+1)%n;
            if(!smooth){poly(mat,{a[i],a[j],b[j],b[i]},away({a[i],a[j],b[j],b[i]}));continue;}
            // Smooth sides (cylinders): per-vertex normals radial from the axis through the ring centres.
            auto ca=away(a),cb=away(b);(void)cb;P3 axis{0,0,0};
            {P3 ma{0,0,0},mb{0,0,0};for(size_t k=0;k<n;++k)for(int d=0;d<3;++d){ma[d]+=a[k][d]/float(n);mb[d]+=b[k][d]/float(n);}
             axis={mb[0]-ma[0],mb[1]-ma[1],mb[2]-ma[2]};float al=std::sqrt(axis[0]*axis[0]+axis[1]*axis[1]+axis[2]*axis[2]);if(al>0)for(auto& x:axis)x/=al;
             auto radial=[&](const P3& q,const P3& centre){P3 r{q[0]-centre[0],q[1]-centre[1],q[2]-centre[2]};float d=r[0]*axis[0]+r[1]*axis[1]+r[2]*axis[2];for(int k=0;k<3;++k)r[k]-=axis[k]*d;
                 float l=std::sqrt(r[0]*r[0]+r[1]*r[1]+r[2]*r[2]);if(l>0)for(auto& x:r)x/=l;return r;};
             if(mat<0||size_t(mat)>=perMaterial.size())return;
             auto base=uint32_t(v.size());
             for(auto [q,centre]:{std::pair{a[i],ma},std::pair{a[j],ma},std::pair{b[j],mb},std::pair{b[i],mb}}){auto r=radial(q,centre);
                 v.push_back({{q[0],q[1],q[2]},{r[0],r[1],r[2]},{0,0}});}
             (void)ca;
             // winding: check against the radial direction of the first corner
             P3 e1{a[j][0]-a[i][0],a[j][1]-a[i][1],a[j][2]-a[i][2]},e2{b[i][0]-a[i][0],b[i][1]-a[i][1],b[i][2]-a[i][2]};
             P3 fn{e1[1]*e2[2]-e1[2]*e2[1],e1[2]*e2[0]-e1[0]*e2[2],e1[0]*e2[1]-e1[1]*e2[0]};auto r0=radial(a[i],ma);
             auto& o=perMaterial[size_t(mat)];
             if(fn[0]*r0[0]+fn[1]*r0[1]+fn[2]*r0[2]>=0){for(uint32_t k:{0u,2u,1u,0u,3u,2u})o.push_back(base+k);}else{for(uint32_t k:{0u,1u,2u,0u,2u,3u})o.push_back(base+k);}
            }
        }
        if(caps){poly(mat,a,away(a));poly(mat,b,away(b));}
    }
    // Cylinder along an axis (0 = x, 1 = y, 2 = z) from `from` for `len` metres.
    void cylinder(int mat,P3 from,int axis,float radius,float len,int sides=10,bool caps=true){
        std::vector<P3> a,b;
        for(int i=0;i<sides;++i){float t=6.2831853f*(float(i)+0.5f)/float(sides),c=std::cos(t)*radius,s=std::sin(t)*radius;P3 q=from;
            if(axis==1){q[0]+=c;q[2]+=s;}else if(axis==0){q[1]+=c;q[2]+=s;}else{q[0]+=c;q[1]+=s;}
            a.push_back(q);q[size_t(axis)]+=len;b.push_back(q);}
        prism(mat,a,b,caps,true);
    }
    // skip: bit mask of faces not to emit (+X 1, -X 2, +Y 4, -Y 8, +Z 16, -Z 32), e.g. a face flush with a wall.
    void box(int mat,float x0,float y0,float z0,float x1,float y1,float z1,unsigned skip=0){
        if(mat<0||size_t(mat)>=perMaterial.size()||x1-x0<1e-4f||y1-y0<1e-4f||z1-z0<1e-4f)return;
        auto quad=[&](std::array<std::array<float,3>,4> p,std::array<float,3> n){
            auto base=uint32_t(v.size());
            for(auto& q:p){StyledVertex s{{q[0],q[1],q[2]},{n[0],n[1],n[2]},{0,0}};
                if(std::fabs(n[0])>0.5f){s.uv[0]=q[2]*n[0];s.uv[1]=-q[1];}else if(std::fabs(n[1])>0.5f){s.uv[0]=q[0];s.uv[1]=q[2]*n[1];}else{s.uv[0]=-q[0]*n[2];s.uv[1]=-q[1];}
                v.push_back(s);}
            auto& o=perMaterial[size_t(mat)];for(uint32_t i:{0u,1u,2u,0u,2u,3u})o.push_back(base+i);};
        if(!(skip&1))quad({{{x1,y0,z0},{x1,y1,z0},{x1,y1,z1},{x1,y0,z1}}},{1,0,0});
        if(!(skip&2))quad({{{x0,y0,z1},{x0,y1,z1},{x0,y1,z0},{x0,y0,z0}}},{-1,0,0});
        if(!(skip&4))quad({{{x0,y1,z0},{x0,y1,z1},{x1,y1,z1},{x1,y1,z0}}},{0,1,0});
        if(!(skip&8))quad({{{x0,y0,z1},{x0,y0,z0},{x1,y0,z0},{x1,y0,z1}}},{0,-1,0});
        if(!(skip&16))quad({{{x1,y0,z1},{x1,y1,z1},{x0,y1,z1},{x0,y0,z1}}},{0,0,1});
        if(!(skip&32))quad({{{x0,y0,z0},{x0,y1,z0},{x1,y1,z0},{x1,y0,z0}}},{0,0,-1});
    }
};
// Outer wall faces: side 0 = -Z (z = 0), 1 = +X (x = W), 2 = +Z (z = D), 3 = -X (x = 0) as in building-gen.
// u runs along the wall in building coordinates (x for sides 0/2, z for 1/3), v is height, n is the distance
// outwards from the face (negative = into the wall).
struct Face{
    uint32_t side;float x0,z0,x1,z1;
    float len()const{return side%2==0?x1-x0:z1-z0;}
    std::array<float,3> at(float u,float v,float n)const{
        switch(side){case 0:return {x0+u,v,z0-n};case 2:return {x0+u,v,z1+n};case 1:return {x1+n,v,z0+u};default:return {x0-n,v,z0+u};}
    }
    std::array<float,3> outward()const{switch(side){case 0:return {0,0,-1};case 2:return {0,0,1};case 1:return {1,0,0};default:return {-1,0,0};}}
    void box(Mesher& m,int mat,float u0,float v0,float n0,float u1,float v1,float n1)const{
        // A box flush with the wall (n0 = 0) has its back face hidden against the wall: not emitted.
        bool flush=n0==0;
        switch(side){
            case 0:m.box(mat,x0+u0,v0,z0-n1,x0+u1,v1,z0-n0,flush?16u:0u);break;
            case 2:m.box(mat,x0+u0,v0,z1+n0,x0+u1,v1,z1+n1,flush?32u:0u);break;
            case 1:m.box(mat,x1+n0,v0,z0+u0,x1+n1,v1,z0+u1,flush?2u:0u);break;
            default:m.box(mat,x0-n1,v0,z0+u0,x0-n0,v1,z0+u1,flush?1u:0u);break;
        }
    }
};
inline float f(Fixed x){return float(x.toDouble());}
}
// Places the mesh like generateBuilding places the boxes: origin = minimum corner after yaw (quarter turns).
inline StyledMesh dressBuilding(const BuildingDef& d,const BuildingStyle& st,uint64_t seed,Vec3 origin={},uint32_t yaw=0){
    using namespace buildingStyleDetail;
    auto side2=[](uint32_t side){return side%2==0;};
    StyledMesh out;auto local=generateBuilding(d,seed,{},0);if(!local||yaw>3)return out;
    Random g(seed,0x5717e);
    out.materials=st.materials;
    if(!st.palettes.empty()){const auto& p=st.palettes[g.below(uint32_t(st.palettes.size()))];
        for(const auto& [m,c]:p){if(m<0||size_t(m)>=out.materials.size())continue;auto& mat=out.materials[size_t(m)];
            if(mat.emissive[0]+mat.emissive[1]+mat.emissive[2]>0)mat.emissive=c;else mat.color=c;}}
    Mesher m(out.materials.size());
    const float W=f(d.width),D=f(d.depth),H=f(d.floorHeight),t=f(d.wall),slab=f(d.slab);const uint32_t floors=d.floors;
    const float top=H*float(floors)+(d.roof?slab:0.f);
    // 1. The collision itself: outer walls, inside (slabs, stairs, partition) and the roof slab.
    for(const auto& b:local->boxes){
        float x0=f(b.min.x),y0=f(b.min.y),z0=f(b.min.z),x1=f(b.max.x),y1=f(b.max.y),z1=f(b.max.z);
        bool roofSlab=d.roof&&y0>=H*float(floors)-1e-3f;
        bool outer=!roofSlab&&y1-y0>slab+1e-3f&&(x1<=t+1e-3f||x0>=W-t-1e-3f||z1<=t+1e-3f||z0>=D-t-1e-3f);
        m.box(roofSlab?st.roof:outer?st.shell:st.interior,x0,y0,z0,x1,y1,z1);
    }
    auto panelFill=[&](const Face& fc,float u0,float u1,float a,float b){
        if(st.panels.material<0||u1-u0<0.1f||b-a<0.1f)return;
        int nu=std::max(1,int(std::lround((u1-u0)/std::max(0.2f,st.panels.width))));
        int nv=st.panels.height>0?std::max(1,int(std::lround((b-a)/st.panels.height))):1;
        float pu=(u1-u0)/float(nu),pv=(b-a)/float(nv),gap=st.panels.gap/2;
        for(int i=0;i<nu;++i)for(int j=0;j<nv;++j){
            int mat=st.panels.accent>=0&&float(g.unit().toDouble())<st.panels.accentChance?st.panels.accent:st.panels.material;
            fc.box(m,mat,u0+pu*float(i)+gap,a+pv*float(j)+gap,0,u0+pu*float(i+1)-gap,a+pv*float(j+1)-gap,st.panels.depth.pick(g));
        }
    };
    auto light=[&](const Face& fc,const StyleLight& l,float u0,float u1,float v,float n){if(l.material<0||float(g.unit().toDouble())>=l.chance)return;fc.box(m,l.material,u0,v,n,u1,v+l.height,n+0.02f);};
    const float cw=st.corners.material>=0?st.corners.width:0.f;
    auto alongOf=[&](const BuildingLayout::Opening& o,bool hi){return side2(o.side)?f(hi?o.hole.max.x:o.hole.min.x):f(hi?o.hole.max.z:o.hole.min.z);};
    // Door span on a side (u0, u1) padded by its jambs; empty when the door is elsewhere.
    auto doorSpan=[&](uint32_t side,float pad){std::pair<float,float> r{1e9f,-1e9f};
        for(const auto& o:local->openings)if(o.door&&o.side==side)r={alongOf(o,false)-pad,alongOf(o,true)+pad};return r;};
    // Places between the window columns of a side (top floor columns; the same on every floor), clear by `room`.
    auto gapCentres=[&](uint32_t side,float room){
        std::vector<std::pair<float,float>> cols;float wf=st.window.frame>=0?st.window.width:0.f;
        for(const auto& o:local->openings)if(o.side==side&&o.floor==floors-1&&!o.door)cols.push_back({alongOf(o,false)-wf,alongOf(o,true)+wf});
        std::sort(cols.begin(),cols.end());
        float len=side%2==0?W:D,cursor=cw+st.corners.chamfer;std::vector<float> out;
        cols.push_back({len-cw-st.corners.chamfer,len});
        for(const auto& c:cols){if(c.first-cursor>=room+0.2f)out.push_back((cursor+c.first)/2);cursor=c.second;}
        return out;};
    // Ground-floor windows bound the skirt: it never covers a pane.
    float lowWindow=1e9f;for(const auto& o:local->openings)if(!o.door&&o.floor==0)lowWindow=std::min(lowWindow,f(o.hole.min.y)-(st.window.frame>=0?st.window.width:0.f)-0.05f);
    const float skirtH=st.skirt.material>=0?std::min(st.skirt.height,lowWindow):0.f;
    if(cw>0&&st.corners.chamfer>0){
        // Heavy corner blocks with the outer edge cut at 45 degrees, and a narrower cap; (a, b) = outward along x / z.
        float dpt=st.corners.depth,c=st.corners.chamfer,h=top+(d.roof?st.parapet.height:0.f)+0.25f;
        for(auto [cx,cz,sx,sz]:std::initializer_list<std::array<float,4>>{{0,0,-1,-1},{W,0,1,-1},{W,D,1,1},{0,D,-1,1}}){
            // How far the block may reach along each wall: never over the door jambs.
            auto reach=[&](uint32_t side,bool atStart){auto dsp=doorSpan(side,st.door.width);float len=side%2==0?W:D;
                float room=atStart?dsp.first:len-dsp.second;return dsp.first>dsp.second?cw:std::clamp(room-0.05f,0.f,cw);};
            float inX=reach(cz==0?0u:2u,cx==0),inZ=reach(cx==0?3u:1u,cz==0);   // along the x wall / the z wall
            auto ring=[&](float k,float y){std::vector<std::array<float,3>> r;
                for(auto [a,b]:std::initializer_list<std::pair<float,float>>{{-inX+k,-inZ+k},{dpt-k,-inZ+k},{dpt-k,dpt-c},{dpt-c,dpt-k},{-inX+k,dpt-k}})r.push_back({cx+sx*a,y,cz+sz*b});return r;};
            m.prism(st.corners.material,ring(0,0),ring(0,h));m.prism(st.corners.material,ring(0.2f,h),ring(0.2f,h+0.35f));
        }
    }
    for(uint32_t side=0;side<4;++side){
        Face fc{side,0,0,W,D};const float len=fc.len();
        // 2. Plinth, floor bands (with optional light strips) and the parapet run around the whole building.
        if(st.base.material>=0){   // the plinth stops at the door jambs
            float gapFrom=1e9f,gapTo=-1e9f;
            for(const auto& o:local->openings)if(o.door&&o.side==side){gapFrom=(side%2==0?f(o.hole.min.x):f(o.hole.min.z))-st.door.width;gapTo=(side%2==0?f(o.hole.max.x):f(o.hole.max.z))+st.door.width;}
            if(gapFrom>gapTo)fc.box(m,st.base.material,-st.base.depth,0,0,len+st.base.depth,st.base.height,st.base.depth);
            else{fc.box(m,st.base.material,-st.base.depth,0,0,gapFrom,st.base.height,st.base.depth);fc.box(m,st.base.material,gapTo,0,0,len+st.base.depth,st.base.height,st.base.depth);}
        }
        for(uint32_t fl=1;fl<floors;++fl)if(st.band.material>=0){
            float y=H*float(fl);fc.box(m,st.band.material,-st.band.depth,y,0,len+st.band.depth,y+st.band.height,st.band.depth);
            light(fc,st.band.light,cw,len-cw,y+st.band.height*0.4f,st.band.depth);
        }
        if(d.roof&&st.parapet.material>=0){
            fc.box(m,st.parapet.material,-st.band.depth,H*float(floors),-st.parapet.width,len+st.band.depth,top+st.parapet.height,std::max(st.band.depth,0.02f));
            light(fc,st.parapet.light,cw,len-cw,top+st.parapet.height*0.45f,std::max(st.band.depth,0.02f));
        }
        if(d.roof&&st.crown.material>=0){
            // The visor leans out from the parapet top: bottom edge on the wall line, top edge `out` further out;
            // each end runs out as far as the visor does, so neighbouring sides meet in a 45-degree mitre.
            float n0=std::max(st.band.depth,0.02f)+0.01f,n1=n0+st.crown.out,y0=top+st.parapet.height-st.crown.height*0.35f,y1=y0+st.crown.height;
            m.quad(st.crown.material,{fc.at(-n0,y0,n0),fc.at(len+n0,y0,n0),fc.at(len+n1,y1,n1),fc.at(-n1,y1,n1)},fc.outward(),true);
            if(st.crown.light.material>=0&&float(g.unit().toDouble())<st.crown.light.chance){float k=0.45f,lh=st.crown.light.height;
                float a=n0+(n1-n0)*k,b=n0+(n1-n0)*(k+lh/st.crown.height);float ya=y0+st.crown.height*k,yb=ya+lh;
                m.quad(st.crown.light.material,{fc.at(-a,ya,a+0.012f),fc.at(len+a,ya,a+0.012f),fc.at(len+b,yb,b+0.012f),fc.at(-b,yb,b+0.012f)},fc.outward());}
        }
        if(cw>0&&st.corners.chamfer<=0)fc.box(m,st.corners.material,-st.corners.depth,0,-0.001f,cw,top+(d.roof?st.parapet.height:0.f)+0.05f,st.corners.depth);
        // Sloped armour skirt: plates of `plate` metres leaning on the ground floor, mitred at the corners, cut at the door.
        if(skirtH>0.3f){
            const auto& sk=st.skirt;auto door=doorSpan(side,st.door.width);
            std::vector<std::pair<float,float>> prof{{0,0},{sk.out,0},{sk.out,0.22f},{0,skirtH}};   // (n, v)
            int plates=std::max(1,int(std::lround((len+sk.out*2)/std::max(0.5f,sk.plate))));float step=(len)/float(plates);
            for(int i=0;i<plates;++i){
                float u0=step*float(i)+(i?sk.gap/2:0.f),u1=step*float(i+1)-(i+1<plates?sk.gap/2:0.f);
                if(u1>door.first&&u0<door.second){   // split around the door
                    if(door.first-u0>0.3f){float a=u0,b=door.first;std::vector<std::array<float,3>> ra,rb;
                        for(auto [n,v]:prof){ra.push_back(fc.at(i==0?-n:a,v,n));rb.push_back(fc.at(b,v,n));}m.prism(sk.material,ra,rb);}
                    if(u1-door.second>0.3f){float a=door.second,b=u1;std::vector<std::array<float,3>> ra,rb;
                        for(auto [n,v]:prof){ra.push_back(fc.at(a,v,n));rb.push_back(fc.at(i+1==plates?len+n:b,v,n));}m.prism(sk.material,ra,rb);}
                    continue;
                }
                std::vector<std::array<float,3>> ra,rb;
                for(auto [n,v]:prof){ra.push_back(fc.at(i==0?-n:u0,v,n));rb.push_back(fc.at(i+1==plates?len+n:u1,v,n));}
                m.prism(sk.material,ra,rb);
            }
        }
        // 3. Cladding panels on every floor between the bands, cut around the openings of that floor.
        for(uint32_t fl=0;fl<floors;++fl){
            float y=H*float(fl);float v0=y+(fl==0?std::max({st.base.height,slab,skirtH*0.8f}):std::min(st.band.height,H/3)),v1=y+H;
            if(fl+1==floors&&!d.roof)v1=top;
            std::vector<std::array<float,4>> holes;   // u0, u1, v0, v1 of this side's openings on this floor
            for(const auto& o:local->openings)if(o.side==side&&o.floor==fl){
                float a=side%2==0?f(o.hole.min.x):f(o.hole.min.z),b=side%2==0?f(o.hole.max.x):f(o.hole.max.z);holes.push_back({a,b,f(o.hole.min.y),f(o.hole.max.y)});}
            std::sort(holes.begin(),holes.end());
            auto fill=[&](float u0,float u1,float a,float b){panelFill(fc,u0,u1,a,b);};
            float cursor=cw;const float end=len-cw;
            for(const auto& h:holes){
                fill(cursor,h[0],v0,v1);fill(h[0],h[1],v0,std::max(v0,h[2]));fill(h[0],h[1],std::min(v1,h[3]),v1);cursor=h[1];
            }
            fill(cursor,end,v0,v1);
        }
        // 4. Window frames, glass (some panes lit), light strips under the sill; the door frame, light and canopy.
        for(const auto& o:local->openings){
            if(o.side!=side)continue;
            float u0=side%2==0?f(o.hole.min.x):f(o.hole.min.z),u1=side%2==0?f(o.hole.max.x):f(o.hole.max.z),v0=f(o.hole.min.y),v1=f(o.hole.max.y);
            if(o.door){
                const auto& dr=st.door;float w=dr.width;
                fc.box(m,dr.frame,u0-w,0,0,u0,v1+w,dr.depth);fc.box(m,dr.frame,u1,0,0,u1+w,v1+w,dr.depth);fc.box(m,dr.frame,u0-w,v1,0,u1+w,v1+w,dr.depth);
                if(dr.light.material>=0)fc.box(m,dr.light.material,u0,v1+w,0,u1,v1+w+dr.light.height,dr.depth*0.6f);
                if(dr.canopy>=0)fc.box(m,dr.canopy,u0-w*2,v1+w+dr.light.height,0,u1+w*2,v1+w+dr.light.height+dr.canopyHeight,dr.canopyDepth);
                if(st.hazard.material>=0){
                    // Stripes on the jambs' faces and on the canopy front.
                    auto stripes=[&](float a,float b,float y0,float y1,float n,bool upright){
                        const auto& hz=st.hazard;fc.box(m,hz.back,a,y0,n-0.02f,b,y1,n);
                        float sw=hz.stripe;
                        if(!upright){float hh=y1-y0;for(float x=a+0.05f;x+sw+hh<=b-0.05f;x+=sw*2)
                            m.quad(hz.material,{fc.at(x,y0+0.02f,n+0.005f),fc.at(x+sw,y0+0.02f,n+0.005f),fc.at(x+sw+hh*0.9f,y1-0.02f,n+0.005f),fc.at(x+hh*0.9f,y1-0.02f,n+0.005f)},fc.outward());}
                        else{float ww=b-a;for(float y=y0+0.05f;y+sw+ww<=y1-0.05f;y+=sw*2)
                            m.quad(hz.material,{fc.at(a+0.02f,y,n+0.005f),fc.at(b-0.02f,y+ww*0.9f,n+0.005f),fc.at(b-0.02f,y+ww*0.9f+sw,n+0.005f),fc.at(a+0.02f,y+sw,n+0.005f)},fc.outward());}
                    };
                    stripes(u0-w,u0,0.05f,v1,dr.depth+0.02f,true);stripes(u1,u1+w,0.05f,v1,dr.depth+0.02f,true);
                    if(dr.canopy>=0){float cy=v1+w+dr.light.height;stripes(u0-w*2,u1+w*2,cy,cy+dr.canopyHeight,dr.canopyDepth+0.02f,false);}
                }
                continue;
            }
            const auto& wn=st.window;float w=wn.width;
            fc.box(m,wn.frame,u0-w,v0-w,0,u0,v1+w,wn.depth);fc.box(m,wn.frame,u1,v0-w,0,u1+w,v1+w,wn.depth);
            fc.box(m,wn.frame,u0,v1,0,u1,v1+w,wn.depth);fc.box(m,wn.frame,u0,v0-w,0,u1,v0,wn.depth*1.4f);
            int glass=wn.lit>=0&&float(g.unit().toDouble())<wn.litChance?wn.lit:wn.glass;
            fc.box(m,glass,u0,v0,-t*0.55f,u1,v1,-t*0.45f);
            light(fc,wn.strip,u0,u1,v0-w-wn.strip.height-0.04f,wn.depth*0.5f);
            if(wn.hood>0){   // a sloped lid: thick at the wall, thin at the front
                std::vector<std::array<float,3>> ra,rb;
                for(auto [n,v]:std::initializer_list<std::pair<float,float>>{{0,v1+w},{wn.hood,v1+w},{wn.hood,v1+w+0.08f},{0,v1+w+0.35f}}){ra.push_back(fc.at(u0-w-0.1f,v,n));rb.push_back(fc.at(u1+w+0.1f,v,n));}
                m.prism(wn.frame,ra,rb);}
        }
    }
    // 5a. Buttresses and down pipes share the gaps between window columns (never in front of the door).
    for(uint32_t side=0;side<4&&(st.buttresses.material>=0||st.pipes.material>=0);++side){
        Face fc{side,0,0,W,D};auto door=doorSpan(side,st.door.width+0.5f);
        float room=std::max(st.buttresses.material>=0?st.buttresses.width:0.f,st.pipes.radius*2+0.2f);
        for(float c:gapCentres(side,room)){
            if(c+room/2>door.first&&c-room/2<door.second)continue;
            float r=float(g.unit().toDouble());
            if(st.buttresses.material>=0&&r<st.buttresses.chance){
                const auto& b=st.buttresses;float h=std::max(skirtH+0.5f,top*b.height),hw=b.width/2;
                std::vector<std::array<float,3>> ra,rb;
                for(auto [n,v]:std::initializer_list<std::pair<float,float>>{{0,0},{b.out,0},{b.out,std::min(1.0f,h*0.25f)},{0.18f,h},{0,h}}){ra.push_back(fc.at(c-hw,v,n));rb.push_back(fc.at(c+hw,v,n));}
                m.prism(b.material,ra,rb);
            }else if(st.pipes.material>=0&&r<st.buttresses.chance+st.pipes.chance){
                const auto& pp=st.pipes;float n=pp.radius+0.06f,y0=std::max(skirtH,st.base.height),y1=top+(d.roof?st.parapet.height:0.f)+pp.radius;
                auto at=fc.at(c,y0,n);m.cylinder(pp.material,at,1,pp.radius,y1-y0,8);
                // Over the parapet: a short run back onto the roof.
                {auto A=fc.at(c,y1,n),B=fc.at(c,y1,-(st.parapet.width+0.6f));int axis=side%2==0?2:0;
                 auto start=A[size_t(axis)]<B[size_t(axis)]?A:B;m.cylinder(pp.material,start,axis,pp.radius,std::fabs(A[size_t(axis)]-B[size_t(axis)]),8);}
                for(float y=y0+1.2f;y<y1-0.5f;y+=2.4f)fc.box(m,pp.bracket,c-pp.radius-0.05f,y,0,c+pp.radius+0.05f,y+0.12f,n+pp.radius+0.04f);
            }
        }
    }
    // 5. Ribs: vertical fins over every floor between the window columns of a side (the same columns on all floors),
    //    and an optional vertical light sign on one corner.
    const float ribTop=top+(d.roof?st.parapet.height:0.f);
    if(st.ribs.material>=0)for(uint32_t side=0;side<4;++side){
        Face fc{side,0,0,W,D};std::vector<std::pair<float,float>> cols;
        for(const auto& o:local->openings)if(o.side==side&&o.floor==floors-1&&!o.door)cols.push_back({side%2==0?f(o.hole.min.x):f(o.hole.min.z),side%2==0?f(o.hole.max.x):f(o.hole.max.z)});
        std::sort(cols.begin(),cols.end());
        for(size_t i=0;i+1<cols.size();++i){
            if(float(g.unit().toDouble())>=st.ribs.chance)continue;
            float c=(cols[i].second+cols[i+1].first)/2,hw=st.ribs.width/2;float wf=st.window.frame>=0?st.window.width:0.f;
            if(cols[i+1].first-cols[i].second<st.ribs.width+wf*2+0.1f)continue;
            float y0=st.base.height;   // a fin never covers the door: it starts above it
            for(const auto& o:local->openings)if(o.door&&o.side==side){float a=side%2==0?f(o.hole.min.x):f(o.hole.min.z),b=side%2==0?f(o.hole.max.x):f(o.hole.max.z);
                float pad=st.door.width*2+0.4f;if(c+hw>a-pad&&c-hw<b+pad)y0=f(o.hole.max.y)+st.door.width+st.door.light.height+st.door.canopyHeight+0.3f;}
            fc.box(m,st.ribs.material,c-hw,y0,0,c+hw,ribTop,st.ribs.depth);
            if(st.ribs.light.material>=0&&float(g.unit().toDouble())<st.ribs.light.chance)
                fc.box(m,st.ribs.light.material,c-hw*0.35f,y0+0.3f,st.ribs.depth,c+hw*0.35f,ribTop-0.3f,st.ribs.depth+0.02f);
        }
    }
    if(st.sign.material>=0&&float(g.unit().toDouble())<st.sign.chance&&floors>=2){
        uint32_t side=g.below(4);Face fc{side,0,0,W,D};bool atEnd=g.below(2)!=0;
        float h=top*st.sign.height.pick(g),y1=top-0.3f,y0=std::max(st.base.height+2.5f,y1-h);
        float u=atEnd?fc.len()-cw-0.6f-st.sign.width:cw+0.6f;
        if(y1-y0>1.5f){
            fc.box(m,st.sign.frame,u-0.06f,y0-0.06f,0.05f,u+st.sign.width+0.06f,y1+0.06f,st.sign.depth);
            fc.box(m,st.sign.material,u,y0,st.sign.depth,u+st.sign.width,y1,st.sign.depth+0.04f);
            // Brackets to the wall above and below.
            fc.box(m,st.sign.frame,u+st.sign.width*0.3f,y1+0.06f,0,u+st.sign.width*0.7f,y1+0.2f,st.sign.depth);
            fc.box(m,st.sign.frame,u+st.sign.width*0.3f,y0-0.2f,0,u+st.sign.width*0.7f,y0-0.06f,st.sign.depth);
        }
    }
    // 6. Roof: blocks (machine rooms, penthouses) and props (units, vents, masts) inside the parapet.
    if(d.roof){
        float inset=std::max(st.parapet.width,t)+0.3f;float ax=inset,az=inset,bx=W-inset,bz=D-inset;
        std::vector<std::array<float,4>> taken;   // x0, z0, x1, z1 on the roof
        auto freeAt=[&](float x0,float z0,float x1,float z1){for(const auto& r:taken)if(x0<r[2]+0.2f&&r[0]<x1+0.2f&&z0<r[3]+0.2f&&r[1]<z1+0.2f)return false;return true;};
        auto place=[&](float sx,float sz,std::array<float,4>& at){
            if(sx>bx-ax||sz>bz-az)return false;
            for(int tries=0;tries<12;++tries){float x=ax+(bx-ax-sx)*float(g.unit().toDouble()),z=az+(bz-az-sz)*float(g.unit().toDouble());
                if(freeAt(x,z,x+sx,z+sz)){at={x,z,x+sx,z+sz};taken.push_back(at);return true;}}
            return false;};
        const auto& rb=st.roofBlocks;
        uint32_t blocks=rb.material<0?0:rb.min+(rb.max>rb.min?g.below(rb.max-rb.min+1):0);
        std::vector<std::array<float,5>> blockTops;   // x0, z0, x1, z1, height
        for(uint32_t i=0;i<blocks;++i){
            std::array<float,4> at{};float sx=rb.width.pick(g)*W,sz=rb.depth.pick(g)*D,h=rb.height.pick(g);
            if(!place(sx,sz,at))continue;
            m.box(rb.material,at[0],top,at[1],at[2],top+h,at[3]);
            for(uint32_t side=0;side<4;++side){Face bf{side,at[0],at[1],at[2],at[3]};panelFill(bf,0.08f,bf.len()-0.08f,top+0.1f,top+h-0.25f);}
            if(st.parapet.material>=0)m.box(st.parapet.material,at[0]-0.08f,top+h,at[1]-0.08f,at[2]+0.08f,top+h+0.15f,at[3]+0.08f);
            if(rb.light.material>=0&&float(g.unit().toDouble())<rb.light.chance){float ly=top+h*0.7f;
                m.box(rb.light.material,at[0]-0.02f,ly,at[1]-0.02f,at[2]+0.02f,ly+rb.light.height,at[3]+0.02f);}
            blockTops.push_back({at[0],at[1],at[2],at[3],top+h+0.15f});
        }
        for(const auto& p:st.props){
            if(p.material<0){continue;}uint32_t n=p.min+(p.max>p.min?g.below(p.max-p.min+1):0);
            for(uint32_t i=0;i<n;++i){
                float sx=p.x.pick(g),sy=p.y.pick(g),sz=p.z.pick(g);
                if(p.kind==BuildingStyle::Prop::Mast){
                    // Masts stand on a block when there is one, otherwise on the roof.
                    float x,z,y0;
                    if(!blockTops.empty()){const auto& b=blockTops[g.below(uint32_t(blockTops.size()))];x=b[0]+(b[2]-b[0])*float(g.unit().toDouble());z=b[1]+(b[3]-b[1])*float(g.unit().toDouble());y0=b[4];}
                    else{std::array<float,4> at{};if(!place(sx,sz,at))continue;x=(at[0]+at[2])/2;z=(at[1]+at[3])/2;y0=top;}
                    m.box(p.material,x-sx/2,y0,z-sz/2,x+sx/2,y0+sy,z+sz/2);
                    if(p.tip.material>=0&&float(g.unit().toDouble())<p.tip.chance){float r=std::max(sx,sz)*1.6f;m.box(p.tip.material,x-r/2,y0+sy,z-r/2,x+r/2,y0+sy+p.tip.height,z+r/2);}
                    continue;
                }
                if(p.kind==BuildingStyle::Prop::Tank||p.kind==BuildingStyle::Prop::Stack||p.kind==BuildingStyle::Prop::Dish){
                    // Round props: size x = diameter, y = height.
                    std::array<float,4> at{};float dia=sx;if(!place(dia,dia,at))continue;float cx=(at[0]+at[2])/2,cz=(at[1]+at[3])/2,r=dia/2;
                    if(p.kind==BuildingStyle::Prop::Tank){
                        m.box(p.material,cx-r*0.8f,top,cz-r*0.8f,cx+r*0.8f,top+0.25f,cz+r*0.8f);   // cradle
                        m.cylinder(p.material,{cx,top+0.25f,cz},1,r,sy,12);
                        m.cylinder(p.material,{cx,top+0.25f+sy*0.3f,cz},1,r*1.06f,0.12f,12);m.cylinder(p.material,{cx,top+0.25f+sy*0.75f,cz},1,r*1.06f,0.12f,12);
                        m.cylinder(p.material,{cx,top+0.25f+sy,cz},1,r*0.55f,0.2f,10);
                        if(p.tip.material>=0&&float(g.unit().toDouble())<p.tip.chance)m.cylinder(p.tip.material,{cx,top+0.25f+sy*0.5f,cz},1,r*1.03f,p.tip.height,12,false);
                    }else if(p.kind==BuildingStyle::Prop::Stack){
                        m.box(p.material,cx-r*1.4f,top,cz-r*1.4f,cx+r*1.4f,top+0.5f,cz+r*1.4f);
                        m.cylinder(p.material,{cx,top+0.5f,cz},1,r,sy,10);m.cylinder(p.material,{cx,top+0.5f+sy-0.3f,cz},1,r*1.15f,0.3f,10);
                        if(p.tip.material>=0&&float(g.unit().toDouble())<p.tip.chance)m.cylinder(p.tip.material,{cx,top+0.5f+sy,cz},1,r*0.85f,p.tip.height,10,true);
                    }else{
                        // Radar dish: a mast, a yoke and an upturned shallow bowl.
                        float ym=top+sy;m.box(p.material,cx-0.12f,top,cz-0.12f,cx+0.12f,ym,cz+0.12f);m.box(p.material,cx-0.5f,top,cz-0.5f,cx+0.5f,top+0.3f,cz+0.5f);
                        std::vector<std::array<float,3>> lo,hi;int k=14;
                        for(int i=0;i<k;++i){float a=6.2831853f*float(i)/float(k);lo.push_back({cx+std::cos(a)*r*0.25f,ym,cz+std::sin(a)*r*0.25f});hi.push_back({cx+std::cos(a)*r,ym+r*0.35f,cz+std::sin(a)*r});}
                        m.prism(p.material,lo,hi);
                        m.box(p.material,cx-0.05f,ym,cz-0.05f,cx+0.05f,ym+r*0.9f,cz+0.05f);
                        if(p.tip.material>=0&&float(g.unit().toDouble())<p.tip.chance)m.box(p.tip.material,cx-0.09f,ym+r*0.9f,cz-0.09f,cx+0.09f,ym+r*0.9f+p.tip.height,cz+0.09f);
                    }
                    continue;
                }
                std::array<float,4> at{};if(!place(sx,sz,at))continue;
                m.box(p.material,at[0],top,at[1],at[2],top+sy,at[3]);
                if(p.tip.material>=0&&float(g.unit().toDouble())<p.tip.chance)m.box(p.tip.material,at[0]+sx*0.15f,top+sy,at[1]+sz*0.15f,at[2]-sx*0.15f,top+sy+p.tip.height,at[3]-sz*0.15f);
            }
        }
    }
    // Into world space: quarter-turn yaw about the footprint like building-gen's turn(), then the origin.
    auto ox=f(origin.x),oy=f(origin.y),oz=f(origin.z);bool first=true;
    for(auto& s:m.v){
        float x=s.pos[0],z=s.pos[2],nx=s.nrm[0],nz=s.nrm[2];
        switch(yaw){case 1:s.pos[0]=D-z;s.pos[2]=x;s.nrm[0]=-nz;s.nrm[2]=nx;break;case 2:s.pos[0]=W-x;s.pos[2]=D-z;s.nrm[0]=-nx;s.nrm[2]=-nz;break;
            case 3:s.pos[0]=z;s.pos[2]=W-x;s.nrm[0]=nz;s.nrm[2]=-nx;break;default:break;}
        s.pos[0]+=ox;s.pos[1]+=oy;s.pos[2]+=oz;
        for(int k=0;k<3;++k){if(first||s.pos[k]<out.boundsMin[k])out.boundsMin[k]=s.pos[k];if(first||s.pos[k]>out.boundsMax[k])out.boundsMax[k]=s.pos[k];}
        first=false;
    }
    out.vertices=std::move(m.v);
    for(size_t i=0;i<m.perMaterial.size();++i)if(!m.perMaterial[i].empty())out.parts.push_back({int(i),std::move(m.perMaterial[i])});
    return out;
}
// Style file (engine/assets/building-styles/<id>.json): metres and linear colours. Every element is optional;
// elements name materials by id. Errors: JsonError("<path>.<key>: ...").
inline BuildingStyle readBuildingStyle(const Json& j,const std::string& path="style"){
    if(!j.isObject())throw JsonError(path+": expected object");
    auto allow=[](const Json& o,const std::string& p,std::initializer_list<const char*> keys){
        if(!o.isObject())throw JsonError(p+": expected object");
        for(const auto& [k,v]:o.members()){(void)v;if(std::none_of(keys.begin(),keys.end(),[&](const char* n){return k==n;}))throw JsonError(p+": unknown key '"+k+"'");}};
    allow(j,path,{"id","name","materials","palettes","shell","interior","roof","panels","base","band","corners","window","door","parapet","roofBlocks","props","ribs","sign","proportions","crown","skirt","buttresses","pipes","hazard","structure","structures"});
    BuildingStyle s;s.id=j["id"].str();s.name=j["name"].str(s.id);
    if(s.id.empty())throw JsonError(path+".id: expected string");
    auto num=[](const Json& v,const std::string& p){if(!v.isNumber())throw JsonError(p+": expected number");return float(v.fx().toDouble());};
    auto rgb=[&](const Json& v,const std::string& p){if(!v.isArray()||v.size()!=3)throw JsonError(p+": expected [r, g, b]");StyleRgb c{};for(size_t i=0;i<3;++i)c[i]=num(v[i],p+"["+std::to_string(i)+"]");return c;};
    if(!j["materials"].isObject()||j["materials"].members().empty())throw JsonError(path+".materials: expected object of materials");
    std::map<std::string,int> ids;
    for(const auto& [k,v]:j["materials"].members()){
        auto p=path+".materials."+k;allow(v,p,{"color","metallic","roughness","emissive","emissiveStrength","textures","uvScale","uvOffset","uvRotation","normalScale","occlusionStrength","surfacePattern","surfaceSeed"});
        StyleMaterial mt;mt.id=k;if(v.has("color"))mt.color=rgb(v["color"],p+".color");if(v.has("metallic"))mt.metallic=num(v["metallic"],p+".metallic");
        if(v.has("roughness")){mt.roughness=num(v["roughness"],p+".roughness");}if(v.has("emissive"))mt.emissive=rgb(v["emissive"],p+".emissive");
        if(v.has("emissiveStrength"))mt.emissiveStrength=num(v["emissiveStrength"],p+".emissiveStrength");
        auto uvPair=[&](const char* name,std::array<float,2>& dest){
            if(!v.has(name))return;
            const auto& value=v[name];
            if(!value.isArray()||value.size()!=2)
                throw JsonError(p+"."+name+": expected [u,v]");
            for(size_t index=0;index<2;++index){
                if(!value[index].isNumber()||
                   !std::isfinite(value[index].toDouble())||
                   std::abs(value[index].toDouble())>1024.)
                    throw JsonError(p+"."+name+": UV component out of range");
                dest[index]=float(value[index].toDouble());
            }
        };
        uvPair("uvScale",mt.uvScale);
        uvPair("uvOffset",mt.uvOffset);
        for(const char* key:{"uvRotation","normalScale","occlusionStrength"})
            if(v.has(key)){
                if(!v[key].isNumber()||!std::isfinite(v[key].toDouble()))
                    throw JsonError(p+"."+key+": expected finite number");
                const double n=v[key].toDouble();
                const double low=std::string(key)=="uvRotation"?-6.283186:0.;
                const double high=std::string(key)=="uvRotation"?6.283186:
                                  std::string(key)=="normalScale"?16.:1.;
                if(n<low||n>high)throw JsonError(p+"."+key+": outside valid range");
                if(std::string(key)=="uvRotation")mt.uvRotation=float(n);
                else if(std::string(key)=="normalScale")mt.normalScale=float(n);
                else mt.occlusionStrength=float(n);
            }
        if(v.has("surfacePattern")){
            if(!v["surfacePattern"].isString()||
               !validBuildingSurfacePattern(v["surfacePattern"].str()))
                throw JsonError(p+".surfacePattern: unknown generated PBR surface");
            mt.surfacePattern=v["surfacePattern"].str();
        }
        if(v.has("surfaceSeed")){
            const auto& value=v["surfaceSeed"];
            if(!value.isNumber()||value.toDouble(-1)<0.||
               value.toDouble(-1)>2147483647.||
               std::floor(value.toDouble(-1))!=value.toDouble(-1))
                throw JsonError(p+".surfaceSeed: expected 0..2147483647 integer");
            mt.surfaceSeed=uint32_t(value.toDouble());
        }
        if(v.has("textures")){
            const auto& tex=v["textures"];
            const std::string tp=p+".textures";
            allow(tex,tp,{"baseColor","metallicRoughness","normal","occlusion","emissive"});
            auto guid=[&](const char* key,std::string& dest){
                if(!tex.has(key))return;
                if(!tex[key].isString())
                    throw JsonError(tp+"."+key+": expected GUID string");
                dest=tex[key].str();
                if(dest.size()!=32||!std::all_of(dest.begin(),dest.end(),[](char ch){
                    return (ch>='0'&&ch<='9')||(ch>='a'&&ch<='f');
                }))throw JsonError(tp+"."+key+": expected 32 lowercase hex GUID");
            };
            guid("baseColor",mt.textures.baseColor);
            guid("metallicRoughness",mt.textures.metallicRoughness);
            guid("normal",mt.textures.normal);
            guid("occlusion",mt.textures.occlusion);
            guid("emissive",mt.textures.emissive);
        }
        ids[k]=int(s.materials.size());s.materials.push_back(mt);
    }
    auto mat=[&](const Json& o,const char* key,const std::string& p){if(!o.has(key))return -1;auto n=o[key].str();auto it=ids.find(n);
        if(it==ids.end()){throw JsonError(p+"."+key+": unknown material '"+n+"'");}return it->second;};
    auto get=[&](const Json& o,const char* key,float& out,const std::string& p){if(o.has(key))out=num(o[key],p+"."+key);};
    auto range=[&](const Json& o,const char* key,StyleRange& out,const std::string& p){if(!o.has(key))return;const auto& a=o[key];
        if(a.isNumber()){out.lo=out.hi=num(a,p+"."+key);return;}
        if(!a.isArray()||a.size()!=2){throw JsonError(p+"."+key+": expected number or [min, max]");}out.lo=num(a[0],p+"."+key+"[0]");out.hi=num(a[1],p+"."+key+"[1]");
        if(out.hi<out.lo)throw JsonError(p+"."+key+": max below min");};
    auto count=[&](const Json& o,uint32_t& lo,uint32_t& hi,const std::string& p){StyleRange r{float(lo),float(hi)};range(o,"count",r,p);
        if(r.lo<0||r.hi>64){throw JsonError(p+".count: expected 0..64");}lo=uint32_t(r.lo);hi=uint32_t(r.hi);};
    auto lightOf=[&](const Json& o,const char* key,StyleLight& out,const std::string& p){if(!o.has(key))return;auto q=p+"."+key;const auto& l=o[key];
        allow(l,q,{"material","height","chance"});out.material=mat(l,"material",q);out.chance=1;get(l,"height",out.height,q);get(l,"chance",out.chance,q);
        if(out.material<0)throw JsonError(q+".material: expected material");};
    if(j.has("palettes")){const auto& ps=j["palettes"];if(!ps.isArray())throw JsonError(path+".palettes: expected array");
        for(size_t i=0;i<ps.size();++i){auto p=path+".palettes["+std::to_string(i)+"]";if(!ps[i].isObject())throw JsonError(p+": expected object");std::map<int,StyleRgb> pal;
            for(const auto& [k,v]:ps[i].members()){auto it=ids.find(k);if(it==ids.end())throw JsonError(p+": unknown material '"+k+"'");pal[it->second]=rgb(v,p+"."+k);}
            s.palettes.push_back(pal);}}
    s.shell=mat(j,"shell",path);s.interior=mat(j,"interior",path);s.roof=mat(j,"roof",path);
    if(s.shell<0||s.interior<0||s.roof<0)throw JsonError(path+": shell, interior and roof materials are required");
    if(j.has("panels")){auto p=path+".panels";const auto& o=j["panels"];allow(o,p,{"material","accent","accentChance","size","gap","depth"});
        s.panels.material=mat(o,"material",p);s.panels.accent=mat(o,"accent",p);get(o,"accentChance",s.panels.accentChance,p);get(o,"gap",s.panels.gap,p);range(o,"depth",s.panels.depth,p);
        if(o.has("size")){const auto& a=o["size"];if(!a.isArray()||a.size()!=2)throw JsonError(p+".size: expected [width, height]");s.panels.width=num(a[0],p+".size[0]");s.panels.height=num(a[1],p+".size[1]");}}
    if(j.has("base")){auto p=path+".base";const auto& o=j["base"];allow(o,p,{"material","height","depth"});s.base.material=mat(o,"material",p);get(o,"height",s.base.height,p);get(o,"depth",s.base.depth,p);}
    else s.base.height=0;
    if(j.has("band")){auto p=path+".band";const auto& o=j["band"];allow(o,p,{"material","height","depth","light"});s.band.material=mat(o,"material",p);get(o,"height",s.band.height,p);get(o,"depth",s.band.depth,p);lightOf(o,"light",s.band.light,p);}
    else{s.band.height=0;s.band.depth=0;}
    if(j.has("corners")){auto p=path+".corners";const auto& o=j["corners"];allow(o,p,{"material","width","depth","chamfer"});s.corners.material=mat(o,"material",p);get(o,"width",s.corners.width,p);get(o,"depth",s.corners.depth,p);get(o,"chamfer",s.corners.chamfer,p);}
    if(j.has("window")){auto p=path+".window";const auto& o=j["window"];allow(o,p,{"frame","glass","lit","litChance","width","depth","strip","hood"});get(o,"hood",s.window.hood,p);
        s.window.frame=mat(o,"frame",p);s.window.glass=mat(o,"glass",p);s.window.lit=mat(o,"lit",p);get(o,"litChance",s.window.litChance,p);get(o,"width",s.window.width,p);get(o,"depth",s.window.depth,p);lightOf(o,"strip",s.window.strip,p);}
    if(j.has("door")){auto p=path+".door";const auto& o=j["door"];allow(o,p,{"frame","width","depth","light","canopy","canopyDepth","canopyHeight"});
        s.door.frame=mat(o,"frame",p);get(o,"width",s.door.width,p);get(o,"depth",s.door.depth,p);lightOf(o,"light",s.door.light,p);s.door.canopy=mat(o,"canopy",p);
        get(o,"canopyDepth",s.door.canopyDepth,p);get(o,"canopyHeight",s.door.canopyHeight,p);}
    if(j.has("parapet")){auto p=path+".parapet";const auto& o=j["parapet"];allow(o,p,{"material","height","width","light"});s.parapet.material=mat(o,"material",p);get(o,"height",s.parapet.height,p);get(o,"width",s.parapet.width,p);lightOf(o,"light",s.parapet.light,p);}
    else s.parapet.height=0;
    if(j.has("roofBlocks")){auto p=path+".roofBlocks";const auto& o=j["roofBlocks"];allow(o,p,{"material","count","width","depth","height","light"});
        s.roofBlocks.material=mat(o,"material",p);count(o,s.roofBlocks.min,s.roofBlocks.max,p);range(o,"width",s.roofBlocks.width,p);range(o,"depth",s.roofBlocks.depth,p);range(o,"height",s.roofBlocks.height,p);lightOf(o,"light",s.roofBlocks.light,p);}
    auto propsOf=[&](const Json& a,const std::string& at,std::vector<BuildingStyle::Prop>& outList){if(!a.isArray())throw JsonError(at+": expected array");
        for(size_t i=0;i<a.size();++i){auto p=at+"["+std::to_string(i)+"]";const auto& o=a[i];allow(o,p,{"kind","material","count","size","tip"});BuildingStyle::Prop pr;
            auto k=o["kind"].str("box");if(k=="box")pr.kind=BuildingStyle::Prop::Box;else if(k=="mast")pr.kind=BuildingStyle::Prop::Mast;else if(k=="tank")pr.kind=BuildingStyle::Prop::Tank;
            else if(k=="stack")pr.kind=BuildingStyle::Prop::Stack;else if(k=="dish")pr.kind=BuildingStyle::Prop::Dish;else throw JsonError(p+".kind: expected box, mast, tank, stack or dish");
            pr.material=mat(o,"material",p);if(pr.material<0)throw JsonError(p+".material: expected material");count(o,pr.min,pr.max,p);
            if(o.has("size")){const auto& sz=o["size"];if(!sz.isArray()||sz.size()!=3)throw JsonError(p+".size: expected [x, y, z] (numbers or [min, max])");
                StyleRange* r[3]={&pr.x,&pr.y,&pr.z};
                for(size_t c=0;c<3;++c){auto q=p+".size["+std::to_string(c)+"]";const auto& e=sz[c];
                    if(e.isNumber()){r[c]->lo=r[c]->hi=num(e,q);}else if(e.isArray()&&e.size()==2){r[c]->lo=num(e[0],q+"[0]");r[c]->hi=num(e[1],q+"[1]");if(r[c]->hi<r[c]->lo)throw JsonError(q+": max below min");}
                    else throw JsonError(q+": expected number or [min, max]");}}
            lightOf(o,"tip",pr.tip,p);outList.push_back(pr);}};
    if(j.has("props"))propsOf(j["props"],path+".props",s.props);
    if(j.has("structure")){auto p=path+".structure";const auto& o=j["structure"];
        allow(o,p,{"hull","plate","team","dark","pad","glow","beacon","exhaust","window"});auto& m=s.structure;
        m.hull=mat(o,"hull",p);m.plate=mat(o,"plate",p);m.team=mat(o,"team",p);m.dark=mat(o,"dark",p);m.pad=mat(o,"pad",p);m.glow=mat(o,"glow",p);
        m.beacon=mat(o,"beacon",p);m.exhaust=mat(o,"exhaust",p);m.window=mat(o,"window",p);
        if(m.hull<0||m.dark<0)throw JsonError(p+": hull and dark materials are required");}
    if(j.has("structures")){const auto& all=j["structures"];if(!all.isObject())throw JsonError(path+".structures: expected object");
        if(s.structure.hull<0)throw JsonError(path+".structures: needs the structure materials block");
        for(const auto& [id,o]:all.members()){auto p=path+".structures."+id;
            allow(o,p,{"size","height","massing","taper","chamfer","roofRise","bands","teamChance","modules","tower","door","legs","thrusters","pad","props","forms"});
            StructureKind k;k.id=id;
            if(o.has("massing")){
                if(!o["massing"].isString())throw JsonError(p+".massing: expected string");
                const auto m=o["massing"].str();
                if(m=="bastion")k.massing=StructureMassing::Bastion;
                else if(m=="spire")k.massing=StructureMassing::Spire;
                else if(m=="twin-bay")k.massing=StructureMassing::TwinBay;
                else if(m=="terraced")k.massing=StructureMassing::Terraced;
                else throw JsonError(p+".massing: expected bastion, spire, twin-bay or terraced");
            }
            if(o.has("size")){const auto& a=o["size"];if(!a.isArray()||a.size()!=2)throw JsonError(p+".size: expected [width, depth]");k.width=num(a[0],p+".size[0]");k.depth=num(a[1],p+".size[1]");}
            range(o,"height",k.height,p);get(o,"taper",k.taper,p);get(o,"chamfer",k.chamfer,p);get(o,"roofRise",k.roofRise,p);get(o,"teamChance",k.teamChance,p);
            if(o.has("bands")){StyleRange r{float(k.bands),float(k.bands)};range(o,"bands",r,p);k.bands=uint32_t(std::clamp(r.lo,1.f,8.f));}
            if(o.has("modules")){auto q=p+".modules";const auto& m=o["modules"];allow(m,q,{"count","width","depth","height"});count(m,k.modulesMin,k.modulesMax,q);
                range(m,"width",k.moduleWidth,q);range(m,"depth",k.moduleDepth,q);range(m,"height",k.moduleHeight,q);}
            if(o.has("tower")){auto q=p+".tower";const auto& m=o["tower"];allow(m,q,{"chance","width","depth","height"});k.towerChance=1;get(m,"chance",k.towerChance,q);
                range(m,"width",k.towerWidth,q);range(m,"depth",k.towerDepth,q);range(m,"height",k.towerHeight,q);}
            if(o.has("door")){auto q=p+".door";const auto& m=o["door"];allow(m,q,{"width","height","count"});get(m,"width",k.doorWidth,q);get(m,"height",k.doorHeight,q);
                StyleRange r{1,1};range(m,"count",r,q);k.doors=uint32_t(std::clamp(r.lo,0.f,3.f));}
            if(o.has("legs"))k.legs=o["legs"].boolean(false);
            if(o.has("thrusters")){StyleRange r{0,0};auto q=p+".thrusters";if(!o["thrusters"].isNumber())throw JsonError(q+": expected number");r.lo=num(o["thrusters"],q);k.thrusters=uint32_t(std::clamp(r.lo,0.f,6.f));}
            get(o,"pad",k.pad,p);
            if(o.has("props"))propsOf(o["props"],p+".props",k.props);
            if(o.has("forms")){
                const auto& a=o["forms"];const auto at=p+".forms";
                if(!a.isArray()||a.size()>24)throw JsonError(at+": expected array of up to 24 forms");
                for(size_t fi=0;fi<a.size();++fi){
                    const std::string q=at+"["+std::to_string(fi)+"]";
                    const auto& v=a[fi];allow(v,q,{"kind","at","size","level","taper","chance","material","repeat","step"});
                    StructureForm form;
                    if(!v["kind"].isString())throw JsonError(q+".kind: expected pod, silo, pylon, gantry, arch, dome or swept-hull");
                    const auto type=v["kind"].str();
                    if(type=="pod")form.kind=StructureForm::Kind::Pod;
                    else if(type=="silo")form.kind=StructureForm::Kind::Silo;
                    else if(type=="pylon")form.kind=StructureForm::Kind::Pylon;
                    else if(type=="gantry")form.kind=StructureForm::Kind::Gantry;
                    else if(type=="arch")form.kind=StructureForm::Kind::Arch;
                    else if(type=="dome")form.kind=StructureForm::Kind::Dome;
                    else if(type=="swept-hull")form.kind=StructureForm::Kind::SweptHull;
                    else throw JsonError(q+".kind: expected pod, silo, pylon, gantry, arch, dome or swept-hull");
                    form.material=mat(v,"material",q);
                    if(form.material<0)throw JsonError(q+".material: expected material");
                    auto pair=[&](const char* key,float* values,size_t length){
                        const auto& list=v[key];
                        if(!list.isArray()||list.size()!=length)throw JsonError(q+"."+key+": expected array of "+std::to_string(length)+" numbers");
                        for(size_t c=0;c<length;++c)values[c]=num(list[c],q+"."+key+"["+std::to_string(c)+"]");
                    };
                    float centre[2]{},dimensions[3]{},step[2]{};
                    pair("at",centre,2);pair("size",dimensions,3);
                    if(v.has("step"))pair("step",step,2);
                    form.x=centre[0];form.z=centre[1];
                    form.width=dimensions[0];form.height=dimensions[1];form.depth=dimensions[2];
                    form.stepX=step[0];form.stepZ=step[1];
                    get(v,"level",form.level,q);get(v,"taper",form.taper,q);get(v,"chance",form.chance,q);
                    if(v.has("repeat")){
                        if(!v["repeat"].isNumber())throw JsonError(q+".repeat: expected integer 1..8");
                        const Fixed val=v["repeat"].fx();
                        if(val<1_fx||val>8_fx||val.raw%Fixed::kOne)
                            throw JsonError(q+".repeat: expected integer 1..8");
                        form.repeat=uint32_t(val.toInt());
                    }
                    const auto within=[](float v,float a,float b){return std::isfinite(v)&&v>=a&&v<=b;};
                    if(!within(form.x,-0.5f,1.5f)||!within(form.z,-0.5f,1.5f)||
                       !within(form.width,0.04f,1.5f)||!within(form.depth,0.04f,1.5f)||
                       !within(form.height,0.04f,1.5f)||!within(form.level,0.f,1.2f)||
                       !within(form.taper,0.f,0.4f)||!within(form.chance,0.f,1.f)||
                       !within(form.stepX,-1.f,1.f)||!within(form.stepZ,-1.f,1.f))
                       throw JsonError(q+": form parameters outside supported bounds");
                    if((form.width*form.depth)>1.5f)
                       throw JsonError(q+": form footprint too large");
                    // A repeated form must not escape the local placement budget.
                    // Prevent a modest 'step' from accumulating an unbounded offset.
                    const float lastX=form.x+form.stepX*float(form.repeat-1);
                    const float lastZ=form.z+form.stepZ*float(form.repeat-1);
                    if(!within(lastX,-0.75f,1.75f)||!within(lastZ,-0.75f,1.75f))
                        throw JsonError(q+".step: repeated form escapes placement bounds");
                    k.forms.push_back(form);
                }
            }
            s.structures.push_back(k);}}
    if(j.has("skirt")){auto p=path+".skirt";const auto& o=j["skirt"];allow(o,p,{"material","height","out","plate","gap"});s.skirt.material=mat(o,"material",p);
        get(o,"height",s.skirt.height,p);get(o,"out",s.skirt.out,p);get(o,"plate",s.skirt.plate,p);get(o,"gap",s.skirt.gap,p);}
    if(j.has("buttresses")){auto p=path+".buttresses";const auto& o=j["buttresses"];allow(o,p,{"material","width","out","height","chance"});s.buttresses.material=mat(o,"material",p);
        get(o,"width",s.buttresses.width,p);get(o,"out",s.buttresses.out,p);get(o,"height",s.buttresses.height,p);get(o,"chance",s.buttresses.chance,p);}
    if(j.has("pipes")){auto p=path+".pipes";const auto& o=j["pipes"];allow(o,p,{"material","bracket","radius","chance"});s.pipes.material=mat(o,"material",p);s.pipes.bracket=mat(o,"bracket",p);
        get(o,"radius",s.pipes.radius,p);get(o,"chance",s.pipes.chance,p);}
    if(j.has("hazard")){auto p=path+".hazard";const auto& o=j["hazard"];allow(o,p,{"material","back","stripe","height"});s.hazard.material=mat(o,"material",p);s.hazard.back=mat(o,"back",p);
        get(o,"stripe",s.hazard.stripe,p);get(o,"height",s.hazard.height,p);if(s.hazard.material<0||s.hazard.back<0)throw JsonError(p+": material and back are required");}
    if(j.has("crown")){auto p=path+".crown";const auto& o=j["crown"];allow(o,p,{"material","height","out","light"});s.crown.material=mat(o,"material",p);
        get(o,"height",s.crown.height,p);get(o,"out",s.crown.out,p);lightOf(o,"light",s.crown.light,p);}
    if(j.has("ribs")){auto p=path+".ribs";const auto& o=j["ribs"];allow(o,p,{"material","width","depth","chance","light"});s.ribs.material=mat(o,"material",p);s.ribs.chance=1;
        get(o,"width",s.ribs.width,p);get(o,"depth",s.ribs.depth,p);get(o,"chance",s.ribs.chance,p);lightOf(o,"light",s.ribs.light,p);}
    if(j.has("sign")){auto p=path+".sign";const auto& o=j["sign"];allow(o,p,{"material","frame","chance","width","depth","height"});s.sign.material=mat(o,"material",p);s.sign.frame=mat(o,"frame",p);
        s.sign.chance=1;get(o,"chance",s.sign.chance,p);get(o,"width",s.sign.width,p);get(o,"depth",s.sign.depth,p);range(o,"height",s.sign.height,p);
        if(s.sign.material<0)throw JsonError(p+".material: expected material");}
    if(j.has("proportions")){auto p=path+".proportions";const auto& o=j["proportions"];allow(o,p,{"window","door","floorHeight","floors"});auto& q=s.proportions;
        auto fx=[&](const Json& v,const std::string& at){if(!v.isNumber())throw JsonError(at+": expected number");return v.fx();};
        auto list=[&](const char* key,std::initializer_list<Fixed*> outs){const auto& a=o[key];if(!a.isArray()||a.size()!=outs.size())throw JsonError(p+"."+key+": expected "+std::to_string(outs.size())+" numbers");
            size_t i=0;for(auto* x:outs){*x=fx(a[i],p+"."+key+"["+std::to_string(i)+"]");++i;}};
        if(o.has("window")){q.window=true;list("window",{&q.windowWidth,&q.windowHeight,&q.windowSill,&q.windowSpacing});}
        if(o.has("door")){q.door=true;list("door",{&q.doorWidth,&q.doorHeight});}
        if(o.has("floors")){Fixed lo,hi;list("floors",{&lo,&hi});if(lo<1_fx||hi>10_fx||hi<lo||lo.raw%Fixed::kOne||hi.raw%Fixed::kOne)throw JsonError(p+".floors: expected [min, max] in 1..10");
            q.minFloors=uint32_t(lo.toInt());q.maxFloors=uint32_t(hi.toInt());}
        if(o.has("floorHeight")){q.floorHeight=true;q.floor=fx(o["floorHeight"],p+".floorHeight");}}
    return s;
}
}
