#pragma once
#include "GeometryConvex.h"
#include <vector>
#include <utility>
namespace aiverse::lib::algorithms {
struct PenetrationResult {ConvexQuery status=ConvexQuery::Invalid;double depth=0;Point3d normal{};Point3d witnessA{},witnessB{};bool hasWitness=false;};
// Initial simplex must consist of actual Minkowski support points and
// strictly enclose the origin. Normal points outward from A-B.
inline PenetrationResult convexEPA(std::span<const Point3d> a,std::span<const Point3d> b,std::array<Point3d,4> seed,double tolerance=1e-7,size_t budget=128,const std::array<Point3d,4>* seedA=nullptr,const std::array<Point3d,4>* seedB=nullptr){
    if(a.empty()||b.empty()||a.size()>1000000||b.size()>1000000||!std::isfinite(tolerance)||tolerance<=0||tolerance>1||!budget||budget>1024)return {};
    for(auto p:a)if(!finite3(p))return {};
    for(auto p:b)if(!finite3(p))return {};
    for(auto p:seed)if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z)||std::abs(p.x)>2e9||std::abs(p.y)>2e9||std::abs(p.z)>2e9)return {};
    std::vector<Point3d> vertices(seed.begin(),seed.end()), witnessesA,witnessesB;
    if(seedA&&seedB){for(size_t i=0;i<4;++i){if(!finite3((*seedA)[i])||!finite3((*seedB)[i])||squaredDistance((*seedA)[i]-(*seedB)[i],seed[i])>tolerance*tolerance)return {};witnessesA.push_back((*seedA)[i]);witnessesB.push_back((*seedB)[i]);}}
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
        auto pa=convexSupport(a,f.n),pb=convexSupport(b,f.n*(-1));auto p=pa-pb;double gap=dot(p,f.n)-f.d;
        if(gap < -tolerance)return {};
        if(gap<=tolerance){
            PenetrationResult result{ConvexQuery::Intersecting,f.d,f.n};
            if(!witnessesA.empty()){
                auto x=vertices[f.a],u=vertices[f.b]-x,v=vertices[f.c]-x,q=f.n*f.d-x;
                double uu=dot(u,u),uv=dot(u,v),vv=dot(v,v),den=dot(cross(u,v),cross(u,v));
                if(den>0){
                    double j=(vv*dot(q,u)-uv*dot(q,v))/den,k=(uu*dot(q,v)-uv*dot(q,u))/den,i=1-j-k;
                    if(i>=-1e-8&&j>=-1e-8&&k>=-1e-8){
                        result.witnessA=witnessesA[f.a]*i+witnessesA[f.b]*j+witnessesA[f.c]*k;
                        result.witnessB=witnessesB[f.a]*i+witnessesB[f.b]*j+witnessesB[f.c]*k;
                        result.hasWitness=true;
                    }
                }
            }
            return result;
        }
        for(auto v:vertices)if(squaredDistance(v,p)<=tolerance*tolerance)return {ConvexQuery::Indeterminate};
        std::vector<std::pair<size_t,size_t>> horizon;
        std::vector<Face> kept;
        auto edge=[&](size_t x,size_t y){auto it=std::find(horizon.begin(),horizon.end(),std::pair<size_t,size_t>{y,x});if(it!=horizon.end())horizon.erase(it);else horizon.emplace_back(x,y);};
        for(auto old:faces){
            if(dot(old.n,p)-old.d>0){edge(old.a,old.b);edge(old.b,old.c);edge(old.c,old.a);}
            else kept.push_back(old);
        }
        if(horizon.empty())return {ConvexQuery::Indeterminate};
        auto id=vertices.size();vertices.push_back(p);if(!witnessesA.empty()){witnessesA.push_back(pa);witnessesB.push_back(pb);}
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

 // Recover a strict enclosing seed using a bounded set of support directions.
 // This is a fallback, not a complete convex hull construction.
inline std::optional<std::array<Point3d,4>> convexSeed(std::span<const Point3d> a,std::span<const Point3d> b){
    if(a.empty()||b.empty()||a.size()>1000000||b.size()>1000000)return {};
    for(auto p:a)if(!finite3(p))return {};
    for(auto p:b)if(!finite3(p))return {};
    std::vector<Point3d> points;
    for(int x=-1;x<=1;++x)for(int y=-1;y<=1;++y)for(int z=-1;z<=1;++z){
        if(!x&&!y&&!z)continue;
        Point3d d{double(x),double(y),double(z)};
        auto p=convexSupport(a,d)-convexSupport(b,d*(-1));
        if(std::none_of(points.begin(),points.end(),[&](auto q){return squaredDistance(p,q)==0;}))points.push_back(p);
    }
    for(size_t i=0;i<points.size();++i)for(size_t j=i+1;j<points.size();++j)
    for(size_t k=j+1;k<points.size();++k)for(size_t l=k+1;l<points.size();++l){
        auto x=points[i],u=points[j]-x,v=points[k]-x,w=points[l]-x,q=x*(-1);
        double det=dot(u,cross(v,w));if(det==0)continue;
        double r=dot(q,cross(v,w))/det,t=dot(u,cross(q,w))/det,h=dot(u,cross(v,q))/det;
        if(r>1e-10&&t>1e-10&&h>1e-10&&r+t+h<1-1e-10)return std::array<Point3d,4>{x,points[j],points[k],points[l]};
    }return {};
}
inline PenetrationResult convexPenetrationExpanded(std::span<const Point3d> a,std::span<const Point3d> b,double tolerance=1e-7,size_t gjkBudget=64,size_t epaBudget=128){
    auto result=convexPenetration(a,b,tolerance,gjkBudget,epaBudget);
    if(result.status!=ConvexQuery::Indeterminate)return result;
    auto seed=convexSeed(a,b);if(!seed)return result;
    auto expanded=convexEPA(a,b,*seed,tolerance,epaBudget);
    return expanded.status==ConvexQuery::Invalid?result:expanded;
}
struct SupportPair {Point3d a,b;Point3d difference()const{return a-b;}};
// Extreme points along a contact direction, NOT paired contact witnesses.
inline std::optional<SupportPair> convexSupportPair(std::span<const Point3d> a,std::span<const Point3d> b,Point3d direction){
    if(a.empty()||b.empty()||!finite3(direction)||dot(direction,direction)==0)return {};
    for(auto p:a)if(!finite3(p))return {};
    for(auto p:b)if(!finite3(p))return {};
    return SupportPair{convexSupport(a,direction),convexSupport(b,direction*(-1))};
}

inline PenetrationResult convexContact(std::span<const Point3d> a,std::span<const Point3d> b,double tolerance=1e-7,size_t budget=128){
    auto seed=convexSeed(a,b);
    if(!seed)return convexPenetrationExpanded(a,b,tolerance,64,budget);
    std::array<Point3d,4> sa{},sb{};std::array<bool,4> found{};
    for(int x=-1;x<=1;++x)for(int y=-1;y<=1;++y)for(int z=-1;z<=1;++z){
        if(!x&&!y&&!z)continue;
        Point3d d{double(x),double(y),double(z)};
        auto pa=convexSupport(a,d),pb=convexSupport(b,d*(-1));
        for(size_t i=0;i<4;++i)if(!found[i]&&squaredDistance(pa-pb,(*seed)[i])==0){sa[i]=pa;sb[i]=pb;found[i]=true;}
    }
    if(!std::all_of(found.begin(),found.end(),[](bool v){return v;}))return {ConvexQuery::Indeterminate};
    return convexEPA(a,b,*seed,tolerance,budget,&sa,&sb);
}
}
