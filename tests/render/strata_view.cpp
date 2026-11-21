// Strata view-plan reference checks; no GPU encoder required.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "engine/render/StrataView.h"
#include <cassert>
#include <cmath>
#include <limits>
#include <string>
using namespace aiverse::render;
using namespace aiverse::render::strata;

static void hysteresisSelectorAndCoverage() {
    // An independent projected-error series across frames: cold start uses
    // the exact threshold. Once refined, coarsen only below 0.8x; once
    // coarse, refine only above 1.2x. No new GPU/residency assumptions.
    std::vector<ClusterNode> nodes(3);
    nodes[0].firstChild=1;
    nodes[0].childCount=2;
    nodes[0].indexCount=6;
    nodes[0].resident=true;
    for(int i=1;i<3;++i){
        nodes[size_t(i)].indexCount=3;
        nodes[size_t(i)].resident=true;
    }
    const std::vector<uint32_t> roots{0};
    float errors[3]{1.05f,0.f,0.f};
    SelectionInput input{};
    input.errorPixels=errors;
    input.errorCount=3;
    input.thresholdPixels=1.f;
    input.hysteresisFraction=.2f;
    SelectionOutput selected;
    assert(select(nodes,roots,input,selected));
    assert(selected.nodes.size()==2);
    assert(selected.refinedNodes.size()==3);
    assert(selected.refinedNodes[0]==1);

    const auto savedRefined=selected.refinedNodes;
    input.previousRefined=savedRefined.data();
    input.previousRefinedCount=uint32_t(savedRefined.size());
    errors[0]=.85f;
    assert(select(nodes,roots,input,selected));
    assert(selected.nodes.size()==2); // stay fine below normal threshold
    errors[0]=.79f;
    assert(select(nodes,roots,input,selected));
    assert(selected.nodes.size()==1); // below 0.8 => coarse
    auto coarse=selected.refinedNodes;
    input.previousRefined=coarse.data();
    errors[0]=1.1f;
    assert(select(nodes,roots,input,selected));
    assert(selected.nodes.size()==1); // coarse stays below 1.2
    errors[0]=1.21f;
    assert(select(nodes,roots,input,selected));
    assert(selected.nodes.size()==2);

    // A physically missing child always overrides hysteresis. The
    // coarser resident parent must still cover the entire region.
    nodes[2].resident=false;
    input.previousRefined=savedRefined.data();
    errors[0]=3.f;
    assert(select(nodes,roots,input,selected));
    assert(selected.nodes.size()==1 && selected.nodes[0]==0);
    assert(selected.missingRefinements==1);
    nodes[2].resident=true;
    input.maxVisibleNodes=1;
    assert(select(nodes,roots,input,selected));
    assert(selected.nodes.size()==1 && selected.budgetExceeded);

    // The backend-neutral selector must still reject invalid options.
    input.maxVisibleNodes=100;
    input.hysteresisFraction=.5f;
    assert(!select(nodes,roots,input,selected));
    input.hysteresisFraction=.2f;
    input.previousRefinedCount=1;
    assert(!select(nodes,roots,input,selected));
    input.previousRefined=nullptr;
    input.previousRefinedCount=0;
    input.hysteresisFraction=0;
    errors[0]=1.05f;
    assert(select(nodes,roots,input,selected));
    assert(selected.nodes.size()==2);
    assert(selected.refinedNodes.empty()); // default cost-free path
}

int main() {
    hysteresisSelectorAndCoverage();
    Asset asset;
    // Parent is a coarser rendering of two adjacent child patches.
    asset.nodes.resize(3);
    asset.bounds.resize(3);
    asset.roots = {0};
    asset.indices = {0, 1, 2, 1, 2, 3, 0, 1, 2, 1, 2, 3};
    auto& p = asset.nodes[0];
    p.firstChild = 1;
    p.childCount = 2;
    p.firstIndex = 0;
    p.indexCount = 6;
    p.geometricError = 0.1f;
    p.materialGroup = 7;
    p.resident = true;
    for (uint32_t i = 1; i <= 2; ++i) {
        auto& n = asset.nodes[i];
        n.firstIndex = i == 1 ? 6 : 9;
        n.indexCount = 3;
        n.materialGroup = 7;
        n.resident = true;
    }
    asset.bounds[0] = {{-0.7f, -0.4f, 0.2f}, {0.7f, 0.4f, 0.7f}};
    asset.bounds[1] = {{-0.7f, -0.4f, 0.2f}, {0.0f, 0.4f, 0.7f}};
    asset.bounds[2] = {{0.0f, -0.4f, 0.2f}, {0.7f, 0.4f, 0.7f}};
    assert(validate(asset.nodes, asset.roots));

    ViewConfig config;
    config.width = 1280;
    config.height = 720;
    config.errorThresholdPixels = 1.f;
    ViewPlan result;
    std::string error;
    assert(planView(asset, config, result, &error));
    assert(error.empty());
    assert(result.ranges.size() == 2);
    assert(result.triangles == 2);
    assert(result.ranges[0].node == 1 && result.ranges[1].node == 2);
    assert(result.ranges[0].materialGroup == 7);
    assert(result.culledNodes == 0);

    // Physical GPU residency is authoritative even when imported CPU
    // nodes say resident. Only a complete sibling group can refine.
    std::vector<uint8_t> rootOnly{1,0,0};
    assert(planView(asset,config,result,&error,&rootOnly));
    assert(result.ranges.size()==1 && result.ranges[0].node==0);
    assert(result.missingRefinements==1);
    std::vector<uint8_t> oneChild{1,1,0};
    assert(planView(asset,config,result,&error,&oneChild));
    assert(result.ranges.size()==1 && result.ranges[0].node==0);
    std::vector<uint8_t> all{1,1,1};
    assert(planView(asset,config,result,&error,&all));
    assert(result.ranges.size()==2);
    const auto validPlan=result;
    std::vector<uint8_t> broken{1,1};
    assert(!planView(asset,config,result,&error,&broken));
    assert(result.ranges.size()==validPlan.ranges.size());
    // Small projected error selects the resident parent.
    config.objectToClip = mat4::scale({0.001f, 0.001f, 1.f});
    assert(planView(asset, config, result, &error));
    assert(result.ranges.size() == 1 && result.ranges[0].node == 0);
    assert(result.triangles == 2);

    // A separate shadow/reflection pass can project the same geometry
    // differently; do not reuse main camera selection implicitly.
    config.objectToClip = mat4::identity();
    config.maxSelectedNodes = 1;
    assert(planView(asset, config, result, &error));
    assert(result.ranges.size() == 1 && result.budgetExceeded);
    config.maxSelectedNodes = 65536;

    // A missing child forces the coarse resident parent, without holes.
    asset.nodes[2].resident = false;
    assert(planView(asset, config, result, &error));
    assert(result.ranges.size() == 1 && result.ranges.front().node == 0);
    assert(result.missingRefinements == 1);
    asset.nodes[2].resident = true;
    asset.nodes[0].resident = false;
    const size_t oldCount = result.ranges.size();
    assert(!planView(asset, config, result, &error));
    assert(result.ranges.size() == oldCount); // transactional error
    asset.nodes[0].resident = true;

    // Frustum clipping can omit all ranges without destroying staged assets.
    config.objectToClip = mat4::translate({8.f, 0.f, 0.f});
    assert(planView(asset, config, result, &error));
    assert(result.ranges.empty());
    assert(result.culledNodes == 2);
    config.frustumCull = false;
    assert(planView(asset, config, result, &error));
    assert(result.ranges.size() == 2);
    config.frustumCull = true;

    // Malformed input cannot publish partial plans.
    const auto previous = result;
    asset.nodes[1].firstIndex = uint32_t(asset.indices.size());
    assert(!planView(asset, config, result, &error));
    assert(result.ranges.size() == previous.ranges.size());
    asset.nodes[1].firstIndex = 6;
    asset.bounds[2].max[0] = std::numeric_limits<float>::quiet_NaN();
    assert(!planView(asset, config, result, &error));
    asset.bounds[2].max[0] = 0.7f;
    config.width = 0;
    assert(!planView(asset, config, result, &error));

    // Stage 15.4: after verified disk source admission the duplicate
    // source index vector can be freed while immutable node ranges still
    // preserve meaningful LOD selection, shadows and reflection plans.
    config.width=1280;
    config.objectToClip=mat4::identity();
    assert(planView(asset,config,result,&error));
    const auto reference=result;
    // The per-view API also rejects malformed temporal masks without
    // modifying a previous successful render plan.
    config.hysteresisFraction=.2f;
    std::vector<uint8_t> invalidHistory{1,0};
    config.previousRefined=&invalidHistory;
    assert(!planView(asset,config,result,&error));
    assert(result.ranges.size()==reference.ranges.size());
    config.previousRefined=nullptr;
    assert(planView(asset,config,result,&error));
    assert(result.refinedNodes.size()==asset.nodes.size());
    auto saved=result.refinedNodes;
    config.previousRefined=&saved;
    assert(planView(asset,config,result,&error));
    assert(result.refinedNodes.size()==saved.size());
    config.previousRefined=nullptr;
    config.hysteresisFraction=0.f;
    assert(planView(asset,config,result,&error));
    assert(result.refinedNodes.empty()); // disabled cost-free by default
    const size_t originalIndices=asset.indices.size();
    const size_t released=asset.discardCpuIndexPayload();
    assert(released==originalIndices*sizeof(uint32_t));
    assert(asset.indices.empty() && asset.logicalIndexCount()==originalIndices);
    assert(planView(asset,config,result,&error));
    assert(result.ranges.size()==reference.ranges.size());
    assert(result.triangles==reference.triangles);
    assert(asset.discardCpuIndexPayload()==0); // no double-release
    return 0;
}
