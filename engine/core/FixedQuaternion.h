#pragma once
#include "FixedMath.h"
#include "FixedInterpolation.h"
namespace aiverse {
struct FixedQuaternion {Fixed x{},y{},z{},w=Fixed::fromInt(1);};
inline bool fxQuaternionBounded(FixedQuaternion q){
    constexpr auto l=Fixed::kOne*2;
    return q.x.raw>=-l&&q.x.raw<=l&&q.y.raw>=-l&&q.y.raw<=l&&q.z.raw>=-l&&q.z.raw<=l&&q.w.raw>=-l&&q.w.raw<=l;
}
inline std::optional<FixedQuaternion> fxQuaternionNormalize(FixedQuaternion q){
    if(!fxQuaternionBounded(q))return {};
    auto n=fxSqrt(q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w);
    if(n.raw<256)return {};
    return FixedQuaternion{q.x/n,q.y/n,q.z/n,q.w/n};
}
inline std::optional<FixedQuaternion> fxQuaternionMultiply(FixedQuaternion a,FixedQuaternion b){
    auto x=fxQuaternionNormalize(a),y=fxQuaternionNormalize(b);if(!x||!y)return {};
    a=*x;b=*y;
    return fxQuaternionNormalize({a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,
        a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,
        a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w,
        a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z});
}
inline std::optional<FixedQuaternion> fxQuaternionAxisAngle(Vec3 axis,Fixed turns){
    constexpr auto l=Fixed::kOne*1024;
    for(auto v:{axis.x,axis.y,axis.z})if(v.raw < -l||v.raw>l)return {};
    auto n=axis.length();if(n.raw<256)return {};
    axis=axis/n;
    auto half=Fixed::fromRaw((turns.raw%(Fixed::kOne*2))/2);
    auto s=fxSinTurns(half);
    return fxQuaternionNormalize({axis.x*s,axis.y*s,axis.z*s,fxCosTurns(half)});
}
inline std::optional<FixedQuaternion> fxQuaternionNlerp(FixedQuaternion a,FixedQuaternion b,Fixed t){
    auto x=fxQuaternionNormalize(a),y=fxQuaternionNormalize(b);if(!x||!y)return {};
    a=*x;b=*y;
    if((a.x*b.x+a.y*b.y+a.z*b.z+a.w*b.w).raw<0)b={-b.x,-b.y,-b.z,-b.w};
    return fxQuaternionNormalize({*fxLerpBounded(a.x,b.x,t),*fxLerpBounded(a.y,b.y,t),*fxLerpBounded(a.z,b.z,t),*fxLerpBounded(a.w,b.w,t)});
}
inline std::optional<Vec3> fxQuaternionRotate(FixedQuaternion q,Vec3 v){
    auto n=fxQuaternionNormalize(q);if(!n)return {};
    constexpr auto l=int64_t(1)<<40;
    for(auto c:{v.x,v.y,v.z})if(c.raw < -l||c.raw>l)return {};
    Vec3 u{n->x,n->y,n->z};auto t=fxCross(u,v)*Fixed::fromInt(2);
    return v+t*n->w+fxCross(u,t);
}
}
