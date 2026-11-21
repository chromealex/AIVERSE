#include "StrataSourceSeams.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <numeric>
#include <utility>
#include <vector>

namespace aiverse::render::strata {
namespace {
using Point=std::array<double,3>;
using Edge=std::pair<uint32_t,uint32_t>;
using GeometricEdge=std::pair<Point,Point>;
struct Segment {
    Point a{},b{};
    uint32_t partition=0;
};
struct Aabb {
    Point low{},high{};
};
struct BvhNode {
    Aabb bounds{};
    size_t begin=0,end=0;
    int left=-1,right=-1;
};
bool fail(std::string* error,const char* message) {
    if(error)*error=message;
    return false;
}
Point point(const Model& source,uint32_t index,const mat4& transform) {
    const float3 p=transform.transformPoint(source.vertices[index].pos);
    return {double(p.x),double(p.y),double(p.z)};
}
bool finite(const Point& point) {
    for(double v:point)if(!std::isfinite(v))return false;
    return true;
}
Aabb emptyBounds() {
    Aabb out;
    for(unsigned axis=0;axis<3;++axis){
        out.low[axis]=std::numeric_limits<double>::infinity();
        out.high[axis]=-out.low[axis];
    }
    return out;
}
void grow(Aabb& bounds,const Point& p) {
    for(unsigned k=0;k<3;++k){
        bounds.low[k]=std::min(bounds.low[k],p[k]);
        bounds.high[k]=std::max(bounds.high[k],p[k]);
    }
}
void grow(Aabb& bounds,const Aabb& p) {
    grow(bounds,p.low);
    grow(bounds,p.high);
}
Aabb segmentBounds(const Segment& seg) {
    Aabb b=emptyBounds();
    grow(b,seg.a);grow(b,seg.b);
    return b;
}
bool nearbyOnSegment(const Point& p,const Segment& s,
                     double epsilon,bool& suspicious) {
    double dot=0,len2=0;
    for(unsigned k=0;k<3;++k){
        const double d=s.b[k]-s.a[k];
        dot+=(p[k]-s.a[k])*d;
        len2+=d*d;
    }
    if(len2==0)return false;
    const double length=std::sqrt(len2);
    const double t=dot/len2;
    const double margin=std::min(.49,epsilon/length);
    if(t < -margin || t > 1.+margin)return false;
    double d2=0;
    for(unsigned k=0;k<3;++k){
        const double closest=s.a[k]+std::clamp(t,0.,1.)*(s.b[k]-s.a[k]);
        d2+=(p[k]-closest)*(p[k]-closest);
    }
    if(d2>epsilon*epsilon)return false;
    // Exact matching endpoint is a valid original welded/duplicated seam;
    // nearby non-identical endpoints or a vertex on the interior of an
    // unrelated material border indicate an imported T junction or gap.
    suspicious=p!=s.a && p!=s.b;
    return true;
}
} // namespace

bool auditModelSourceSeams(const Model& model,const ModelAsset& asset,
                           SourceSeamAudit& report,std::string* error) {
    if(error)error->clear();
    if(asset.primitives.empty() || model.vertices.empty() ||
       model.indices.empty() ||
       (!model.nodes.empty() &&
        model.bindWorld.size()!=model.nodes.size()))
        return fail(error,"source seam audit requires original CPU source and finalized node transforms");
    SourceSeamAudit next{};
    std::vector<std::vector<mat4>> meshTransforms(model.meshes.size());
    if(model.nodes.empty()) {
        for(auto& transforms:meshTransforms)transforms.push_back(mat4::identity());
    }else{
        for(size_t n=0;n<model.nodes.size();++n){
            const int mesh=model.nodes[n].mesh;
            if(mesh>=0 && size_t(mesh)<meshTransforms.size())
                meshTransforms[size_t(mesh)].push_back(model.bindWorld[n]);
        }
    }

    std::vector<Segment> segments;
    constexpr size_t kMaximumSourceBoundarySegments=250000;
    for(const auto& staged:asset.primitives){
        if(staged.mesh>=model.meshes.size() ||
           staged.primitive>=model.meshes[staged.mesh].primitives.size() ||
           !staged.geometry.seamCertified)
            return fail(error,"source seam audit requires certified original primitives");
        const auto& primitive=model.meshes[staged.mesh].primitives[staged.primitive];
        if(primitive.firstIndex>model.indices.size() ||
           primitive.indexCount<3 || primitive.indexCount%3 ||
           primitive.indexCount>model.indices.size()-primitive.firstIndex)
            return fail(error,"source seam audit has invalid original index span");
        // Topological islands may share only a vertex, or even have
        // DUPLICATE physical UV-seam vertices inside ONE material. Group
        // triangles by shared manifold EDGES, never merely by material ID.
        // A seam between two components of one primitive must be checked
        // by the same cross-boundary BVH as two different materials.
        struct EdgeIncidence {
            std::array<uint8_t,2> direction{};
            uint32_t firstTriangle=UINT32_MAX;
        };
        std::map<Edge,EdgeIncidence> edges;
        const uint32_t triangles=primitive.indexCount/3;
        std::vector<uint32_t> components(triangles);
        std::iota(components.begin(),components.end(),0);
        const auto componentOf=[&](uint32_t id)->uint32_t {
            while(components[id]!=id){
                components[id]=components[components[id]];
                id=components[id];
            }
            return id;
        };
        for(uint32_t t=0;t<triangles;++t){
            const size_t base=primitive.firstIndex+size_t(t)*3;
            const uint32_t tri[3]{model.indices[base],
                                  model.indices[base+1],
                                  model.indices[base+2]};
            for(uint32_t index:tri)
                if(index>=model.vertices.size())
                    return fail(error,"source seam audit vertex out of bounds");
            for(unsigned k=0;k<3;++k){
                const uint32_t a=tri[k],b=tri[(k+1)%3];
                if(a==b)return fail(error,"source seam audit degenerate triangle");
                auto& incidence=edges[{std::min(a,b),std::max(a,b)}];
                uint8_t& count=incidence.direction[a<b?0:1];
                if(++count>1)
                    return fail(error,"source seam audit has nonmanifold material border");
                if(incidence.firstTriangle==UINT32_MAX)
                    incidence.firstTriangle=t;
                else
                    components[componentOf(t)]=
                        componentOf(incidence.firstTriangle);
            }
        }
        for(const auto& transform:meshTransforms[staged.mesh]) {
            std::map<uint32_t,uint32_t> componentPartitions;
            for(const auto& [edge,incidence]:edges){
                if(incidence.direction[0] && incidence.direction[1])
                    continue;
                const uint32_t component=componentOf(
                    incidence.firstTriangle);
                auto it=componentPartitions.find(component);
                if(it==componentPartitions.end()){
                    it=componentPartitions.emplace(
                        component,next.examinedPartitions++).first;
                }
                Segment s{point(model,edge.first,transform),
                          point(model,edge.second,transform),it->second};
                if(!finite(s.a)||!finite(s.b)||s.a==s.b)
                    return fail(error,"source seam audit encountered invalid transformed edge");
                if(segments.size()>=kMaximumSourceBoundarySegments)
                    return fail(error,"source seam audit exceeds bounded boundary budget");
                segments.push_back(s);
            }
        }
    }
    next.boundarySegments=segments.size();
    // A single source partition has no cross-material seams to screen.
    if(segments.empty()){
        report=next;
        return true;
    }

    // An exact position segment may use DIFFERENT original vertex IDs
    // because adjacent materials have independent UV/normal vertices.
    std::map<GeometricEdge,std::vector<uint32_t>> geometric;
    Aabb sourceBounds=emptyBounds();
    for(const auto& s:segments){
        grow(sourceBounds,s.a);grow(sourceBounds,s.b);
        GeometricEdge key=s.a<s.b ? GeometricEdge{s.a,s.b}
                                 : GeometricEdge{s.b,s.a};
        geometric[key].push_back(s.partition);
    }
    for(auto& [edge,parts]:geometric) {
        std::sort(parts.begin(),parts.end());
        parts.erase(std::unique(parts.begin(),parts.end()),parts.end());
        if(parts.size()>2)
            return fail(error,"more than two material borders share one spatial edge");
        if(parts.size()==2)++next.matchedSegments;
    }
    // Relative distance is used ONLY as a suspicious-neighbor filter.
    // Actual matching segments always require exact float coordinates.
    double diagonal2=0;
    for(unsigned k=0;k<3;++k){
        const double d=sourceBounds.high[k]-sourceBounds.low[k];
        diagonal2+=d*d;
    }
    if(!std::isfinite(diagonal2))
        return fail(error,"source seam bounds overflow");
    const double epsilon=std::clamp(
        std::sqrt(diagonal2)*1.0e-5,1.0e-6,1.0e-3);

    // Build a spatial AABB tree of ORIGINAL material-boundary segments;
    // query every endpoint against segments in OTHER partitions. O(E log E)
    // average rather than O(E^2) pairwise checking for huge grid meshes.
    std::vector<size_t> order(segments.size());
    std::iota(order.begin(),order.end(),0);
    std::vector<BvhNode> nodes;
    nodes.reserve(segments.size()*2u);
    const auto build=[&](auto&& self,size_t begin,size_t end)->int {
        const int id=int(nodes.size());
        nodes.emplace_back();
        Aabb bounds=emptyBounds(),centers=emptyBounds();
        for(size_t i=begin;i<end;++i){
            const auto& segment=segments[order[i]];
            grow(bounds,segmentBounds(segment));
            Point center{};
            for(unsigned k=0;k<3;++k)
                center[k]=.5*(segment.a[k]+segment.b[k]);
            grow(centers,center);
        }
        nodes[size_t(id)].bounds=bounds;
        nodes[size_t(id)].begin=begin;
        nodes[size_t(id)].end=end;
        if(end-begin<=8)return id;
        unsigned axis=0;
        for(unsigned k=1;k<3;++k)
            if(centers.high[k]-centers.low[k]>
               centers.high[axis]-centers.low[axis])
                axis=k;
        const size_t midpoint=begin+(end-begin)/2;
        std::nth_element(order.begin()+begin,order.begin()+midpoint,
                         order.begin()+end,[&](size_t a,size_t b){
            const auto& x=segments[a],&y=segments[b];
            const double cx=x.a[axis]+x.b[axis];
            const double cy=y.a[axis]+y.b[axis];
            return cx==cy ? a<b:cx<cy;
        });
        const int left=self(self,begin,midpoint);
        const int right=self(self,midpoint,end);
        nodes[size_t(id)].left=left;
        nodes[size_t(id)].right=right;
        return id;
    };
    const int root=build(build,0,segments.size());
    const auto collision=[&](auto&& self,int id,const Point& p,
                             uint32_t origin)->bool {
        const auto& node=nodes[size_t(id)];
        for(unsigned k=0;k<3;++k)
            if(p[k]<node.bounds.low[k]-epsilon ||
               p[k]>node.bounds.high[k]+epsilon)return false;
        if(node.left<0){
            for(size_t i=node.begin;i<node.end;++i){
                const auto& candidate=segments[order[i]];
                if(candidate.partition==origin)continue;
                bool suspicious=false;
                if(nearbyOnSegment(p,candidate,epsilon,suspicious) &&
                   suspicious)return true;
            }
            return false;
        }
        return self(self,node.left,p,origin) ||
               self(self,node.right,p,origin);
    };
    for(const auto& segment:segments){
        if(collision(collision,root,segment.a,segment.partition) ||
           collision(collision,root,segment.b,segment.partition)){
            ++next.rejectedNearJunctions;
            return fail(error,
                "cross-material seam has a split, near-offset or T-junction border");
        }
    }
    report=next;
    return true;
}
} // namespace aiverse::render::strata
