// CPU-only portal/camera graph: order, cycles, recursion and bad inputs.
#include "engine/render/CameraCaptureGraph.h"
#include <stdexcept>
using namespace aiverse::render;
static void check(bool pass){if(!pass)throw std::runtime_error("camera capture DAG");}
int main() {
 std::vector<CameraCaptureNode> nodes(3);
 for(int i=0;i<3;++i) {
    nodes[size_t(i)].capture.targetAssetKey=uint64_t(i+1);
    nodes[size_t(i)].capture.width=128;
    nodes[size_t(i)].capture.height=128;
 }
 nodes[0].sampleCount=1;nodes[0].samples[0]=2;
 nodes[1].sampleCount=1;nodes[1].samples[0]=3;
 auto plan=makeCameraCaptureGraphPlan(nodes);
 check(plan.diagnostics.empty()&&plan.ordered.size()==3);
 check(plan.ordered[0].targetAssetKey==3&&
       plan.ordered[1].targetAssetKey==2&&plan.ordered[2].targetAssetKey==1);
 nodes[2].sampleCount=1;nodes[2].samples[0]=1;
 plan=makeCameraCaptureGraphPlan(nodes);
 check(!plan.diagnostics.empty()&&plan.ordered.empty());
 nodes[2].sampleCount=0;nodes[2].recursionDepth=9;
 plan=makeCameraCaptureGraphPlan(nodes,4);
 check(!plan.diagnostics.empty());
 nodes[2].recursionDepth=0;
 nodes[1].samples[0]=42;
 plan=makeCameraCaptureGraphPlan(nodes);
 check(!plan.diagnostics.empty());
}
