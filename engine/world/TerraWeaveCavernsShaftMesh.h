#pragma once
// CPU-only transition shaft wall, not a second surface renderer.
// Top ring uses terrain height callback; authored triangle planes may differ
// and require seam validation against CutSurface before publication.
#include "engine/world/TerraWeaveCavernsEntrances.h"
#include <cmath>
#include <limits>
namespace aiverse::terraweave::caverns {
struct ShaftVertexCm {
    double x=0,y=0,z=0;
};
struct ShaftSurface {
    uint64_t entranceId=0;
    std::vector<ShaftVertexCm> vertices;
    std::vector<uint32_t> indices;
};
template<class HeightAt>
ShaftSurface makeEntranceShaftSurface(const Entrance& entrance,HeightAt heightAt,
                                    uint32_t sides=16,uint32_t verticalSections=8) {
    if(entrance.radiusCm<1||entrance.surfaceYcm<entrance.caveYcm||
       sides<6||sides>64||verticalSections<1||verticalSections>256)
        throw std::invalid_argument("Caverns shaft surface settings");
    constexpr double pi=3.1415926535897932384626433832795;
    ShaftSurface out;out.entranceId=entrance.id;
    const size_t count=size_t(sides)*(verticalSections+1u);
    out.vertices.reserve(count);out.indices.reserve(size_t(sides)*verticalSections*6);
    for(uint32_t row=0;row<=verticalSections;++row) {
        const double t=double(row)/verticalSections;
        for(uint32_t side=0;side<sides;++side){
            const double angle=2*pi*double(side)/sides;
            const double x=double(entrance.xCm)+double(entrance.radiusCm)*std::cos(angle);
            const double z=double(entrance.zCm)+double(entrance.radiusCm)*std::sin(angle);
            const int64_t sx=int64_t(std::llround(x)),sz=int64_t(std::llround(z));
            const double topY=double(heightAt(sx,sz));
            const double bottomY=double(entrance.caveYcm);
            const double y=topY+(bottomY-topY)*t;
            out.vertices.push_back({x,y,z});
        }
    }
    for(uint32_t row=0;row<verticalSections;++row)
    for(uint32_t side=0;side<sides;++side) {
        const uint32_t next=(side+1)%sides;
        const uint32_t a=row*sides+side,b=row*sides+next;
        const uint32_t c=(row+1)*sides+side,d=(row+1)*sides+next;
        out.indices.insert(out.indices.end(),{a,b,c,b,d,c});
    }
    return out;
}
} // namespace aiverse::terraweave::caverns
