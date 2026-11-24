// Planetary GRANULAR vertical-section adapter. The Field owns loose mass;
// Surface owns ground geometry, PHYSIS owns acceleration. This header
// deliberately does not edit planet corners or create another soil store.
#pragma once
#include "WorldProcessGravity.h"
#include "engine/core/FixedMath.h"
#include "engine/sim/CommonComponents.h"
#include <array>

namespace aiverse::granular {
struct SurfaceFieldPlacement {
    Vec3 origin{}; // canonical simulation position at the section's base
    int32_t cellSizeCm=100;
    // Section's bottom boundary above canonical ground, in centimetres.
    int32_t bottomClearanceCm=0;
};
inline bool valid(const SurfaceFieldPlacement& p){
    return p.cellSizeCm>=10&&p.cellSizeCm<=10000&&
        p.bottomClearanceCm>=0&&p.bottomClearanceCm<=20000;
}
struct SurfaceGrainCell {
    int body=-1,surfaceCell=-1;
    Vec3 center{},ground{},up{};
    I mass=0;
    bool solid=false;
};
// Width follows a great circle in the origin's east direction, with
// columns centered about the origin. Row zero is the highest row.
// All rows are a vertical section, NOT an XZ heightfield. Terrain relief
// is obtained from the canonical Surface and altitude is radial.
inline std::optional<SurfaceGrainCell> surfaceGrainCell(
        const game::Surface& surface,const Field& field,
        const SurfaceFieldPlacement& placement,uint16_t x,uint16_t row){
    if(!valid(placement)||!valid(field)||x>=field.width||row>=field.height)return {};
    const auto frame=lib::worldprocess::surfaceFrame(&surface,placement.origin);
    if(!frame)return {};
    const auto radius=surface.bodies[size_t(frame->body)].radius;
    if(radius.raw<=0)return {};
    const auto offset=Fixed::fromRatio(
        (int64_t(2)*x+1-field.width)*placement.cellSizeCm,200);
    const auto turns=offset/(radius*game::kFxTwoPi);
    // One local patch must not wrap around the body or reach its antipode.
    if(fxAbs(turns)>=Fixed::fromRatio(1,4))return {};
    const auto direction=fxNormalize(frame->up*fxCosTurns(turns)+frame->east*fxSinTurns(turns));
    const auto ground=surface.project(game::Surface::anchor(frame->body)+direction*radius);
    const auto height=Fixed::fromRatio(
        int64_t(2)*placement.bottomClearanceCm+
        (int64_t(2)*(field.height-row)-1)*placement.cellSizeCm,200);
    const auto center=ground+direction*height;
    const auto resolved=lib::worldprocess::surfaceFrame(&surface,center);
    if(!resolved||resolved->body!=frame->body)return {};
    const size_t i=size_t(row)*field.width+x;
    return SurfaceGrainCell{frame->body,resolved->cell,center,ground,direction,
        field.mass[i],field.solid[i]!=0};
}
// Bounded patch read. Exact same coordinate mapping for physics and view
// consumers; never derive colliders/mesh from an independent XZ terrain.
// This returns centers and radial bases, not a finished mesh or collider.
inline std::optional<std::vector<SurfaceGrainCell>> sampleSurfaceGrainPatch(
        const World& world,const game::Surface& surface,Entity e,
        const SurfaceFieldPlacement& placement,uint16_t firstX,uint16_t firstRow,
        uint16_t columns,uint16_t rows,size_t maximumCells=1024){
    const auto* field=world.tryGetConst<Field>(e);
    if(!field||!field->enabled||!valid(*field)||!valid(placement)||
       !columns||!rows||columns>32||rows>32||
       size_t(columns)*rows>maximumCells||maximumCells>1024||
       size_t(firstX)+columns>field->width||size_t(firstRow)+rows>field->height)return {};
    std::vector<SurfaceGrainCell> out;
    out.reserve(size_t(columns)*rows);
    for(uint16_t row=0;row<rows;++row)for(uint16_t x=0;x<columns;++x){
        auto cell=surfaceGrainCell(surface,*field,placement,
            uint16_t(firstX+x),uint16_t(firstRow+row));
        if(!cell)return {};
        out.push_back(*cell);
    }
    return out;
}
// Sample the actual PHYSIS field at the authoritative field Transform.
// Units: existing GameCore world unit = metre contract. A custom host
// with a different scale must supply its own resolver instead.
inline std::optional<Vec3> surfaceFieldGravity(const World& read,
        const game::Surface& surface,Entity e){
    const auto* tr=read.tryGetConst<Transform>(e);
    if(!tr)return {};
    const auto frame=lib::worldprocess::surfaceFrame(&surface,tr->pos);
    if(!frame)return {};
    return lib::worldprocess::sampleGravity(read,*frame,tr->pos).local;
}
inline bool advanceSurfaceFields(World& world,const game::Surface& surface,uint32_t tick){
    return lib::worldprocess::advanceGranularWithGravity(world,tick,
        [&](const World& read,Entity e){return surfaceFieldGravity(read,surface,e);});
}
} // namespace aiverse::granular
