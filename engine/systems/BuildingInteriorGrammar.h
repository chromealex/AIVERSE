#pragma once
// Bounded visual grammar. All coordinates are normalized to the authored
// furniture/fixture bounds; geometry does not change gameplay or socket identity.
#include "BuildingStructure.h"
#include "BuildingStructureGraph.h"

namespace aiverse::lib::systems::buildingInteriorDetail {
struct Form {
    buildingStyleDetail::Mesher& mesh;
    float x,y,z,w,h,d;
    void box(int mat,float a,float b,float c,float A,float B,float C){
        mesh.box(mat,x+w*a,y+h*b,z+d*c,x+w*A,y+h*B,z+d*C);
    }
    void ring(int mat,float a,float b,float c,float A,float B,float C,int sides,float scale0=1,float scale1=1,float twist=0){
        using P=buildingStyleDetail::P3;
        std::vector<P> lo,hi;
        const float cx=(a+A)*.5f,cz=(c+C)*.5f,rx=(A-a)*.5f,rz=(C-c)*.5f;
        for(int i=0;i<sides;++i){
            const float t=6.28318530718f*i/sides+twist;
            const float u=std::cos(t),v=std::sin(t);
            lo.push_back({x+w*(cx+rx*u*scale0),y+h*b,z+d*(cz+rz*v*scale0)});
            hi.push_back({x+w*(cx+rx*u*scale1),y+h*B,z+d*(cz+rz*v*scale1)});
        }
        mesh.prism(mat,lo,hi);
    }
    void chamfer(int mat,float a,float b,float c,float A,float B,float C,float cut,float taper=1){
        using P=buildingStyleDetail::P3;
        const float cx=(a+A)*.5f,cz=(c+C)*.5f;
        const float kx=(A-a)*cut,kz=(C-c)*cut;
        const std::array<std::array<float,2>,8> p{{{a+kx,c},{A-kx,c},{A,c+kz},{A,C-kz},{A-kx,C},{a+kx,C},{a,C-kz},{a,c+kz}}};
        std::vector<P> lo,hi;
        for(auto q:p){lo.push_back({x+w*q[0],y+h*b,z+d*q[1]});hi.push_back({x+w*(cx+(q[0]-cx)*taper),y+h*B,z+d*(cz+(q[1]-cz)*taper)});}
        mesh.prism(mat,lo,hi);
    }
    void beam(int mat,float a,float b,float c,float A,float B,float C,float width){
        // Flat-ended inclined beam in X/Y, extruded along Z.
        using P=buildingStyleDetail::P3;
        const float dx=A-a,dy=B-b,l=std::sqrt(dx*dx+dy*dy);
        if(l<.0001f)return;
        const float u=-dy/l*width*.5f,v=dx/l*width*.5f;
        std::vector<P> p,q;
        for(auto t:std::array<std::array<float,2>,4>{{{a+u,b+v},{A+u,B+v},{A-u,B-v},{a-u,b-v}}}){
            p.push_back({x+w*std::clamp(t[0],0.f,1.f),y+h*std::clamp(t[1],0.f,1.f),z+d*c});
            q.push_back({x+w*std::clamp(t[0],0.f,1.f),y+h*std::clamp(t[1],0.f,1.f),z+d*C});
        }
        mesh.prism(mat,p,q);
    }
};
inline void surface(Form& f,InteriorForm form,int mat,float a,float b,float c,float A,float B,float C){
    switch(form){
        case InteriorForm::Organic: f.ring(mat,a,b,c,A,B,C,12,.82f,1);break;
        case InteriorForm::Faceted:f.chamfer(mat,a,b,c,A,B,C,.24f,.9f);break;
        case InteriorForm::Pressure:f.chamfer(mat,a,b,c,A,B,C,.32f);break;
        case InteriorForm::Layered:f.chamfer(mat,a,b,c,A,B,C,.1f);break;
        case InteriorForm::Monumental:f.chamfer(mat,a,b,c,A,B,C,.08f,.94f);break;
        default:f.box(mat,a,b,c,A,B,C);break;
    }
}
inline void supports(Form& f,InteriorSupport support,int mat,float top){
    switch(support){
        case InteriorSupport::Trestle:
            for(float z:{.14f,.76f}){f.box(mat,.1f,0,z,.9f,.13f,z+.1f);f.beam(mat,.17f,.08f,z,.42f,top,z+.1f,.12f);f.beam(mat,.83f,.08f,z,.58f,top,z+.1f,.12f);}
            f.box(mat,.43f,.22f,.18f,.57f,.34f,.82f);break;
        case InteriorSupport::Buttress:
            f.chamfer(mat,.1f,0,.1f,.32f,top,.9f,.1f,.58f);f.chamfer(mat,.68f,0,.1f,.9f,top,.9f,.1f,.58f);break;
        case InteriorSupport::Pipe:
            for(float x:{.04f,.78f})for(float z:{.05f,.76f})f.ring(mat,x,0,z,x+.18f,top,z+.18f,8);
            f.box(mat,.1f,.1f,.12f,.9f,.2f,.2f);break;
        case InteriorSupport::Rail:
            for(float x:{.07f,.75f}){f.box(mat,x,0,.06f,x+.18f,.13f,.94f);f.beam(mat,x+.09f,.13f,.38f,x+.2f,top,.62f,.13f);}break;
        case InteriorSupport::Pedestal:
            f.chamfer(mat,.24f,0,.18f,.76f,.14f,.82f,.25f);f.chamfer(mat,.32f,.14f,.32f,.68f,top,.68f,.25f,.88f);break;
        case InteriorSupport::Organic:
            f.ring(mat,.12f,0,.12f,.88f,top,.88f,10,.56f,.96f);break;
        case InteriorSupport::Salvage:
            for(float z:{.1f,.79f}){f.beam(mat,.12f,0,z,.8f,top,z+.1f,.1f);f.beam(mat,.87f,0,z,.2f,top,z+.1f,.1f);}break;
        case InteriorSupport::Column:
            for(float x:{.08f,.7f})for(float z:{.06f,.74f}){f.ring(mat,x,0,z,x+.22f,.12f,z+.2f,8);f.ring(mat,x+.025f,.12f,z+.025f,x+.195f,top,z+.175f,8,.95f,.78f);}break;
        case InteriorSupport::Plinth:
            f.box(mat,.06f,0,.05f,.94f,.18f,.95f);f.box(mat,.13f,.18f,.12f,.87f,top,.88f);break;
        case InteriorSupport::Sled:
            for(float z:{.1f,.78f}){f.chamfer(mat,.06f,0,z,.94f,.1f,z+.12f,.22f);f.beam(mat,.2f,.1f,z,.38f,top,z+.12f,.12f);f.beam(mat,.8f,.1f,z,.62f,top,z+.12f,.12f);}break;
        default:
            for(float a:{.04f,.84f})for(float c:{.06f,.8f})f.box(mat,a,0,c,a+.12f,top,c+.14f);break;
    }
}
inline void furniture(Form& f,const BuildingStyle& style,BuildingFurnitureKind kind,int finish,int detail,int dark){
    const auto& g=style.interiorGrammar;
    if(g.finish>=0)finish=g.finish;if(g.frame>=0)detail=g.frame;
    const int accent=g.accent>=0?g.accent:detail;
    using K=BuildingFurnitureKind;
    auto shell=[&](int m,float a,float b,float c,float A,float B,float C){surface(f,g.form,m,a,b,c,A,B,C);};
    if(kind==K::Desk||kind==K::Table||kind==K::Bench){
        const float top=kind==K::Bench?.77f:.82f;
        supports(f,g.support,detail,top);
        shell(finish,0,top,0,1,1,1);
        if(g.form==InteriorForm::Layered)f.box(accent,.04f,top-.05f,.04f,.96f,top,.96f);
    }else if(kind==K::Bed){
        supports(f,g.support,detail,.34f);
        shell(detail,0,.34f,0,1,.51f,1);shell(finish,.04f,.51f,.04f,.96f,.74f,.96f);
        shell(accent,.07f,.74f,.07f,.93f,.86f,.29f);shell(detail,0,.34f,0,1,1,.1f);
    }else if(kind==K::Sofa){
        supports(f,g.support,detail,.15f);
        shell(finish,0,.15f,0,1,.49f,1);shell(detail,0,.49f,0,1,1,.21f);
        shell(accent,0,.49f,.21f,.14f,.8f,1);shell(accent,.86f,.49f,.21f,1,.8f,1);
    }else if(kind==K::Shelf){
        supports(f,g.support,detail,.12f);
        shell(detail,0,.12f,0,.12f,1,1);shell(detail,.88f,.12f,0,1,1,1);
        for(float b:{.12f,.39f,.66f,.94f})shell(finish,0,b,0,1,b+.06f,1);
    }else{
        supports(f,g.support,detail,.12f);shell(finish,0,.12f,0,1,.91f,1);
        shell(detail,0,.91f,0,1,1,1);
        // The front panel is inset into the silhouette, never a detached prop.
        if(kind==K::Machine){f.box(dark,.22f,.34f,.015f,.78f,.63f,.11f);f.box(accent,.3f,.43f,.005f,.7f,.53f,.025f);}
        else f.box(accent,.28f,.43f,.015f,.72f,.49f,.1f);
    }
    // Style-specific framing stays within the conservative object AABB.
    if(g.form==InteriorForm::Ribbed)for(float z:{.22f,.48f,.74f})f.box(accent,.02f,.18f,z,.07f,.79f,z+.06f);
    if(g.form==InteriorForm::Layered)f.box(accent,.1f,.12f,.03f,.9f,.19f,.09f);
}
inline void fixture(Form& f,const BuildingStyle& style,int frame,int glow){
    const auto& g=style.interiorGrammar;
    if(g.frame>=0)frame=g.frame;if(g.emitter>=0)glow=g.emitter;
    f.box(frame,.28f,.75f,.28f,.72f,1,.72f);
    switch(g.fixture){
        case InteriorFixture::Lantern:
            f.chamfer(frame,.12f,.03f,.12f,.88f,.24f,.88f,.16f);f.chamfer(glow,.21f,.24f,.21f,.79f,.72f,.79f,.12f);
            for(float x:{.14f,.78f})for(float z:{.14f,.78f})f.box(frame,x,.18f,z,x+.08f,.78f,z+.08f);break;
        case InteriorFixture::Ring:f.ring(frame,.04f,.38f,.04f,.96f,.74f,.96f,12);f.ring(glow,.12f,.12f,.12f,.88f,.38f,.88f,12,.8f,1);break;
        case InteriorFixture::Strip:
            f.box(frame,.02f,.42f,.26f,.98f,.76f,.74f);f.box(glow,.06f,.16f,.38f,.94f,.42f,.62f);break;
        case InteriorFixture::Cage:
            f.ring(glow,.23f,.14f,.23f,.77f,.75f,.77f,8);
            for(float x:{.14f,.8f})for(float z:{.14f,.8f})f.box(frame,x,.08f,z,x+.06f,.82f,z+.06f);
            f.chamfer(frame,.1f,0,.1f,.9f,.12f,.9f,.2f);break;
        case InteriorFixture::Orb:f.ring(glow,.08f,0,.08f,.92f,.38f,.92f,12,.25f,1);f.ring(glow,.08f,.38f,.08f,.92f,.8f,.92f,12,1,.38f);break;
        case InteriorFixture::Layered:
            for(int i=0;i<3;++i){const float t=.06f+i*.12f;f.chamfer(i==1?glow:frame,t,.12f+i*.2f,t,1-t,.32f+i*.2f,1-t,.1f);}break;
        default:f.box(frame,0,.48f,0,1,1,1);f.box(glow,.12f,.06f,.12f,.88f,.48f,.88f);break;
    }
}
inline void decor(Form& f,const BuildingStyle& style,int frame,int accent){
    const auto& g=style.interiorGrammar;
    if(g.frame>=0)frame=g.frame;if(g.accent>=0)accent=g.accent;
    f.chamfer(frame,.04f,0,.04f,.96f,.13f,.96f,.13f);
    switch(g.decor){
        case InteriorDecor::Vessel:f.ring(accent,.08f,.13f,.08f,.92f,.44f,.92f,10,.6f,1);f.ring(accent,.08f,.44f,.08f,.92f,.9f,.92f,10,1,.56f);break;
        case InteriorDecor::Crystal:f.ring(accent,.12f,.13f,.12f,.88f,.72f,.88f,5,.7f,1);f.ring(accent,.12f,.72f,.12f,.88f,1,.88f,5,1,.04f);break;
        case InteriorDecor::Gauge:f.box(frame,.4f,.13f,.4f,.6f,.65f,.6f);f.chamfer(accent,.08f,.5f,.18f,.92f,1,.82f,.25f);break;
        case InteriorDecor::Terminal:f.chamfer(frame,.12f,.13f,.1f,.88f,.82f,.9f,.16f,.8f);f.box(accent,.26f,.35f,.04f,.74f,.67f,.12f);break;
        case InteriorDecor::Plant:f.ring(frame,.16f,.13f,.16f,.84f,.48f,.84f,8,.8f,1);f.ring(accent,.06f,.48f,.06f,.94f,.94f,.94f,7,.3f,1);break;
        case InteriorDecor::Relic:f.chamfer(accent,.18f,.13f,.18f,.82f,.48f,.82f,.12f,.7f);f.ring(accent,.22f,.48f,.22f,.78f,.96f,.78f,4,1,.3f,.785f);break;
        case InteriorDecor::Stack:for(int i=0;i<3;++i){float t=.04f+i*.08f;f.box(i%2?frame:accent,t,.13f+i*.23f,t,1-t,.36f+i*.23f,1-t);}break;
        default:f.box(frame,.35f,.13f,.35f,.65f,.71f,.65f);f.box(accent,.05f,.71f,.05f,.95f,1,.95f);break;
    }
}
} // namespace aiverse::lib::systems::buildingInteriorDetail
