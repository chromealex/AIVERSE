#pragma once
#include "Geometry3D.h"
#include <array>
namespace aiverse::lib::algorithms {
struct Segment3d {Point3d a,b;};
struct Capsule3d {Segment3d axis;double radius;};
inline Point3d closestPoint(Segment3d s,Point3d p){
    auto d=s.b-s.a;double n=dot(d,d);
    return s.a+d*(n>0?std::clamp(dot(p-s.a,d)/n,0.,1.):0.);
}
inline double squaredDistance(Point3d a,Point3d b){auto d=a-b;return dot(d,d);}
inline Point3d closestPoint(Triangle3d t,Point3d p){
    auto ab=t.b-t.a,ac=t.c-t.a,n=cross(ab,ac);
    double nn=dot(n,n);
    if(nn>0){
        auto q=p-n*(dot(p-t.a,n)/nn);
        if(dot(cross(ab,q-t.a),n)>=0&&dot(cross(t.c-t.b,q-t.b),n)>=0&&dot(cross(t.a-t.c,q-t.c),n)>=0)return q;
    }
    auto a=closestPoint(Segment3d{t.a,t.b},p),b=closestPoint(Segment3d{t.b,t.c},p),c=closestPoint(Segment3d{t.c,t.a},p);
    if(squaredDistance(b,p)<squaredDistance(a,p))a=b;
    if(squaredDistance(c,p)<squaredDistance(a,p))a=c;return a;
}
inline double segmentDistanceSquared(Segment3d a,Segment3d b){
    auto u=a.b-a.a,v=b.b-b.a,w=a.a-b.a;
    double aa=dot(u,u),bb=dot(u,v),cc=dot(v,v),dd=dot(u,w),ee=dot(v,w);
    double best=std::min({squaredDistance(a.a,closestPoint(b,a.a)),squaredDistance(a.b,closestPoint(b,a.b)),squaredDistance(b.a,closestPoint(a,b.a)),squaredDistance(b.b,closestPoint(a,b.b))});
    double den=dot(cross(u,v),cross(u,v));
    if(den>0){double s=(bb*ee-cc*dd)/den,t=(aa*ee-bb*dd)/den;if(s>=0&&s<=1&&t>=0&&t<=1)best=std::min(best,squaredDistance(a.a+u*s,b.a+v*t));}
    return best;
}
inline bool valid(Capsule3d c){return finite3(c.axis.a)&&finite3(c.axis.b)&&std::isfinite(c.radius)&&c.radius>=0&&c.radius<=1e9;}
inline bool overlaps(Capsule3d a,Capsule3d b){if(!valid(a)||!valid(b))return false;double r=a.radius+b.radius;return segmentDistanceSquared(a.axis,b.axis)<=r*r;}
inline bool overlaps(Capsule3d a,Sphere3d b){if(!valid(a)||!valid(b))return false;double r=a.radius+b.radius;return squaredDistance(closestPoint(a.axis,b.center),b.center)<=r*r;}
struct Obb3d {Point3d center;std::array<Point3d,3> axis;std::array<double,3> half;};
inline bool valid(const Obb3d& b){
    if(!finite3(b.center))return false;
    for(size_t i=0;i<3;++i){
        if(!finite3(b.axis[i])||std::abs(dot(b.axis[i],b.axis[i])-1)>1e-8||!std::isfinite(b.half[i])||b.half[i]<0||b.half[i]>1e9)return false;
        for(size_t j=0;j<i;++j)if(std::abs(dot(b.axis[i],b.axis[j]))>1e-8)return false;
    }return true;
}
inline bool overlaps(const Obb3d& a,const Obb3d& b){
    if(!valid(a)||!valid(b))return false;
    auto delta=b.center-a.center;
    auto separated=[&](Point3d axis){
        if(dot(axis,axis)==0)return false;
        double ra=0,rb=0;
        for(size_t i=0;i<3;++i){ra+=a.half[i]*std::abs(dot(a.axis[i],axis));rb+=b.half[i]*std::abs(dot(b.axis[i],axis));}
        return std::abs(dot(delta,axis))>ra+rb;
    };
    for(auto axis:a.axis)if(separated(axis))return false;
    for(auto axis:b.axis)if(separated(axis))return false;
    for(auto x:a.axis)for(auto y:b.axis)if(separated(cross(x,y)))return false;
    return true;
}
}
