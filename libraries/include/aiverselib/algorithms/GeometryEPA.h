#pragma once
#include "GeometryConvex.h"
#include <vector>
#include <utility>
namespace aiverse::lib::algorithms {
struct PenetrationResult {ConvexQuery status=ConvexQuery::Invalid;double depth=0;Point3d normal{};};
// Initial simplex must consist of actual Minkowski support points and
// strictly enclose the origin. Normal points outward from A-B.
inline PenetrationResult convexEPA(std::span<const Point3d> a,std::span<const Point3d> b,std::array<Point3d,4> seed,double tolerance=1e-7,size_t budget=128){
    if(a.empty()||b.empty()||a.size()>1000000||b.size()>1000000||!std::isfinite(tolerance)||tolerance<=0||tolerance>1||!budget||budget>1024)return {};
    for(auto p:a)if(!finite3(p))return {};
    for(auto p:b)if(!finite3(p))return {};
    for(auto p:seed)if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z)||std::abs(p.x)>2e9||std::abs(p.y)>2e9||std::abs(p.z)>2e9)return {};
    std::vector<Point3d> vertices(seed.begin(),seed.end());
    struct Face {size_t a,b,c;Point3d n;double d;};
    std::vector<Face> faces;
    auto face=[&](size_t i,size_t j,size_t k)->std::optional<Face>{
        auto n=cross(vertices[j]-vertices[i],vertices[k]-vertices[i]);double length=std::sqrt(dot(n,n));
        if(length==0)return {};
        n=n*(1/length);double d=dot(n,vertices[i]);
        if(d<0){std::swap(j,k);n=n*(-1);d=-d;}
        return Face{i,j,k,n,d};
    };
    for(auto t:std::array<std::array<size_t,4>,4>{{{0,1,2,3},{0,3,1,2},{0,2,3,1},{1,3,2,0}}}){
        auto f=face(t[0],t[1],t[2]);if(!f||f->d<=0||dot(f->n,vertices[t[3]])>=f->d)return {};
        faces.push_back(*f);
    }
    for(size_t step=0;step<budget;++step){
        if(faces.empty()||faces.size()>8192)return {ConvexQuery::Indeterminate};
        auto f=*std::min_element(faces.begin(),faces.end(),[](auto x,auto y){return x.d<y.d;});
        auto p=convexSupport(a,f.n)-convexSupport(b,f.n*(-1));double gap=dot(p,f.n)-f.d;
        if(gap < -tolerance)return {};
        if(gap<=tolerance)return {ConvexQuery::Intersecting,f.d,f.n};
        for(auto v:vertices)if(squaredDistance(v,p)<=tolerance*tolerance)return {ConvexQuery::Indeterminate};
        std::vector<std::pair<size_t,size_t>> horizon;
        std::vector<Face> kept;
        auto edge=[&](size_t x,size_t y){auto it=std::find(horizon.begin(),horizon.end(),std::pair<size_t,size_t>{y,x});if(it!=horizon.end())horizon.erase(it);else horizon.emplace_back(x,y);};
        for(auto old:faces){
            if(dot(old.n,p)-old.d>0){edge(old.a,old.b);edge(old.b,old.c);edge(old.c,old.a);}
            else kept.push_back(old);
        }
        if(horizon.empty())return {ConvexQuery::Indeterminate};
        auto id=vertices.size();vertices.push_back(p);
        for(auto [x,y]:horizon){auto nf=face(x,y,id);if(!nf||nf->d<=0)return {ConvexQuery::Indeterminate};kept.push_back(*nf);}
        faces.swap(kept);
    }return {ConvexQuery::Indeterminate};
}

 // Automatic pipeline: lower-dimensional GJK contacts may lack an EPA seed.
inline PenetrationResult convexPenetration(std::span<const Point3d> a,std::span<const Point3d> b,double tolerance=1e-7,size_t gjkBudget=64,size_t epaBudget=128){
    std::array<Point3d,4> seed{};
    auto status=convexIntersect(a,b,tolerance,gjkBudget,&seed);
    if(status!=ConvexQuery::Intersecting)return {status};
    auto result=convexEPA(a,b,seed,tolerance,epaBudget);
    if(result.status==ConvexQuery::Invalid)return {ConvexQuery::Indeterminate};
    return result;
}
}
