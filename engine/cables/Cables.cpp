#include "Cables.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace aiverse::cables {
namespace {
using namespace render;
constexpr float pi = 3.14159265358979323846f;
void check(bool ok, const char *message) {
    if (!ok) throw std::invalid_argument(std::string("cable: ") + message);
}
bool finite(float3 v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z) &&
           std::max({std::abs(v.x), std::abs(v.y), std::abs(v.z)}) <= 100000;
}
float3 perpendicular(float3 t) {
    return normalize(cross(std::abs(t.y) < .9f ? float3{0,1,0} : float3{1,0,0}, t));
}
}
Mesh build(const Options &o, const std::atomic<bool> *cancel) {
    check(o.anchors.size() >= 2 && o.anchors.size() <= 128, "anchors must contain 2..128 points");
    check(o.segments >= 2 && o.segments <= 256, "segments must be 2..256");
    check(o.sides >= 3 && o.sides <= 32, "sides must be 3..32");
    check(o.strands >= 1 && o.strands <= 8, "strands must be 1..8");
    check(std::isfinite(o.radius) && o.radius >= .0001f && o.radius <= 10, "invalid radius");
    check(std::isfinite(o.sag) && o.sag >= 0 && o.sag <= 10000, "invalid sag");
    check(std::isfinite(o.uvRepeat) && o.uvRepeat >= .001f && o.uvRepeat <= 10000, "invalid UV repeat");
    check(std::isfinite(o.spacing) && o.spacing >= 0 && o.spacing <= 100, "invalid spacing");
    check(o.strands == 1 || o.spacing >= 2*o.radius, "bundle spacing must exceed wire diameter");
    check(finite(o.down) && length(o.down) > 1e-6f, "invalid down direction");
    check(finite(o.bundleAxis) && length(o.bundleAxis) > 1e-6f, "invalid bundle axis");
    for (auto a : o.anchors) check(finite(a), "invalid anchor");
    auto down = normalize(o.down), axis = normalize(o.bundleAxis);
    for (size_t i=1; i<o.anchors.size(); ++i) {
        auto d = o.anchors[i]-o.anchors[i-1];
        check(length(d) >= .001f, "duplicate or too close anchors");
        check(o.sag == 0 || length(cross(normalize(d), down)) > .001f,
              "vertical spans require zero sag (avoid a folded cable)");
    }
    size_t spans=(o.anchors.size()-1)*o.strands;
    size_t vertexCount=spans*((o.segments+1)*(o.sides+1)+(o.caps?2*(o.sides+2):0));
    size_t indexCount=spans*(o.segments*o.sides*6+(o.caps?o.sides*6:0));
    check(vertexCount <= 500000 && indexCount <= 1500000, "mesh budget exceeded");
    Mesh m; m.vertices.reserve(vertexCount); m.indices.reserve(indexCount);
    for (unsigned strand=0; strand<o.strands; ++strand) {
        float distance=0;
        auto offset=axis*((float(strand)-float(o.strands-1)*.5f)*o.spacing);
        for (size_t span=1; span<o.anchors.size(); ++span) {
            if (cancel && cancel->load()) throw std::runtime_error("cable: build cancelled");
            auto a=o.anchors[span-1]+offset, b=o.anchors[span]+offset, d=b-a;
            auto tangent=[&](float t){return normalize(d+down*(4*o.sag*(1-2*t)));};
            auto point=[&](float t){return a+d*t+down*(4*o.sag*t*(1-t));};
            uint32_t start=uint32_t(m.vertices.size());
            auto u=perpendicular(tangent(0)); auto previous=a;
            for (unsigned j=0; j<=o.segments; ++j) {
                float t=float(j)/float(o.segments); auto centre=point(t), dir=tangent(t);
                auto projected=u-dir*dot(u,dir);
                u=length(projected)>.0001f?normalize(projected):perpendicular(dir);
                auto v=cross(dir,u);
                if (j) {float step=length(centre-previous); distance+=step; m.length+=step;}
                previous=centre;
                for (unsigned k=0; k<=o.sides; ++k) {
                    float angle=2*pi*float(k)/float(o.sides);
                    auto normal=u*std::cos(angle)+v*std::sin(angle);
                    m.vertices.push_back({centre+normal*o.radius,normal,{distance/o.uvRepeat,float(k)/float(o.sides)}});
                }
            }
            for (unsigned j=0; j<o.segments; ++j) for (unsigned k=0; k<o.sides; ++k) {
                uint32_t x=start+j*(o.sides+1)+k, y=x+o.sides+1;
                for (uint32_t ix : {x,x+1,y+1,x,y+1,y}) m.indices.push_back(ix);
            }
            if (o.caps) for (unsigned end=0; end<2; ++end) {
                auto dir=tangent(float(end))*(end?1.f:-1.f);
                uint32_t centre=uint32_t(m.vertices.size()), ring=start+(end?o.segments*(o.sides+1):0);
                m.vertices.push_back({end?b:a,dir,{.5f,.5f}});
                for (unsigned k=0; k<=o.sides; ++k) {
                    float angle=2*pi*float(k)/float(o.sides);
                    m.vertices.push_back({m.vertices[ring+k].position,dir,{.5f+.5f*std::cos(angle),.5f+.5f*std::sin(angle)}});
                }
                for (unsigned k=0; k<o.sides; ++k) {
                    m.indices.push_back(centre);
                    m.indices.push_back(centre+1+k+(end?0:1));
                    m.indices.push_back(centre+1+k+(end?1:0));
                }
            }
        }
    }
    return m;
}
} // namespace aiverse::cables
