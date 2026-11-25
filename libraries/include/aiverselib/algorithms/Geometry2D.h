#pragma once
#include <algorithm>
#include <cstdint>
#include <vector>
#include <span>
#include <stdexcept>
#include <compare>
#include <array>
#include <optional>
#include <numeric>
namespace aiverse::lib::algorithms {
struct Point2i {int64_t x=0,y=0;auto operator<=>(const Point2i&)const=default;};
inline constexpr int64_t geometryLimit=1000000000;
inline void validatePoint(Point2i p){
    if(p.x < -geometryLimit||p.x>geometryLimit||p.y < -geometryLimit||p.y>geometryLimit)throw std::invalid_argument("geometry coordinate limit");
}
inline int orientation(Point2i a,Point2i b,Point2i c){
    validatePoint(a);validatePoint(b);validatePoint(c);
    auto v=(b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x);
    return (v>0)-(v<0);
}
inline bool onSegment(Point2i a,Point2i b,Point2i p){
    return orientation(a,b,p)==0&&p.x>=std::min(a.x,b.x)&&p.x<=std::max(a.x,b.x)&&p.y>=std::min(a.y,b.y)&&p.y<=std::max(a.y,b.y);
}
inline bool segmentsIntersect(Point2i a,Point2i b,Point2i c,Point2i d){
    int x=orientation(a,b,c),y=orientation(a,b,d),z=orientation(c,d,a),w=orientation(c,d,b);
    return (x*y<0&&z*w<0)||(!x&&onSegment(a,b,c))||(!y&&onSegment(a,b,d))||(!z&&onSegment(c,d,a))||(!w&&onSegment(c,d,b));
}
inline std::vector<Point2i> convexHull(std::vector<Point2i> points){
    for(auto p:points)validatePoint(p);
    std::sort(points.begin(),points.end());points.erase(std::unique(points.begin(),points.end()),points.end());
    if(points.size()<3)return points;
    std::vector<Point2i> hull;
    for(auto p:points){while(hull.size()>1&&orientation(hull[hull.size()-2],hull.back(),p)<=0)hull.pop_back();hull.push_back(p);}
    auto lower=hull.size();
    for(size_t i=points.size()-1;i>0;--i){auto p=points[i-1];while(hull.size()>lower&&orientation(hull[hull.size()-2],hull.back(),p)<=0)hull.pop_back();hull.push_back(p);}
    hull.pop_back();return hull;
}
enum class PolygonLocation {Outside,Inside,Boundary};
inline PolygonLocation pointInPolygon(std::span<const Point2i> poly,Point2i p){
    validatePoint(p);for(auto v:poly)validatePoint(v);
    if(poly.size()<3)return PolygonLocation::Outside;
    bool inside=false;
    for(size_t i=0,j=poly.size()-1;i<poly.size();j=i++){
        auto a=poly[j],b=poly[i];if(onSegment(a,b,p))return PolygonLocation::Boundary;
        if((a.y>p.y)!=(b.y>p.y)){
            auto side=orientation(a,b,p);
            if((b.y>a.y&&side>0)||(b.y<a.y&&side<0))inside=!inside;
        }
    }return inside?PolygonLocation::Inside:PolygonLocation::Outside;
}

 // Simple polygon, no holes, no repeated closing vertex. Original vertex IDs.
inline std::optional<std::vector<std::array<size_t,3>>> triangulatePolygon(std::span<const Point2i> p){
    if(p.size()<3||p.size()>4096)return {};
    for(auto v:p)validatePoint(v);
    const size_t n=p.size();
    for(size_t i=0;i<n;++i){
        if(p[i]==p[(i+1)%n]||orientation(p[(i+n-1)%n],p[i],p[(i+1)%n])==0)return {};
        for(size_t j=i+1;j<n;++j){
            if(j==i+1||(i==0&&j==n-1))continue;
            if(segmentsIntersect(p[i],p[(i+1)%n],p[j],p[(j+1)%n]))return {};
        }
    }
    auto extreme=size_t(std::min_element(p.begin(),p.end())-p.begin());
    int winding=orientation(p[(extreme+n-1)%n],p[extreme],p[(extreme+1)%n]);
    std::vector<size_t> ring(n);std::iota(ring.begin(),ring.end(),0);
    std::vector<std::array<size_t,3>> out;out.reserve(n-2);
    while(ring.size()>3){
        bool found=false;
        for(size_t i=0;i<ring.size();++i){
            auto a=ring[(i+ring.size()-1)%ring.size()],b=ring[i],c=ring[(i+1)%ring.size()];
            if(orientation(p[a],p[b],p[c])!=winding)continue;
            bool blocked=false;
            for(auto v:ring){
                if(v==a||v==b||v==c)continue;
                if(orientation(p[a],p[b],p[v])*winding>=0&&orientation(p[b],p[c],p[v])*winding>=0&&orientation(p[c],p[a],p[v])*winding>=0){blocked=true;break;}
            }
            if(blocked)continue;
            out.push_back({a,b,c});ring.erase(ring.begin()+i);found=true;break;
        }
        if(!found)return {};
    }
    if(orientation(p[ring[0]],p[ring[1]],p[ring[2]])!=winding)return {};
    out.push_back({ring[0],ring[1],ring[2]});return out;
}
// Exact simplification of an open polyline: remove duplicates and redundant
// collinear points only, preserving reversals and original geometric trace.
inline std::vector<Point2i> simplifyCollinear(std::span<const Point2i> points){
    std::vector<Point2i> out;
    for(auto p:points){
        validatePoint(p);if(!out.empty()&&out.back()==p)continue;
        while(out.size()>1&&onSegment(out[out.size()-2],p,out.back()))out.pop_back();
        out.push_back(p);
    }return out;
}
}
