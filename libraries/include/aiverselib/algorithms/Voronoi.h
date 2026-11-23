#pragma once
#include "GeometryClip.h"
#include <map>
namespace aiverse::lib::algorithms {
struct VoronoiBounds {Point2d min,max;};
inline std::optional<std::vector<std::vector<Point2d>>> voronoiCells(std::span<const Point2d> sites,VoronoiBounds b){
    if(sites.size()>2048||!finitePoint(b.min)||!finitePoint(b.max)||b.min.x>=b.max.x||b.min.y>=b.max.y)return {};
    for(size_t i=0;i<sites.size();++i){
        if(!finitePoint(sites[i]))return {};
        for(size_t j=0;j<i;++j)if(sites[i].x==sites[j].x&&sites[i].y==sites[j].y)return {};
    }
    std::vector<std::vector<Point2d>> result;
    for(size_t i=0;i<sites.size();++i){
        std::vector<Point2d> cell{{b.min.x,b.min.y},{b.max.x,b.min.y},{b.max.x,b.max.y},{b.min.x,b.max.y}},next;
        for(size_t j=0;j<sites.size()&&!cell.empty();++j){
            if(i==j)continue;
            auto a=sites[i],c=sites[j];double dx=c.x-a.x,dy=c.y-a.y;
            auto side=[&](Point2d p){return dx*(p.x-a.x-dx*.5)+dy*(p.y-a.y-dy*.5);};
            next.clear();auto prev=cell.back();double dp=side(prev);
            for(auto cur:cell){double dc=side(cur);
                if((dp<=0)!=(dc<=0)){double t=dp/(dp-dc);next.push_back({prev.x+t*(cur.x-prev.x),prev.y+t*(cur.y-prev.y)});}
                if(dc<=0)next.push_back(cur);prev=cur;dp=dc;
            }cell.swap(next);
        }
        result.push_back(std::move(cell));
    }return result;
}
// Bowyer-Watson, floating-point predicates; near-degenerate inputs may
// require a robust exact-predicate backend. Output CCW original indices.
inline std::optional<std::vector<std::array<size_t,3>>> delaunay(std::span<const Point2d> input){
    if(input.size()>4096)return {};
    std::vector<Point2d> p(input.begin(),input.end());
    for(size_t i=0;i<p.size();++i){if(!finitePoint(p[i]))return {};for(size_t j=0;j<i;++j)if(p[i].x==p[j].x&&p[i].y==p[j].y)return {};}
    const size_t n=p.size();if(n<3)return std::vector<std::array<size_t,3>>{};
    double x0=p[0].x,x1=x0,y0=p[0].y,y1=y0;
    for(auto v:p){x0=std::min(x0,v.x);x1=std::max(x1,v.x);y0=std::min(y0,v.y);y1=std::max(y1,v.y);}
    double scale=std::max(x1-x0,y1-y0);if(!scale)return {};
    for(auto& v:p){v.x=(v.x-x0)/scale;v.y=(v.y-y0)/scale;}
    p.push_back({-32,-32});p.push_back({32,-32});p.push_back({0,32});
    using Tri=std::array<size_t,3>;std::vector<Tri> tris{{n,n+1,n+2}};
    auto inCircle=[&](Tri t,Point2d q){
        auto a=p[t[0]],b=p[t[1]],c=p[t[2]];
        double ax=a.x-q.x,ay=a.y-q.y,bx=b.x-q.x,by=b.y-q.y,cx=c.x-q.x,cy=c.y-q.y;
        return (ax*ax+ay*ay)*(bx*cy-by*cx)-(bx*bx+by*by)*(ax*cy-ay*cx)+(cx*cx+cy*cy)*(ax*by-ay*bx)>0;
    };
    for(size_t i=0;i<n;++i){
        std::map<std::pair<size_t,size_t>,size_t> edges;std::vector<Tri> next;
        for(auto t:tris){if(!inCircle(t,p[i])){next.push_back(t);continue;}
            for(size_t k=0;k<3;++k){auto a=t[k],b=t[(k+1)%3];if(a>b)std::swap(a,b);++edges[{a,b}];}}
        if(edges.empty())return {};
        for(auto [edge,count]:edges)if(count==1){auto [a,b]=edge;double cross=clipCross(p[a],p[b],p[i]);if(cross==0)continue;if(cross<0)std::swap(a,b);next.push_back({a,b,i});}
        tris.swap(next);
    }
    tris.erase(std::remove_if(tris.begin(),tris.end(),[&](Tri t){return t[0]>=n||t[1]>=n||t[2]>=n;}),tris.end());
    for(auto& t:tris)std::rotate(t.begin(),std::min_element(t.begin(),t.end()),t.end());
    std::sort(tris.begin(),tris.end());return tris;
}
}
