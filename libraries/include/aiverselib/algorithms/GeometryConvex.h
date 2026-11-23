#pragma once
#include "GeometryDistance.h"
#include <span>
namespace aiverse::lib::algorithms {
inline Point3d convexSupport(std::span<const Point3d> points,Point3d d){
    // Precondition: nonempty finite point set.
    auto best=points.front();double score=dot(best,d);
    for(auto p:points){double v=dot(p,d);if(v>score){best=p;score=v;}}
    return best;
}
enum class ConvexQuery {Separated,Intersecting,Indeterminate,Invalid};
// GJK intersection via closest simplex reduction. Tolerance is in world units.
// Indeterminate is deliberately distinct from separated.
inline ConvexQuery convexIntersect(std::span<const Point3d> a,std::span<const Point3d> b,double tolerance=1e-8,size_t budget=64){
    if(a.empty()||b.empty()||a.size()>1000000||b.size()>1000000||!std::isfinite(tolerance)||tolerance<=0||tolerance>1||!budget||budget>1024)return ConvexQuery::Invalid;
    for(auto p:a)if(!finite3(p))return ConvexQuery::Invalid;
    for(auto p:b)if(!finite3(p))return ConvexQuery::Invalid;
    auto support=[&](Point3d d){return convexSupport(a,d)-convexSupport(b,d*(-1));};
    std::vector<Point3d> simplex{support({1,0,0})};
    Point3d nearest=simplex.front();
    for(size_t step=0;step<budget;++step){
        double dist=dot(nearest,nearest);if(dist<=tolerance*tolerance)return ConvexQuery::Intersecting;
        auto d=nearest*(-1),p=support(d);
        if(dot(p,d)<-tolerance*std::sqrt(dist))return ConvexQuery::Separated;
        for(auto q:simplex)if(squaredDistance(p,q)<=tolerance*tolerance)return ConvexQuery::Indeterminate;
        simplex.push_back(p);
        // Check tetrahedron containment, then reduce to closest vertex/edge/face.
        if(simplex.size()==4){
            auto x=simplex[0],u=simplex[1]-x,v=simplex[2]-x,w=simplex[3]-x;
            double det=dot(u,cross(v,w));
            if(det!=0){
                auto q=x*(-1);double i=dot(q,cross(v,w))/det,j=dot(u,cross(q,w))/det,k=dot(u,cross(v,q))/det;
                if(i>=0&&j>=0&&k>=0&&i+j+k<=1)return ConvexQuery::Intersecting;
            }
        }
        double best=std::numeric_limits<double>::infinity();std::vector<Point3d> reduced;
        auto consider=[&](Point3d q,std::initializer_list<Point3d> vertices){
            double n=dot(q,q);if(n<best){best=n;nearest=q;reduced.assign(vertices);}
        };
        for(size_t i=0;i<simplex.size();++i)consider(simplex[i],{simplex[i]});
        for(size_t i=0;i<simplex.size();++i)for(size_t j=i+1;j<simplex.size();++j){
            auto x=simplex[i],y=simplex[j];consider(closestPoint(Segment3d{x,y},{}),{x,y});
        }
        for(size_t i=0;i<simplex.size();++i)for(size_t j=i+1;j<simplex.size();++j)for(size_t k=j+1;k<simplex.size();++k){
            auto x=simplex[i],y=simplex[j],z=simplex[k];consider(closestPoint(Triangle3d{x,y,z},{}),{x,y,z});
        }
        simplex.swap(reduced);
        if(best<=tolerance*tolerance)return ConvexQuery::Intersecting;
        if(best>=dist)return ConvexQuery::Indeterminate;
    }return ConvexQuery::Indeterminate;
}
// Continuous sphere/sphere query with constant velocities, t in [0,maxTime].
inline std::optional<double> sweepSpheres(Sphere3d a,Point3d va,Sphere3d b,Point3d vb,double maxTime){
    if(!valid(a)||!valid(b)||!finite3(va)||!finite3(vb)||!std::isfinite(maxTime)||maxTime<0)return {};
    if(overlaps(a,b))return 0.;
    auto v=va-vb,m=a.center-b.center;double r=a.radius+b.radius;
    double aa=dot(v,v),bb=dot(m,v),cc=dot(m,m)-r*r;
    if(aa==0||bb>=0)return {};
    double disc=bb*bb-aa*cc;if(disc<0)return {};
    double t=cc/(-bb+std::sqrt(disc));if(t<0||t>maxTime)return {};return t;
}
}
