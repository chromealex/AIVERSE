#pragma once
#include "GeometryEPA.h"
namespace aiverse::lib::algorithms {
struct FaceContact {Point3d pointA,pointB;double depth;};
struct ContactManifold {Point3d normal;std::vector<FaceContact> contacts;};
// Caller supplies convex planar faces, reference A outward normal toward B.
// Incident B is clipped against reference side planes. No automatic face selection.
inline std::optional<ContactManifold> clipContactFaces(std::span<const Point3d> a,std::span<const Point3d> b,Point3d normal,double tolerance=1e-7,size_t maxContacts=4){
    if(a.size()<3||b.size()<3||a.size()>256||b.size()>256||!finite3(normal)||!std::isfinite(tolerance)||tolerance<=0||tolerance>1||!maxContacts||maxContacts>64)return {};
    double length=std::sqrt(dot(normal,normal));if(length<=1e-12)return {};normal=normal*(1/length);
    for(auto p:a)if(!finite3(p)||std::abs(dot(p-a.front(),normal))>tolerance)return {};
    for(auto p:b)if(!finite3(p))return {};
    Point3d center{};for(auto p:a)center=center+p;center=center*(1./a.size());
    std::vector<Point3d> poly(b.begin(),b.end()),next;
    for(size_t i=0;i<a.size();++i){
        auto origin=a[i],edge=a[(i+1)%a.size()]-origin,side=cross(normal,edge);
        double len=std::sqrt(dot(side,side));if(len<=1e-12)return {};side=side*(1/len);
        double c=dot(center-origin,side);if(std::abs(c)<=tolerance)return {};if(c<0)side=side*(-1);
        for(auto p:a)if(dot(p-origin,side)<-tolerance)return {};
        if(poly.empty())continue;
        next.clear();auto prev=poly.back();double dp=dot(prev-origin,side)+tolerance;
        for(auto cur:poly){
            double dc=dot(cur-origin,side)+tolerance;
            if((dp>=0)!=(dc>=0))next.push_back(prev+(cur-prev)*(dp/(dp-dc)));
            if(dc>=0)next.push_back(cur);prev=cur;dp=dc;
        }poly.swap(next);
    }
    std::vector<FaceContact> candidates;
    for(auto p:poly){
        double depth=dot(a.front()-p,normal);if(depth < -tolerance)continue;
        if(std::any_of(candidates.begin(),candidates.end(),[&](auto q){return squaredDistance(q.pointB,p)<=tolerance*tolerance;}))continue;
        candidates.push_back({p+normal*depth,p,std::max(0.,depth)});
    }
    ContactManifold out{normal,{}};
    if(candidates.empty())return out;
    auto deepest=std::max_element(candidates.begin(),candidates.end(),[](auto x,auto y){return x.depth<y.depth;});
    out.contacts.push_back(*deepest);candidates.erase(deepest);
    while(!candidates.empty()&&out.contacts.size()<maxContacts){
        size_t best=0;double score=-1;
        for(size_t i=0;i<candidates.size();++i){
            double d=std::numeric_limits<double>::infinity();
            for(auto q:out.contacts)d=std::min(d,squaredDistance(q.pointB,candidates[i].pointB));
            if(d>score){score=d;best=i;}
        }
        out.contacts.push_back(candidates[best]);candidates.erase(candidates.begin()+best);
    }return out;
}
}
