#include "SpatialWorld.h"
#include <algorithm>
#include <array>

namespace aiverse::world {
namespace {
Vec3 cross(Vec3 a,Vec3 b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
Vec3 unit(Vec3 v){auto n=v.length();return n.raw?v/n:Vec3{};}
bool triangleCast(Vec3 p,Vec3 delta,Vec3 half,const Triangle& triangle,Fixed& fraction,Vec3& normal) {
    // Translate before projections: precision and intermediate magnitudes depend on local geometry.
    const auto a=triangle.a-p,b=triangle.b-p,c=triangle.c-p;
    const Vec3 edges[]={unit(triangle.b-triangle.a),unit(triangle.c-triangle.b),unit(triangle.a-triangle.c)};
    const Vec3 basis[]={{1_fx,{},{}},{{},1_fx,{}},{{},{},1_fx}};
    std::array<Vec3,13> axes;size_t count=0;
    for(auto v:basis)axes[count++]=v;
    axes[count++]=unit(cross(edges[0],edges[1]));
    for(auto edge:edges)for(auto v:basis)axes[count++]=cross(edge,v);
    Fixed enter=-1_fx,leave=1_fx;Vec3 entryNormal{};bool boundary=false;
    for(auto axis:axes) {
        if(axis.lengthSq().raw<=1)continue;
        auto x=a.dot(axis),y=b.dot(axis),z=c.dot(axis);
        auto radius=fxAbs(axis.x)*half.x+fxAbs(axis.y)*half.y+fxAbs(axis.z)*half.z;
        auto low=fxMin(x,fxMin(y,z))-radius,high=fxMax(x,fxMax(y,z))+radius;
        auto velocity=delta.dot(axis);
        boundary|=low>=0_fx||high<=0_fx;
        if(!velocity.raw){if(low>0_fx||high<0_fx)return false;continue;}
        auto first=low/velocity,last=high/velocity;auto n=axis*(-1_fx);
        if(first>last){std::swap(first,last);n=axis;}
        if(first>enter){enter=first;entryNormal=n;}
        leave=fxMin(leave,last);if(enter>leave)return false;
    }
    if(leave<0_fx||enter>1_fx)return false;
    // Tangent contact with a floor must not arrest horizontal movement.
    if(enter<0_fx&&boundary)return false;
    fraction=fxMax(0_fx,enter);normal=unit(entryNormal);
    if(normal.lengthSq().raw==0){if(boundary)return false;normal={0_fx,1_fx,0_fx};}
    return true;
}
}
bool SpatialWorld::addTriangle(uint32_t id,Triangle triangle) {
    for(auto p:{triangle.a,triangle.b,triangle.c})if(p.x< -1000000_fx||p.x>1000000_fx||p.y< -1000000_fx||p.y>1000000_fx||p.z< -1000000_fx||p.z>1000000_fx)return false;
    // Chunk-local edges keep all SAT arithmetic within Fixed's supported range.
    for(auto e:{triangle.b-triangle.a,triangle.c-triangle.a})if(fxAbs(e.x)>4096_fx||fxAbs(e.y)>20000_fx||fxAbs(e.z)>4096_fx)return false;
    if(cross(unit(triangle.b-triangle.a),unit(triangle.c-triangle.a)).lengthSq().raw<=1)return false;
    Bounds bounds{{fxMin(triangle.a.x,fxMin(triangle.b.x,triangle.c.x)),fxMin(triangle.a.y,fxMin(triangle.b.y,triangle.c.y)),fxMin(triangle.a.z,fxMin(triangle.b.z,triangle.c.z))},
                  {fxMax(triangle.a.x,fxMax(triangle.b.x,triangle.c.x)),fxMax(triangle.a.y,fxMax(triangle.b.y,triangle.c.y)),fxMax(triangle.a.z,fxMax(triangle.b.z,triangle.c.z))}};
    const auto epsilon=Fixed::fromRaw(1);
    if(bounds.min.x==bounds.max.x)bounds.max.x+=epsilon;
    if(bounds.min.y==bounds.max.y)bounds.max.y+=epsilon;
    if(bounds.min.z==bounds.max.z)bounds.max.z+=epsilon;
    if(!add({id,bounds}))return false;
    triangles_[id]=triangle;return true;
}
bool SpatialWorld::blocked(Bounds bounds)const {
    const auto center=(bounds.min+bounds.max)*Fixed::fromRatio(1,2);
    const auto half=(bounds.max-bounds.min)*Fixed::fromRatio(1,2);
    for(auto id:query(bounds))if(!triangles_.contains(id)||castCollider(id,center,{},half).hit)return true;
    return false;
}
Hit SpatialWorld::castCollider(uint32_t id,Vec3 p,Vec3 delta,Vec3 half)const {
    auto collider=colliders_.find(id);if(collider==colliders_.end())return {};
    Fixed t;Vec3 normal;bool hit=false;
    auto triangle=triangles_.find(id);
    if(triangle!=triangles_.end())hit=triangleCast(p,delta,half,triangle->second,t,normal);
    else {const auto& box=collider->second.bounds;hit=intersect(p,delta,{box.min-half,box.max+half},t,normal);}
    return hit?Hit{true,id,t,normal}:Hit{};
}
}
