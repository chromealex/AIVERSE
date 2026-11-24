#pragma once
#include "engine/mechanics/Namespace.h"
// Shared helpers of the battle-royale package (genre/royale/): seeded points in discs, circles and bounds.
// Everything is integer / Fixed (simulation-safe). Policies keep their own PCG32 stream in their state, so
// the result depends only on the seed and the reported inputs, never on call order of other systems.
#include "engine/core/Fixed.h"
#include "engine/core/FixedMath.h"
#include "engine/core/Random.h"
#include <cstdint>
#include <optional>
#include <vector>
namespace aiverse::lib::mechanics {
inline constexpr uint32_t kRoyaleMaxTicks=100000000;      // ~38 days at 30 Hz
inline constexpr int64_t kRoyaleMaxCoord=1000000;          // metres
inline constexpr std::size_t kRoyaleMaxPhases=32;
inline constexpr std::size_t kRoyaleMaxCompetitors=1024;

// A circle on the ground plane (x = world X, y = world Z). Touching the edge counts as inside.
struct RoyaleCircle{Vec2 center;Fixed radius;bool operator==(const RoyaleCircle&)const=default;};
struct RoyaleBounds{Vec2 min,max;bool operator==(const RoyaleBounds&)const=default;};
struct RoyaleRng{uint64_t state=0,inc=0;bool operator==(const RoyaleRng&)const=default;};

namespace royaleDetail {
inline bool coordOk(Fixed v){return v>=Fixed::fromInt(-kRoyaleMaxCoord)&&v<=Fixed::fromInt(kRoyaleMaxCoord);}
inline bool pointOk(Vec2 p){return coordOk(p.x)&&coordOk(p.y);}
inline bool pointOk(Vec3 p){return coordOk(p.x)&&coordOk(p.y)&&coordOk(p.z);}
inline bool radiusOk(Fixed r){return r>=Fixed{}&&r<=Fixed::fromInt(kRoyaleMaxCoord);}
inline bool nextTick(bool initialized,uint32_t last,uint32_t tick){return tick<=kRoyaleMaxTicks&&(!initialized||tick>last);}
inline Random rng(const RoyaleRng& r){Random g;g.restore(r.state,r.inc);return g;}
inline RoyaleRng store(const Random& g){return {g.state(),g.inc()};}
inline RoyaleRng seeded(uint64_t seed,uint64_t stream){Random g(seed,stream);return store(g);}
// Uniform point in the unit disc (rejection sampling, bounded tries; the centre is the deterministic fallback).
inline Vec2 unitDisc(Random& g){
    for(int i=0;i<24;++i){Vec2 p{g.unit()*2-1_fx,g.unit()*2-1_fx};if(p.lengthSq()<=1_fx)return p;}
    return {};
}
inline Vec2 clampTo(Vec2 p,const RoyaleBounds& b){return {fxClamp(p.x,b.min.x,b.max.x),fxClamp(p.y,b.min.y,b.max.y)};}
inline Vec2 lerp(Vec2 a,Vec2 b,Fixed t){return a+(b-a)*t;}
inline Vec3 lerp(Vec3 a,Vec3 b,Fixed t){return a+(b-a)*t;}
inline Fixed lerp(Fixed a,Fixed b,Fixed t){return a+(b-a)*t;}
}
inline bool valid(const RoyaleBounds& b){return royaleDetail::pointOk(b.min)&&royaleDetail::pointOk(b.max)&&b.min.x<b.max.x&&b.min.y<b.max.y;}
inline bool valid(const RoyaleCircle& c){return royaleDetail::pointOk(c.center)&&royaleDetail::radiusOk(c.radius);}
inline bool insideCircle(const RoyaleCircle& c,Vec2 p){auto d=p-c.center;return d.lengthSq()<=c.radius*c.radius;}
inline bool insideCircle(const RoyaleCircle& c,Vec3 p){return insideCircle(c,p.xz());}
// Distance from the edge to an outside point (0 inside): HUD arrows, bots, damage falloff.
inline Fixed distanceOutside(const RoyaleCircle& c,Vec2 p){auto d=(p-c.center).length();return d>c.radius?d-c.radius:Fixed{};}
}
