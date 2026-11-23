#pragma once
#include <array>
#include <map>
#include <vector>
#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <cmath>
#include <limits>
#include <utility>
#include <cstddef>
namespace aiverse::lib::algorithms {
template<std::size_t N,class Id=uint64_t> class SpatialGrid {
public:
    using Point=std::array<int64_t,N>;
    struct Bounds {Point min,max;}; // inclusive
private:
    int64_t cellSize_;
    std::map<Id,Bounds> objects_;
    std::map<Point,std::vector<Id>> cells_;
    std::vector<Id> large_;
    static constexpr size_t maxCells=4096;
    int64_t cell(int64_t x)const {return x/cellSize_-(x%cellSize_<0);}
    bool range(const Bounds& b,Point& lo,Point& hi)const {
        size_t count=1;
        for(size_t i=0;i<N;++i){
            if(b.min[i]>b.max[i])throw std::invalid_argument("invalid bounds");
            lo[i]=cell(b.min[i]);hi[i]=cell(b.max[i]);
            const auto span=uint64_t(hi[i])-uint64_t(lo[i]);
            if(span>=maxCells||count>maxCells/(span+1))return false;
            count*=size_t(span+1);
        }return true;
    }
    template<class F> static void visit(Point lo,const Point& hi,F f){
        Point p=lo;
        for(;;){f(p);size_t i=0;for(;i<N;++i){if(p[i]<hi[i]){++p[i];break;}p[i]=lo[i];}if(i==N)break;}
    }
    static bool intersects(const Bounds&a,const Bounds&b){for(size_t i=0;i<N;++i)if(a.max[i]<b.min[i]||b.max[i]<a.min[i])return false;return true;}
public:
    explicit SpatialGrid(int64_t size):cellSize_(size){static_assert(N>0);if(size<=0)throw std::invalid_argument("invalid cell size");}
    void clear(){objects_.clear();cells_.clear();large_.clear();}
    bool erase(Id id){
        auto it=objects_.find(id);if(it==objects_.end())return false;
        Point lo{},hi{};
        if(range(it->second,lo,hi))visit(lo,hi,[&](const Point&p){auto c=cells_.find(p);if(c!=cells_.end()){auto& v=c->second;v.erase(std::remove(v.begin(),v.end(),id),v.end());if(v.empty())cells_.erase(c);}});
        else large_.erase(std::remove(large_.begin(),large_.end(),id),large_.end());
        objects_.erase(it);return true;
    }
    void upsert(Id id,Bounds b){
        for(size_t i=0;i<N;++i)if(b.min[i]>b.max[i])throw std::invalid_argument("invalid bounds");
        Point lo{},hi{};const bool small=range(b,lo,hi);
        erase(id);objects_.emplace(id,b);
        if(small)visit(lo,hi,[&](const Point&p){cells_[p].push_back(id);});else large_.push_back(id);
    }
    void upsertPoint(Id id,Point p){upsert(id,{p,p});}
    void query(Bounds b,std::vector<Id>& out)const{
        for(size_t i=0;i<N;++i)if(b.min[i]>b.max[i])throw std::invalid_argument("invalid bounds");
        out.clear();Point lo{},hi{};
        if(range(b,lo,hi)){
            out=large_;visit(lo,hi,[&](const Point&p){auto it=cells_.find(p);if(it!=cells_.end())out.insert(out.end(),it->second.begin(),it->second.end());});
            std::sort(out.begin(),out.end());out.erase(std::unique(out.begin(),out.end()),out.end());
            out.erase(std::remove_if(out.begin(),out.end(),[&](Id id){return !intersects(objects_.at(id),b);}),out.end());
        }else for(const auto& [id,a]:objects_)if(intersects(a,b))out.push_back(id);
    }
    // Distance to the nearest point of an inclusive AABB.
    static long double distanceSquared(const Point& p,const Bounds& b){
        long double d=0;
        for(size_t i=0;i<N;++i){
            const long double delta=p[i]<b.min[i]?static_cast<long double>(b.min[i])-p[i]:
                p[i]>b.max[i]?static_cast<long double>(p[i])-b.max[i]:0;
            d+=delta*delta;
        }return d;
    }
    void radius(const Point& p,int64_t r,std::vector<Id>& out)const{
        if(r<0)throw std::invalid_argument("negative radius");
        Bounds b{};
        for(size_t i=0;i<N;++i){
            b.min[i]=p[i]<std::numeric_limits<int64_t>::min()+r?std::numeric_limits<int64_t>::min():p[i]-r;
            b.max[i]=p[i]>std::numeric_limits<int64_t>::max()-r?std::numeric_limits<int64_t>::max():p[i]+r;
        }
        query(b,out);
        const long double rr=static_cast<long double>(r)*r;
        out.erase(std::remove_if(out.begin(),out.end(),[&](Id id){return distanceSquared(p,objects_.at(id))>rr;}),out.end());
    }
    // Exact exhaustive nearest search; bounded output, stable ID tie break.
    void nearest(const Point& p,size_t k,std::vector<Id>& out)const{
        out.clear();if(!k)return;
        std::vector<std::pair<long double,Id>> ranked;
        ranked.reserve(objects_.size());
        for(const auto& [id,b]:objects_)ranked.emplace_back(distanceSquared(p,b),id);
        k=std::min(k,ranked.size());
        std::partial_sort(ranked.begin(),ranked.begin()+k,ranked.end());
        for(size_t i=0;i<k;++i)out.push_back(ranked[i].second);
    }
    // Segment query via slab clipping. Results sorted by entry parameter then ID.
    void segment(const Point& from,const Point& to,std::vector<Id>& out)const{
        Bounds box{};for(size_t i=0;i<N;++i){box.min[i]=std::min(from[i],to[i]);box.max[i]=std::max(from[i],to[i]);}
        query(box,out);
        std::vector<std::pair<long double,Id>> hits;
        for(Id id:out){
            const auto& b=objects_.at(id);long double lo=0,hi=1;
            for(size_t i=0;i<N&&lo<=hi;++i){
                const long double d=static_cast<long double>(to[i])-from[i];
                if(d==0){if(from[i]<b.min[i]||from[i]>b.max[i]){lo=1;hi=0;}continue;}
                auto a=(static_cast<long double>(b.min[i])-from[i])/d;
                auto z=(static_cast<long double>(b.max[i])-from[i])/d;
                if(a>z)std::swap(a,z);lo=std::max(lo,a);hi=std::min(hi,z);
            }
            if(lo<=hi)hits.emplace_back(lo,id);
        }
        std::sort(hits.begin(),hits.end());out.clear();for(const auto& h:hits)out.push_back(h.second);
    }
    size_t size()const{return objects_.size();}
};
template<class Id=uint64_t> using SpatialGrid2D=SpatialGrid<2,Id>;
template<class Id=uint64_t> using SpatialGrid3D=SpatialGrid<3,Id>;
}
