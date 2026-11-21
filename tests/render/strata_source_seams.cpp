// STRATA Stage17.2 positional seams across distinct material/mesh vertex IDs.
// CPU-only authored regression. Native build/CTest not yet executed.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "engine/render/StrataSourceSeams.h"
#include <cassert>
#include <string>
#include <vector>

using namespace aiverse::render;
using namespace aiverse::render::strata;

static Model twoQuads(bool differentMesh=false) {
    Model model;
    model.batching.autoLod=false;
    model.materials.resize(2);
    const float xyz[][3]={
        {0,0,0},{1,0,0},{0,1,0},{1,1,0},
        // Independent UV/material vertex IDs but EXACT same physical
        // shared edge at x=1 (right 4,6 == left 1,3).
        {1,0,0},{2,0,0},{1,1,0},{2,1,0}
    };
    for(const auto& p:xyz) {
        ModelVertex v{};
        v.pos={p[0],p[1],p[2]};
        v.nrm={0,0,1};
        model.vertices.push_back(v);
    }
    model.indices={0,1,2,1,3,2,4,5,6,5,7,6};
    if(!differentMesh){
        model.meshes.resize(1);
        for(uint32_t i=0;i<2;++i){
            Primitive p{};
            p.material=int(i);
            p.firstIndex=i*6;
            p.indexCount=6;
            p.firstVertex=i*4;
            p.vertexCount=4;
            model.meshes[0].primitives.push_back(p);
        }
        Node node{};
        node.mesh=0;
        model.nodes.push_back(node);
    }else{
        model.meshes.resize(2);
        for(uint32_t i=0;i<2;++i){
            Primitive p{};
            p.material=int(i);
            p.firstIndex=i*6;
            p.indexCount=6;
            p.firstVertex=i*4;
            p.vertexCount=4;
            model.meshes[i].primitives.push_back(p);
            Node node{};
            node.mesh=int(i);
            model.nodes.push_back(node);
        }
    }
    model.finalize();
    return model;
}
static void validDuplicatedMaterialIds() {
    Model model=twoQuads();
    ModelAsset asset;
    std::string error;
    assert(buildModelAsset(model,{},asset,&error));
    assert(error.empty());
    assert(asset.sourceSeamsCertified);
    assert(asset.primitives.size()==2);
    assert(asset.sharedGeometricBoundarySegments==1);
    for(const auto& p:asset.primitives)
        assert(p.geometry.seamCertified);
    SourceSeamAudit report{};
    assert(auditModelSourceSeams(model,asset,report,&error));
    assert(report.matchedSegments==1);
    assert(report.examinedPartitions==2);
    SceneStaging staging;
    assert(staging.stage("materials/duplicated-ids.glb",model));
    assert(staging.stats().crossMaterialCertifiedSources==1);
    assert(staging.stats().crossMaterialMatchedSeams==1);
}
static void rejectSplitBoundaryTjunction() {
    Model model=twoQuads();
    // Replace right quad with two smaller quads along its material border,
    // without adding the midpoint vertex to the LEFT material.
    ModelVertex mid{},far{};
    mid.pos={1,.5f,0};
    far.pos={2,.5f,0};
    model.vertices.push_back(mid); // 8
    model.vertices.push_back(far); // 9
    model.indices={
        0,1,2,1,3,2,
        4,5,8,5,9,8,8,9,6,9,7,6
    };
    model.meshes[0].primitives[1].indexCount=12;
    model.meshes[0].primitives[1].vertexCount=6;
    model.finalize();
    std::string error;
    ModelAsset untouched;
    untouched.primitives.push_back({});
    assert(!buildModelAsset(model,{},untouched,&error));
    assert(error.find("source-wide seam")!=std::string::npos);
    assert(untouched.primitives.size()==1); // transactional failure
    SceneStaging staging;
    assert(!staging.stage("T-junction.glb",model));
    assert(staging.find("T-junction.glb")==nullptr);
}
static void rejectNearButDifferentMaterialCoordinates() {
    Model model=twoQuads();
    model.vertices[4].pos.x+=.00001f;
    model.vertices[6].pos.x+=.00001f;
    model.finalize();
    ModelAsset asset;
    std::string error;
    assert(!buildModelAsset(model,{},asset,&error));
    assert(!error.empty());
}
static void sameMaterialDisconnectedUvIslands() {
    Model model=twoQuads();
    // Merge the two material draws into one primitive, but leave duplicate
    // physical boundary IDs: two independent triangle-edge components
    // inside the SAME original material must still be checked.
    model.meshes[0].primitives.resize(1);
    model.meshes[0].primitives[0].indexCount=12;
    model.meshes[0].primitives[0].vertexCount=8;
    model.finalize();
    ModelAsset source;
    std::string error;
    assert(buildModelAsset(model,{},source,&error));
    assert(source.primitives.size()==1);
    assert(source.sourceSeamsCertified);
    assert(source.sharedGeometricBoundarySegments==1);
    SourceSeamAudit report{};
    assert(auditModelSourceSeams(model,source,report,&error));
    assert(report.examinedPartitions==2);
    assert(report.matchedSegments==1);
    // A tiny crack in that otherwise matching duplicate-ID source seam
    // must be rejected even though the *material ID is unchanged*.
    model.vertices[4].pos.x+=.00001f;
    model.vertices[6].pos.x+=.00001f;
    model.finalize();
    const auto previousCount=source.primitives.size();
    assert(!buildModelAsset(model,{},source,&error));
    assert(source.primitives.size()==previousCount);
}

static void differentMeshesAndNodeTransforms() {
    Model model=twoQuads(true);
    // Shift the right mesh's LOCAL geometry to [0,1], then restore the
    // shared seam with the right mesh node transform x=+1.
    for(uint32_t i=4;i<8;++i)model.vertices[i].pos.x-=1.f;
    model.nodes[1].t={1,0,0};
    model.finalize();
    ModelAsset asset;
    std::string error;
    assert(buildModelAsset(model,{},asset,&error));
    assert(asset.sourceSeamsCertified);
    assert(asset.sharedGeometricBoundarySegments==1);
    // A transform offset that moves the seam by a small visible crack
    // is conservatively rejected (not silently treated as welded).
    model.nodes[1].t={1.00001f,0,0};
    model.finalize();
    assert(!buildModelAsset(model,{},asset,&error));
    assert(asset.sourceSeamsCertified); // previous out preserved
}
int main() {
    validDuplicatedMaterialIds();
    rejectSplitBoundaryTjunction();
    rejectNearButDifferentMaterialCoordinates();
    sameMaterialDisconnectedUvIslands();
    differentMeshesAndNodeTransforms();
    return 0;
}
