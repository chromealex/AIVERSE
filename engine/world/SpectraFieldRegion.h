// Shared bounded world-space sampling lattice for visual Data Interfaces.
#pragma once
#include "engine/render/Math3D.h"
#include "engine/core/Fixed.h"
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
namespace aiverse::world {
struct SpectraFieldRegion {
    static constexpr size_t kMaxSamples=4096;
    render::float3 origin{};
    float spacing=16;
    std::array<unsigned,3> dimensions{1,1,1};
    size_t count() const {
        if(!std::isfinite(spacing)||spacing<0.01f||spacing>1e6f)return 0;
        const std::array<float,3> o{origin.x,origin.y,origin.z};
        size_t n=1;
        for(size_t i=0;i<3;++i) {
            const auto d=dimensions[i];
            if(d<1||d>64||n>kMaxSamples/d||!std::isfinite(o[i])||
               std::abs(o[i])>1e7f||std::abs(double(o[i])+double(d-1)*spacing)>1e7)return 0;
            n*=d;
        }
        return n;
    }
    // Requires validated region and valid lattice coordinates.
    std::array<Fixed,3> point(unsigned x,unsigned y,unsigned z) const {
        const auto fixed=[](double v){return Fixed::fromRaw(int64_t(std::llround(v*Fixed::kOne)));};
        return {fixed(double(origin.x)+double(x)*spacing),
                fixed(double(origin.y)+double(y)*spacing),
                fixed(double(origin.z)+double(z)*spacing)};
    }
    bool index(render::float3 p,size_t& out) const {
        const std::array<float,3> o{origin.x,origin.y,origin.z},v{p.x,p.y,p.z};
        std::array<size_t,3> i{};
        for(size_t a=0;a<3;++a) {
            const double t=(double(v[a])-o[a])/spacing;
            if(!std::isfinite(t)||t<-.5||t>=double(dimensions[a])-.5)return false;
            i[a]=size_t(std::floor(t+.5));
        }
        out=(i[2]*dimensions[1]+i[1])*dimensions[0]+i[0];
        return true;
    }
};
}
