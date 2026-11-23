#pragma once
// Structures: a whole building as one armoured hull (RTS bases, outposts, depots, landing platforms) rather
// than floors with windows. The style file's `structures` block describes kinds (size, hull height and taper,
// side modules, a superstructure tower, gates, landing legs, thrusters, roof props); `structure` names the
// materials (hull, plates, team-coloured plates, dark parts, pad, glow, beacons, exhaust, slit windows).
// dressStructure() builds the mesh and simple collision boxes for one structure.
//
// Presentation (float), deterministic per kind + style + seed. Front (gates) = -Z; origin = the footprint's
// minimum corner on the ground; quarter-turn yaw as building-gen.
#include "BuildingStyle.h"
namespace aiverse::lib::systems {
// All zero preserves legacy style dimensions. Explicit dimensions are the final envelope,
// including projecting components, props, foundations and gates in the mesh.
struct StructureSize{float width=0,height=0,depth=0;};
struct StyledStructure{StyledMesh mesh;std::vector<BuildingBox> boxes;std::vector<Vec3> gates;
    StructureSize constructionSize{}; // actual authored body dimensions, before any rigid translation
    StructureSize fitResidual{}; // signed unsatisfied W/H/D after last attempt
    std::string fitError; // empty when successful; intended for Studio diagnostics
    uint32_t fitIterations=0;
};   // gates: ground points in front of each gate
namespace buildingStyleDetail {
using P3=std::array<float,3>;
// Rectangle with cut corners (8 points; 4 when the cut is 0) at height y, counter-clockwise from -Z.
inline std::vector<P3> cutRect(float x0,float z0,float x1,float z1,float c,float y){
    c=std::clamp(c,0.f,std::min(x1-x0,z1-z0)*0.45f);
    if(c<=1e-4f)return {{x0,y,z0},{x1,y,z0},{x1,y,z1},{x0,y,z1}};
    return {{x0+c,y,z0},{x1-c,y,z0},{x1,y,z0+c},{x1,y,z1-c},{x1-c,y,z1},{x0+c,y,z1},{x0,y,z1-c},{x0,y,z0+c}};
}

// Architectural curves: bounded tessellation counts, true 3D shells rather
// than merely adding more rectangular decorations. Coordinates in metres.
inline void addArch(Mesher& m,int material,float cx,float y0,float z0,float width,float height,float depth){
    const float x0=cx-width*0.5f,x1=cx+width*0.5f,z1=z0+depth;
    const float leg=std::clamp(width*0.12f,0.24f,0.85f);
    const float spring=y0+height*0.42f,rise=height*0.58f,rx=width*0.5f;
    m.box(material,x0,y0,z0,x0+leg,spring,z1);
    m.box(material,x1-leg,y0,z0,x1,spring,z1);
    constexpr int segments=12;
    constexpr float pi=3.14159265358979323846f;
    auto arcPoint=[&](float theta,bool inner,float z)->P3{
        const float radial=inner?rx-leg:rx,ry=inner?std::max(0.08f,rise-leg):rise;
        return {cx+radial*std::cos(theta),spring+ry*std::sin(theta),z};
    };
    for(int i=0;i<segments;++i){
        const float a=pi*float(i)/segments,b=pi*float(i+1)/segments;
        std::vector<P3> near={arcPoint(a,false,z0),arcPoint(b,false,z0),
                              arcPoint(b,true,z0),arcPoint(a,true,z0)};
        std::vector<P3> far={arcPoint(a,false,z1),arcPoint(b,false,z1),
                             arcPoint(b,true,z1),arcPoint(a,true,z1)};
        m.prism(material,near,far);
    }
}
inline void addDome(Mesher& m,int material,float cx,float y0,float cz,float width,float height,float depth){
    constexpr int sides=16,bands=7;
    constexpr float pi=3.14159265358979323846f;
    const float rx=width*0.5f,rz=depth*0.5f;
    auto point=[&](int side,int ring)->P3{
        const float phi=2*pi*float(side)/sides;
        const float theta=(pi*0.5f-0.06f)*float(ring)/bands;
        const float radius=std::cos(theta);
        return {cx+rx*radius*std::cos(phi),y0+height*std::sin(theta),
                cz+rz*radius*std::sin(phi)};
    };
    for(int band=0;band<bands;++band)for(int side=0;side<sides;++side){
        const auto a=point(side,band),b=point((side+1)%sides,band),
                   c=point((side+1)%sides,band+1),d=point(side,band+1);
        const float phi=2*pi*(float(side)+0.5f)/sides;
        m.quad(material,{a,b,c,d},
               {std::cos(phi),float(band+1)/bands,std::sin(phi)});
    }
    const P3 apex{cx,y0+height,cz};
    for(int side=0;side<sides;++side){
        const auto a=point(side,bands),b=point((side+1)%sides,bands);
        const float phi=2*pi*(float(side)+0.5f)/sides;
        m.poly(material,{a,b,apex},{std::cos(phi),1.f,std::sin(phi)});
    }
    std::vector<P3> base;
    for(int side=0;side<sides;++side)base.push_back(point(side,0));
    m.poly(material,std::move(base),{0,-1,0});
}
inline void addSweptHull(Mesher& m,int material,float cx,float y0,float cz,float width,float height,float depth){
    constexpr int sides=12,segments=8;
    constexpr float pi=3.14159265358979323846f;
    auto point=[&](int side,int station)->P3{
        const float t=float(station)/segments,a=2*pi*float(side)/sides;
        const float profile=0.72f+0.28f*std::sin(pi*t);
        const float yCentre=y0+height*(0.5f+0.06f*std::sin(2*pi*t));
        return {cx+width*0.5f*profile*std::cos(a),
                yCentre+height*0.48f*profile*std::sin(a),
                cz+depth*t};
    };
    for(int station=0;station<segments;++station)
        for(int side=0;side<sides;++side){
            const auto a=point(side,station),b=point((side+1)%sides,station),
                       c=point((side+1)%sides,station+1),d=point(side,station+1);
            const float phi=2*pi*(float(side)+0.5f)/sides;
            m.quad(material,{a,b,c,d},{std::cos(phi),std::sin(phi),0});
        }
    std::vector<P3> near,far;
    for(int i=0;i<sides;++i){near.push_back(point(i,0));far.push_back(point(i,segments));}
    m.poly(material,std::move(near),{0,0,-1});
    m.poly(material,std::move(far),{0,0,1});
}

}
// Raw constructive geometry: preserve absolute thickness/steps/props. The public
// exact-envelope entry point below solves for body proportions by regeneration.
inline StyledStructure dressStructureGeometry(const BuildingStyle& st,const std::string& kindId,uint64_t seed,Vec3 origin={},uint32_t yaw=0,StructureSize body={}){
    using namespace buildingStyleDetail;
    StyledStructure out;const StructureKind* kp=st.structureKind(kindId);const auto& sm=st.structure;
    if(!kp||sm.hull<0||yaw>3)return out;
    const StructureKind& k=*kp;
    const bool custom=body.width!=0||body.height!=0||body.depth!=0;
    if(custom&&(!std::isfinite(body.width)||!std::isfinite(body.height)||!std::isfinite(body.depth)||
        body.width<=0||body.depth<=0||body.height<=0||
        body.width>180.f||body.depth>180.f||body.height>90.f))return out;
    Random g(seed,0x57c7);
    auto u=[&]{return float(g.unit().toDouble());};
    out.mesh.materials=st.materials;
    if(!st.palettes.empty()){const auto& p=st.palettes[g.below(uint32_t(st.palettes.size()))];
        for(const auto& [m,c]:p){if(m<0||size_t(m)>=out.mesh.materials.size())continue;auto& mat=out.mesh.materials[size_t(m)];
            if(mat.emissive[0]+mat.emissive[1]+mat.emissive[2]>0)mat.emissive=c;else mat.color=c;}}
    Mesher m(out.mesh.materials.size());
    const float W=custom?body.width:k.width,D=custom?body.depth:k.depth,Hh=custom?body.height:k.height.pick(g),pad=k.pad;
    out.constructionSize={W,Hh,D};
    const float cut=custom?std::min(k.chamfer,std::min(W,D)*0.22f):k.chamfer;
    const float padH=0.35f;
    // When solving an explicit envelope, the number of armour plates changes
    // with body dimensions. Keep that decorative RNG on an independent stream,
    // otherwise every size iteration randomly moves major modules/props.
    // Legacy unsized generation retains its original single-stream sequence.
    Random details(seed,0x57c7d37a);
    auto detailU=[&]{return custom?float(details.unit().toDouble()):u();};
    auto plateMat=[&]{return sm.team>=0&&detailU()<k.teamChance?sm.team:(sm.plate>=0?sm.plate:sm.hull);};
    auto addBox=[&](float x0,float y0,float z0,float x1,float y1,float z1){out.boxes.push_back({{Fixed::fromRatio(int64_t(std::lround(x0*1000)),1000),Fixed::fromRatio(int64_t(std::lround(y0*1000)),1000),Fixed::fromRatio(int64_t(std::lround(z0*1000)),1000)},
        {Fixed::fromRatio(int64_t(std::lround(x1*1000)),1000),Fixed::fromRatio(int64_t(std::lround(y1*1000)),1000),Fixed::fromRatio(int64_t(std::lround(z1*1000)),1000)}});};
    // An armoured block: cut-corner plan, walls leaning in by `lean` metres over their height, armour belts
    // (slightly proud, with seams) and a shallow raised roof. Returns the roof rectangle and height.
    struct Top{float x0,z0,x1,z1,y;};
    auto block=[&](float x0,float z0,float x1,float z1,float y0,float h,float c,float lean,uint32_t belts,float rise)->Top{
        float y1=y0+h;
        m.prism(sm.dark,cutRect(x0,z0,x1,z1,c,y0),cutRect(x0+lean,z0+lean,x1-lean,z1-lean,c,y1));
        // Belts of armour plates: one plate per wall face and belt (long faces split in two or three), proud of
        // the wall by a random depth, with dark seams between them.
        for(uint32_t b=0;b<belts;++b){
            float t0=float(b)/float(belts),t1=float(b+1)/float(belts);float gap=0.09f;
            float ya=y0+h*t0+(b?gap/2:0.f),yb=y0+h*t1-(b+1<belts?gap/2:0.f);
            float la=lean*(ya-y0)/h,lb=lean*(yb-y0)/h;
            auto ra=cutRect(x0+la,z0+la,x1-la,z1-la,c,ya),rb=cutRect(x0+lb,z0+lb,x1-lb,z1-lb,c,yb);
            P3 mid{(x0+x1)/2,0,(z0+z1)/2};
            for(size_t e=0;e<ra.size();++e){
                size_t f=(e+1)%ra.size();float len=std::hypot(ra[f][0]-ra[e][0],ra[f][2]-ra[e][2]);
                int pieces=len>9?3:len>4.5f?2:1;
                for(int q=0;q<pieces;++q){
                    float s0=float(q)/float(pieces),s1=float(q+1)/float(pieces);
                    float sh=gap/2/std::max(len,0.1f);s0+=q?sh:sh*0.5f;s1-=q+1<pieces?sh:sh*0.5f;
                    float proud=0.05f+0.1f*detailU();
                    // Outward = away from the plan centre (horizontal), plates sit `proud` outside the wall.
                    auto pt=[&](const std::vector<P3>& r,float t,float off){P3 p{r[e][0]+(r[f][0]-r[e][0])*t,r[e][1],r[e][2]+(r[f][2]-r[e][2])*t};
                        float dx=r[f][2]-r[e][2],dz=-(r[f][0]-r[e][0]);float l=std::hypot(dx,dz);dx/=l;dz/=l;
                        if((p[0]-mid[0])*dx+(p[2]-mid[2])*dz<0){dx=-dx;dz=-dz;}p[0]+=dx*off;p[2]+=dz*off;return p;};
                    int mat=b+1==belts?sm.hull:plateMat();
                    m.prism(mat,{pt(ra,s0,proud),pt(ra,s1,proud),pt(ra,s1,-0.05f),pt(ra,s0,-0.05f)},{pt(rb,s0,proud),pt(rb,s1,proud),pt(rb,s1,-0.05f),pt(rb,s0,-0.05f)});
                }
            }
        }
        // A glowing strip under the roof rim on some blocks.
        if(sm.glow>=0&&detailU()<0.5f){float yl=y1-0.22f,l=lean*(yl-y0)/h-0.2f;m.prism(sm.glow,cutRect(x0+l,z0+l,x1-l,z1-l,c,yl),cutRect(x0+l,z0+l,x1-l,z1-l,c,yl+0.07f),false);}
        // Roof: a dark rim, then a raised armoured deck.
        float r0=lean+0.25f;
        m.prism(sm.hull,cutRect(x0+lean-0.05f,z0+lean-0.05f,x1-lean+0.05f,z1-lean+0.05f,c,y1),cutRect(x0+r0,z0+r0,x1-r0,z1-r0,c,y1+rise*0.4f));
        float r1=r0+std::min(x1-x0,z1-z0)*0.12f;
        m.prism(sm.plate>=0?sm.plate:sm.hull,cutRect(x0+r0,z0+r0,x1-r0,z1-r0,c,y1+rise*0.4f),cutRect(x0+r1,z0+r1,x1-r1,z1-r1,c*0.7f,y1+rise));
        addBox(x0,y0,z0,x1,y1+rise,z1);
        return {x0+r1,z0+r1,x1-r1,z1-r1,y1+rise};
    };
    // 1. Foundation pad with corner beacons.
    if(sm.pad>=0){
        m.prism(sm.pad,cutRect(-pad,-pad,W+pad,D+pad,cut+pad*0.6f,0),cutRect(-pad+0.25f,-pad+0.25f,W+pad-0.25f,D+pad-0.25f,cut+pad*0.6f,padH));
        if(sm.beacon>=0)for(auto [x,z]:std::initializer_list<std::pair<float,float>>{{-pad+0.5f,-pad+0.5f},{W+pad-0.5f,-pad+0.5f},{W+pad-0.5f,D+pad-0.5f},{-pad+0.5f,D+pad-0.5f}}){
            if(sm.glow>=0)m.box(sm.dark,x-0.18f,padH,z-0.18f,x+0.18f,padH+0.12f,z+0.18f);
            m.box(sm.beacon,x-0.1f,padH+0.12f,z-0.1f,x+0.1f,padH+0.24f,z+0.1f);}
    }
    // 2. Main hull.
    const float lean=custom?std::min(Hh*k.taper,std::min(W,D)*0.20f):Hh*k.taper;
    auto top=block(0,0,W,D,padH,Hh,cut,lean,k.bands,k.roofRise);
    // Data-chosen silhouettes: a function changes the large shapes, not just the roof props.
    const float rw=top.x1-top.x0,rd=top.z1-top.z0;
    if(rw>2.5f&&rd>2.5f){
        switch(k.massing){
            case StructureMassing::Bastion: break;
            case StructureMassing::Spire:{
                const float w=rw*0.38f,d=rd*0.40f,x=top.x0+(rw-w)*0.5f,z=top.z0+(rd-d)*0.5f;
                auto cap=block(x,z,x+w,z+d,top.y,Hh*0.66f,std::min(cut,0.8f),std::min(w,d)*0.07f,3,k.roofRise*0.3f);
                if(sm.beacon>=0)m.box(sm.beacon,(cap.x0+cap.x1)*0.5f-0.15f,cap.y,(cap.z0+cap.z1)*0.5f-0.15f,
                    (cap.x0+cap.x1)*0.5f+0.15f,cap.y+0.24f,(cap.z0+cap.z1)*0.5f+0.15f);
                break;}
            case StructureMassing::TwinBay:{
                const float w=rw*0.37f,z=top.z0+rd*0.08f,z1=top.z1-rd*0.08f;
                for(int side=0;side<2;++side){
                    const float x=top.x0+rw*(side?0.56f:0.07f);
                    block(x,z,x+w,z1,top.y,Hh*0.22f,std::min(cut*0.3f,0.5f),
                          std::min(w,z1-z)*0.025f,2,k.roofRise*0.25f);
                }
                break;}
            case StructureMassing::Terraced:{
                const float w=rw*0.72f,d=rd*0.66f,x=top.x0+rw*0.17f,z=top.z0+rd*0.26f;
                auto tier=block(x,z,x+w,z+d,top.y,Hh*0.23f,std::min(cut,0.6f),std::min(w,d)*0.045f,2,k.roofRise*0.2f);
                const float tw=(tier.x1-tier.x0)*0.57f,td=(tier.z1-tier.z0)*0.58f;
                const float tx=tier.x0+(tier.x1-tier.x0-tw)*0.5f,tz=tier.z0+(tier.z1-tier.z0-td)*0.5f;
                block(tx,tz,tx+tw,tz+td,tier.y,Hh*0.15f,0.3f,std::min(tw,td)*0.03f,1,k.roofRise*0.15f);
                break;}
        }
    }
    // 3. Side modules (left / right / back), lower, overlapping the hull.
    uint32_t modules=k.modulesMin+(k.modulesMax>k.modulesMin?g.below(k.modulesMax-k.modulesMin+1):0);
    std::vector<Top> moduleTops;
    for(uint32_t i=0;i<modules;++i){
        uint32_t side=g.below(3);   // 0 = -X, 1 = +X, 2 = +Z (the front stays clear for the gates)
        float mw=k.moduleWidth.pick(g)*W,md=k.moduleDepth.pick(g)*D,mh=k.moduleHeight.pick(g)*Hh;
        float x0,z0,x1,z1;
        if(side==2){float c=W*(0.2f+0.6f*u());x0=c-md/2;x1=c+md/2;z0=D-lean-0.6f;z1=D+mw;}
        else{float c=D*(0.25f+0.5f*u());z0=c-md/2;z1=c+md/2;if(side==0){x0=-mw;x1=lean+0.6f;}else{x0=W-lean-0.6f;x1=W+mw;}}
        moduleTops.push_back(block(x0,z0,x1,z1,padH,mh,std::min(cut*0.6f,0.9f),mh*k.taper,std::max(1u,k.bands-1),k.roofRise*0.5f));
        // A hatch light on the module's outer face.
        if(sm.glow>=0){float hy=padH+mh*0.55f;
            if(side==2)m.box(sm.glow,(x0+x1)/2-0.5f,hy,z1-0.02f-mh*k.taper*0.55f,(x0+x1)/2+0.5f,hy+0.1f,z1+0.04f-mh*k.taper*0.55f);
            else{float xo=side==0?x0+mh*k.taper*0.55f:x1-mh*k.taper*0.55f;m.box(sm.glow,xo-0.04f,hy,(z0+z1)/2-0.5f,xo+0.04f,hy+0.1f,(z0+z1)/2+0.5f);}}
    }
    // 4. Gates on the front: a framed opening proud of the hull with slats, a light bar, hazard stripes and a ramp.
    for(uint32_t i=0;i<k.doors;++i){
        const float nominalWidth=k.doorWidth*k.width;
        const float nominalHeight=k.doorHeight*(k.height.lo+k.height.hi)*0.5f;
        float dw=std::min(custom?nominalWidth:k.doorWidth*W,(W-2*cut)/float(k.doors)-0.6f);
        float dh=std::min(custom?nominalHeight:k.doorHeight*Hh,Hh-0.8f);
        if(dw<1.2f||dh<1.5f)break;
        float cx=cut+(W-2*cut)*(float(i)+0.5f)/float(k.doors),x0=cx-dw/2,x1=cx+dw/2,y0=padH,y1=padH+dh,fr=0.35f,front=-0.45f,back=lean*(dh/Hh)+0.3f;
        m.box(sm.hull,x0-fr,y0,front,x0,y1+fr,back);m.box(sm.hull,x1,y0,front,x1+fr,y1+fr,back);m.box(sm.hull,x0-fr,y1,front,x1+fr,y1+fr*1.6f,back);
        m.box(sm.dark,x0,y0,front+0.25f,x1,y1,back);                                  // recessed door leaf
        for(float y=y0+0.35f;y<y1-0.2f;y+=0.45f)m.box(plateMat()==sm.team?sm.team:sm.hull,x0+0.12f,y,front+0.18f,x1-0.12f,y+0.2f,front+0.26f);   // slats
        if(sm.glow>=0)m.box(sm.glow,x0,y1+0.08f,front-0.03f,x1,y1+0.2f,front);
        if(st.hazard.material>=0){
            m.box(st.hazard.back,x0-fr,y1+fr*0.25f,front-0.02f,x1+fr,y1+fr*1.45f,front);
            float sw=st.hazard.stripe,hh=fr*1.1f;
            for(float x=x0-fr+0.05f;x+sw+hh<=x1+fr-0.05f;x+=sw*2)
                m.quad(st.hazard.material,{P3{x,y1+fr*0.3f,front-0.025f},P3{x+sw,y1+fr*0.3f,front-0.025f},P3{x+sw+hh,y1+fr*1.4f,front-0.025f},P3{x+hh,y1+fr*1.4f,front-0.025f}},{0,0,-1});
        }
        // Ramp down from the pad to the ground.
        m.prism(sm.hull,{P3{x0,0,front},P3{x0,padH,front},P3{x0,0,front-2.2f}},{P3{x1,0,front},P3{x1,padH,front},P3{x1,0,front-2.2f}});
        out.gates.push_back({Fixed::fromRatio(int64_t(std::lround(cx*1000)),1000),Fixed{},Fixed::fromRatio(int64_t(std::lround((front-3.0f)*1000)),1000)});
    }
    // 5. Landing legs on the four cut corners: an angled strut, a piston and a foot.
    if(k.legs){
        for(auto [cx,cz,sx,sz]:std::initializer_list<std::array<float,4>>{{0,0,-1,-1},{W,0,1,-1},{W,D,1,1},{0,D,-1,1}}){
            float c=std::min(cut,std::min(W,D)*0.45f),hy=padH+Hh*0.62f,reach=2.4f,s=0.38f;
            // Corner face midpoint at hy (the hull leans in by lean * 0.5 there).
            float l=lean*0.5f;float ax=cx-sx*(c/2+l*0.7f),az=cz-sz*(c/2+l*0.7f);
            float bx=ax+sx*reach,bz=az+sz*reach;
            auto sq=[&](float x,float y,float z,float r){return std::vector<P3>{{x-r,y,z-r},{x+r,y,z-r},{x+r,y,z+r},{x-r,y,z+r}};};
            m.prism(sm.hull,sq(ax,hy,az,s),sq(bx,padH*0.5f+0.3f,bz,s*0.9f));
            m.cylinder(sm.dark,{bx,0.0f,bz},1,s*0.55f,padH*0.5f+0.4f+Hh*0.12f,8);
            m.prism(sm.dark,cutRect(bx-0.7f,bz-0.7f,bx+0.7f,bz+0.7f,0.25f,0),cutRect(bx-0.55f,bz-0.55f,bx+0.55f,bz+0.55f,0.2f,0.3f));
            if(sm.beacon>=0)m.box(sm.beacon,ax-0.09f,hy+s,az-0.09f,ax+0.09f,hy+s+0.14f,az+0.09f);
        }
    }
    // 5b. Vent grilles on the side walls: a dark recess with slats, at mid height.
    for(uint32_t side=0;side<2;++side){
        uint32_t n=1+g.below(2);
        for(uint32_t i=0;i<n;++i){
            float zc=D*(0.3f+0.4f*(float(i)+0.5f)/float(n)),vy=padH+Hh*(0.35f+0.15f*u()),vh=0.9f,vw=1.6f;
            float l=lean*(vy+vh/2-padH)/Hh;float x=side?W-l:l;float sx=side?1.f:-1.f;
            m.box(sm.dark,std::min(x,x+sx*0.2f),vy,zc-vw/2,std::max(x,x+sx*0.2f),vy+vh,zc+vw/2);
            for(float y=vy+0.12f;y<vy+vh-0.1f;y+=0.2f)m.box(sm.hull,std::min(x+sx*0.18f,x+sx*0.26f),y,zc-vw/2+0.08f,std::max(x+sx*0.18f,x+sx*0.26f),y+0.07f,zc+vw/2-0.08f);
        }
    }
    // 6. Thrusters on the back: nozzles leaning out of the rear wall with glowing throats.
    for(uint32_t i=0;i<k.thrusters;++i){
        float r=std::min(0.9f,W*0.07f),cx=W*(float(i)+0.5f)/float(k.thrusters),cy=padH+Hh*0.42f,zb=D-lean*0.42f;
        m.cylinder(sm.dark,{cx,cy,zb-0.3f},2,r*1.15f,0.5f,12);
        m.cylinder(sm.hull,{cx,cy,zb+0.2f},2,r,0.9f,12,false);
        m.cylinder(sm.hull,{cx,cy,zb+1.1f},2,r*1.12f,0.25f,12);
        if(sm.exhaust>=0)m.cylinder(sm.exhaust,{cx,cy,zb+1.3f},2,r*0.8f,0.08f,12);
    }
    // 7. Superstructure: a smaller armoured tower on the roof with a glowing window slit.
    std::vector<std::array<float,4>> taken;
    // Architecture occupies roof area before placing optional props. Otherwise
    // industrial tanks/dishes could spawn through the generated silhouette.
    if(rw>2.5f&&rd>2.5f){
        if(k.massing==StructureMassing::Spire)
            taken.push_back({top.x0+rw*0.31f,top.z0+rd*0.30f,top.x0+rw*0.69f,top.z0+rd*0.70f});
        else if(k.massing==StructureMassing::TwinBay){
            for(int side=0;side<2;++side){
                const float x=top.x0+rw*(side?0.56f:0.07f);
                taken.push_back({x,top.z0+rd*0.08f,x+rw*0.37f,top.z1-rd*0.08f});
            }
        }else if(k.massing==StructureMassing::Terraced)
            taken.push_back({top.x0+rw*0.17f,top.z0+rd*0.26f,
                             top.x0+rw*0.89f,top.z0+rd*0.92f});
    }
    if(k.massing!=StructureMassing::Spire&&k.towerChance>0&&u()<k.towerChance){
        float tw=k.towerWidth.pick(g)*W,td=k.towerDepth.pick(g)*D,th=k.towerHeight.pick(g);
        float tx=top.x0+(top.x1-top.x0-tw)*(u()<0.5f?0.1f:0.9f),tz=top.z0+(top.z1-top.z0-td)*0.15f;
        tw=std::min(tw,top.x1-top.x0);td=std::min(td,top.z1-top.z0);
        auto t=block(tx,tz,tx+tw,tz+td,top.y,th,0.5f,th*0.12f,2,0.3f);
        if(sm.window>=0){float wy=top.y+th*0.6f,l=th*0.12f*0.6f;m.box(sm.window,tx+0.4f+l,wy,tz+l-0.03f,tx+tw-0.4f-l,wy+0.28f,tz+l+0.05f);}
        taken.push_back({tx-0.3f,tz-0.3f,tx+tw+0.3f,tz+td+0.3f});
        if(sm.beacon>=0)m.box(sm.beacon,(t.x0+t.x1)/2-0.12f,t.y,(t.z0+t.z1)/2-0.12f,(t.x0+t.x1)/2+0.12f,t.y+0.2f,(t.z0+t.z1)/2+0.12f);
    }
    // 7b. Bounded, data-authored large-scale geometry. Layout coordinates are
    // relative to the footprint and height; the material belongs to the style.
    // This grammar is independent of the setting and gameplay role.
    for(const StructureForm& form:k.forms)for(uint32_t i=0;i<form.repeat;++i){
        if(u()>form.chance)continue;
        const float cx=(form.x+form.stepX*float(i))*W;
        const float cz=(form.z+form.stepZ*float(i))*D;
        const float fw=form.width*W,fd=form.depth*D,fh=form.height*Hh;
        const float x0=cx-fw*0.5f,x1=cx+fw*0.5f,z0=cz-fd*0.5f,z1=cz+fd*0.5f;
        const float y0=padH+form.level*Hh,y1=y0+fh;
        if(fw<0.35f||fd<0.35f||fh<0.3f)continue;
        const int material=form.material;
        switch(form.kind){
        case StructureForm::Kind::Pod:{
            // Cut-corner wing, possibly protruding past the main hull.
            if(fw<1.5f||fd<1.5f)break;
            const float chamfer=std::min({cut*0.45f,fw*0.15f,fd*0.15f});
            const float inset=std::min(fw,fd)*form.taper;
            m.prism(material,cutRect(x0,z0,x1,z1,chamfer,y0),
                    cutRect(x0+inset,z0+inset,x1-inset,z1-inset,chamfer*0.8f,y1));
            addBox(x0,y0,z0,x1,y1,z1);
            break;}
        case StructureForm::Kind::Silo:{
            // Round reactor, storage silo or coolant vessel.
            const float radius=std::min(fw,fd)*0.46f;
            m.cylinder(material,{cx,y0,cz},1,radius,fh,16);
            if(sm.dark>=0)
                m.cylinder(sm.dark,{cx,y0+fh*0.78f,cz},1,radius*1.06f,
                           std::max(0.08f,fh*0.055f),16);
            addBox(cx-radius,y0,cz-radius,cx+radius,y1,cz+radius);
            break;}
        case StructureForm::Kind::Pylon:{
            // Four-sided tapered support with a light at its apex.
            const float inset=std::min(fw,fd)*form.taper;
            const float bevel=std::min({fw*0.2f,fd*0.2f,0.8f});
            m.prism(material,cutRect(x0,z0,x1,z1,bevel,y0),
                    cutRect(x0+inset,z0+inset,x1-inset,z1-inset,
                            bevel*0.45f,y1));
            addBox(x0,y0,z0,x1,y1,z1);
            if(sm.beacon>=0)
                m.box(sm.beacon,cx-fw*0.12f,y1,cz-fd*0.12f,
                      cx+fw*0.12f,y1+0.15f,cz+fd*0.12f);
            break;}
        case StructureForm::Kind::Arch:{
            // Free-standing curved portal. AABB collision is deliberately split
            // along each 12-piece arc, leaving the middle of the passage free.
            if(fw<3.5f||fh<2.8f)break;
            addArch(m,material,cx,y0,z0,fw,fh,fd);
            const float leg=std::clamp(fw*0.12f,0.24f,0.85f);
            const float spring=y0+fh*0.42f,rx=fw*0.5f,rise=fh*0.58f;
            addBox(x0,y0,z0,x0+leg,spring,z1);
            addBox(x1-leg,y0,z0,x1,spring,z1);
            constexpr int seg=12;
            constexpr float pi=3.14159265358979323846f;
            for(int q=0;q<seg;++q){
                const float a=pi*float(q)/seg,b=pi*float(q+1)/seg;
                const float cornersX[]={cx+rx*std::cos(a),cx+rx*std::cos(b),
                    cx+(rx-leg)*std::cos(a),cx+(rx-leg)*std::cos(b)};
                const float cornersY[]={spring+rise*std::sin(a),spring+rise*std::sin(b),
                    spring+(rise-leg)*std::sin(a),spring+(rise-leg)*std::sin(b)};
                const auto [minX,maxX]=std::minmax_element(std::begin(cornersX),std::end(cornersX));
                const auto [minY,maxY]=std::minmax_element(std::begin(cornersY),std::end(cornersY));
                addBox(*minX,*minY,z0,*maxX,*maxY,z1);
            }
            break;}
        case StructureForm::Kind::Dome:{
            if(fw<1.5f||fd<1.5f||fh<0.8f)break;
            addDome(m,material,cx,y0,cz,fw,fh,fd);
            addBox(x0,y0,z0,x1,y1,z1); // conservative for non-walkable roof
            break;}
        case StructureForm::Kind::SweptHull:{
            if(fw<1.5f||fd<2.5f||fh<1.2f)break;
            addSweptHull(m,material,cx,y0,cz,fw,fh,fd);
            addBox(x0,y0,z0,x1,y1,z1); // conservative closed body
            break;}
        case StructureForm::Kind::Gantry:{
            // A portal built from two uprights and a crossbeam; unlike a
            // solid block, its centre is empty also in coarse collision.
            const float leg=std::max(0.15f,std::min(fw*0.16f,0.75f));
            const float beam=std::max(0.18f,std::min(fh*0.20f,0.85f));
            if(fw<=leg*2.8f||fh<=beam*2.f)break;
            m.box(material,x0,y0,z0,x0+leg,y1,z1);
            m.box(material,x1-leg,y0,z0,x1,y1,z1);
            m.box(material,x0+leg,y1-beam,z0,x1-leg,y1,z1);
            addBox(x0,y0,z0,x0+leg,y1,z1);
            addBox(x1-leg,y0,z0,x1,y1,z1);
            addBox(x0+leg,y1-beam,z0,x1-leg,y1,z1);
            if(sm.glow>=0)
                m.box(sm.glow,x0+leg,y1-beam-0.10f,z0-0.08f,
                      x1-leg,y1-beam,z0+0.02f);
            break;}
        }
        // Large authored modules keep random roof props clear of their
        // footprint whenever they intersect the rooftop elevation.
        if(y1>=top.y-0.15f)
            taken.push_back({x0-0.15f,z0-0.15f,x1+0.15f,z1+0.15f});
    }
    // 8. Roof props on the main deck (and module roofs for small ones).
    auto freeAt=[&](float x0,float z0,float x1,float z1){for(const auto& r:taken)if(x0<r[2]&&r[0]<x1&&z0<r[3]&&r[1]<z1)return false;return true;};
    auto place=[&](const Top& t,float sx,float sz,std::array<float,4>& at){
        if(sx>t.x1-t.x0||sz>t.z1-t.z0)return false;
        for(int tries=0;tries<14;++tries){float x=t.x0+(t.x1-t.x0-sx)*u(),z=t.z0+(t.z1-t.z0-sz)*u();
            if(freeAt(x-0.2f,z-0.2f,x+sx+0.2f,z+sz+0.2f)){at={x,z,x+sx,z+sz};taken.push_back(at);return true;}}
        return false;};
    for(const auto& p:k.props){
        uint32_t n=p.min+(p.max>p.min?g.below(p.max-p.min+1):0);
        for(uint32_t i=0;i<n;++i){
            float sx=p.x.pick(g),sy=p.y.pick(g),sz=p.z.pick(g);bool round=p.kind!=BuildingStyleProp::Box&&p.kind!=BuildingStyleProp::Mast;
            Top t=top;if(!moduleTops.empty()&&u()<0.35f)t=moduleTops[g.below(uint32_t(moduleTops.size()))];
            std::array<float,4> at{};if(!place(t,round?sx:std::max(sx,0.3f),round?sx:std::max(sz,0.3f),at))continue;
            float cx=(at[0]+at[2])/2,cz=(at[1]+at[3])/2,y=t.y,r=sx/2;bool tip=p.tip.material>=0&&u()<p.tip.chance;
            switch(p.kind){
                case BuildingStyleProp::Tank:
                    m.box(p.material,cx-r*0.8f,y,cz-r*0.8f,cx+r*0.8f,y+0.25f,cz+r*0.8f);m.cylinder(p.material,{cx,y+0.25f,cz},1,r,sy,12);
                    m.cylinder(p.material,{cx,y+0.25f+sy*0.3f,cz},1,r*1.06f,0.12f,12);m.cylinder(p.material,{cx,y+0.25f+sy,cz},1,r*0.55f,0.2f,10);
                    if(tip)m.cylinder(p.tip.material,{cx,y+0.25f+sy*0.6f,cz},1,r*1.03f,p.tip.height,12,false);break;
                case BuildingStyleProp::Stack:
                    m.box(p.material,cx-r*1.4f,y,cz-r*1.4f,cx+r*1.4f,y+0.5f,cz+r*1.4f);m.cylinder(p.material,{cx,y+0.5f,cz},1,r,sy,10);
                    m.cylinder(p.material,{cx,y+0.5f+sy-0.3f,cz},1,r*1.15f,0.3f,10);if(tip)m.cylinder(p.tip.material,{cx,y+0.5f+sy,cz},1,r*0.85f,p.tip.height,10);break;
                case BuildingStyleProp::Dish:{
                    float ym=y+sy;m.box(p.material,cx-0.12f,y,cz-0.12f,cx+0.12f,ym,cz+0.12f);m.box(p.material,cx-0.5f,y,cz-0.5f,cx+0.5f,y+0.3f,cz+0.5f);
                    std::vector<P3> lo,hi;for(int q=0;q<14;++q){float a=6.2831853f*float(q)/14.f;lo.push_back({cx+std::cos(a)*r*0.25f,ym,cz+std::sin(a)*r*0.25f});hi.push_back({cx+std::cos(a)*r,ym+r*0.35f,cz+std::sin(a)*r});}
                    m.prism(p.material,lo,hi);m.box(p.material,cx-0.05f,ym,cz-0.05f,cx+0.05f,ym+r*0.9f,cz+0.05f);
                    if(tip)m.box(p.tip.material,cx-0.09f,ym+r*0.9f,cz-0.09f,cx+0.09f,ym+r*0.9f+p.tip.height,cz+0.09f);break;}
                case BuildingStyleProp::Mast:
                    m.box(p.material,cx-sx/2,y,cz-sz/2,cx+sx/2,y+sy,cz+sz/2);
                    if(tip){float q=std::max(sx,sz)*1.6f;m.box(p.tip.material,cx-q/2,y+sy,cz-q/2,cx+q/2,y+sy+p.tip.height,cz+q/2);}break;
                default:
                    m.prism(p.material,cutRect(at[0],at[1],at[2],at[3],std::min(sx,sz)*0.15f,y),cutRect(at[0]+0.08f,at[1]+0.08f,at[2]-0.08f,at[3]-0.08f,std::min(sx,sz)*0.12f,y+sy));
                    if(tip)m.box(p.tip.material,at[0]+sx*0.2f,y+sy,at[1]+sz*0.2f,at[2]-sx*0.2f,y+sy+p.tip.height,at[3]-sz*0.2f);break;
            }
        }
    }
    // Into world space (quarter-turn yaw about the footprint, then the origin), like dressBuilding.
    auto ox=float(origin.x.toDouble()),oy=float(origin.y.toDouble()),oz=float(origin.z.toDouble());bool first=true;
    for(auto& s:m.v){
        float x=s.pos[0],z=s.pos[2],nx=s.nrm[0],nz=s.nrm[2];
        switch(yaw){case 1:s.pos[0]=D-z;s.pos[2]=x;s.nrm[0]=-nz;s.nrm[2]=nx;break;case 2:s.pos[0]=W-x;s.pos[2]=D-z;s.nrm[0]=-nx;s.nrm[2]=-nz;break;
            case 3:s.pos[0]=z;s.pos[2]=W-x;s.nrm[0]=nz;s.nrm[2]=-nx;break;default:break;}
        s.pos[0]+=ox;s.pos[1]+=oy;s.pos[2]+=oz;
        for(int q=0;q<3;++q){if(first||s.pos[q]<out.mesh.boundsMin[q])out.mesh.boundsMin[q]=s.pos[q];if(first||s.pos[q]>out.mesh.boundsMax[q])out.mesh.boundsMax[q]=s.pos[q];}
        first=false;
    }
    auto Wf=Fixed::fromRatio(int64_t(std::lround(W*1000)),1000),Df=Fixed::fromRatio(int64_t(std::lround(D*1000)),1000);
    for(auto& b:out.boxes){auto p=buildingDetail::turn(b.min,yaw,Wf,Df)+origin,q=buildingDetail::turn(b.max,yaw,Wf,Df)+origin;
        b={{fxMin(p.x,q.x),fxMin(p.y,q.y),fxMin(p.z,q.z)},{fxMax(p.x,q.x),fxMax(p.y,q.y),fxMax(p.z,q.z)}};}
    for(auto& gp:out.gates)gp=buildingDetail::turn(gp,yaw,Wf,Df)+origin;
    out.mesh.vertices=std::move(m.v);
    for(size_t i=0;i<m.perMaterial.size();++i)if(!m.perMaterial[i].empty())out.mesh.parts.push_back({int(i),std::move(m.perMaterial[i])});
    return out;
}

// The requested Width/Height/Depth are bounds on the complete finished mesh.
// Solve the *construction* dimensions, never scale the output geometry.
// Door leaves, hazard strips, support thicknesses and roof props therefore
// retain their world-space dimensions. Unachievable requests fail explicitly.
inline StyledStructure dressStructure(const BuildingStyle& st,const std::string& kindId,
                                     uint64_t seed,Vec3 origin={},uint32_t yaw=0,
                                     StructureSize size={}){
    StyledStructure empty;
    const auto* kind=st.structureKind(kindId);
    if(!kind||yaw>3){empty.fitError="Unknown function or invalid orientation";return empty;}
    if(size.width==0&&size.height==0&&size.depth==0)
        return dressStructureGeometry(st,kindId,seed,origin,yaw);
    if(!std::isfinite(size.width)||!std::isfinite(size.height)||!std::isfinite(size.depth)||
       size.width<std::max(8.f,float(kind->doors)*4.f+4.f)||size.width>160.f||
       size.depth<8.f||size.depth>160.f||size.height<5.f||size.height>80.f){
        empty.fitError="Requested envelope outside allowed size range";
        return empty;
    }

    // The hull occupies less than the final bounds: legs, side modules,
    // chimneys, pylons, towers and foundations project out of the body.
    StructureSize body{std::max(5.f,size.width*0.78f),
                       std::max(2.6f,size.height*0.62f),
                       std::max(5.f,size.depth*0.78f)};
    const float targets[3]={yaw%2?size.depth:size.width,size.height,
                            yaw%2?size.width:size.depth};
    constexpr float tolerance=0.0015f;
    for(int attempt=0;attempt<32;++attempt){
        auto result=dressStructureGeometry(st,kindId,seed,{},yaw,body);
        if(result.mesh.vertices.empty()){
            empty.fitError="No valid raw architecture for the current body dimensions";
            empty.constructionSize=body;empty.fitIterations=unsigned(attempt+1);
            return empty;
        }
        float delta[3]{};
        bool aligned=true;
        for(int i=0;i<3;++i){
            const float extent=result.mesh.boundsMax[i]-result.mesh.boundsMin[i];
            if(extent<=0.f||!std::isfinite(extent)){
                empty.fitError="Degenerate generated geometry";
                return empty;
            }
            delta[i]=targets[i]-extent;
            aligned&=std::abs(delta[i])<=tolerance;
        }
        empty.fitResidual={yaw%2?delta[2]:delta[0],delta[1],yaw%2?delta[0]:delta[2]};
        empty.constructionSize=body;empty.fitIterations=unsigned(attempt+1);
        if(aligned){
            // A minimum practical gate aperture. Use *unscaled* dimensions;
            // a 12-metre envelope cannot magically compress a five-metre
            // industrial door without a physically different door design.
            if(kind->doors){
                const float cut=std::min(kind->chamfer,std::min(body.width,body.depth)*0.22f);
                const float dw=std::min(kind->doorWidth*kind->width,
                         (body.width-2*cut)/float(kind->doors)-0.6f);
                const float dh=std::min(kind->doorHeight*(kind->height.lo+kind->height.hi)*0.5f,
                         body.height-0.8f);
                if(dw<1.6f||dh<2.1f){empty.fitError="Requested size reduces gate clearance below 1.6 by 2.1 metres";return empty;}
            }
            const float translation[3]={
                float(origin.x.toDouble())-result.mesh.boundsMin[0],
                float(origin.y.toDouble())-result.mesh.boundsMin[1],
                float(origin.z.toDouble())-result.mesh.boundsMin[2]
            };
            // Rigid translation only; no alteration of vertex normals or UVs.
            for(auto& v:result.mesh.vertices)
                for(int a=0;a<3;++a)v.pos[a]+=translation[a];
            const Vec3 shift{
                Fixed::fromRatio(int64_t(std::llround(double(translation[0])*1000.0)),1000),
                Fixed::fromRatio(int64_t(std::llround(double(translation[1])*1000.0)),1000),
                Fixed::fromRatio(int64_t(std::llround(double(translation[2])*1000.0)),1000)
            };
            for(auto& box:result.boxes){box.min+=shift;box.max+=shift;}
            for(auto& gate:result.gates)gate+=shift;
            for(int i=0;i<3;++i){
                result.mesh.boundsMin[i]+=translation[i];
                result.mesh.boundsMax[i]+=translation[i];
            }
            result.fitIterations=unsigned(attempt+1);
            return result;
        }
        // Damped constructive fit: the XZ volume depends mainly on its base
        // footprint, with weak cross-axis and protruding module effects.
        // Damping preserves stability around seeded small-prop placement.
        const float xdelta=yaw%2?delta[2]:delta[0];
        const float zdelta=yaw%2?delta[0]:delta[2];
        body.width=std::clamp(body.width+xdelta*0.72f,4.f,180.f);
        body.depth=std::clamp(body.depth+zdelta*0.72f,4.f,180.f);
        body.height=std::clamp(body.height+delta[1]*0.58f,2.4f,90.f);
    }
    empty.fitError="Unable to fit full envelope without deforming geometry";
    return empty; // impossible envelope, discontinuous seeded layout or no convergence
}
}
