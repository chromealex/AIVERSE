#include "SpatialWorld.h"
#include <algorithm>

namespace aiverse::world {
namespace {
Fixed component(Vec3 v,int a) { return a==0?v.x:a==1?v.y:v.z; }
}
bool overlaps(const Bounds& a,const Bounds& b) {
    return a.min.x<=b.max.x && a.max.x>=b.min.x && a.min.y<=b.max.y && a.max.y>=b.min.y && a.min.z<=b.max.z && a.max.z>=b.min.z;
}
bool intersect(Vec3 start,Vec3 delta,const Bounds& b,Fixed& t,Vec3& normal) {
    Fixed enter{},leave=1_fx; normal={};
    for(int a=0;a<3;++a) {
        auto p=component(start,a),d=component(delta,a),lo=component(b.min,a),hi=component(b.max,a);
        if(d.raw==0) { if(p<lo || p>hi)return false; continue; }
        auto first=(lo-p)/d,last=(hi-p)/d; Fixed sign=-1_fx;
        if(first>last) { std::swap(first,last);sign=1_fx; }
        if(first>=enter) { enter=first;normal={};if(a==0)normal.x=sign;else if(a==1)normal.y=sign;else normal.z=sign; }
        leave=fxMin(leave,last);
        if(enter>leave)return false;
    }
    if(leave<Fixed{} || enter>1_fx)return false;
    t=enter;return true;
}
Cell SpatialWorld::cell(Vec3 p) const {
    const int64_t size=int64_t(cellSize_)*Fixed::kOne;
    auto floor=[&](Fixed f){int64_t q=f.raw/size;if(f.raw%size<0)--q;return int32_t(q);};
    return {floor(p.x),floor(p.y),floor(p.z)};
}
std::vector<Cell> SpatialWorld::cells(Bounds b) const {
    auto lo=cell(b.min),hi=cell(b.max);
    int64_t nx=int64_t(hi.x)-lo.x+1,ny=int64_t(hi.y)-lo.y+1,nz=int64_t(hi.z)-lo.z+1;
    if(nx<=0||ny<=0||nz<=0||nx>4096||ny>4096||nz>4096||nx*ny*nz>4096)return {};
    std::vector<Cell> result;
    for(int64_t x=lo.x;x<=hi.x;++x)for(int64_t y=lo.y;y<=hi.y;++y)for(int64_t z=lo.z;z<=hi.z;++z)
        result.push_back({int32_t(x),int32_t(y),int32_t(z)});
    return result;
}
bool SpatialWorld::add(Collider c) {
    constexpr auto limit=Fixed::fromInt(1000000);
    for(auto p:{c.bounds.min,c.bounds.max})if(fxAbs(p.x)>limit||fxAbs(p.y)>limit||fxAbs(p.z)>limit)return false;
    if(c.id==0||c.bounds.min.x>=c.bounds.max.x||c.bounds.min.y>=c.bounds.max.y||c.bounds.min.z>=c.bounds.max.z)return false;
    remove(c.id);colliders_.emplace(c.id,c);
    auto occupied=cells(c.bounds);
    if(occupied.empty())oversized_.insert(c.id);
    else for(auto cell:occupied)index_[cell].insert(c.id);
    return true;
}
void SpatialWorld::remove(uint32_t id) {
    auto it=colliders_.find(id);if(it==colliders_.end())return;
    for(auto cell:cells(it->second.bounds)) { auto bucket=index_.find(cell);if(bucket!=index_.end()){bucket->second.erase(id);if(bucket->second.empty())index_.erase(bucket);} }
    oversized_.erase(id);colliders_.erase(it);
}
void SpatialWorld::clear(){colliders_.clear();index_.clear();oversized_.clear();}
std::vector<uint32_t> SpatialWorld::query(Bounds b) const {
    auto occupied=cells(b);std::set<uint32_t> candidates=oversized_;
    if(occupied.empty())for(const auto& [id,c]:colliders_) { (void)c;candidates.insert(id); }
    else for(auto cell:occupied) {auto it=index_.find(cell);if(it!=index_.end())candidates.insert(it->second.begin(),it->second.end());}
    std::vector<uint32_t> result;
    for(auto id:candidates)if(overlaps(b,colliders_.at(id).bounds))result.push_back(id);
    return result;
}
Hit SpatialWorld::cast(Vec3 p,Vec3 delta,Vec3 half) const {
    auto end=p+delta;Bounds swept{{fxMin(p.x,end.x)-half.x,fxMin(p.y,end.y)-half.y,fxMin(p.z,end.z)-half.z},
                                {fxMax(p.x,end.x)+half.x,fxMax(p.y,end.y)+half.y,fxMax(p.z,end.z)+half.z}};
    Hit hit;
    for(auto id:query(swept)) {
        const auto& box=colliders_.at(id).bounds;Fixed t;Vec3 normal;
        if(intersect(p,delta,{box.min-half,box.max+half},t,normal) && (!hit.hit||t<hit.fraction))hit={true,id,t,normal};
    }
    return hit;
}
Vec3 relativeTo(Vec3 p,Cell c,int size) {
    return p-Vec3{Fixed::fromInt(int64_t(c.x)*size),Fixed::fromInt(int64_t(c.y)*size),Fixed::fromInt(int64_t(c.z)*size)};
}
void Streamer::pump(size_t budget,const Load& load,const Unload& unload) {
    // Load first: a failed replacement must not destroy still-useful resident content.
    for(auto c:desired_)if(!resident_.contains(c)&&budget){--budget;if(load(c))resident_.insert(c);}
    if(!ready())return;
    for(auto it=resident_.begin();it!=resident_.end();)if(!desired_.contains(*it)){unload(*it);it=resident_.erase(it);}else ++it;
}
bool Streamer::ready() const {for(auto c:desired_)if(!resident_.contains(c))return false;return true;}
}
