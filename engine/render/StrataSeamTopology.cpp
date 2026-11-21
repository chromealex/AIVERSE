#include "StrataSeamTopology.h"
#include <algorithm>
#include <limits>
#include <map>
#include <utility>
#include <vector>

namespace aiverse::render::strata {
namespace {
using Edge=std::pair<uint32_t,uint32_t>;
struct Incidence {
    uint32_t forward=0,reverse=0;
};
using EdgeMap=std::map<Edge,Incidence>;
bool reject(std::string* err,const char* message) {
    if(err)*err=message;
    return false;
}
bool edgesOf(const uint32_t* indices,size_t count,EdgeMap& edges,
             std::string* error) {
    if(!indices || count<3 || count%3)
        return reject(error,"seam audit requires nonempty triangle indices");
    for(size_t i=0;i<count;i+=3) {
        const uint32_t tri[3]{indices[i],indices[i+1],indices[i+2]};
        if(tri[0]==tri[1] || tri[1]==tri[2] || tri[2]==tri[0])
            return reject(error,"degenerate triangle in seam contour");
        for(unsigned e=0;e<3;++e) {
            const uint32_t a=tri[e],b=tri[(e+1)%3];
            const Edge key{std::min(a,b),std::max(a,b)};
            auto& incidence=edges[key];
            const bool forward=a<b;
            uint32_t& direction=forward ? incidence.forward:incidence.reverse;
            if(++direction>1)
                return reject(error,"duplicate or nonmanifold oriented edge");
        }
    }
    return true;
}
bool contourOf(const uint32_t* indices,size_t count,EdgeMap& boundary,
               std::string* error) {
    EdgeMap all;
    if(!edgesOf(indices,count,all,error))return false;
    for(const auto& [edge,n]:all) {
        if(n.forward && n.reverse)continue; // manifold internal pair
        boundary.emplace(edge,n);
    }
    return true;
}
bool boundariesEqual(const EdgeMap& a,const EdgeMap& b) {
    if(a.size()!=b.size())return false;
    auto lhs=a.begin(),rhs=b.begin();
    for(;lhs!=a.end();++lhs,++rhs) {
        if(lhs->first!=rhs->first ||
           lhs->second.forward!=rhs->second.forward ||
           lhs->second.reverse!=rhs->second.reverse)return false;
    }
    return true;
}
bool rangeValid(const Asset& a,const ClusterNode& node) {
    const size_t count=a.logicalIndexCount();
    return node.indexCount>=3 && node.indexCount%3==0 &&
           node.firstIndex<=count &&
           node.indexCount<=count-node.firstIndex;
}
} // namespace

bool sameSeamContour(const uint32_t* original,size_t originalIndexCount,
                     const uint32_t* candidate,size_t candidateIndexCount,
                     std::string* error) {
    if(error)error->clear();
    EdgeMap a,b;
    if(!contourOf(original,originalIndexCount,a,error) ||
       !contourOf(candidate,candidateIndexCount,b,error))return false;
    if(!boundariesEqual(a,b))
        return reject(error,"simplified LOD modified the source boundary segments");
    return true;
}

bool auditSeamHierarchy(const Asset& asset,SeamAudit& report,
                        std::string* error) {
    if(error)error->clear();
    if(asset.indices.empty() ||
       asset.bounds.size()!=asset.nodes.size() ||
       !validate(asset.nodes,asset.roots))
        return reject(error,"seam audit requires complete live LOD hierarchy");
    SeamAudit next{};
    for(size_t i=0;i<asset.nodes.size();++i) {
        const auto& node=asset.nodes[i];
        if(!rangeValid(asset,node))
            return reject(error,"seam audit encountered invalid node index span");
        EdgeMap boundary;
        if(!contourOf(asset.indices.data()+node.firstIndex,
                      node.indexCount,boundary,error))
            return false;
        ++next.checkedNodes;
        next.contourEdges+=boundary.size();
        if(!node.childCount)continue;
        std::vector<uint32_t> childUnion;
        uint64_t sum=0;
        for(uint32_t c=0;c<node.childCount;++c) {
            const auto& child=asset.nodes[node.firstChild+c];
            if(!rangeValid(asset,child))
                return reject(error,"seam audit child has invalid index span");
            sum+=child.indexCount;
            if(sum>uint64_t(std::numeric_limits<uint32_t>::max()))
                return reject(error,"seam audit child union too large");
        }
        childUnion.reserve(size_t(sum));
        for(uint32_t c=0;c<node.childCount;++c) {
            const auto& child=asset.nodes[node.firstChild+c];
            const auto begin=asset.indices.begin()+child.firstIndex;
            childUnion.insert(childUnion.end(),begin,begin+child.indexCount);
        }
        if(!sameSeamContour(childUnion.data(),childUnion.size(),
                            asset.indices.data()+node.firstIndex,
                            node.indexCount,error))
            return false;
        ++next.verifiedParents;
    }
    report=next;
    return true;
}
} // namespace aiverse::render::strata
