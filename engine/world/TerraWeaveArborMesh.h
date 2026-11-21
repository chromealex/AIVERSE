// TerraWeave Arbor CPU render-mesh builder (presentation only).
// Converts deterministic local-space tree skeletons to bark tubes + leaf cards.
// GPU resources, shaders, LOD impostor baking and RHI uploads belong to adapters.
#pragma once
#include "engine/world/TerraWeaveArbor.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace aiverse::terraweave {

struct ArborVertex {
    float x = 0, y = 0, z = 0;
    float nx = 0, ny = 1, nz = 0;
    float u = 0, v = 0;
    uint8_t material = 0; // 0 bark, 1 leaves
    int32_t owner = -1;   // structural branch (TreeSkeleton::branches index); wind rigs, -1 unknown
};

struct ArborMesh {
    std::vector<ArborVertex> vertices;
    std::vector<uint32_t> indices;
};

struct ArborMeshSettings {
    uint32_t radialSegments = 6; // 3..16
    bool doubleSidedLeaves = true;
    uint32_t leafPlanes = 2; // 1: one card, 2: crossed cards
    bool naturalConifer = false; // opt-in 2x2 needle-spray atlas, cosmetic only
    uint32_t longitudinalSegments = 6; // 1..12, curved tube tessellation
    float sprayDroop = .18f; // downward shoot angle, presentation only
    bool compactConifer = false; // bounded volumetric sprays and trunk flare
    uint32_t sprayPairs = 7; // 2..12, retained branch silhouette at every LOD
    bool naturalBroadleaf = false; // connected2x2 spray atlas on owned twigs
    uint32_t leafCoveragePermille = 1000; // existing ecology/phenology input
    bool trunkFlare = true, includeLeader = true; // isolated branch adapters set these explicitly
    bool organicRootFlare = false; // continuous seeded basal buttresses, not exposed-root simulation
    bool organicBranches = false; // shared curved wood/child/foliage attachment
    uint32_t curveSegments = 4; // 2..8 structural rings retained across independently selected LODs
};

namespace arbor_detail {
struct V3 { float x = 0, y = 0, z = 0; };
inline V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline V3 operator*(V3 a, float f) { return {a.x * f, a.y * f, a.z * f}; }
inline V3 cross(V3 a, V3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x};
}
inline V3 normalize(V3 v) {
    const float len2 = v.x * v.x + v.y * v.y + v.z * v.z;
    if (len2 < 1.0e-12f) return {0, 1, 0};
    return v * (1.0f / std::sqrt(len2));
}
// One presentation curve per structural owner. Logical integer skeletons stay
// unchanged. Sample the ACTUAL tessellated centreline so a child never starts
// above a coarse parent's chord when the LOD reduces its ring count.
struct BranchCurve {
    V3 start,delta,bend;
    float sag=0;
    uint32_t rings=1;
    uint8_t spacing=0; // 0 uniform,1 scaffold shoulder,2 basal trunk
};
inline float curveRingT(const BranchCurve& c,float step) {
    return c.spacing==2?step*step*step:c.spacing==1?step*step:step;
}
inline V3 curvePoint(const BranchCurve& c,float t) {
    return c.start+c.delta*t+c.bend*(t*(1-t)*(1-t))+
        V3{0,-c.sag*std::sin(3.14159265358979323846f*t),0};
}
inline V3 curveSample(const BranchCurve& c,float t) {
    t=std::clamp(t,0.f,1.f);
    const float step=c.spacing==2?std::cbrt(t):c.spacing==1?std::sqrt(t):t;
    const uint32_t ring=std::min(c.rings-1,uint32_t(step*c.rings));
    const float a=curveRingT(c,float(ring)/c.rings),b=curveRingT(c,float(ring+1)/c.rings);
    const float blend=b>a?(t-a)/(b-a):0;
    return curvePoint(c,a)*(1-blend)+curvePoint(c,b)*blend;
}
inline V3 curveTangent(const BranchCurve& c,float t) {
    return normalize(curveSample(c,std::min(1.f,t+.001f))-
                     curveSample(c,std::max(0.f,t-.001f)));
}
inline std::vector<BranchCurve> branchCurves(const TreeSkeleton& tree,const ArborMeshSettings& settings) {
    std::vector<BranchCurve> curves;curves.reserve(tree.branches.size());
    for(size_t i=0;i<tree.branches.size();++i) {
        const auto& b=tree.branches[i];
        if(b.parent < -1 || (b.parent>=0&&size_t(b.parent)>=i))throw std::invalid_argument("Arbor curve requires preceding parent");
        BranchCurve c;c.start={float(b.x0),float(b.y0),float(b.z0)};
        c.delta={float(b.x1-b.x0),float(b.y1-b.y0),float(b.z1-b.z0)};
        const float len=std::sqrt(c.delta.x*c.delta.x+c.delta.y*c.delta.y+c.delta.z*c.delta.z);
        const auto axis=normalize(c.delta);
        const bool root=settings.organicRootFlare&&settings.trunkFlare&&b.parent<0;
        const bool curvedScaffold=settings.naturalBroadleaf&&b.parent>=0&&std::max(b.startRadiusCm,b.endRadiusCm)>=4;
        c.rings=root?(settings.radialSegments>=6?10u:settings.radialSegments>=4?6u:3u):
            curvedScaffold?settings.curveSegments:
            settings.naturalBroadleaf&&b.parent>=0?1u:
            b.parent>=0?settings.curveSegments:settings.longitudinalSegments;
        c.spacing=b.parent<0&&(settings.compactConifer||settings.naturalBroadleaf||root)?2:curvedScaffold?1:0;
        // The structural trunk axis stays straight. Root-skin burial changes
        // its surface, not the interior attachment/pivot coordinate system.
        if(settings.naturalConifer&&std::abs(axis.y)<.95f)c.sag=len*.12f;
        if(b.parent>=0) {
            const auto& parent=tree.branches[size_t(b.parent)];const auto& pc=curves[size_t(b.parent)];
            const V3 rawParent{float(parent.x0),float(parent.y0),float(parent.z0)};
            const V3 from=c.start-rawParent;
            const float ll=pc.delta.x*pc.delta.x+pc.delta.y*pc.delta.y+pc.delta.z*pc.delta.z;
            const float at=ll>1e-9f?std::clamp((from.x*pc.delta.x+from.y*pc.delta.y+from.z*pc.delta.z)/ll,0.f,1.f):0.f;
            c.start=c.start+curveSample(pc,at)-(rawParent+pc.delta*at);
            if(curvedScaffold) {
                const auto parentAxis=curveTangent(pc,at);
                const auto h=detail::mix64(tree.seed^uint64_t(i)*7919u);
                const float shoulder=.40f+float(h%201u)*.001f;
                c.bend=(parentAxis-axis)*(len*shoulder);
                const auto side=normalize(cross(axis,parentAxis));
                c.bend=c.bend+side*(len*(float((h>>16)%201u)-100.f)*.0005f);
            }
        }
        curves.push_back(c);
    }
    return curves;
}
inline void addTriangle(ArborMesh& mesh, uint32_t a, uint32_t b, uint32_t c) {
    mesh.indices.push_back(a);
    mesh.indices.push_back(b);
    mesh.indices.push_back(c);
}
inline void addLeafQuad(ArborMesh& mesh, V3 center, V3 side, float radius,
                        bool doubleSided) {
    const V3 up{0, radius, 0};
    const V3 horizontal = side * radius;
    const V3 n = normalize(cross(horizontal, up));
    const V3 coords[4] = {
        center - horizontal - up, center + horizontal - up,
        center + horizontal + up, center - horizontal + up
    };
    const float uv[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    const uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
    for (int i = 0; i < 4; ++i)
        mesh.vertices.push_back({coords[i].x, coords[i].y, coords[i].z,
                                 n.x, n.y, n.z, uv[i][0], uv[i][1], 1});
    addTriangle(mesh, base, base + 1, base + 2);
    addTriangle(mesh, base, base + 2, base + 3);
    if (doubleSided) {
        addTriangle(mesh, base + 2, base + 1, base);
        addTriangle(mesh, base + 3, base + 2, base);
    }
}
} // namespace arbor_detail

inline ArborMesh buildArborMesh(const TreeSkeleton& tree,
                               ArborMeshSettings settings = {},
                               int32_t branchOnly = -1,bool outerLeafOnly = false) {
    if (settings.radialSegments < 3 || settings.radialSegments > 16 ||
        settings.leafPlanes < 1 || settings.leafPlanes > 2 ||
        settings.sprayPairs < 1 || settings.sprayPairs > 12 ||
        (!std::isfinite(settings.sprayDroop)||settings.sprayDroop<0||settings.sprayDroop>1) ||
        (settings.compactConifer && !settings.naturalConifer) ||
        settings.longitudinalSegments<1 || settings.longitudinalSegments>12 ||
        settings.leafCoveragePermille>1000 || (settings.naturalConifer&&settings.naturalBroadleaf) ||
        (settings.organicBranches&&(!settings.naturalConifer&&!settings.naturalBroadleaf)) ||
        (settings.organicBranches&&(settings.curveSegments<2||settings.curveSegments>8)) ||
        branchOnly < -1 || (branchOnly>=0&&size_t(branchOnly)>=tree.branches.size()))
        throw std::invalid_argument("TerraWeave Arbor radial segments");
    ArborMesh out;
    // Owner tagging for hierarchical wind rigs: every vertex emitted after
    // own(x) and before the next own() belongs to structural branch x.
    size_t ownedUpTo=0; int32_t currentOwner=-1;
    const auto own=[&](int32_t next){
        for(size_t i=ownedUpTo;i<out.vertices.size();++i)out.vertices[i].owner=currentOwner;
        ownedUpTo=out.vertices.size(); currentOwner=next;
    };
    constexpr float kTau = 6.2831853071795864769f;
    using namespace arbor_detail;
    const auto curves=settings.organicBranches?branchCurves(tree,settings):std::vector<BranchCurve>{};
    const LeafCluster* outerLeaf=nullptr;
    if(outerLeafOnly&&branchOnly>=0)for(const auto& leaf:tree.leaves)
        if(leaf.parentBranch==branchOnly)outerLeaf=&leaf;
    for (size_t branchIndex=0;branchIndex<tree.branches.size();++branchIndex) {
        own(int32_t(branchIndex));
        const auto& b=tree.branches[branchIndex];
        if(branchOnly>=0&&branchIndex!=size_t(branchOnly))continue;
        // At far detail the bough card's textured stem provides the twig;
        // subpixel branch tubes would cost more than the foliage itself.
        if(settings.compactConifer&&settings.sprayPairs==1&&b.parent>=0)continue;
        V3 start{float(b.x0), float(b.y0), float(b.z0)};
        V3 end{float(b.x1), float(b.y1), float(b.z1)};
        const V3 delta = end - start;
        const float length = std::sqrt(delta.x * delta.x +
                                       delta.y * delta.y + delta.z * delta.z);
        if (length <= 0.001f) continue;
        const V3 axis = delta * (1.0f / length);
        const V3 helper = std::fabs(axis.y) < 0.9f ? V3{0, 1, 0} : V3{1, 0, 0};
        const V3 tangent = normalize(cross(axis, helper));
        const V3 bitangent = normalize(cross(axis, tangent));
        const uint32_t base = static_cast<uint32_t>(out.vertices.size());
        const size_t branchFirstIndex=out.indices.size();
        // Broadleaf nontrunk tubes have a straight, linearly tapered centreline:
        // extra longitudinal rings only subdivide the same planar surface.
        // Thin twigs also do not need a trunk-sized circular cross section.
        const bool straightTwig=settings.naturalBroadleaf&&b.parent>=0;
        const int32_t thick=std::max(b.startRadiusCm,b.endRadiusCm);
        const bool organicRoot=settings.organicRootFlare&&settings.trunkFlare&&b.parent<0&&
            (settings.naturalBroadleaf||settings.naturalConifer);
        float parentBarkRadius=0,parentBarkSlope=0;
        V3 parentBarkCentre{},parentBarkAxis{};
        if(settings.organicBranches&&b.parent>=0) {
            const auto& parent=tree.branches[size_t(b.parent)];
            const V3 from{float(b.x0-parent.x0),float(b.y0-parent.y0),float(b.z0-parent.z0)};
            const V3 d{float(parent.x1-parent.x0),float(parent.y1-parent.y0),float(parent.z1-parent.z0)};
            const float ll=d.x*d.x+d.y*d.y+d.z*d.z;
            const float at=ll>1e-9f?std::clamp((from.x*d.x+from.y*d.y+from.z*d.z)/ll,0.f,1.f):0;
            parentBarkRadius=float(parent.startRadiusCm)*(1-at)+float(parent.endRadiusCm)*at;
            parentBarkCentre=curveSample(curves[size_t(b.parent)],at);
            parentBarkAxis=curveTangent(curves[size_t(b.parent)],at);
            parentBarkSlope=ll>1e-9f?float(parent.endRadiusCm-parent.startRadiusCm)/std::sqrt(ll):0.f;
            if(settings.naturalConifer&&std::abs(normalize(d).y)<.95f){parentBarkRadius*=.30f;parentBarkSlope*=.30f;}
            if(parent.parent>=0){const float tip=std::clamp((at-.8f)*5.f,0.f,1.f);parentBarkRadius*=1.f-.95f*tip*tip*(3-2*tip);}
        }
        const uint32_t radial=organicRoot?(settings.radialSegments>=6?48u:settings.radialSegments>=4?32u:16u):
            straightTwig?std::min(settings.radialSegments,
            thick<=2?3u:thick<=5?4u:thick<=12?5u:8u):settings.radialSegments;
        const uint32_t side = radial + 1u;
        const uint32_t rings = settings.organicBranches?curves[branchIndex].rings:organicRoot?(settings.radialSegments>=6?10u:settings.radialSegments>=4?6u:3u):straightTwig?1u:
            (settings.naturalConifer||settings.naturalBroadleaf) ? settings.longitudinalSegments : 1;
        const auto organicSurface=[&](float step,float angle) {
            const float t=step*step*step;
            // The open foot ring is buried; the trunk and every buttress share
            // this one skin. Nothing ends in a visible capped root stump.
            const float y=delta.y*t-15.f*std::exp(-t*12.f);
            const float taper=float(b.startRadiusCm)*(1-t)+float(b.endRadiusCm)*t;
            // A square-root shoulder approaches the soil tangentially instead
            // of ending in the steep side wall of a conical support strut.
            const float fade=std::exp(-2.f*std::sqrt(std::max(0.f,y)/
                std::max(16.f,float(b.startRadiusCm)*1.8f)));
            const uint32_t roots=5u+uint32_t(detail::mix64(tree.seed)%3u);
            const float phase=float(detail::mix64(tree.seed^7919u)%6283u)*.001f;
            float lobes=0;
            for(uint32_t j=0;j<roots;++j) {
                const auto h=detail::mix64(tree.seed^uint64_t(j+1)*104729u);
                const float jitter=(float(h%1001u)/1000.f-.5f)*.45f;
                const float heading=phase+kTau*(float(j)+jitter)/float(roots);
                const float strength=.7f+float((h>>16)%1001u)/1000.f*.6f;
                lobes+=strength*std::pow(std::max(0.f,std::cos(angle-heading)),24.f);
            }
            const float radius=taper*(1.f+.35f*fade)+float(b.startRadiusCm)*2.6f*lobes*fade;
            return start+delta*t+V3{0,y-delta.y*t,0}+
                (tangent*std::cos(angle)+bitangent*std::sin(angle))*radius;
        };
        const auto curvedSurface=[&](float t,float angle) {
            const auto& curve=curves[branchIndex];
            const auto direction=curveTangent(curve,t);
            const auto around=normalize(cross(direction,helper));
            const auto across=normalize(cross(direction,around));
            const float root=(settings.trunkFlare&&(settings.compactConifer||settings.naturalBroadleaf)&&b.parent<0)?std::exp(-t*12.f):0;
            const float collar=b.parent>=0&&b.startRadiusCm>=4?
                .35f*std::exp(-t*length/std::max(8.f,float(b.startRadiusCm)*3.f)):0;
            float radius=(float(b.startRadiusCm)*(1-t)+float(b.endRadiusCm)*t)*
                (settings.naturalConifer&&std::abs(axis.y)<.95f?.30f:1.f)*(1.f+.65f*root)*(1.f+collar);
            if(b.parent>=0){
                // An open child foot ring must be wholly INSIDE its parent.
                // A swollen ring wider than the trunk made visible bark shelves.
                const float emerge=std::clamp(t*length/std::max(8.f,parentBarkRadius*1.2f),0.f,1.f);
                const float blend=emerge*emerge*(3-2*emerge);
                radius=std::min(radius,parentBarkRadius*.65f)*(1-blend)+radius*blend;
                const float tip=std::clamp((t-.8f)*5.f,0.f,1.f);
                radius*=1.f-.95f*tip*tip*(3-2*tip);
            }
            const auto burial=b.parent<0?V3{0,-15.f*root,0}:V3{};
            return curveSample(curve,t)+burial+(around*std::cos(angle)+across*std::sin(angle))*radius;
        };
        float curvedDistance=0;V3 previousCenter{};
        for (uint32_t ring = 0; ring <= rings; ++ring) {
            const float step = float(ring) / rings;
            const float t = settings.organicBranches?curveRingT(curves[branchIndex],step):
                (settings.compactConifer||settings.naturalBroadleaf)&&b.parent<0?step*step*step:step;
            if(settings.organicBranches){
                const auto center=curveSample(curves[branchIndex],t);
                if(ring){const auto d=center-previousCenter;curvedDistance+=std::sqrt(d.x*d.x+d.y*d.y+d.z*d.z);}
                previousCenter=center;
            }
            const float sag = settings.naturalConifer && std::fabs(axis.y)<0.95f ? length*.12f : 0.f;
            const bool flare=settings.trunkFlare&&(settings.compactConifer||settings.naturalBroadleaf)&&b.parent<0;
            const float root=flare?std::exp(-t*12.f):0.f;
            const V3 center = start + delta*t + V3{0,-sag*std::sin(kTau*.5f*t)-15.f*root,0};
            const float radius = (float(b.startRadiusCm)*(1-t) + float(b.endRadiusCm)*t) *
                (settings.naturalConifer && std::fabs(axis.y)<.95f ? .30f : 1.f)*(1.f+.65f*root);
            for (uint32_t seg = 0; seg <= radial; ++seg) {
                const float angle = kTau * float(seg) / float(radial);
                V3 normal = tangent * std::cos(angle) + bitangent * std::sin(angle);
                V3 at = center + normal * radius;
                float barkV=t*length/100.f;
                if(settings.organicBranches&&!organicRoot) {
                    at=curvedSurface(t,angle);
                    normal=normalize(at-curveSample(curves[branchIndex],t));
                    barkV=curvedDistance/100.f;
                }
                if(organicRoot) {
                    at=organicSurface(step,angle);
                    const V3 around=organicSurface(step,angle+.001f)-organicSurface(step,angle-.001f);
                    const V3 along=organicSurface(std::min(1.f,step+.003f),angle)-
                        organicSurface(std::max(0.f,step-.003f),angle);
                    normal=normalize(cross(around,along));
                    const V3 offset=at-(start+delta*t);
                    const float radialLength=std::sqrt(offset.x*offset.x+offset.z*offset.z);
                    // Continue longitudinal bark grain out over the shoulder;
                    // mapping only Y stretches one row across an entire root.
                    barkV=(at.y-start.y-std::max(0.f,radialLength-float(b.startRadiusCm)))/100.f;
                }
                out.vertices.push_back({at.x, at.y, at.z,
                                        normal.x, normal.y, normal.z,
                                        float(seg) / float(radial),
                                        barkV, 0});
            }
        }
        for (uint32_t ring=0; ring<rings; ++ring)
        for (uint32_t seg = 0; seg < radial; ++seg) {
            const uint32_t a = base + ring*side + seg, b0 = a + 1u;
            const uint32_t c = a + side, d = c + 1u;
            // tangent/bitangent circle advances around the outward normal.
            // Reversing these faces culls the front bark and exposes the
            // hollow back wall at the ground and branch junctions.
            addTriangle(out, a, b0, c);
            addTriangle(out, b0, d, c);
        }
        if(settings.organicBranches&&!organicRoot) {
            // Normals follow the emitted polyline tube, not derivatives of a
            // smooth curve that coarse rings no longer actually represent.
            std::vector<V3> normals(out.vertices.size()-base);
            for(size_t i=branchFirstIndex;i<out.indices.size();i+=3) {
                const auto a=out.indices[i],b0=out.indices[i+1],c=out.indices[i+2];
                const auto point=[&](uint32_t v){const auto& p=out.vertices[v];return V3{p.x,p.y,p.z};};
                const auto n=cross(point(b0)-point(a),point(c)-point(a));
                normals[a-base]=normals[a-base]+n;normals[b0-base]=normals[b0-base]+n;normals[c-base]=normals[c-base]+n;
            }
            for(uint32_t ring=0;ring<=rings;++ring) {
                const uint32_t first=ring*side,last=first+radial;
                normals[first]=normals[last]=normals[first]+normals[last];
            }
            for(size_t i=0;i<normals.size();++i) {
                auto n=normalize(normals[i]);auto& v=out.vertices[base+i];
                if(b.parent>=0 && parentBarkRadius>0) {
                    const auto dot=[](V3 a,V3 b0){return a.x*b0.x+a.y*b0.y+a.z*b0.z;};
                    const V3 at{v.x,v.y,v.z};const auto from=at-parentBarkCentre;
                    const auto radial=from-parentBarkAxis*dot(from,parentBarkAxis);
                    const auto parentNormal=normalize(normalize(radial)-parentBarkAxis*parentBarkSlope);
                    const float t=curveRingT(curves[branchIndex],float(i/side)/rings);
                    const auto ownRadial=at-curveSample(curves[branchIndex],t);
                    // Only bend toward an outward parent normal. Hidden inner
                    // skin must never invert and leak backfaces at another LOD.
                    if(dot(parentNormal,ownRadial)>0 && dot(parentNormal,n)>0) {
                        const float edge=std::clamp((std::sqrt(dot(radial,radial))-parentBarkRadius)/
                            std::max(2.f,float(b.startRadiusCm)*1.8f),0.f,1.f);
                        const auto fromRoot=at-curves[branchIndex].start;
                        const float reach=parentBarkRadius*8.f+float(b.startRadiusCm)*2.f;
                        const float end=std::clamp((std::sqrt(dot(fromRoot,fromRoot))-reach*.5f)/std::max(1.f,reach*.5f),0.f,1.f);
                        const float blend=.5f*(1-edge*edge*(3-2*edge))*(1-end*end*(3-2*end));
                        n=normalize(n*(1-blend)+parentNormal*blend);
                    }
                }
                v.nx=n.x;v.ny=n.y;v.nz=n.z;
            }
        }
    }
    if(settings.naturalBroadleaf) {
        // Every spray's stem is on its owning woody twig. Two atlas cells have
        // vertical stems; diagonal cells need a different authored anchor.
        for(const auto& leaf:tree.leaves) {
            own(leaf.parentBranch);
            if(branchOnly>=0&&leaf.parentBranch!=branchOnly)continue;
            if(outerLeaf && &leaf!=outerLeaf)continue;
            if(leaf.variation%1000>=settings.leafCoveragePermille)continue;
            if(leaf.parentBranch<0||size_t(leaf.parentBranch)>=tree.branches.size())continue;
            const auto& branch=tree.branches[size_t(leaf.parentBranch)];
            const V3 axis=normalize(V3{float(branch.x1-branch.x0),float(branch.y1-branch.y0),float(branch.z1-branch.z0)});
            const V3 leafAt{float(leaf.xCm),float(leaf.yCm),float(leaf.zCm)};
            const V3 branchStart{float(branch.x0),float(branch.y0),float(branch.z0)};
            const V3 branchDelta{float(branch.x1-branch.x0),float(branch.y1-branch.y0),float(branch.z1-branch.z0)};
            const float branchLength2=branchDelta.x*branchDelta.x+branchDelta.y*branchDelta.y+branchDelta.z*branchDelta.z;
            const V3 fromStart=leafAt-branchStart;
            const float leafT=branchLength2>.001f?(fromStart.x*branchDelta.x+fromStart.y*branchDelta.y+fromStart.z*branchDelta.z)/branchLength2:1.f;
            const V3 helper=std::abs(axis.y)<.9f?V3{0,1,0}:V3{1,0,0};
            const V3 side=normalize(cross(axis,helper));
            const float length=float(leaf.radiusCm)*2.f;
            for(uint32_t plane=0;plane<settings.leafPlanes*settings.sprayPairs;++plane) {
                const uint32_t spray=plane/settings.leafPlanes;
                const float at=std::clamp(leafT+(float(spray)-float(settings.sprayPairs-1)*.5f)*.10f,.25f,1.f);
                const V3 anchor=settings.organicBranches?curveSample(curves[size_t(leaf.parentBranch)],at):branchStart+branchDelta*at;
                const V3 localAxis=settings.organicBranches?curveTangent(curves[size_t(leaf.parentBranch)],at):axis;
                const V3 localSide=settings.organicBranches?normalize(cross(localAxis,helper)):side;
                const uint64_t shoot=detail::mix64(uint64_t(leaf.variation)^uint64_t(spray+1)*7919u);
                const float lateral=(float(shoot%1001)/1000.f-.5f)*1.3f;
                const float rise=float((shoot>>16)%1001)/1000.f*.9f-.15f;
                const V3 forward=normalize(localAxis*.65f+localSide*lateral+V3{0,rise-1.1f*settings.sprayDroop,0});
                const V3 spraySide=normalize(cross(forward,std::abs(forward.y)<.9f?V3{0,1,0}:V3{1,0,0}));
                const float angle=float(leaf.variation%6283)*.001f+plane*1.5707963f;
                const V3 across=normalize(spraySide*std::cos(angle)+cross(forward,spraySide)*std::sin(angle));
                const V3 face=normalize(cross(across,forward));
                const V3 points[4]={anchor-across*length*.38f,anchor+across*length*.38f,
                    anchor+forward*length+across*length*.38f,anchor+forward*length-across*length*.38f};
                const uint32_t base=uint32_t(out.vertices.size());
                const float v=float((leaf.variation>>8)&1)*.5f;
                const float uv[4][2]={{.006f,v+.494f},{.494f,v+.494f},{.494f,v+.006f},{.006f,v+.006f}};
                for(int k=0;k<4;++k) {
                    const V3 n=normalize(face*.35f+normalize(V3{points[k].x,tree.heightCm*.2f,points[k].z})*.65f);
                    out.vertices.push_back({points[k].x,points[k].y,points[k].z,n.x,n.y,n.z,uv[k][0],uv[k][1],1});
                }
                addTriangle(out,base,base+1,base+2);addTriangle(out,base,base+2,base+3);
                if(settings.doubleSidedLeaves){addTriangle(out,base+2,base+1,base);addTriangle(out,base+3,base+2,base);}
            }
        }
        own(-1);
        return out;
    }
    if (settings.naturalConifer) {
        // The same structural branches feed all tiers. Sprays grow from their
        // curved centreline, never from a disconnected radial star at a leaf.
        for (size_t bi=0; bi<tree.branches.size(); ++bi) {
            own(int32_t(bi));
            const auto& b=tree.branches[bi];
            if(branchOnly>=0&&bi!=size_t(branchOnly))continue;
            const V3 start{float(b.x0),float(b.y0),float(b.z0)};
            const V3 delta{float(b.x1-b.x0),float(b.y1-b.y0),float(b.z1-b.z0)};
            const float len=std::sqrt(delta.x*delta.x+delta.y*delta.y+delta.z*delta.z);
            if(len<2 || std::fabs(delta.y)>len*.95f)continue;
            const V3 axis=normalize(delta), side=normalize(cross(axis,{0,1,0}));
            const uint64_t h=detail::mix64(tree.seed ^ uint64_t(b.y0)*7919 ^ uint64_t(b.x1)*104729);
            if(h%1000>=settings.leafCoveragePermille)continue;
            if(settings.compactConifer&&settings.sprayPairs==1) {
                // Far detail represents a whole foliated bough, not two tiny
                // shoots. Keep projected cover while reducing needle geometry.
                const V3 anchor=settings.organicBranches?curveSample(curves[bi],0):start;
                const V3 forward=normalize(axis+V3{0,-settings.sprayDroop*.12f,0});
                const float length=len+65.f,width=std::clamp(len*.48f+65.f,70.f,240.f);
                for(uint32_t plane=0;plane<2;++plane) {
                    const V3 across=plane?normalize(cross(forward,side)):normalize(side+V3{0,.25f,0});
                    const V3 normal=normalize(cross(across,forward));
                    const V3 points[4]={anchor-across*width*.5f,anchor+across*width*.5f,
                        anchor+forward*length+across*width*.5f,anchor+forward*length-across*width*.5f};
                    const uint32_t cell=uint32_t((h>>(plane*8))%4),base=uint32_t(out.vertices.size());
                    const float u=float(cell%2)*.5f,v=float(cell/2)*.5f;
                    const float uv[4][2]={{u+.006f,v+.494f},{u+.494f,v+.494f},{u+.494f,v+.006f},{u+.006f,v+.006f}};
                    for(int k=0;k<4;++k){const V3 radial=normalize(V3{points[k].x,tree.heightCm*.12f,points[k].z});const V3 n=normalize(normal*.35f+radial*.65f);
                        out.vertices.push_back({points[k].x,points[k].y,points[k].z,n.x,n.y,n.z,uv[k][0],uv[k][1],1});}
                    addTriangle(out,base,base+1,base+2);addTriangle(out,base,base+2,base+3);
                    if(settings.doubleSidedLeaves){addTriangle(out,base+2,base+1,base);addTriangle(out,base+3,base+2,base);}
                }
                continue;
            }
            for(uint32_t j=0;j<settings.sprayPairs;++j) {
                const float t=settings.sprayPairs==1?.86f:settings.compactConifer&&settings.sprayPairs==3?.18f+.70f*float(j)/2.f:.20f+.78f*float(j)/float(settings.sprayPairs-1);
                const V3 anchor=settings.organicBranches?curveSample(curves[bi],t):start+delta*t+V3{0,-len*.12f*std::sin(kTau*.5f*t),0};
                const V3 localAxis=settings.organicBranches?curveTangent(curves[bi],t):axis;
                const V3 localSide=settings.organicBranches?normalize(cross(localAxis,{0,1,0})):side;
                for(int sign : {-1,1}) {
                    const uint64_t leafHash=detail::mix64(h+j*97+uint64_t(sign+1));
                    const float length=(settings.compactConifer?
                        std::clamp(len*(.38f-.14f*t),std::clamp((float(tree.heightCm)-anchor.y)*.25f,22.f,65.f),145.f)*(settings.sprayPairs==1?1.25f:1.f):std::clamp(len*(.60f-.30f*t),30.f,170.f))*(settings.compactConifer&&settings.sprayPairs==3?1.20f:1.f);
                    const V3 forward=normalize(localAxis*.70f+localSide*(float(sign)*.72f)+V3{0,-settings.sprayDroop,0});
                    // Tilt breaks the paper-flat horizontal shelf silhouette.
                    const float tilt=float(int((leafHash>>8)%101)-50)*.020f;
                    const V3 baseAcross=normalize(cross(forward,{0,1,0})+V3{0,tilt,0});
                    for(uint32_t plane=0;plane<settings.leafPlanes;++plane){
                    const V3 across=plane==0?baseAcross:normalize(baseAcross*.38f+cross(forward,baseAcross)*.92f);
                    const V3 normal=normalize(cross(across,forward));
                    const float width=length*(.62f+.12f*float(leafHash%101)/100.f)*(settings.compactConifer&&settings.sprayPairs==3?1.35f:1.f);
                    const uint32_t base=uint32_t(out.vertices.size());
                    const uint32_t cell=uint32_t((leafHash>>20)%4);
                    const float u=float(cell%2)*.5f, v=float(cell/2)*.5f;
                    const V3 points[4]={anchor-across*width*.5f,anchor+across*width*.5f,
                        anchor+forward*length+across*width*.5f,anchor+forward*length-across*width*.5f};
                    const float uv[4][2]={{u+.006f,v+.494f},{u+.494f,v+.494f},{u+.494f,v+.006f},{u+.006f,v+.006f}};
                    for(int k=0;k<4;++k) {
                        // Needle clusters shade as a volume rather than four
                        // unrelated paper faces. Normal maps remain shared PBR.
                        const V3 outward=normalize(V3{points[k].x,tree.heightCm*.12f,points[k].z});
                        const V3 n=settings.compactConifer?normalize(normal*.35f+outward*.65f):normal;
                        out.vertices.push_back({points[k].x,points[k].y,points[k].z,n.x,n.y,n.z,uv[k][0],uv[k][1],1});
                    }
                    addTriangle(out,base,base+1,base+2);addTriangle(out,base,base+2,base+3);
                    if(settings.doubleSidedLeaves){addTriangle(out,base+2,base+1,base);addTriangle(out,base+3,base+2,base);}
                    }
                }
            }
        }
        if((branchOnly<0||branchOnly==0)&&settings.compactConifer&&settings.includeLeader) {
            // The terminal leader carries short ascending shoots too. Without
            // them the shrinking upper limbs leave a bare pole above the crown.
            const uint32_t shoots=settings.sprayPairs==1?4u:settings.sprayPairs==3?6u:12u;
            for(uint32_t i=0;i<shoots;++i) {
                const uint64_t h=detail::mix64(tree.seed^uint64_t(i+1)*7919);
                if(h%1000>=settings.leafCoveragePermille)continue;
                const float a=kTau*float(h%65536)/65536.f;
                const V3 forward=normalize(V3{std::cos(a)*.14f,1,std::sin(a)*.14f});
                const V3 across{std::sin(a),0,-std::cos(a)};
                const float startY=float(tree.heightCm)-130.f+80.f*float(i)/float(std::max(1u,shoots-1));
                const V3 anchor{0,startY,0};const float length=float(tree.heightCm)-startY+12;
                const float width=std::min(35.f,length*.48f);
                const V3 points[4]={anchor-across*width*.5f,anchor+across*width*.5f,
                    anchor+forward*length+across*width*.5f,anchor+forward*length-across*width*.5f};
                const uint32_t cell=uint32_t(h%4),base=uint32_t(out.vertices.size());
                const float u=float(cell%2)*.5f,v=float(cell/2)*.5f;
                const float uv[4][2]={{u+.006f,v+.494f},{u+.494f,v+.494f},{u+.494f,v+.006f},{u+.006f,v+.006f}};
                const V3 normal=normalize(V3{std::cos(a),.4f,std::sin(a)});
                for(int k=0;k<4;++k)out.vertices.push_back({points[k].x,points[k].y,points[k].z,normal.x,normal.y,normal.z,uv[k][0],uv[k][1],1});
                addTriangle(out,base,base+1,base+2);addTriangle(out,base,base+2,base+3);
                if(settings.doubleSidedLeaves){addTriangle(out,base+2,base+1,base);addTriangle(out,base+3,base+2,base);}
            }
        }
        own(-1);
        return out;
    }
    for (const LeafCluster& leaf : tree.leaves) {
        own(leaf.parentBranch);
        if(branchOnly>=0&&leaf.parentBranch!=branchOnly)continue;
        if(outerLeaf && &leaf!=outerLeaf)continue;
        if(leaf.variation%1000>=settings.leafCoveragePermille)continue;
        const V3 at{float(leaf.xCm), float(leaf.yCm), float(leaf.zCm)};
        const float r = float(leaf.radiusCm);
        if (r <= 0) continue;
        // Two crossed alpha-cutout cards per cluster. A later GPU material
        // controls alpha mask, wind and LOD blend; no textures are baked here.
        addLeafQuad(out, at, {1, 0, 0}, r, settings.doubleSidedLeaves);
        if (settings.leafPlanes == 2)
            addLeafQuad(out, at, {0, 0, 1}, r, settings.doubleSidedLeaves);
    }
    own(-1);
    return out;
}

} // namespace aiverse::terraweave
