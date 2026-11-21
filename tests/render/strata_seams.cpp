// Stage17.1 deterministic exact index-edge boundary certification.
// These checks are authored for the final native CTest run, NOT executed.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "engine/render/StrataSeamTopology.h"
#include <cassert>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

using namespace aiverse::render::strata;

static void exactContourAndTjunctions() {
    // A square with a central vertex; removing interior vertex 4 is safe
    // ONLY if its four perimeter edge segments remain identical.
    const uint32_t fine[]={
        0,1,4, 1,2,4, 2,3,4, 3,0,4
    };
    const uint32_t coarse[]={
        0,1,2, 0,2,3
    };
    std::string error;
    assert(sameSeamContour(fine,12,coarse,6,&error));
    assert(error.empty());
    // A coarse candidate that skips boundary vertex 1 leaves its neighbor's
    // shared 0-1 and 1-2 edges as a crack / T junction at 0-2.
    const uint32_t splitBorder[]={0,2,4,2,3,4,3,0,4};
    assert(!sameSeamContour(fine,12,splitBorder,9,&error));
    assert(!error.empty());
    // Wrong orientation of the same boundary is NOT equivalent for
    // consistent manifold winding.
    const uint32_t flipped[]={0,2,1,0,3,2};
    assert(!sameSeamContour(fine,12,flipped,6,&error));
    const uint32_t duplicate[]={0,1,2,0,1,2};
    assert(!sameSeamContour(duplicate,6,coarse,6,&error));
    const uint32_t degenerate[]={0,0,1};
    assert(!sameSeamContour(degenerate,3,coarse,6,&error));
    assert(!sameSeamContour(nullptr,0,coarse,6,&error));
}

static Asset twoChildAsset() {
    Asset a;
    a.roots={0};
    a.nodes.resize(3);
    a.bounds.resize(3);
    a.indices={
        // root: no internal center, preserves same four outer edges
        0,1,2, 0,2,3,
        // child1 (top half)
        0,1,4, 1,2,4,
        // child2 (bottom half)
        2,3,4, 3,0,4
    };
    auto& root=a.nodes[0];
    root.firstChild=1;
    root.childCount=2;
    root.indexCount=6;
    root.geometricError=.5f;
    root.resident=true;
    a.nodes[1].firstIndex=6;
    a.nodes[1].indexCount=6;
    a.nodes[1].resident=true;
    a.nodes[2].firstIndex=12;
    a.nodes[2].indexCount=6;
    a.nodes[2].resident=true;
    for(auto& b:a.bounds)b={{0.f,0.f,0.f},{2.f,2.f,0.f}};
    return a;
}
static void hierarchyAuditAndTransactions() {
    std::string error;
    Asset a=twoChildAsset();
    SeamAudit report{777,888,999};
    assert(auditSeamHierarchy(a,report,&error));
    assert(report.verifiedParents==1);
    assert(report.checkedNodes==3);
    assert(report.contourEdges>=4);
    const auto certified=report;
    // Topological crack: root now bypasses a child boundary vertex.
    a.indices[1]=3;
    assert(!auditSeamHierarchy(a,report,&error));
    assert(report.verifiedParents==certified.verifiedParents);
    assert(report.checkedNodes==certified.checkedNodes);
    a=twoChildAsset();
    a.nodes[2].firstIndex=UINT32_MAX;
    assert(!auditSeamHierarchy(a,report,&error));
    assert(report.contourEdges==certified.contourEdges);
    a=twoChildAsset();
    a.indices[6]=a.indices[7]; // degenerate leaf is never certified
    assert(!auditSeamHierarchy(a,report,&error));
}

static void builderWeldedSurface() {
    // An explicit open tiled surface, non-flat heights force meshoptimizer
    // to simplify interior geometry while every perimeter vertex remains
    // identical. Parent candidates that cannot prove it are skipped.
    constexpr uint32_t n=33;
    std::vector<float> xyz;
    std::vector<uint32_t> idx;
    xyz.reserve(n*n*3);
    for(uint32_t y=0;y<n;++y)
        for(uint32_t x=0;x<n;++x){
            xyz.push_back(float(x));
            xyz.push_back(float(y));
            xyz.push_back(.4f*std::sin(float(x)*.31f)*
                          std::cos(float(y)*.27f));
        }
    for(uint32_t y=0;y+1<n;++y)
        for(uint32_t x=0;x+1<n;++x) {
            const uint32_t a=y*n+x,b=a+1,c=a+n,d=c+1;
            idx.insert(idx.end(),{a,b,c,b,d,c});
        }
    BuildInput input{};
    input.indices=idx.data();
    input.indexCount=idx.size();
    input.positions=xyz.data();
    input.vertexCount=xyz.size()/3;
    input.positionStride=3*sizeof(float);
    BuildOptions options{};
    options.relativeError=.25f;
    options.targetRatio=.35f;
    Asset result;
    std::string error;
    assert(build(input,options,result,&error));
    assert(result.seamCertified);
    SeamAudit report{};
    assert(auditSeamHierarchy(result,report,&error));
    uint64_t leafIndices=0;
    for(const auto& node:result.nodes)
        if(node.childCount==0)leafIndices+=node.indexCount;
    assert(leafIndices==idx.size()); // unchanged LOD0
    assert(result.nodes.size()>=result.meshlets.size());
    // All accepted hierarchy levels satisfy the exact frontier proof.
    assert(report.verifiedParents ==
           result.nodes.size()-result.meshlets.size());
}
int main() {
    exactContourAndTjunctions();
    hierarchyAuditAndTransactions();
    builderWeldedSurface();
    return 0;
}
