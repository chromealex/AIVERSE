#include "engine/render/graph/TextureGraphLiveness.h"
#include <cstdlib>
#include <iostream>
using namespace aiverse::render::texture_graph;
static void check(bool ok){if(!ok)std::abort();}
int main(){
    // Diamond, duplicated input, an unreachable branch, pinned producer and duplicate output.
    const std::vector<std::vector<size_t>> edges={{},{0},{0,0},{1,2},{}};
    const std::vector<size_t> order={0,1,2,3,4};
    auto p=planDagLifetime(edges,order,{3,3});
    check(p.steps.size()==4&&p.peakLive==3&&p.retainedOutputs==1);
    check(p.steps[0].releaseAfter.empty()&&p.steps[1].releaseAfter.empty());
    check(p.steps[2].releaseAfter==std::vector<size_t>{0});
    check((p.steps[3].releaseAfter==std::vector<size_t>{1,2}));
    p=planDagLifetime(edges,order,{0,3});
    check(p.peakLive==4&&p.retainedOutputs==2&&p.steps[2].releaseAfter.empty());
    std::vector<std::vector<size_t>> chain(32);
    std::vector<size_t> chainOrder;
    for(size_t i=0;i<32;++i){chainOrder.push_back(i);if(i)chain[i]={i-1};}
    p=planDagLifetime(chain,chainOrder,{31});
    check(p.steps.size()==32&&p.peakLive==2&&p.retainedOutputs==1);
    for(size_t i=1;i<32;++i)check(p.steps[i].releaseAfter==std::vector<size_t>{i-1});
    bool bad=false;try{(void)planDagLifetime(edges,{1,0,2,3,4},{3});}catch(const std::invalid_argument&){bad=true;}check(bad);
    bad=false;try{(void)planDagLifetime(edges,order,{5});}catch(const std::invalid_argument&){bad=true;}check(bad);
    bad=false;try{(void)planDagLifetime(edges,{0,1,2,3,3},{3});}catch(const std::invalid_argument&){bad=true;}check(bad);
    std::cout<<"PASS: shared dependencies, duplicate edges, pinned outputs, pruning, chain lifetime, invalid plans\n";
}
