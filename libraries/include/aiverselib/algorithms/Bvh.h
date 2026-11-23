#pragma once
#include "Spatial.h"
#include <span>
#include <limits>
namespace aiverse::lib::algorithms {
template<std::size_t N,class Id=uint64_t> class Bvh {
public:
    using Grid=SpatialGrid<N,Id>;
    using Bounds=typename Grid::Bounds;
    using Point=typename Grid::Point;
    struct Entry {Id id;Bounds bounds;};
private:
    struct Node {Bounds bounds;size_t begin,end,left=0,right=0;bool leaf=true;};
    std::vector<Entry> entries_;
    std::vector<Node> nodes_;
    size_t leafSize_=8;
    static Bounds merge(Bounds a,const Bounds& b){
        for(size_t i=0;i<N;++i){a.min[i]=std::min(a.min[i],b.min[i]);a.max[i]=std::max(a.max[i],b.max[i]);}return a;
    }
    static bool overlap(const Bounds&a,const Bounds&b){
        for(size_t i=0;i<N;++i)if(a.max[i]<b.min[i]||b.max[i]<a.min[i])return false;return true;
    }
    static void validate(const Bounds& b){for(size_t i=0;i<N;++i)if(b.min[i]>b.max[i])throw std::invalid_argument("invalid BVH bounds");}
    size_t buildNode(size_t begin,size_t end){
        Bounds b=entries_[begin].bounds;
        for(size_t i=begin+1;i<end;++i)b=merge(b,entries_[i].bounds);
        size_t index=nodes_.size();nodes_.push_back({b,begin,end});
        if(end-begin<=leafSize_)return index;
        size_t axis=0;
        for(size_t i=1;i<N;++i)if(uint64_t(b.max[i])-uint64_t(b.min[i])>uint64_t(b.max[axis])-uint64_t(b.min[axis]))axis=i;
        size_t mid=begin+(end-begin)/2;
        std::nth_element(entries_.begin()+begin,entries_.begin()+mid,entries_.begin()+end,[axis](const Entry&a,const Entry&b){
            if(a.bounds.min[axis]!=b.bounds.min[axis])return a.bounds.min[axis]<b.bounds.min[axis];
            if(a.bounds.max[axis]!=b.bounds.max[axis])return a.bounds.max[axis]<b.bounds.max[axis];return a.id<b.id;
        });
        const size_t left=buildNode(begin,mid),right=buildNode(mid,end);
        nodes_[index].leaf=false;nodes_[index].left=left;nodes_[index].right=right;return index;
    }
    void queryNode(size_t i,const Bounds& b,std::vector<Id>& out)const{
        const auto& n=nodes_[i];if(!overlap(n.bounds,b))return;
        if(n.leaf){for(size_t j=n.begin;j<n.end;++j)if(overlap(entries_[j].bounds,b))out.push_back(entries_[j].id);}
        else{queryNode(n.left,b,out);queryNode(n.right,b,out);}
    }
public:
    void build(std::span<const Entry> entries,size_t leafSize=8){
        if(!leafSize)throw std::invalid_argument("invalid leaf size");
        Bvh next;next.leafSize_=leafSize;next.entries_.assign(entries.begin(),entries.end());
        for(const auto& e:next.entries_)validate(e.bounds);
        std::sort(next.entries_.begin(),next.entries_.end(),[](const Entry&a,const Entry&b){return a.id<b.id;});
        for(size_t i=1;i<next.entries_.size();++i)if(next.entries_[i-1].id==next.entries_[i].id)throw std::invalid_argument("duplicate BVH id");
        if(!next.entries_.empty())next.buildNode(0,next.entries_.size());
        *this=std::move(next);
    }
    void query(const Bounds& b,std::vector<Id>& out)const{
        validate(b);out.clear();if(!nodes_.empty())queryNode(0,b,out);std::sort(out.begin(),out.end());
    }
    void queryPoint(const Point& p,std::vector<Id>& out)const{query({p,p},out);}
    size_t size()const{return entries_.size();}
    size_t nodeCount()const{return nodes_.size();}
};
template<class Id=uint64_t> using Bvh2D=Bvh<2,Id>;
template<class Id=uint64_t> using Bvh3D=Bvh<3,Id>;
}
