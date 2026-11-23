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
    struct Node {Bounds bounds;std::vector<Id> ids;std::array<size_t,K> children{};size_t parent=0,slot=0;};
    std::vector<Node> nodes_;
    std::map<Id,Bounds> entries_;
    size_t depth_;
    std::map<Id,size_t> locations_;
    std::vector<size_t> free_;
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
                if(!next){Node fresh{child};fresh.parent=index;fresh.slot=slot;if(free_.empty()){next=nodes_.size();nodes_.push_back(std::move(fresh));}else{next=free_.back();free_.pop_back();nodes_[next]=std::move(fresh);}nodes_[index].children[slot]=next;}
                insert(next,id,b,depth+1);return;
            }
        }
        nodes_[index].ids.push_back(id);locations_[id]=index;
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
        auto index=locations_.at(id);locations_.erase(id);
        auto& ids=nodes_[index].ids;ids.erase(std::remove(ids.begin(),ids.end(),id),ids.end());
        while(index){
            auto& n=nodes_[index];
            if(!n.ids.empty()||std::any_of(n.children.begin(),n.children.end(),[](size_t c){return c!=0;}))break;
            const auto parent=n.parent;nodes_[parent].children[n.slot]=0;
            n=Node{};free_.push_back(index);index=parent;
        }
        return true;
    }
    void query(Bounds b,std::vector<Id>& out)const{validate(b);out.clear();visit(0,b,out);std::sort(out.begin(),out.end());}
    void clear(){auto root=nodes_[0].bounds;nodes_.clear();entries_.clear();locations_.clear();free_.clear();nodes_.push_back(Node{root});}
    size_t activeNodeCount()const{return nodes_.size()-free_.size();}
    size_t allocatedNodeCount()const{return nodes_.size();}
    size_t size()const{return entries_.size();}
};
template<class Id=uint64_t> using Quadtree=SpatialTree<2,Id>;
template<class Id=uint64_t> using Octree=SpatialTree<3,Id>;
}
