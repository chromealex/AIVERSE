// Fixed, read-only geometry for optional native render/physics consumers.
// Coordinates are canonical simulation metres (including body anchor).
#pragma once
#include "GranularSurfaceField.h"
#include "ImprintSurfaceTrails.h"
#include <array>
#include <optional>
#include <vector>

namespace aiverse::lib::worldprocess {
struct PlanetProcessQuad {
    Entity entity=kNullEntity;
    int body=-1,cell=-1;
    std::array<Vec3,4> vertices{}; // two outward-facing triangles: 0,1,2 and 0,2,3
    Vec3 normal{};
    uint16_t column=0,row=0;
};
inline bool orientPlanetQuad(PlanetProcessQuad& q,Vec3 outward){
    const auto n=fxCross(q.vertices[1]-q.vertices[0],q.vertices[2]-q.vertices[0]);
    if(n.lengthSq().raw<=0||outward.lengthSq().raw<=0)return false;
    if(n.dot(outward).raw<0)std::swap(q.vertices[1],q.vertices[3]);
    const auto a=fxCross(q.vertices[1]-q.vertices[0],q.vertices[2]-q.vertices[0]);
    const auto b=fxCross(q.vertices[2]-q.vertices[0],q.vertices[3]-q.vertices[0]);
    if(a.dot(outward).raw<=0||b.dot(outward).raw<=0)return false;
    q.normal=fxNormalize(a+b);return q.normal.lengthSq().raw>0;
}
// A loose-solid vertical section extruded along its transverse tangent.
// Width = section cell size; thickness is an explicit presentation setting.
// Mass fraction changes radial fill height, not physical soil volume.
// Top surfaces only: no walls, overhangs or dynamic grain bodies.
inline std::optional<std::vector<PlanetProcessQuad>> planetGranularTops(
        const World& world,const game::Surface& surface,Entity entity,
        const granular::SurfaceFieldPlacement& placement,int32_t depthCm,
        uint16_t firstX,uint16_t firstRow,uint16_t columns,uint16_t rows,
        uint32_t completedTick){
    const auto* field=world.tryGetConst<granular::Field>(entity);
    const auto* tr=world.tryGetConst<Transform>(entity);
    if(!field||!tr||tr->pos!=placement.origin||completedTick==UINT32_MAX||
       field->sampledTick!=completedTick||depthCm<10||depthCm>10000)return {};
    const auto sampled=granular::sampleSurfaceGrainPatch(world,surface,entity,placement,
        firstX,firstRow,columns,rows);
    const auto frame=surfaceFrame(&surface,placement.origin);
    if(!sampled||!frame)return {};
    const auto radius=surface.bodies[size_t(frame->body)].radius;
    const auto circumference=radius*game::kFxTwoPi;
    if(circumference.raw<=0||
       Fixed::fromRatio(int64_t(field->width)*placement.cellSizeCm,200)>=circumference/4||
       Fixed::fromRatio(depthCm,200)>=circumference/4)return {};
    const auto transverse=fxNormalize(fxCross(frame->up,frame->east));
    if(transverse.lengthSq().raw<=0)return {};
    const auto anchor=game::Surface::anchor(frame->body);
    std::vector<PlanetProcessQuad> output;output.reserve(sampled->size());
    for(uint16_t row=0;row<rows;++row)for(uint16_t col=0;col<columns;++col){
        const auto& grain=(*sampled)[size_t(row)*columns+col];
        if(grain.solid||!grain.mass)continue;
        const auto x=uint16_t(firstX+col),y=uint16_t(firstRow+row);
        const auto height=Fixed::fromRatio(
            int64_t(placement.bottomClearanceCm)+int64_t(field->height-1-y)*placement.cellSizeCm,100)
            +Fixed::fromRatio(placement.cellSizeCm,100)*Fixed::fromRatio(grain.mass,field->capacity);
        PlanetProcessQuad q;q.entity=entity;q.body=grain.body;q.cell=grain.surfaceCell;
        q.column=x;q.row=y;
        for(size_t k=0;k<4;++k){
            const auto edge=int64_t(x)+(k==1||k==2?1:0);
            const auto along=Fixed::fromRatio((2*edge-field->width)*placement.cellSizeCm,200)/circumference;
            const auto across=Fixed::fromRatio((k<2?-1:1)*depthCm,200)/circumference;
            const auto center=frame->up*fxCosTurns(along)+frame->east*fxSinTurns(along);
            const auto dir=fxNormalize(center*fxCosTurns(across)+transverse*fxSinTurns(across));
            const auto p=surface.project(anchor+dir*radius,height);
            const auto actual=surfaceFrame(&surface,p);
            if(!actual||actual->body!=frame->body)return {};
            q.vertices[k]=p;
        }
        if(!orientPlanetQuad(q,grain.up))return {};
        output.push_back(q);
    }
    return output;
}
struct PlanetImprintQuad {
    PlanetProcessQuad geometry;
    imprint::I footprint=0,scent=0,residue=0;
};
// Exact canonical cell footprint, raised radially by the display offset.
// Read one chosen trace entity: host owns visibility, material and batching.
inline std::optional<PlanetImprintQuad> planetImprintQuad(
        const World& world,const game::Surface& surface,Entity entity,
        uint32_t completedTick,Fixed lift=Fixed::fromRatio(1,20)){
    const auto* trace=world.tryGetConst<imprint::Cell>(entity);
    const auto* tr=world.tryGetConst<Transform>(entity);
    if(!trace||!tr||!trace->enabled||!imprint::valid(*trace)||
       completedTick==UINT32_MAX||trace->sampledTick!=completedTick||
       lift<Fixed{}||lift>Fixed::fromInt(1))return {};
    const auto frame=surfaceFrame(&surface,tr->pos);
    if(!frame)return {};
    int body,face,x,y;
    if(!surface.cellFace(frame->cell,body,face,x,y)||body!=frame->body)return {};
    PlanetImprintQuad result;
    auto& q=result.geometry;q.entity=entity;q.body=body;q.cell=frame->cell;
    for(size_t k=0;k<4;++k){
        const auto a=Fixed::fromInt(x+(k==1||k==2?1:0));
        const auto b=Fixed::fromInt(y+(k>=2?1:0));
        const auto local=surface.facePoint(body,face,a,b);
        const auto up=surface.cubeOf(body).dirFromFace(face,a,b);
        q.vertices[k]=game::Surface::anchor(body)+local+up*lift;
    }
    if(!orientPlanetQuad(q,frame->up))return {};
    result.footprint=trace->footprint;result.scent=trace->scent;result.residue=trace->residue;
    return result;
}
} // namespace aiverse::lib::worldprocess
