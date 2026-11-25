#pragma once
#include "FixedMath.h"
#include "FixedInterpolation.h"
#include <initializer_list>
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

inline std::optional<FixedQuaternion> fxQuaternionInverse(FixedQuaternion q){
    auto n=fxQuaternionNormalize(q);if(!n)return {};
    return FixedQuaternion{-n->x,-n->y,-n->z,n->w};
}
inline std::optional<FixedQuaternion> fxQuaternionSlerp(FixedQuaternion a,FixedQuaternion b,Fixed t){
    auto x=fxQuaternionNormalize(a),y=fxQuaternionNormalize(b);if(!x||!y)return {};
    a=*x;b=*y;auto one=Fixed::fromInt(1);t=fxClamp(t,Fixed{},one);
    auto d=a.x*b.x+a.y*b.y+a.z*b.z+a.w*b.w;
    if(d.raw<0){b={-b.x,-b.y,-b.z,-b.w};d=-d;}
    d=fxClamp(d,Fixed{},one);
    if(d.raw>=Fixed::kOne-128)return fxQuaternionNlerp(a,b,t);
    auto angle=fxAcosTurns(d),den=fxSinTurns(angle);
    if(den.raw<=128)return fxQuaternionNlerp(a,b,t);
    auto u=fxSinTurns(angle*(one-t))/den,v=fxSinTurns(angle*t)/den;
    return fxQuaternionNormalize({a.x*u+b.x*v,a.y*u+b.y*v,a.z*u+b.z*v,a.w*u+b.w*v});
}
// Right-handed basis columns; +Z forward, +Y up.
struct FixedRotationBasis {Vec3 right,up,forward;};
inline std::optional<FixedRotationBasis> fxQuaternionBasis(FixedQuaternion q){
    auto x=fxQuaternionRotate(q,{1_fx,0_fx,0_fx}),y=fxQuaternionRotate(q,{0_fx,1_fx,0_fx}),z=fxQuaternionRotate(q,{0_fx,0_fx,1_fx});
    if(!x||!y||!z)return {};return FixedRotationBasis{*x,*y,*z};
}
// Converts an approximately orthonormal right-handed basis to a rotation.
inline std::optional<FixedQuaternion> fxQuaternionFromBasis(FixedRotationBasis b){
    auto x=b.right,y=b.up,z=b.forward;
    for(auto v:{x.x,x.y,x.z,y.x,y.y,y.z,z.x,z.y,z.z})if(v.raw < -2*Fixed::kOne||v.raw>2*Fixed::kOne)return {};
    auto one=1_fx,eps=Fixed::fromRatio(1,100);
    for(auto v:{x,y,z})if(fxAbs(v.lengthSq()-one)>eps)return {};
    if(fxAbs(x.dot(y))>eps||fxAbs(x.dot(z))>eps||fxAbs(y.dot(z))>eps||fxAbs(fxCross(x,y).dot(z)-one)>eps)return {};
    auto trace=x.x+y.y+z.z;FixedQuaternion q;Fixed s;
    if(trace.raw>0){
        s=fxSqrt(one+trace)*2;if(s.raw<=128)return {};
        q={(y.z-z.y)/s,(z.x-x.z)/s,(x.y-y.x)/s,s/4};
    }else if(x.x>=y.y&&x.x>=z.z){
        s=fxSqrt(fxMax(Fixed{},one+x.x-y.y-z.z))*2;if(s.raw<=128)return {};
        q={s/4,(y.x+x.y)/s,(z.x+x.z)/s,(y.z-z.y)/s};
    }else if(y.y>=z.z){
        s=fxSqrt(fxMax(Fixed{},one+y.y-x.x-z.z))*2;if(s.raw<=128)return {};
        q={(y.x+x.y)/s,s/4,(z.y+y.z)/s,(z.x-x.z)/s};
    }else{
        s=fxSqrt(fxMax(Fixed{},one+z.z-x.x-y.y))*2;if(s.raw<=128)return {};
        q={(z.x+x.z)/s,(z.y+y.z)/s,s/4,(x.y-y.x)/s};
    }
    return fxQuaternionNormalize(q);
}
inline std::optional<FixedQuaternion> fxQuaternionLookRotation(Vec3 forward,Vec3 up){
    constexpr auto l=Fixed::kOne*1024;
    for(auto v:{forward.x,forward.y,forward.z,up.x,up.y,up.z})if(v.raw < -l||v.raw>l)return {};
    auto a=forward.length(),b=up.length();if(a.raw<256||b.raw<256)return {};
    auto z=forward/a,x=fxCross(up/b,z);auto n=x.length();if(n.raw<256)return {};
    x=x/n;auto y=fxCross(z,x);return fxQuaternionFromBasis({x,y,z});
}
}
