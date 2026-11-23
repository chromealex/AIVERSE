#pragma once
#include "GeometryManifold.h"
#include <cstdint>
namespace aiverse::lib::algorithms {
struct ConvexFaceView {std::span<const Point3d> vertices;Point3d outward;};
inline std::optional<size_t> selectContactFace(std::span<const ConvexFaceView> faces,Point3d direction){
    if(faces.empty()||faces.size()>4096||!finite3(direction)||dot(direction,direction)==0)return {};
    size_t best=0;double score=-std::numeric_limits<double>::infinity();
    for(size_t i=0;i<faces.size();++i){
        auto f=faces[i];if(f.vertices.size()<3||f.vertices.size()>256||!finite3(f.outward))return {};
        double n=dot(f.outward,f.outward);if(n<=1e-24)return {};
        double s=dot(f.outward,direction)/std::sqrt(n);
        if(s>score){score=s;best=i;}
    }return best;
}
// Faces must be convex, planar, consistently outward-oriented and in common space.
inline std::optional<ContactManifold> convexFaceManifold(std::span<const ConvexFaceView> a,std::span<const ConvexFaceView> b,Point3d normal,double tolerance=1e-7,size_t maxContacts=4){
    auto i=selectContactFace(a,normal),j=selectContactFace(b,normal*(-1));if(!i||!j)return {};
    auto ref=a[*i];auto n=ref.outward;
    return clipContactFaces(ref.vertices,b[*j].vertices,n,tolerance,maxContacts);
}
struct PersistentContact {uint64_t id;FaceContact contact;};
struct ContactCache {
    uint64_t nextId=1;
    std::vector<PersistentContact> contacts;
    // Caller supplies contacts in a stable common coordinate frame per pair.
    // Matching is one-to-one, nearest combined witness displacement.
    bool update(std::span<const FaceContact> incoming,double matchRadius){
        if(incoming.size()>64||contacts.size()>64||!std::isfinite(matchRadius)||matchRadius<0||matchRadius>1e9||nextId==0)return false;
        for(auto c:incoming)if(!finite3(c.pointA)||!finite3(c.pointB)||!std::isfinite(c.depth)||c.depth<0)return false;
        std::vector<bool> used(contacts.size());
        std::vector<PersistentContact> next;auto counter=nextId;
        for(auto c:incoming){
            size_t best=contacts.size();double score=std::numeric_limits<double>::infinity();
            for(size_t i=0;i<contacts.size();++i){
                if(used[i])continue;
                auto old=contacts[i].contact;
                double a=squaredDistance(old.pointA,c.pointA),b=squaredDistance(old.pointB,c.pointB);
                if(a>matchRadius*matchRadius||b>matchRadius*matchRadius)continue;
                if(a+b<score){score=a+b;best=i;}
            }
            if(best<contacts.size()){used[best]=true;next.push_back({contacts[best].id,c});}
            else{if(counter==std::numeric_limits<uint64_t>::max())return false;next.push_back({counter++,c});}
        }
        contacts.swap(next);nextId=counter;return true;
    }
    void clear(){contacts.clear();}
};
}
