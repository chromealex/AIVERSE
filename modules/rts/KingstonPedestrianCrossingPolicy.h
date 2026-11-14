#pragma once
#include "modules/rts/RtsDriving.h"
namespace kingston {
// Relative swept rectangle. Horizon includes the time needed to clear the lane.
inline bool crossingDanger(aiverse::Vec2 at,aiverse::Vec2 relativeVelocity,aiverse::Vec2 forward,
 aiverse::Fixed halfLength,aiverse::Fixed halfWidth,aiverse::Fixed radius,aiverse::Fixed horizon){
 using namespace aiverse;Fixed lo{},hi=horizon;Vec2 side{-forward.y,forward.x};
 auto slab=[&](Fixed p,Fixed v,Fixed extent){if(!v.raw)return fxAbs(p)<extent;auto a=(-extent-p)/v,b=(extent-p)/v;if(a>b)std::swap(a,b);lo=fxMax(lo,a);hi=fxMin(hi,b);return lo<hi;};
 return slab(at.dot(forward),relativeVelocity.dot(forward),halfLength+radius)&&slab(at.dot(side),relativeVelocity.dot(side),halfWidth+radius)&&hi>Fixed{}&&lo<horizon;
}
}
