#pragma once
#include "Geometry2D.h"
#include <cmath>
namespace aiverse::lib::algorithms {
struct Point2d {double x=0,y=0;};
inline bool finitePoint(Point2d p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::abs(p.x)<=1e9&&std::abs(p.y)<=1e9;}
inline double segmentDistanceSquared(Point2d p,Point2d a,Point2d b){
    double x=b.x-a.x,y=b.y-a.y,d=x*x+y*y;
    double t=d?std::clamp(((p.x-a.x)*x+(p.y-a.y)*y)/d,0.,1.):0.;
    x=p.x-a.x-t*x;y=p.y-a.y-t*y;return x*x+y*y;
}
inline std::optional<std::vector<size_t>> simplifyDouglasPeucker(std::span<const Point2d> p,double tolerance){
    if(!std::isfinite(tolerance)||tolerance<0||tolerance>2e9||p.size()>1000000)return {};
    for(auto v:p)if(!finitePoint(v))return {};
    if(p.empty())return std::vector<size_t>{};
    std::vector<bool> keep(p.size());keep.front()=keep.back()=true;
    std::vector<std::pair<size_t,size_t>> stack{{0,p.size()-1}};
    while(!stack.empty()){
        auto [a,b]=stack.back();stack.pop_back();if(b-a<2)continue;
        double best=tolerance*tolerance;size_t index=a;
        for(size_t i=a+1;i<b;++i){auto d=segmentDistanceSquared(p[i],p[a],p[b]);if(d>best){best=d;index=i;}}
        if(index!=a){keep[index]=true;stack.emplace_back(a,index);stack.emplace_back(index,b);}
    }
    std::vector<size_t> out;for(size_t i=0;i<keep.size();++i)if(keep[i])out.push_back(i);return out;
}
inline double clipCross(Point2d a,Point2d b,Point2d p){return (b.x-a.x)*(p.y-a.y)-(b.y-a.y)*(p.x-a.x);}
// Convex subject and convex clip only; either winding, no holes.
// Floating point presentation/procedural geometry, not lockstep predicates.
inline std::optional<std::vector<Point2d>> clipConvexPolygon(std::span<const Point2d> subject,std::span<const Point2d> clip){
    auto winding=[](std::span<const Point2d> p)->int{
        if(p.size()<3||p.size()>4096)return 0;
        for(auto v:p)if(!finitePoint(v))return 0;
        double c=clipCross(p[0],p[1],p[2]);if(c==0)return 0;int sign=c>0?1:-1;
        for(size_t i=0;i<p.size();++i){
            auto a=p[i],b=p[(i+1)%p.size()];
            if(clipCross(a,b,p[(i+2)%p.size()])*sign<=0)return 0;
            for(auto v:p)if(clipCross(a,b,v)*sign<0)return 0;
        }return sign;
    };
    int sign=winding(clip);if(!sign||!winding(subject))return {};
    std::vector<Point2d> out(subject.begin(),subject.end()),next;
    for(size_t i=0;i<clip.size()&&!out.empty();++i){
        auto a=clip[i],b=clip[(i+1)%clip.size()];next.clear();
        auto prev=out.back();double dp=sign*clipCross(a,b,prev);
        for(auto cur:out){
            double dc=sign*clipCross(a,b,cur);
            if((dp>=0)!=(dc>=0)){double t=dp/(dp-dc);next.push_back({prev.x+t*(cur.x-prev.x),prev.y+t*(cur.y-prev.y)});}
            if(dc>=0)next.push_back(cur);prev=cur;dp=dc;
        }
        out.swap(next);
    }
    out.erase(std::unique(out.begin(),out.end(),[](auto a,auto b){return a.x==b.x&&a.y==b.y;}),out.end());
    if(out.size()>1&&out.front().x==out.back().x&&out.front().y==out.back().y)out.pop_back();
    return out;
}
}
