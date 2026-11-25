#pragma once
#include <algorithm>
#include <cmath>
#include <optional>
#include <limits>
namespace aiverse::lib::algorithms {
struct Point3d {double x=0,y=0,z=0;};
inline Point3d operator+(Point3d a,Point3d b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
inline Point3d operator-(Point3d a,Point3d b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
inline Point3d operator*(Point3d a,double s){return {a.x*s,a.y*s,a.z*s};}
inline double dot(Point3d a,Point3d b){return a.x*b.x+a.y*b.y+a.z*b.z;}
inline Point3d cross(Point3d a,Point3d b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
inline bool finite3(Point3d p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z)&&std::abs(p.x)<=1e9&&std::abs(p.y)<=1e9&&std::abs(p.z)<=1e9;}
struct Ray3d {Point3d origin,direction;};
struct Box3d {Point3d min,max;};
struct Sphere3d {Point3d center;double radius;};
struct Plane3d {Point3d point,normal;};
struct Triangle3d {Point3d a,b,c;};
struct RayInterval {double enter,exit;};
inline bool valid(Box3d b){return finite3(b.min)&&finite3(b.max)&&b.min.x<=b.max.x&&b.min.y<=b.max.y&&b.min.z<=b.max.z;}
inline bool valid(Ray3d r){return finite3(r.origin)&&finite3(r.direction)&&dot(r.direction,r.direction)>1e-24;}
inline bool valid(Sphere3d s){return finite3(s.center)&&std::isfinite(s.radius)&&s.radius>=0&&s.radius<=1e9;}
inline bool overlaps(Box3d a,Box3d b){return valid(a)&&valid(b)&&a.min.x<=b.max.x&&b.min.x<=a.max.x&&a.min.y<=b.max.y&&b.min.y<=a.max.y&&a.min.z<=b.max.z&&b.min.z<=a.max.z;}
inline Point3d closestPoint(Box3d b,Point3d p){return {std::clamp(p.x,b.min.x,b.max.x),std::clamp(p.y,b.min.y,b.max.y),std::clamp(p.z,b.min.z,b.max.z)};}
inline bool overlaps(Box3d b,Sphere3d s){if(!valid(b)||!valid(s))return false;auto d=closestPoint(b,s.center)-s.center;return dot(d,d)<=s.radius*s.radius;}
inline bool overlaps(Sphere3d a,Sphere3d b){if(!valid(a)||!valid(b))return false;auto d=a.center-b.center;auto r=a.radius+b.radius;return dot(d,d)<=r*r;}
inline std::optional<RayInterval> rayBox(Ray3d r,Box3d b){
    if(!valid(r)||!valid(b))return {};
    double lo=0,hi=std::numeric_limits<double>::infinity();
    auto slab=[&](double o,double d,double a,double z){if(d==0)return o>=a&&o<=z;double t=(a-o)/d,u=(z-o)/d;if(t>u)std::swap(t,u);lo=std::max(lo,t);hi=std::min(hi,u);return lo<=hi;};
    if(!slab(r.origin.x,r.direction.x,b.min.x,b.max.x)||!slab(r.origin.y,r.direction.y,b.min.y,b.max.y)||!slab(r.origin.z,r.direction.z,b.min.z,b.max.z))return {};
    return RayInterval{lo,hi};
}
inline std::optional<RayInterval> raySphere(Ray3d r,Sphere3d s){
    if(!valid(r)||!valid(s))return {};
    auto m=r.origin-s.center;double a=dot(r.direction,r.direction),b=dot(m,r.direction),c=dot(m,m)-s.radius*s.radius;
    double disc=b*b-a*c;if(disc<0)return {};double root=std::sqrt(disc),lo=(-b-root)/a,hi=(-b+root)/a;
    if(hi<0)return {};return RayInterval{std::max(0.,lo),hi};
}
inline std::optional<double> rayPlane(Ray3d r,Plane3d p){
    if(!valid(r)||!finite3(p.point)||!finite3(p.normal)||dot(p.normal,p.normal)<=1e-24)return {};
    double d=dot(r.direction,p.normal);if(std::abs(d)<=1e-12*std::sqrt(dot(r.direction,r.direction)*dot(p.normal,p.normal)))return {};
    double t=dot(p.point-r.origin,p.normal)/d;if(t<0)return {};return t;
}
struct TriangleHit {double t,u,v;};
inline std::optional<TriangleHit> rayTriangle(Ray3d r,Triangle3d tri){
    if(!valid(r)||!finite3(tri.a)||!finite3(tri.b)||!finite3(tri.c))return {};
    auto e=tri.b-tri.a,f=tri.c-tri.a,p=cross(r.direction,f);double det=dot(e,p);
    double scale=std::sqrt(dot(e,e)*dot(f,f)*dot(r.direction,r.direction));
    if(scale==0||std::abs(det)<=1e-12*scale)return {};
    auto s=r.origin-tri.a;double u=dot(s,p)/det;if(u<0||u>1)return {};
    auto q=cross(s,e);double v=dot(r.direction,q)/det;if(v<0||u+v>1)return {};
    double t=dot(f,q)/det;if(t<0)return {};return TriangleHit{t,u,v};
}
}
