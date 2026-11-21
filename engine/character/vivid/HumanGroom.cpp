#include "HumanGroom.h"
#include "GroomSelfCollision.h"
#include "GroomSegmentCollision.h"
#include "HumanMaterials.h"
#include <algorithm>
#include <array>
#include <functional>
#include <numeric>
#include <unordered_map>
#include <chrono>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace aiverse::vivid {
namespace {
using namespace render;
constexpr float kGolden=2.39996322972865332f;
float3 cardSide(float3 tangent) {
    auto axis=std::fabs(tangent.z)<0.92f?float3{0,0,1}:float3{1,0,0};
    return normalize(cross(tangent,axis));
}
bool good(float3 v){
    return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z) &&
           std::fabs(v.x)<100000.0f&&std::fabs(v.y)<100000.0f&&std::fabs(v.z)<100000.0f;
}

float3 closestTriangle(float3 p,float3 a,float3 b,float3 c) {
    // Real-Time Collision Detection, closest point on triangle.
    const auto ab=b-a, ac=c-a, ap=p-a;
    const float d1=dot(ab,ap), d2=dot(ac,ap);
    if(d1<=0&&d2<=0)return a;
    const auto bp=p-b;
    const float d3=dot(ab,bp),d4=dot(ac,bp);
    if(d3>=0&&d4<=d3)return b;
    const float vc=d1*d4-d3*d2;
    if(vc<=0&&d1>=0&&d3<=0)return a+ab*(d1/(d1-d3));
    const auto cp=p-c;
    const float d5=dot(ab,cp),d6=dot(ac,cp);
    if(d6>=0&&d5<=d6)return c;
    const float vb=d5*d2-d1*d6;
    if(vb<=0&&d2>=0&&d6<=0)return a+ac*(d2/(d2-d6));
    const float va=d3*d6-d5*d4;
    if(va<=0&&(d4-d3)>=0&&(d5-d6)>=0)
        return b+(c-b)*((d4-d3)/((d4-d3)+(d5-d6)));
    const float denom=1.0f/(va+vb+vc);
    return a+ab*(vb*denom)+ac*(vc*denom);
}
bool rayHits(float3 origin,float3 direction,float3 a,float3 b,float3 c) {
    const auto e1=b-a,e2=c-a;
    const auto crossP=cross(direction,e2);
    const float det=dot(e1,crossP);
    if(std::fabs(det)<1e-7f)return false;
    const float inv=1.0f/det;
    const auto tvec=origin-a;
    const float u=dot(tvec,crossP)*inv;
    if(u<=1e-6f || u>=1.0f-1e-6f)return false;
    const auto qvec=cross(tvec,e1);
    const float v=dot(direction,qvec)*inv;
    if(v<=1e-6f || u+v>=1.0f-1e-6f)return false;
    return dot(e2,qvec)*inv>1e-5f;
}
}

bool HumanGroomRuntime::setCollisionMesh(
        std::span<const render::float3> vertices,
        std::span<const std::uint32_t> triangles) {
    if(vertices.empty()||vertices.size()>16384||
       triangles.empty()||triangles.size()%3||
       triangles.size()>32768*3)return false;
    for(const auto p:vertices)if(!good(p))return false;
    std::vector<CollisionTriangle> proposed;
    proposed.reserve(triangles.size()/3);
    for(std::size_t i=0;i<triangles.size();i+=3) {
        const auto a=triangles[i],b=triangles[i+1],c=triangles[i+2];
        if(a>=vertices.size()||b>=vertices.size()||c>=vertices.size()||
           a==b||a==c||b==c)return false;
        const auto ab=vertices[b]-vertices[a],ac=vertices[c]-vertices[a];
        if(length(cross(ab,ac))<1e-7f)return false;
        proposed.push_back({vertices[a],vertices[b],vertices[c]});
    }
    // Reject open/non-manifold surfaces before parity testing. Every
    // undirected edge must belong to exactly two triangles.
    std::unordered_map<std::uint64_t,std::uint8_t> edges;
    edges.reserve(triangles.size());
    for(std::size_t i=0;i<triangles.size();i+=3) {
        for(int e=0;e<3;++e) {
            const auto a=triangles[i+e],b=triangles[i+(e+1)%3];
            const auto lo=std::min(a,b),hi=std::max(a,b);
            const auto key=(std::uint64_t(lo)<<32)|hi;
            auto& count=edges[key];
            if(++count>2)return false;
        }
    }
    for(const auto& edge:edges)if(edge.second!=2)return false;
    std::vector<std::uint32_t> order(proposed.size());
    std::iota(order.begin(),order.end(),std::uint32_t(0));
    std::vector<CollisionNode> nodes;
    nodes.reserve(proposed.size()*2);
    auto coord=[](render::float3 p,int axis) {
        return axis==0?p.x:axis==1?p.y:p.z;
    };
    std::function<std::uint32_t(std::uint32_t,std::uint32_t)> build =
        [&](std::uint32_t begin,std::uint32_t count)->std::uint32_t {
        CollisionNode node;
        const float inf=std::numeric_limits<float>::max();
        node.min={inf,inf,inf};node.max={-inf,-inf,-inf};
        auto expand=[&](render::float3 p){
            node.min.x=std::min(node.min.x,p.x);
            node.min.y=std::min(node.min.y,p.y);
            node.min.z=std::min(node.min.z,p.z);
            node.max.x=std::max(node.max.x,p.x);
            node.max.y=std::max(node.max.y,p.y);
            node.max.z=std::max(node.max.z,p.z);
        };
        for(std::uint32_t i=begin;i<begin+count;++i) {
            const auto& t=proposed[order[i]];
            expand(t.a);expand(t.b);expand(t.c);
        }
        const auto id=std::uint32_t(nodes.size());
        nodes.push_back(node);
        if(count<=kCollisionLeafSize) {
            nodes[id].begin=begin;
            nodes[id].count=count;
            return id;
        }
        const auto extent=node.max-node.min;
        const int axis=(extent.x>=extent.y && extent.x>=extent.z)?0:
                       (extent.y>=extent.z)?1:2;
        const auto middle=begin+count/2;
        std::nth_element(order.begin()+begin,order.begin()+middle,
            order.begin()+begin+count,[&](auto a,auto b){
                const auto& ta=proposed[a];const auto& tb=proposed[b];
                const auto ca=(ta.a+ta.b+ta.c)*(1.0f/3.0f);
                const auto cb=(tb.a+tb.b+tb.c)*(1.0f/3.0f);
                const auto va=coord(ca,axis),vb=coord(cb,axis);
                return va==vb?a<b:va<vb;
            });
        const auto left=build(begin,count/2);
        const auto right=build(middle,count-count/2);
        nodes[id].left=left;nodes[id].right=right;
        return id;
    };
    build(0,std::uint32_t(order.size()));
    // Stage every allocation before publishing the new collider. A failed
    // allocation must not leave triangles and source indices mismatched.
    std::vector<std::uint32_t> sourceIndices(triangles.begin(),triangles.end());
    collisionTriangles_=std::move(proposed);
    collisionOrder_=std::move(order);
    collisionNodes_=std::move(nodes);
    collisionSourceIndices_=std::move(sourceIndices);
    collisionSourceVertexCount_=vertices.size();
    // Triangle IDs are specific to the old topology. Replacing a collider
    // invalidates old barycentric anchor IDs until explicit reattachment.
    for(auto& guide:guides_)
        guide.scalpTriangle=std::numeric_limits<std::uint32_t>::max();
    return true;
}
bool HumanGroomRuntime::refitCollisionMesh(
        std::span<const render::float3> vertices) {
    using namespace render;
    if(collisionTriangles_.empty()||
       vertices.size()!=collisionSourceVertexCount_)return false;
    for(const auto& p:vertices)if(!good(p))return false;
    auto proposed=collisionTriangles_;
    for(std::size_t i=0;i<proposed.size();++i) {
        const auto base=i*3;
        const auto a=collisionSourceIndices_[base];
        const auto b=collisionSourceIndices_[base+1];
        const auto c=collisionSourceIndices_[base+2];
        proposed[i]={vertices[a],vertices[b],vertices[c]};
        if(length(cross(proposed[i].b-proposed[i].a,
                        proposed[i].c-proposed[i].a))<1e-7f)return false;
    }
    auto nodes=collisionNodes_;
    const float inf=std::numeric_limits<float>::max();
    auto expand=[](float3& lo,float3& hi,float3 p) {
        lo.x=std::min(lo.x,p.x);lo.y=std::min(lo.y,p.y);
        lo.z=std::min(lo.z,p.z);
        hi.x=std::max(hi.x,p.x);hi.y=std::max(hi.y,p.y);
        hi.z=std::max(hi.z,p.z);
    };
    for(std::size_t i=nodes.size();i-- >0;) {
        auto& node=nodes[i];
        if(!node.count) {
            const auto& a=nodes[node.left];
            const auto& b=nodes[node.right];
            node.min={std::min(a.min.x,b.min.x),
                      std::min(a.min.y,b.min.y),
                      std::min(a.min.z,b.min.z)};
            node.max={std::max(a.max.x,b.max.x),
                      std::max(a.max.y,b.max.y),
                      std::max(a.max.z,b.max.z)};
        }else{
            float3 lo{inf,inf,inf},hi{-inf,-inf,-inf};
            for(std::uint32_t j=node.begin;j<node.begin+node.count;++j) {
                const auto& tri=proposed[collisionOrder_[j]];
                expand(lo,hi,tri.a);expand(lo,hi,tri.b);expand(lo,hi,tri.c);
            }
            node.min=lo;node.max=hi;
        }
    }
    // Stage anchored-guide updates too, so a bad deformation cannot
    // partially publish either geometry or root coordinates.
    auto movedGuides=guides_;
    const auto center=(nodes.front().min+nodes.front().max)*0.5f;
    for(auto& guide:movedGuides) {
        if(guide.scalpTriangle==std::numeric_limits<std::uint32_t>::max())
            continue;
        if(guide.scalpTriangle>=proposed.size())return false;
        const auto& tri=proposed[guide.scalpTriangle];
        auto normal=normalize(cross(tri.b-tri.a,tri.c-tri.a));
        const auto contact=tri.a*guide.scalpBary.x+
                           tri.b*guide.scalpBary.y+
                           tri.c*guide.scalpBary.z;
        if(dot(normal,contact-center)<0.0f)normal=normal*(-1.0f);
        const auto newRoot=contact+normal*0.002f;
        if(!good(normal)||!good(newRoot))return false;
        const auto offset=newRoot-guide.root;
        guide.root=newRoot;
        guide.normal=normal;
        for(auto& p:guide.rest)p=p+offset;
        for(auto& p:guide.position)p=p+offset;
        for(auto& p:guide.previous)p=p+offset;
        guide.rest[0]=guide.position[0]=guide.previous[0]=newRoot;
    }
    collisionTriangles_=std::move(proposed);
    collisionNodes_=std::move(nodes);
    guides_=std::move(movedGuides);
    return true;
}
bool HumanGroomRuntime::attachRootsToCollisionMesh() {
    if(collisionTriangles_.empty()||guides_.empty())return false;
    using namespace render;
    const auto& bounds=collisionNodes_.front();
    const auto center=(bounds.min+bounds.max)*0.5f;
    // Stage guide attachment as a single transaction. A malformed collider,
    // degenerate outward normal or non-finite result cannot partly move hair.
    auto attached=guides_;
    for(auto& guide:attached) {
        float nearest=std::numeric_limits<float>::max();
        float3 root=guide.root,normal=guide.normal;
        // BVH nearest-surface query for every generated root. This is
        // O(log triangles) on typical closed scalp proxies; unlike the old
        // all-triangle scan it scales to dense strand authoring.
        auto distance2=[&](const CollisionNode& node) {
            const float dx=std::max({node.min.x-root.x,0.0f,root.x-node.max.x});
            const float dy=std::max({node.min.y-root.y,0.0f,root.y-node.max.y});
            const float dz=std::max({node.min.z-root.z,0.0f,root.z-node.max.z});
            return dx*dx+dy*dy+dz*dz;
        };
        std::array<std::uint32_t,64> stack{};
        std::size_t size=0;
        stack[size++]=0;
        float3 nearestPoint=root;
        std::uint32_t nearestTriangle=std::numeric_limits<std::uint32_t>::max();
        while(size) {
            const auto& node=collisionNodes_[stack[--size]];
            if(distance2(node)>nearest)continue;
            if(node.count) {
                for(std::uint32_t i=node.begin;i<node.begin+node.count;++i) {
                    const auto& tri=collisionTriangles_[collisionOrder_[i]];
                    const auto p=closestTriangle(guide.root,tri.a,tri.b,tri.c);
                    const auto delta=p-guide.root;
                    const float dist2=dot(delta,delta);
                    if(dist2>=nearest)continue;
                    nearest=dist2;
                    nearestPoint=p;
                    nearestTriangle=collisionOrder_[i];
                    normal=normalize(cross(tri.b-tri.a,tri.c-tri.a));
                    if(dot(normal,p-center)<0.0f)normal=normal*(-1.0f);
                }
            }else{
                stack[size++]=node.left;
                stack[size++]=node.right;
            }
        }
        if(!std::isfinite(nearest) || !good(nearestPoint) || !good(normal))
            return false;
        if(nearestTriangle>=collisionTriangles_.size())return false;
        const auto& triangle=collisionTriangles_[nearestTriangle];
        const auto u=triangle.b-triangle.a, v=triangle.c-triangle.a;
        const auto w=nearestPoint-triangle.a;
        const float aa=dot(u,u),ab=dot(u,v),bb=dot(v,v);
        const float wa=dot(w,u),wb=dot(w,v);
        const float determinant=aa*bb-ab*ab;
        if(!std::isfinite(determinant)||determinant<1e-10f)return false;
        const float beta=(wa*bb-wb*ab)/determinant;
        const float gamma=(wb*aa-wa*ab)/determinant;
        guide.scalpTriangle=nearestTriangle;
        guide.scalpBary={1.0f-beta-gamma,beta,gamma};
        root=nearestPoint;
        const auto nextRoot=root+normal*0.002f;
        if(!good(nextRoot))return false;
        const auto offset=nextRoot-guide.root;
        guide.root=nextRoot;
        guide.normal=normal;
        for(auto& p:guide.rest)p=p+offset;
        for(auto& p:guide.position)p=p+offset;
        for(auto& p:guide.previous)p=p+offset;
        guide.rest[0]=guide.position[0]=guide.previous[0]=guide.root;
    }
    guides_=std::move(attached);
    return true;
}
void HumanGroomRuntime::clearCollisionMesh() noexcept {
    collisionTriangles_.clear();
    collisionOrder_.clear();
    collisionNodes_.clear();
    collisionSourceIndices_.clear();
    collisionSourceVertexCount_=0;
    for(auto& guide:guides_)
        guide.scalpTriangle=std::numeric_limits<std::uint32_t>::max();
}
render::float3 HumanGroomRuntime::resolveCollision(
        render::float3 p,const GroomGuide& guide) const {
    using namespace render;
    if(!collisionTriangles_.empty()) {
        // BVH traversal: ray parity plus nearest surface in a single pass.
        // No per-strand allocation; tree depth is bounded by median splits.
        const float3 ray=normalize(float3{1.0f,0.173f,0.091f});
        auto boxDistance2=[&](const CollisionNode& n) {
            const float dx=std::max({n.min.x-p.x,0.0f,p.x-n.max.x});
            const float dy=std::max({n.min.y-p.y,0.0f,p.y-n.max.y});
            const float dz=std::max({n.min.z-p.z,0.0f,p.z-n.max.z});
            return dx*dx+dy*dy+dz*dz;
        };
        auto rayIntersectsBox=[&](const CollisionNode& n) {
            float nearT=0.0f,farT=std::numeric_limits<float>::max();
            for(int axis=0;axis<3;++axis) {
                const float o=axis==0?p.x:axis==1?p.y:p.z;
                const float d=axis==0?ray.x:axis==1?ray.y:ray.z;
                const float a=axis==0?n.min.x:axis==1?n.min.y:n.min.z;
                const float b=axis==0?n.max.x:axis==1?n.max.y:n.max.z;
                if(std::fabs(d)<1e-8f) {
                    if(o<a||o>b)return false;
                    continue;
                }
                const float t1=(a-o)/d,t2=(b-o)/d;
                nearT=std::max(nearT,std::min(t1,t2));
                farT=std::min(farT,std::max(t1,t2));
                if(farT<nearT)return false;
            }
            return true;
        };
        std::size_t intersections=0;
        auto closest=p;
        float nearest=std::numeric_limits<float>::max();
        struct Visit {std::uint32_t index;bool nearest;};
        std::array<Visit,128> stack{};
        std::size_t size=0;
        stack[size++]={0,true};
        stack[size++]={0,false};
        while(size) {
            const auto visit=stack[--size];
            const auto& node=collisionNodes_[visit.index];
            if(visit.nearest) {
                if(boxDistance2(node)>nearest)continue;
            }else if(!rayIntersectsBox(node))continue;
            if(node.count) {
                for(std::uint32_t i=node.begin;i<node.begin+node.count;++i) {
                    const auto& tri=collisionTriangles_[collisionOrder_[i]];
                    if(visit.nearest) {
                        const auto candidate=closestTriangle(p,tri.a,tri.b,tri.c);
                        const auto delta=candidate-p;
                        const float d2=dot(delta,delta);
                        if(d2<nearest){nearest=d2;closest=candidate;}
                    }else if(rayHits(p,ray,tri.a,tri.b,tri.c)) {
                        ++intersections;
                    }
                }
            }else {
                // Median-split BVH depth is at most 13 for configured budget.
                // 128 entries leaves generous room even with both query modes.
                stack[size++]={node.left,visit.nearest};
                stack[size++]={node.right,visit.nearest};
            }
        }
        if(intersections%2==1) {
            // Move from inside to the nearest surface, then slightly beyond.
            const auto direction=closest-p;
            if(length(direction)>1e-6f)
                return closest+normalize(direction)*0.001f;
            return closest+guide.normal*0.001f;
        }
        return p;
    }
    const float3 scaled{p.x/scalpRadii_.x,
                        p.y/scalpRadii_.y,
                        p.z/scalpRadii_.z};
    const float radius=length(scaled);
    if(radius>=1.0f)return p;
    return radius>1e-5f
        ? p*(1.001f/radius)
        : guide.root+guide.normal*0.001f;
}
bool HumanGroomRuntime::setSelfCollision(GroomSelfCollisionConfig config) noexcept {
    if(!GroomSelfCollision::validate(config))return false;
    selfCollision_=config;
    return true;
}
void HumanGroomRuntime::generate(const HumanDNA& dna,GroomQuality quality) {
    dna.validate();
    const auto& g=dna.groom;
    std::size_t count=std::max<std::size_t>(
        4,std::size_t(std::lround(float(g.guideCount)*
                                 (0.12f+0.88f*g.density))));
    std::size_t segments=g.segments;
    if(quality==GroomQuality::Medium) {
        count=std::max<std::size_t>(4,count/3);
        segments=std::max<std::size_t>(2,segments/2);
    }else if(quality==GroomQuality::Low) {
        count=std::max<std::size_t>(4,count/8);
        segments=std::max<std::size_t>(2,segments/4);
    }
    segments_=segments;
    stiffness_=g.stiffness;
    damping_=g.damping;
    gravity_=g.gravity;
    windStrength_=g.wind;
    guides_.clear();
    guides_.reserve(count);
    const float headScale=dna.anatomy.headScale;
    const float radiusX=0.72f*dna.anatomy.headWidth*headScale;
    const float radiusY=0.90f*headScale;
    const float radiusZ=0.75f*headScale;
    scalpRadii_={radiusX,radiusY,radiusZ};
    for(std::size_t i=0;i<count;++i) {
        const float u=(float(i)+0.5f)/float(count);
        const float theta=std::acos(1.0f-0.75f*u);
        const float angle=float((i+std::size_t(dna.seed))*kGolden);
        const float st=std::sin(theta),ct=std::cos(theta);
        const float3 n=normalize(float3{
            st*std::cos(angle),ct,st*std::sin(angle)});
        const float3 root{n.x*radiusX,n.y*radiusY,n.z*radiusZ};
        GroomGuide guide;
        guide.root=root;
        guide.normal=n;
        guide.rest.resize(segments+1);
        guide.position.resize(segments+1);
        guide.previous.resize(segments+1);
        const auto dir=normalize(n*0.27f+float3{0,-0.85f,0});
        for(std::size_t j=0;j<=segments;++j) {
            const float t=float(j)/float(segments);
            const float curlAngle=angle+t*11.0f;
            const float curl=g.curl*0.06f*t*t;
            const float clump=g.clump*0.018f*t*t;
            const auto p=root+dir*(g.lengthMeters*t)+
                          float3{std::cos(curlAngle)*curl+
                                     std::sin(angle*0.27f)*clump,0,
                                 std::sin(curlAngle)*curl+
                                     std::cos(angle*0.27f)*clump};
            guide.rest[j]=guide.position[j]=guide.previous[j]=p;
        }
        guides_.push_back(std::move(guide));
    }
    lastStepMs_=0;
    selfCollisionStats_={};
    particleWorkspace_.reset();
    segmentWorkspace_.reset();
}
bool HumanGroomRuntime::step(float dt,render::float3 wind) {
    using namespace render;
    if(!std::isfinite(dt)||dt<0||dt>0.05f||!good(wind))return false;
    if(dt==0||guides_.empty())return true;
    const auto start=std::chrono::steady_clock::now();
    // Repeatable bounded integration on variable presentation frame rates.
    // 1/120 seconds is the maximum internal step; avoid one large impulse.
    const int substeps=std::max(1,int(std::ceil(dt*120.0f)));
    const float h=dt/float(substeps);
    const float dt2=h*h;
    const float friction=std::pow(1.0f-damping_*0.65f,h*60.0f);
    const float3 force=wind*windStrength_+float3{0,-9.81f*gravity_,0};
    selfCollisionStats_={};
    for(int substep=0;substep<substeps;++substep) {
    for(auto& guide:guides_) {
        guide.position[0]=guide.previous[0]=guide.root;
        for(std::size_t j=1;j<guide.position.size();++j) {
            const auto p=guide.position[j];
            const auto velocity=(p-guide.previous[j])*friction;
            const auto shapePull=(guide.rest[j]-p)*(stiffness_*3.0f);
            // Bound a single Verlet displacement under violent wind or a
            // stale previous frame. This avoids tunnelling across the scalp
            // while retaining the authored rest-length constraint.
            auto delta=velocity+(force+shapePull)*dt2;
            const float maxTravel=std::max(0.10f,
                length(guide.rest[j]-guide.rest[j-1])*2.0f);
            const float travel=length(delta);
            if(travel>maxTravel)delta=delta*(maxTravel/travel);
            guide.position[j]=p+delta;
            guide.previous[j]=p;
        }
        for(int iteration=0;iteration<5;++iteration) {
            for(std::size_t j=1;j<guide.position.size();++j) {
                const auto prev=guide.position[j-1];
                const auto delta=guide.position[j]-prev;
                const float len=length(delta);
                const float restLen=length(guide.rest[j]-guide.rest[j-1]);
                if(len>1e-6f) {
                    const auto displacement=delta*((len-restLen)/len);
                    if(j>1)guide.position[j-1]+=displacement*0.30f;
                    guide.position[j]=guide.position[j]-displacement*(j==1?1.0f:0.70f);
                }
                guide.position[j]=resolveCollision(guide.position[j],guide);
            }
        }
        // Final contact pass after segment constraints have converged.
        for(std::size_t j=1;j<guide.position.size();++j) {
            const auto before=guide.position[j];
            guide.position[j]=resolveCollision(before,guide);
            // Collisions are inelastic at this coarse LOD: discard velocity
            // normal to the surface rather than bounce out next frame.
            if(length(guide.position[j]-before)>1e-5f)
                guide.previous[j]=guide.position[j];
        }
        for(auto v:guide.position)if(!good(v))return false;
    }
    if(selfCollision_.enabled) {
        GroomSelfCollisionStats collisions;
        if(!GroomSelfCollision::resolve(guides_,selfCollision_,collisions,particleWorkspace_))
            return false;
        selfCollisionStats_.pairChecks+=collisions.pairChecks;
        selfCollisionStats_.contacts+=collisions.contacts;
        selfCollisionStats_.budgetHit|=
            collisions.budgetHit;
        if(selfCollision_.segmentEnabled) {
            GroomSelfCollisionStats segmentContacts;
            if(!GroomSegmentCollision::resolve(
                guides_,selfCollision_,segmentContacts,segmentWorkspace_))
                return false;
            selfCollisionStats_.pairChecks+=segmentContacts.pairChecks;
            selfCollisionStats_.contacts+=segmentContacts.contacts;
            selfCollisionStats_.budgetHit|=segmentContacts.budgetHit;

            // Reproject only if the segment pass actually moved guides.
            // A budget-skipped pass must not perturb existing curls.
            if(segmentContacts.contacts>0)
            for(auto& guide:guides_) {
                for(int iteration=0;iteration<2;++iteration) {
                    for(std::size_t j=2;j<guide.position.size();++j) {
                        const auto restCurvature=
                            guide.rest[j]-guide.rest[j-1]*2.f+
                            guide.rest[j-2];
                        const auto expected=
                            guide.position[j-1]*2.f-guide.position[j-2]+
                            restCurvature;
                        const auto bend=(expected-guide.position[j])*
                            (selfCollision_.bendPreservation*0.20f);
                        const float bendLength=length(bend);
                        const float limit=selfCollision_.separation*0.5f;
                        const auto correction=bendLength>limit
                            ?bend*(limit/bendLength):bend;
                        if(!good(guide.position[j]+correction))return false;
                        guide.position[j]+=correction;
                        guide.previous[j]+=correction;
                    }
                    for(std::size_t j=1;j<guide.position.size();++j) {
                        const auto d=guide.position[j]-guide.position[j-1];
                        const float len=length(d);
                        const float restLen=
                            length(guide.rest[j]-guide.rest[j-1]);
                        if(len<=1.e-6f || !std::isfinite(len))return false;
                        const auto correction=d*((len-restLen)/len);
                        const auto previous=guide.position[j];
                        guide.position[j]=guide.position[j]-correction*(j==1?1.f:0.75f);
                        if(j>1)guide.position[j-1]+=correction*0.25f;
                        guide.previous[j]+=guide.position[j]-previous;
                    }
                }
            }
        }
        // The collision solver resolves inter-guide contacts, but the
        // scalp proxy and the ellipsoidal fallback remain authoritative.
        for(auto& guide:guides_)
            for(std::size_t j=1;j<guide.position.size();++j) {
                const auto before=guide.position[j];
                guide.position[j]=resolveCollision(before,guide);
                if(length(guide.position[j]-before)>1e-6f)
                    guide.previous[j]=guide.position[j];
            }
    }
    }
    lastStepMs_=float(std::chrono::duration<double,std::milli>(
        std::chrono::steady_clock::now()-start).count());
    return true;
}
bool HumanGroomRuntime::updateCardVertices(
        std::span<render::ModelVertex> output) const {
    using namespace render;
    if(guides_.empty()||!segments_||output.size()!=vertexCount())return false;
    std::size_t k=0;
    for(const auto& guide:guides_) {
        for(std::size_t j=0;j<=segments_;++j) {
            const auto& pos=guide.position[j];
            const auto a=guide.position[j?j-1:0];
            const auto b=guide.position[std::min(j+1,segments_)];
            const auto tangent=normalize(b-a);
            const auto side=cardSide(tangent);
            const auto normal=normalize(cross(side,tangent));
            const float t=float(j)/float(segments_);
            const float width=0.0035f*(1.0f-0.72f*t);
            for(int edge=0;edge<2;++edge) {
                auto& v=output[k++];
                v.pos=pos+side*(edge?width:-width);
                v.nrm=normal;
                v.uv={float(edge),t};
                v.tangent={tangent.x,tangent.y,tangent.z,1.0f};
                v.color=0xffffffffu;
            }
        }
    }
    return true;
}
std::unique_ptr<render::Model> HumanGroomRuntime::makeCardModel(
        const HumanDNA& dna,HumanMaterialQuality quality) const {
    using namespace render;
    dna.validate();
    if(guides_.empty()||segments_==0)return {};
    auto m=std::make_unique<Model>();
    m->path="vivid-groom-"+dna.id;
    m->vertices.resize(vertexCount());
    if(!updateCardVertices(m->vertices))
        throw std::logic_error("VIVID groom card generation failed");
    m->indices.reserve(triangleCount()*3);
    for(std::size_t i=0;i<guides_.size();++i) {
        const std::uint32_t base=std::uint32_t(i*(segments_+1)*2);
        for(std::size_t j=0;j<segments_;++j) {
            const std::uint32_t at=base+std::uint32_t(j*2);
            m->indices.insert(m->indices.end(),
                             {at,at+1,at+2,at+1,at+3,at+2});
        }
    }
    m->materials.push_back(humanMaterial(HumanSurface::Hair,dna,quality));
    MeshDef hair;
    hair.name="vivid_groom_cards";
    hair.deformable=true; // never merge/put dynamic groom in static pool
    Primitive prim;
    prim.firstVertex=0;
    prim.vertexCount=std::uint32_t(m->vertices.size());
    prim.firstIndex=0;
    prim.indexCount=std::uint32_t(m->indices.size());
    hair.primitives.push_back(prim);
    m->meshes.push_back(hair);
    Node node;
    node.name="groom";
    node.mesh=0;
    node.runtimeMutable=true;
    m->nodes.push_back(node);
    m->finalize();
    // Model::finalize derives deformability only from skin/morph metadata.
    // A CPU-updated groom is dynamic even when it has no skin joint palette.
    // Restore this explicit dynamic policy AFTER finalize to prevent the
    // general rigid geometry pool from reusing our mutable vertex stream.
    m->meshes[0].deformable=true;
    // The generated stream already has stable normal/tangent/UV/index
    // topology. Do not let auto LOD or material batching rewrite its
    // vertex IDs; the editor updates exactly this dynamic buffer per frame.
    m->uploadPrepared=true;
    return m;
}
std::unique_ptr<render::Model> HumanGroomRuntime::makeCardModel(
        const HumanDNA& dna) const {
    return makeCardModel(dna,HumanMaterialQuality::High);
}
} // namespace aiverse::vivid
