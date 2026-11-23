#pragma once
#include "Spatial.h"
#include <numeric>
namespace aiverse::lib::algorithms {
template<size_t N,class Id=uint64_t> class SpatialTree {
    static_assert(N==2||N==3);
public:
    using Grid=SpatialGrid<N,Id>;
    using Bounds=typename Grid::Bounds;
    using Point=typename Grid::Point;
private:
    static constexpr size_t K=1u<<N;
    struct Node {Bounds bounds;std::vector<Id> ids;std::array<size_t,K> children{};};
    std::vector<Node> nodes_;
    std::map<Id,Bounds> entries_;
    size_t depth_;
    static bool contains(const Bounds&a,const Bounds&b){for(size_t i=0;i<N;++i)if(a.min[i]>b.min[i]||a.max[i]<b.max[i])return false;return true;}
    static bool overlaps(const Bounds&a,const Bounds&b){for(size_t i=0;i<N;++i)if(a.max[i]<b.min[i]||b.max[i]<a.min[i])return false;return true;}
    static void validate(const Bounds&b){for(size_t i=0;i<N;++i)if(b.min[i]>b.max[i])throw std::invalid_argument("invalid tree bounds");}
    void insert(size_t index,Id id,const Bounds& b,size_t depth){
        const auto parent=nodes_[index].bounds;
        if(depth<depth_){
            Bounds child=parent;size_t slot=0;bool fits=true;
            for(size_t i=0;i<N;++i){
                const auto mid=std::midpoint(parent.min[i],parent.max[i]);
                if(b.max[i]<=mid)child.max[i]=mid;
                else if(b.min[i]>mid){child.min[i]=mid+1;slot|=size_t(1)<<i;}
                else{fits=false;break;}
            }
            if(fits&&(child.min!=parent.min||child.max!=parent.max)){
                auto next=nodes_[index].children[slot];
                if(!next){next=nodes_.size();nodes_.push_back(Node{child});nodes_[index].children[slot]=next;}
                insert(next,id,b,depth+1);return;
            }
        }
        nodes_[index].ids.push_back(id);
    }
    void visit(size_t i,const Bounds& b,std::vector<Id>& out)const{
        const auto& n=nodes_[i];if(!overlaps(n.bounds,b))return;
        for(auto id:n.ids)if(overlaps(entries_.at(id),b))out.push_back(id);
        for(auto child:n.children)if(child)visit(child,b,out);
    }
public:
    explicit SpatialTree(Bounds root,size_t depth=16):depth_(depth){validate(root);if(depth>64)throw std::invalid_argument("tree depth");nodes_.push_back(Node{root});}
    void upsert(Id id,Bounds b){
        validate(b);if(!contains(nodes_[0].bounds,b))throw std::invalid_argument("outside tree root");
        erase(id);entries_.emplace(id,b);insert(0,id,b,0);
    }
    bool erase(Id id){
        if(!entries_.erase(id))return false;
        for(auto& n:nodes_)n.ids.erase(std::remove(n.ids.begin(),n.ids.end(),id),n.ids.end());
        return true;
    }
    void query(Bounds b,std::vector<Id>& out)const{validate(b);out.clear();visit(0,b,out);std::sort(out.begin(),out.end());}
    void clear(){auto root=nodes_[0].bounds;nodes_.clear();entries_.clear();nodes_.push_back(Node{root});}
    size_t size()const{return entries_.size();}
};
template<class Id=uint64_t> using Quadtree=SpatialTree<2,Id>;
template<class Id=uint64_t> using Octree=SpatialTree<3,Id>;
}
