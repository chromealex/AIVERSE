#pragma once
#include <algorithm>
#include <cstdint>
#include <vector>
#include <span>
#include <stdexcept>
#include <compare>
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
}
