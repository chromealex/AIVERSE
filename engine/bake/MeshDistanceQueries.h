// Reusable offline double precision mesh distance and winding queries.
// No rendering, ECS, GPU or light-baker target dependency.
#pragma once
#include "engine/render/Math3D.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numeric>
#include <vector>

namespace aiverse::bake {
struct MeshDistanceQueryStats {
    uint64_t nodeVisits=0,distanceTests=0,rayTests=0,solidAngleTests=0;
    uint64_t rayRetries=0,exactFallbacks=0;
    uint64_t work()const{return nodeVisits+distanceTests+rayTests+solidAngleTests;}
};
namespace distance_detail {
struct D3 {
    double x=0,y=0,z=0;
    D3 operator+(D3 b)const{return {x+b.x,y+b.y,z+b.z};}
    D3 operator-(D3 b)const{return {x-b.x,y-b.y,z-b.z};}
    D3 operator*(double k)const{return {x*k,y*k,z*k};}
};
inline D3 d(render::float3 p){return {p.x,p.y,p.z};}
inline double dot(D3 a,D3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
inline D3 cross(D3 a,D3 b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
inline double len2(D3 p){return dot(p,p);}
inline double length(D3 p){return std::sqrt(len2(p));}
inline bool validPoint(render::float3 p){
    return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z)&&
        std::abs(p.x)<=1e6f&&std::abs(p.y)<=1e6f&&std::abs(p.z)<=1e6f;
}
struct Tri { D3 a,b,c; };
// Closest-point triangle Voronoi regions, double precision offline.
inline double distance2(D3 p,const Tri& t){
    const D3 ab=t.b-t.a,ac=t.c-t.a,ap=p-t.a;
    const double d1=dot(ab,ap),d2=dot(ac,ap);
    if(d1<=0&&d2<=0)return len2(ap);
    const D3 bp=p-t.b;
    const double d3=dot(ab,bp),d4=dot(ac,bp);
    if(d3>=0&&d4<=d3)return len2(bp);
    const double vc=d1*d4-d3*d2;
    if(vc<=0&&d1>=0&&d3<=0)return len2(ap-ab*(d1/(d1-d3)));
    const D3 cp=p-t.c;
    const double d5=dot(ab,cp),d6=dot(ac,cp);
    if(d6>=0&&d5<=d6)return len2(cp);
    const double vb=d5*d2-d1*d6;
    if(vb<=0&&d2>=0&&d6<=0)return len2(ap-ac*(d2/(d2-d6)));
    const double va=d3*d6-d5*d4;
    if(va<=0&&d4-d3>=0&&d5-d6>=0)
        return len2(bp-(t.c-t.b)*((d4-d3)/((d4-d3)+(d5-d6))));
    const double inv=1/(va+vb+vc);
    return len2(ap-ab*(vb*inv)-ac*(vc*inv));
}
inline double solidAngle(D3 p,const Tri& t){
    const D3 a=t.a-p,b=t.b-p,c=t.c-p;
    const double la=length(a),lb=length(b),lc=length(c);
    return 2*std::atan2(dot(a,cross(b,c)),
        la*lb*lc+dot(a,b)*lc+dot(b,c)*la+dot(c,a)*lb);
}

struct QueryWork {
    MeshDistanceQueryStats stats;
    uint64_t limit=0;
    bool charge(uint64_t& counter){
        if(stats.work()>=limit)return false;
        ++counter;return true;
    }
};
inline double component(D3 p,unsigned axis){return axis==0?p.x:axis==1?p.y:p.z;}
struct DistanceBox {
    D3 lo{1e30,1e30,1e30},hi{-1e30,-1e30,-1e30};
    void add(D3 p){
        lo={std::min(lo.x,p.x),std::min(lo.y,p.y),std::min(lo.z,p.z)};
        hi={std::max(hi.x,p.x),std::max(hi.y,p.y),std::max(hi.z,p.z)};
    }
    void add(const DistanceBox& b){add(b.lo);add(b.hi);}
    double lower2(D3 p)const{
        const auto gap=[](double x,double a,double b){return std::max({a-x,x-b,0.0});};
        return len2({gap(p.x,lo.x,hi.x),gap(p.y,lo.y,hi.y),gap(p.z,lo.z,hi.z)});
    }
    bool rayPossible(D3 p,unsigned axis)const{
        for(unsigned a=0;a<3;++a){
            const double v=component(p,a),mn=component(lo,a),mx=component(hi,a);
            const double tol=1e-12*(1+std::max({std::abs(v),std::abs(mn),std::abs(mx)}));
            if(v>mx+tol||(a!=axis&&v<mn-tol))return false;
        }
        return true;
    }
};
inline DistanceBox bounds(const Tri& t){DistanceBox b;b.add(t.a);b.add(t.b);b.add(t.c);return b;}

// Query only a validated closed consistently oriented mesh. A ray touching
// a projected edge/vertex or numerically ambiguous plane is never counted
// by an arbitrary half-open convention: retry a different axis, then use
// the original exhaustive solid-angle winding if all axes are ambiguous.
enum class RayCrossing { Miss, Positive, Negative, Ambiguous };
inline RayCrossing orientedCrossing(D3 p,const Tri& tri,unsigned axis,const DistanceBox& box){
    if(!box.rayPossible(p,axis))return RayCrossing::Miss;
    const unsigned u=(axis+1)%3,v=(axis+2)%3;
    const double ax=component(tri.a,u)-component(p,u),ay=component(tri.a,v)-component(p,v);
    const double bx=component(tri.b,u)-component(p,u),by=component(tri.b,v)-component(p,v);
    const double cx=component(tri.c,u)-component(p,u),cy=component(tri.c,v)-component(p,v);
    const double wa=bx*cy-by*cx,wb=cx*ay-cy*ax,wc=ax*by-ay*bx;
    const double det=wa+wb+wc;
    const double scale=std::max({std::abs(ax),std::abs(ay),std::abs(bx),std::abs(by),std::abs(cx),std::abs(cy)});
    const double tol=std::max(1e-30,scale*scale*1e-12);
    if(std::abs(det)<=tol)return RayCrossing::Ambiguous;
    const double sign=det>0?1.0:-1.0;
    if(wa*sign < -tol||wb*sign < -tol||wc*sign < -tol)return RayCrossing::Miss;
    const double hit=(wa*(component(tri.a,axis)-component(p,axis))+
                      wb*(component(tri.b,axis)-component(p,axis))+
                      wc*(component(tri.c,axis)-component(p,axis)))/det;
    const double hitTol=1e-12*(1+std::max({std::abs(component(p,axis)),
        std::abs(component(box.lo,axis)),std::abs(component(box.hi,axis))}));
    if(hit < -hitTol)return RayCrossing::Miss;
    if(hit<=hitTol||wa*sign<=tol||wb*sign<=tol||wc*sign<=tol)return RayCrossing::Ambiguous;
    return det>0?RayCrossing::Positive:RayCrossing::Negative;
}

// Balanced median BVH with deterministic triangle-index tie breaks. Build
// work is bounded by the caller's triangle cap, queries by QueryWork.
class MeshDistanceBvh final {
    struct Node {DistanceBox box;uint32_t begin=0,count=0,left=0,right=0;};
    const std::vector<Tri>& tris_;
    std::vector<DistanceBox> boxes_;
    std::vector<uint32_t> order_;
    std::vector<Node> nodes_;
    uint32_t build(uint32_t begin,uint32_t end){
        const uint32_t id=uint32_t(nodes_.size());nodes_.push_back({});
        DistanceBox box,centroids;
        for(uint32_t i=begin;i<end;++i){
            const auto& b=boxes_[order_[i]];
            box.add(b);centroids.add((b.lo+b.hi)*0.5);
        }
        nodes_[id].box=box;
        const uint32_t count=end-begin;
        if(count<=4){nodes_[id].begin=begin;nodes_[id].count=count;return id;}
        const D3 span=centroids.hi-centroids.lo;
        const unsigned axis=span.x>=span.y&&span.x>=span.z?0u:span.y>=span.z?1u:2u;
        const uint32_t middle=begin+count/2;
        std::nth_element(order_.begin()+begin,order_.begin()+middle,order_.begin()+end,
            [&](uint32_t a,uint32_t b){
                const double x=component(boxes_[a].lo+boxes_[a].hi,axis);
                const double y=component(boxes_[b].lo+boxes_[b].hi,axis);
                return x<y||(x==y&&a<b);
            });
        const uint32_t left=build(begin,middle),right=build(middle,end);
        nodes_[id].left=left;nodes_[id].right=right;return id;
    }
    bool nearest(uint32_t id,D3 p,double& best,QueryWork& work)const{
        if(!work.charge(work.stats.nodeVisits))return false;
        const auto& n=nodes_[id];
        if(n.box.lower2(p)>best*(1+1e-12)+1e-24)return true;
        if(n.count){
            for(uint32_t i=n.begin;i<n.begin+n.count;++i){
                if(!work.charge(work.stats.distanceTests))return false;
                best=std::min(best,distance2(p,tris_[order_[i]]));
            }
            return true;
        }
        auto left=n.left,right=n.right;
        if(nodes_[right].box.lower2(p)<nodes_[left].box.lower2(p))std::swap(left,right);
        return nearest(left,p,best,work)&&nearest(right,p,best,work);
    }
    bool ray(uint32_t id,D3 p,unsigned axis,int& winding,bool& ambiguous,QueryWork& work)const{
        if(!work.charge(work.stats.nodeVisits))return false;
        const auto& n=nodes_[id];
        if(!n.box.rayPossible(p,axis))return true;
        if(n.count){
            for(uint32_t i=n.begin;i<n.begin+n.count;++i){
                if(!work.charge(work.stats.rayTests))return false;
                const auto index=order_[i];
                const auto crossing=orientedCrossing(p,tris_[index],axis,boxes_[index]);
                if(crossing==RayCrossing::Ambiguous){ambiguous=true;return true;}
                if(crossing==RayCrossing::Positive)++winding;
                if(crossing==RayCrossing::Negative)--winding;
            }
            return true;
        }
        if(!ray(n.left,p,axis,winding,ambiguous,work))return false;
        return ambiguous||ray(n.right,p,axis,winding,ambiguous,work);
    }
public:
    explicit MeshDistanceBvh(const std::vector<Tri>& tris):tris_(tris){
        boxes_.reserve(tris.size());for(const auto& t:tris)boxes_.push_back(bounds(t));
        order_.resize(tris.size());std::iota(order_.begin(),order_.end(),0u);
        nodes_.reserve(tris.size()*2);
        if(!tris.empty())build(0,uint32_t(tris.size()));
    }
    bool nearestSquared(D3 p,double& best,QueryWork& work)const{
        best=std::numeric_limits<double>::infinity();
        return !nodes_.empty()&&nearest(0,p,best,work);
    }
    bool inside(D3 p,bool& result,QueryWork& work,bool& windingInvalid)const{
        windingInvalid=false;
        if(nodes_.empty())return false;
        for(unsigned axis=0;axis<3;++axis){
            int winding=0;bool ambiguous=false;
            if(!ray(0,p,axis,winding,ambiguous,work))return false;
            if(!ambiguous){result=winding!=0;return true;}
            ++work.stats.rayRetries;
        }
        ++work.stats.exactFallbacks;
        double angle=0;
        for(const auto& tri:tris_){
            if(!work.charge(work.stats.solidAngleTests))return false;
            angle+=solidAngle(p,tri);
        }
        constexpr double fourPi=12.56637061435917295385;
        const double winding=std::abs(angle/fourPi);
        if(!std::isfinite(winding)||std::abs(winding-std::round(winding))>0.05){
            windingInvalid=true;return false;
        }
        result=winding>0.5;return true;
    }
};
} // distance_detail
} // aiverse::bake
