// NEREID stage 7B: genre-neutral water presentation geometry.
// Pure CPU geometry; no renderer, GPU or gameplay mutation. All floats here
// are PRESENTATION ONLY and must never feed deterministic ECS/physics.
#pragma once
#include <aiverselib/water/NereidGeometry.h>
#include <aiverselib/water/NereidRiverGrade.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstddef>
#include <iterator>
#include <limits>
#include <map>
#include <utility>
#include <vector>

namespace aiverse::lib::nereid {

enum class WaterSurfaceLod : uint8_t { Near, Medium, Far };
// Presentation-only detail policy; Medium preserves the existing look.
enum class WaterRapidDetail : uint8_t { Low=0, Medium=1, High=2 };
struct WaterSurfaceMeshOptions {
    WaterSurfaceLod lod=WaterSurfaceLod::Medium;
    float cameraX=0, cameraZ=0;
    // Optional double-precision world anchor; output positions are relative.
    double originX=0, originZ=0;
    double originY=0; // subtract before converting water height to float
    // Opt-in precise camera for far-world ocean patch generation.
    bool preciseCamera=false;
    double cameraX64=0, cameraZ64=0;
    float oceanPatchRadius=256;
    float uvScale=0.05f;
    float foamWidth=3.0f;
    uint32_t maxVertices=65536;
    // Opt-in static tile ranges for large ocean/box surfaces. 1 = legacy.
    uint8_t rippleTileGrid=1; // N x N tiles, N<=8 (max 64 indexed draws)
    WaterRiverGradeComponent grade{};
    bool useGrade=false;
};
struct WaterSurfaceVertex {
    float x=0,y=0,z=0,u=0,v=0,shore=0;
};
struct WaterTileRange {
    uint32_t firstIndex=0,indexCount=0;
    double centerX=0,centerZ=0; // world metres; precise relative-origin mapping
    double minX=0,maxX=0,minZ=0,maxZ=0;
};
struct WaterSurfaceMesh {
    std::vector<WaterSurfaceVertex> vertices;
    std::vector<uint32_t> indices;
    bool infinite=false;
    std::vector<WaterTileRange> tiles; // optional non-overlapping index partitions
    float normalX=0,normalZ=0;
    bool empty()const{return indices.empty();}
    size_t triangleCount()const{return indices.size()/3;}
};
// Generic material inputs for an RHI renderer. The current fallback bridge
// applies tint/foam/opacity; advanced refraction/caustics are future passes.
struct WaterSurfaceStyle {
    float deepR=0.08f, deepG=0.32f, deepB=0.43f;
    float foamR=0.68f, foamG=0.84f, foamB=0.85f;
    float foamStrength=0.65f;
    float opacity=0.73f;
    float specular=0.95f;
    float refractionStrength=0.f; // opt-in trace-tier opaque-scene refraction
    float absorption=0.f; // optical attenuation per metre, 0..4
    float shoreFoamDepth=0.f; // depth-based shallow foam width in metres, 0..20
    float depthTint=0.f; // deep-water tint contribution, 0..1
    float underwaterDistortion=0.f; // 0..1, compositor only
    float underwaterScattering=0.f; // 0..1, compositor only
    float rapidFoamStrength=0.f; // 0..1, opt-in current-generated whitewater
    float rapidFlowThreshold=3.f; // 0..100 m/s before foam begins
    float rapidNoiseScale=1.f; // 0.1..8 visual breakup frequency
    // Linear RGB for current-driven whitewater, distinct from shoreline foam.
    // Defaults reproduce Stage 38 hard-coded shader colour.
    float rapidFoamR=0.84f, rapidFoamG=0.93f, rapidFoamB=0.94f;
    WaterRapidDetail rapidDetail=WaterRapidDetail::Medium;
    // Suspended silt, 0..1. One authored value drives both views: from above
    // the bed fades sooner and the body turns milky; under water visibility
    // shrinks and the haze takes the silt colour. 0 keeps legacy optics.
    float turbidity=0.f;
    float siltR=0.42f, siltG=0.43f, siltB=0.31f; // linear in-scatter colour
};

// Effective optics after turbidity; renderers and the underwater view read
// this instead of the authored fields so above/below water always agree.
inline WaterSurfaceStyle resolvedWaterOptics(WaterSurfaceStyle s) {
    const float t=std::clamp(s.turbidity,0.f,1.f);
    if(t<=0.f)return s;
    const float milk=0.75f*t;
    s.deepR+=(std::clamp(s.siltR,0.f,1.f)-s.deepR)*milk;
    s.deepG+=(std::clamp(s.siltG,0.f,1.f)-s.deepG)*milk;
    s.deepB+=(std::clamp(s.siltB,0.f,1.f)-s.deepB)*milk;
    s.absorption=std::min(4.f,s.absorption+1.6f*t);   // extinction per metre (t=1: ~2 m visibility)
    s.depthTint=s.depthTint+(1.f-s.depthTint)*t;        // in-scatter replaces bed
    s.underwaterScattering=std::max(s.underwaterScattering,0.85f*t);
    s.refractionStrength*=1.f-0.5f*t;                   // silt blurs the bed
    s.turbidity=0.f;
    return s;
}

namespace water_mesh_detail {
struct P {double x=0,z=0;};
inline double cross(P a,P b,P c) {
    return (b.x-a.x)*(c.z-a.z)-(b.z-a.z)*(c.x-a.x);
}
inline double distanceSq(P point,P a,P b) {
    const double dx=b.x-a.x,dz=b.z-a.z;
    const double len=dx*dx+dz*dz;
    const double t=len>1e-18?std::clamp(((point.x-a.x)*dx+
                                      (point.z-a.z)*dz)/len,0.0,1.0):0.0;
    const double x=point.x-(a.x+t*dx),z=point.z-(a.z+t*dz);
    return x*x+z*z;
}
inline bool withinTriangle(P p,P a,P b,P c,int sign) {
    constexpr double eps=1e-9;
    return cross(a,b,p)*sign>=-eps &&
           cross(b,c,p)*sign>=-eps && cross(c,a,p)*sign>=-eps;
}
inline bool segmentCross(P a,P b,P c,P d) {
    // Adjacent polygon edges are skipped by the caller.
    constexpr double eps=1e-9;
    const double abC=cross(a,b,c),abD=cross(a,b,d);
    const double cdA=cross(c,d,a),cdB=cross(c,d,b);
    if(((abC>eps&&abD<-eps)||(abC<-eps&&abD>eps)) &&
       ((cdA>eps&&cdB<-eps)||(cdA<-eps&&cdB>eps)))return true;
    auto on=[](P a,P b,P p) {
        return std::abs(cross(a,b,p))<=1e-9 &&
               p.x>=std::min(a.x,b.x)-1e-9 &&
               p.x<=std::max(a.x,b.x)+1e-9 &&
               p.z>=std::min(a.z,b.z)-1e-9 &&
               p.z<=std::max(a.z,b.z)+1e-9;
    };
    return on(a,b,c)||on(a,b,d)||on(c,d,a)||on(c,d,b);
}
inline bool simplePolygon(const std::vector<P>& points) {
    const size_t n=points.size();
    if(n<3)return false;
    for(size_t i=0;i<n;++i) {
        const P a=points[i],b=points[(i+1)%n];
        const double dx=b.x-a.x,dz=b.z-a.z;
        if(dx*dx+dz*dz<=1e-16)return false;
        for(size_t j=i+1;j<n;++j) {
            if(j==i || j==(i+1)%n || (j+1)%n==i)continue;
            if(segmentCross(a,b,points[j],points[(j+1)%n]))return false;
        }
    }
    return true;
}
inline uint32_t vertex(WaterSurfaceMesh& mesh,P p,float y,float uvScale,
                       double originX=0,double originZ=0) {
    mesh.vertices.push_back({float(p.x-originX),y,float(p.z-originZ),
                             float((p.x-originX)*uvScale),float((p.z-originZ)*uvScale),0});
    return uint32_t(mesh.vertices.size()-1);
}
inline void tri(WaterSurfaceMesh& mesh,uint32_t a,uint32_t b,uint32_t c) {
    mesh.indices.push_back(a);mesh.indices.push_back(b);mesh.indices.push_back(c);
}
inline bool earClip(WaterSurfaceMesh& mesh,const std::vector<P>& points,
                    float y,float uvScale,double originX=0,double originZ=0) {
    if(!simplePolygon(points))return false;
    const size_t n=points.size();
    double area=0;
    for(size_t i=0;i<n;++i) {
        const P a=points[i],b=points[(i+1)%n];
        area+=a.x*b.z-a.z*b.x;
    }
    if(std::abs(area)<=1e-9)return false;
    const int sign=area>0?1:-1;
    for(P p:points)vertex(mesh,p,y,uvScale,originX,originZ);
    std::vector<uint32_t> remaining;
    for(size_t i=0;i<n;++i)remaining.push_back(uint32_t(i));
    while(remaining.size()>3) {
        bool clipped=false;
        for(size_t i=0;i<remaining.size();++i) {
            const uint32_t a=remaining[(i+remaining.size()-1)%remaining.size()];
            const uint32_t b=remaining[i];
            const uint32_t c=remaining[(i+1)%remaining.size()];
            if(cross(points[a],points[b],points[c])*sign<=1e-9)continue;
            bool blocked=false;
            for(uint32_t d:remaining) {
                if(d!=a && d!=b && d!=c &&
                   withinTriangle(points[d],points[a],points[b],points[c],sign)) {
                    blocked=true;break;
                }
            }
            if(blocked)continue;
            // X/Z positive winding points toward -Y; flip for +Y.
            if(sign>0)tri(mesh,c,b,a);else tri(mesh,a,b,c);
            remaining.erase(remaining.begin()+std::ptrdiff_t(i));
            clipped=true;break;
        }
        if(!clipped)return false;
    }
    const uint32_t a=remaining[0],b=remaining[1],c=remaining[2];
    if(cross(points[a],points[b],points[c])*sign<=1e-9)return false;
    if(sign>0)tri(mesh,c,b,a);else tri(mesh,a,b,c);
    return true;
}
inline void splitMesh(WaterSurfaceMesh& mesh,int levels,uint32_t maxVertices) {
    for(int level=0;level<levels;++level) {
        std::map<std::pair<uint32_t,uint32_t>,uint32_t> edges;
        std::vector<uint32_t> next;next.reserve(mesh.indices.size()*4);
        bool overflow=false;
        auto midpoint=[&](uint32_t aa,uint32_t bb) {
            const auto key=std::minmax(aa,bb);
            const std::pair<uint32_t,uint32_t> pair{key.first,key.second};
            const auto found=edges.find(pair);
            if(found!=edges.end())return found->second;
            if(mesh.vertices.size()>=maxVertices){overflow=true;return uint32_t(0);}
            const auto& a=mesh.vertices[aa];const auto& b=mesh.vertices[bb];
            const uint32_t id=uint32_t(mesh.vertices.size());
            mesh.vertices.push_back({(a.x+b.x)*0.5f,(a.y+b.y)*0.5f,
                                     (a.z+b.z)*0.5f,(a.u+b.u)*0.5f,
                                     (a.v+b.v)*0.5f,0});
            edges.emplace(pair,id);return id;
        };
        for(size_t i=0;i<mesh.indices.size();i+=3) {
            const uint32_t a=mesh.indices[i],b=mesh.indices[i+1],
                           c=mesh.indices[i+2];
            const uint32_t ab=midpoint(a,b),bc=midpoint(b,c),
                           ca=midpoint(c,a);
            if(overflow)break;
            const uint32_t triangles[]={a,ab,ca,ab,b,bc,ca,bc,c,ab,bc,ca};
            next.insert(next.end(),std::begin(triangles),std::end(triangles));
        }
        if(overflow)break; // retain the last COMPLETE triangle topology
        mesh.indices=std::move(next);
    }
}
inline void foam(WaterSurfaceMesh& mesh,
                 const std::vector<std::pair<P,P>>& boundary,float width,
                 double originX=0,double originZ=0) {
    if(width<=0 || boundary.empty())return;
    const double ww=double(width)*width;
    for(auto& vertex:mesh.vertices) {
        double closest=std::numeric_limits<double>::infinity();
        for(const auto& edge:boundary)
            closest=std::min(closest,distanceSq({vertex.x,vertex.z},
                {edge.first.x-originX,edge.first.z-originZ},
                {edge.second.x-originX,edge.second.z-originZ}));
        vertex.shore=float(std::clamp(1.0-std::sqrt(closest/ww),0.0,1.0));
    }
}
} // namespace water_mesh_detail

// Stateless visual mesh from one ECS WaterBody and optional footprint.
// Geometry must be static during this call; callers may cache/upload the
// result and rebuild when ECS water geometry, visual LOD or camera patch
// changes. Fluid wave offsets are GPU presentation only.
inline WaterSurfaceMesh buildWaterSurfaceMesh(
        const WaterBodyComponent& body,
        const WaterGeometryComponent* geometry=nullptr,
        WaterSurfaceMeshOptions options={}) {
    using namespace water_mesh_detail;
    WaterSurfaceMesh mesh;
    if(!body.enabled || body.surfaceY<=body.bottomY ||
       !std::isfinite(options.uvScale) ||
       !std::isfinite(options.foamWidth) ||
       !std::isfinite(options.cameraX) ||
       !std::isfinite(options.cameraZ) ||
       !std::isfinite(options.oceanPatchRadius) ||
       !std::isfinite(options.originX) ||
       !std::isfinite(options.originZ) ||
       !std::isfinite(options.originY) ||
       (options.preciseCamera && (!std::isfinite(options.cameraX64) ||
                                  !std::isfinite(options.cameraZ64))) ||
       options.oceanPatchRadius<=0 || options.maxVertices<4 ||
       options.rippleTileGrid<1 || options.rippleTileGrid>8 ||
       options.foamWidth<0)return mesh;
    const float y=float(body.surfaceY.toDouble()-options.originY);
    std::vector<std::pair<P,P>> boundary;
    auto addQuad=[&](P a,P b,P c,P d) {
        const auto ia=vertex(mesh,a,y,options.uvScale,options.originX,options.originZ);
        const auto ib=vertex(mesh,b,y,options.uvScale,options.originX,options.originZ);
        const auto ic=vertex(mesh,c,y,options.uvScale,options.originX,options.originZ);
        const auto id=vertex(mesh,d,y,options.uvScale,options.originX,options.originZ);
        tri(mesh,ia,ib,ic);tri(mesh,ia,ic,id);
        const P pts[]={a,b,c,d};
        for(int i=0;i<4;++i)boundary.emplace_back(pts[i],pts[(i+1)%4]);
    };
    // Tile triangles are contiguous in one shared index buffer so the
    // renderer can submit different ripple uniforms per tile without
    // allocating a separate mesh or duplicating its VB/IB on the GPU.
    // The source rectangle still owns one continuous external coastline:
    // tile seams are NOT shoreline edges for foam.
    auto addTiledQuad=[&](double x0,double x1,double z0,double z1) {
        const int grid=int(options.rippleTileGrid);
        const int sub=options.lod==WaterSurfaceLod::Near?4:
                      options.lod==WaterSurfaceLod::Medium?2:1;
        for(int iz=0;iz<grid;++iz)for(int ix=0;ix<grid;++ix) {
            const uint32_t first=uint32_t(mesh.indices.size());
            const double sx0=x0+(x1-x0)*double(ix)/grid;
            const double sx1=x0+(x1-x0)*double(ix+1)/grid;
            const double sz0=z0+(z1-z0)*double(iz)/grid;
            const double sz1=z0+(z1-z0)*double(iz+1)/grid;
            for(int a=0;a<sub;++a)for(int b=0;b<sub;++b) {
                const double xa=sx0+(sx1-sx0)*double(a)/sub;
                const double xb=sx0+(sx1-sx0)*double(a+1)/sub;
                const double za=sz0+(sz1-sz0)*double(b)/sub;
                const double zb=sz0+(sz1-sz0)*double(b+1)/sub;
                const uint32_t ia=vertex(mesh,{xa,za},y,options.uvScale,
                                          options.originX,options.originZ);
                const uint32_t ib=vertex(mesh,{xa,zb},y,options.uvScale,
                                          options.originX,options.originZ);
                const uint32_t ic=vertex(mesh,{xb,zb},y,options.uvScale,
                                          options.originX,options.originZ);
                const uint32_t id=vertex(mesh,{xb,za},y,options.uvScale,
                                          options.originX,options.originZ);
                tri(mesh,ia,ib,ic);tri(mesh,ia,ic,id);
            }
            mesh.tiles.push_back({first,uint32_t(mesh.indices.size())-first,
                                  (sx0+sx1)*0.5,(sz0+sz1)*0.5,
                                  sx0,sx1,sz0,sz1});
        }
        boundary.emplace_back(P{x0,z0},P{x0,z1});
        boundary.emplace_back(P{x0,z1},P{x1,z1});
        boundary.emplace_back(P{x1,z1},P{x1,z0});
        boundary.emplace_back(P{x1,z0},P{x0,z0});
    };
    if(geometry && geometry->enabled) {
        if(!validWaterGeometry(*geometry))return {};
        const auto& input=geometry->points;
        if(geometry->kind==WaterGeometryKind::Polygon) {
            std::vector<P> points;points.reserve(input.size());
            for(const auto& p:input)points.push_back({p.x.toDouble(),p.z.toDouble()});
            if(!earClip(mesh,points,y,options.uvScale,options.originX,options.originZ))return {};
            for(size_t i=0;i<points.size();++i)
                boundary.emplace_back(points[i],points[(i+1)%points.size()]);
        }else if(geometry->kind==WaterGeometryKind::RiverSpline) {
            const size_t n=input.size();
            std::vector<P> points;points.reserve(n);
            std::vector<P> normals;normals.reserve(n-1);
            for(const auto& p:input)points.push_back({p.x.toDouble(),p.z.toDouble()});
            for(size_t i=1;i<n;++i) {
                const double dx=points[i].x-points[i-1].x;
                const double dz=points[i].z-points[i-1].z;
                const double len=std::hypot(dx,dz);
                if(len<=1e-9)return {};
                normals.push_back({-dz/len,dx/len});
            }
            std::vector<P> left,right;
            for(size_t i=0;i<n;++i) {
                P normal=normals[i==n-1?n-2:i];
                double scale=1.0;
                if(i>0 && i<n-1) {
                    const P prev=normals[i-1],next=normals[i];
                    const double mx=prev.x+next.x,mz=prev.z+next.z;
                    const double length=std::hypot(mx,mz);
                    if(length>1e-6) {
                        normal={mx/length,mz/length};
                        scale=std::min(2.0,1.0/std::max(0.5,
                              normal.x*next.x+normal.z*next.z));
                    }
                }
                const double width=input[i].halfWidth.toDouble()*scale;
                left.push_back({points[i].x+normal.x*width,
                                points[i].z+normal.z*width});
                right.push_back({points[i].x-normal.x*width,
                                 points[i].z-normal.z*width});
                vertex(mesh,left.back(),y,options.uvScale,options.originX,options.originZ);
                vertex(mesh,right.back(),y,options.uvScale,options.originX,options.originZ);
            }
            for(size_t i=0;i+1<n;++i) {
                const uint32_t a=uint32_t(i*2),b=uint32_t((i+1)*2);
                tri(mesh,a,b,b+1);tri(mesh,a,b+1,a+1);
                boundary.emplace_back(left[i],left[i+1]);
                boundary.emplace_back(right[i],right[i+1]);
            }
            boundary.emplace_back(left.front(),right.front());
            boundary.emplace_back(left.back(),right.back());
        }else return {};
    }else{
        if(!validWaterBody(body))return {};
        if(body.footprint==WaterFootprint::Infinite) {
            const double r=options.oceanPatchRadius;
            const double x=options.preciseCamera?options.cameraX64:double(options.cameraX);
            const double z=options.preciseCamera?options.cameraZ64:double(options.cameraZ);
            if(options.rippleTileGrid>1)addTiledQuad(x-r,x+r,z-r,z+r);
            else addQuad({x-r,z-r},{x-r,z+r},{x+r,z+r},{x+r,z-r});
            boundary.clear();mesh.infinite=true;
        }else if(body.footprint==WaterFootprint::Box) {
            const double x=body.center.x.toDouble(),z=body.center.z.toDouble();
            const double hx=body.halfExtents.x.toDouble(),
                         hz=body.halfExtents.y.toDouble();
            if(options.rippleTileGrid>1)addTiledQuad(x-hx,x+hx,z-hz,z+hz);
            else addQuad({x-hx,z-hz},{x-hx,z+hz},{x+hx,z+hz},{x+hx,z-hz});
        }else if(body.footprint==WaterFootprint::Disc) {
            const double x=body.center.x.toDouble(),z=body.center.z.toDouble();
            const double r=body.radius.toDouble();
            const int segments=options.lod==WaterSurfaceLod::Near?64:
                options.lod==WaterSurfaceLod::Medium?32:12;
            constexpr double tau=6.28318530717958647692;
            vertex(mesh,{x,z},y,options.uvScale,options.originX,options.originZ);
            std::vector<P> ring;ring.reserve(size_t(segments));
            for(int i=0;i<segments;++i) {
                const double t=tau*i/double(segments);
                P p{x+r*std::cos(t),z+r*std::sin(t)};
                ring.push_back(p);vertex(mesh,p,y,options.uvScale,options.originX,options.originZ);
            }
            for(int i=0;i<segments;++i) {
                const int j=(i+1)%segments;
                tri(mesh,0,uint32_t(j+1),uint32_t(i+1)); // +Y
                boundary.emplace_back(ring[size_t(i)],ring[size_t(j)]);
            }
        }else return {};
    }
    if(mesh.vertices.size()>options.maxVertices)return {};
    const int levels=options.lod==WaterSurfaceLod::Near?2:
                     options.lod==WaterSurfaceLod::Medium?1:0;
    // A tiled quad has already been subdivided at the selected LOD;
    // splitting again would invalidate stored index ranges.
    if(mesh.tiles.empty())splitMesh(mesh,levels,options.maxVertices);
    // Far LOD has perimeter-only vertices for boxes/polygons/rivers.
    // No interior gradients exist there: suppress vertex foam rather than
    // tinting an entire far-away water surface white.
    if(options.lod!=WaterSurfaceLod::Far)
        foam(mesh,boundary,options.foamWidth,options.originX,options.originZ);
    if(options.useGrade){
        if(!validWaterRiverGrade(options.grade))return {};
        mesh.normalX=-float(options.grade.risePerMetre.x.toDouble());mesh.normalZ=-float(options.grade.risePerMetre.y.toDouble());
        for(auto& v:mesh.vertices){const Vec3 position{Fixed::fromRaw(int64_t(std::llround((double(v.x)+options.originX)*Fixed::kOne))),{},Fixed::fromRaw(int64_t(std::llround((double(v.z)+options.originZ)*Fixed::kOne)))};v.y+=float(waterGradeOffset(options.grade,position).toDouble());}
    }
    return mesh;
}

} // namespace aiverse::lib::nereid
