#pragma once
#include "GeometryContactCache.h"
namespace aiverse::lib::algorithms {
struct SweepHit {double enter,exit;Point3d normal;};
inline std::optional<SweepHit> sweepBoxes(Box3d a,Point3d va,Box3d b,Point3d vb,double maxTime){
    if(!valid(a)||!valid(b)||!finite3(va)||!finite3(vb)||!std::isfinite(maxTime)||maxTime<0)return {};
    auto v=va-vb;double lo=0,hi=maxTime;Point3d normal{};
    auto axis=[&](double amin,double amax,double bmin,double bmax,double speed,Point3d n){
        if(speed==0)return amin<=bmax&&bmin<=amax;
        double t=(bmin-amax)/speed,u=(bmax-amin)/speed;
        if(t>u)std::swap(t,u);
        if(t>lo){lo=t;normal=n*(speed>0?1.:-1.);}
        hi=std::min(hi,u);return lo<=hi;
    };
    if(!axis(a.min.x,a.max.x,b.min.x,b.max.x,v.x,{1,0,0})||
       !axis(a.min.y,a.max.y,b.min.y,b.max.y,v.y,{0,1,0})||
       !axis(a.min.z,a.max.z,b.min.z,b.max.z,v.z,{0,0,1}))return {};
    return SweepHit{lo,hi,normal};
}
// Exact translational SAT in real arithmetic, subject to floating rounding.
// Caller supplies complete separating axes: face normals of both convex
// polyhedra and cross products of their edge directions. No rotation.
inline std::optional<SweepHit> sweepConvexAxes(std::span<const Point3d> a,std::span<const Point3d> b,std::span<const Point3d> axes,Point3d relativeVelocity,double maxTime){
    if(a.empty()||b.empty()||axes.empty()||a.size()>65536||b.size()>65536||axes.size()>65536||!finite3(relativeVelocity)||!std::isfinite(maxTime)||maxTime<0)return {};
    for(auto p:a)if(!finite3(p))return {};
    for(auto p:b)if(!finite3(p))return {};
    double lo=0,hi=maxTime;Point3d normal{};bool used=false;
    for(auto n:axes){
        if(!finite3(n))return {};
        double len=std::sqrt(dot(n,n));if(len==0)continue;used=true;n=n*(1/len);
        auto project=[&](std::span<const Point3d> p){double l=dot(p.front(),n),h=l;for(auto x:p){double d=dot(x,n);l=std::min(l,d);h=std::max(h,d);}return std::pair<double,double>{l,h};};
        auto [al,ah]=project(a);auto [bl,bh]=project(b);double speed=dot(relativeVelocity,n);
        if(speed==0){if(ah<bl||bh<al)return {};continue;}
        double t=(bl-ah)/speed,u=(bh-al)/speed;if(t>u)std::swap(t,u);
        if(t>lo){lo=t;normal=n*(speed>0?1.:-1.);}hi=std::min(hi,u);if(lo>hi)return {};
    }
    if(!used)return {};return SweepHit{lo,hi,normal};
}
// Nondegenerate triangle intersection, including coplanar cases.
// Degenerate triangles use the existing convex query classification.
inline ConvexQuery trianglesIntersect(Triangle3d a,Triangle3d b){
    std::array<Point3d,3> av{a.a,a.b,a.c},bv{b.a,b.b,b.c};
    for(auto p:av)if(!finite3(p))return ConvexQuery::Invalid;
    for(auto p:bv)if(!finite3(p))return ConvexQuery::Invalid;
    std::array<Point3d,3> ae{a.b-a.a,a.c-a.b,a.a-a.c},be{b.b-b.a,b.c-b.b,b.a-b.c};
    auto an=cross(ae[0],ae[1]),bn=cross(be[0],be[1]);
    if(dot(an,an)==0||dot(bn,bn)==0)return convexIntersect(av,bv);
    auto separated=[&](Point3d n){
        double l=dot(av[0],n),h=l,m=dot(bv[0],n),k=m;
        for(auto p:av){auto d=dot(p,n);l=std::min(l,d);h=std::max(h,d);}
        for(auto p:bv){auto d=dot(p,n);m=std::min(m,d);k=std::max(k,d);}
        return h<m||k<l;
    };
    if(separated(an)||separated(bn))return ConvexQuery::Separated;
    for(auto x:ae){if(separated(cross(an,x)))return ConvexQuery::Separated;for(auto y:be)if(separated(cross(x,y)))return ConvexQuery::Separated;}
    for(auto y:be)if(separated(cross(bn,y)))return ConvexQuery::Separated;
    return ConvexQuery::Intersecting;
}
}
