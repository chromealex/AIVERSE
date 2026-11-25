#include "engine/render/graph/TextureGraphGroupProfile.h"
#include <cstdlib>
#include <iostream>
using namespace aiverse::graph_gpu_profile;
static void check(bool ok){if(!ok)std::abort();}
int main(){
    TextureGroupScope meta{2,1,256,"root",{"noise","invert","root"}};
    check(meta.valid());
    auto bad=meta;bad.root="noise";check(!bad.valid());
    bad=meta;bad.members={"root","root"};check(!bad.valid());
    bad=meta;bad.ordinal=64;check(!bad.valid());
    bad=meta;bad.slot=64;check(!bad.valid());
    bad=meta;bad.slotBytes=0;check(!bad.valid());
    bad=meta;bad.members.clear();check(!bad.valid());
    bad=meta;bad.members.insert(bad.members.begin(),std::string(129,'x'));check(!bad.valid());
    TextureGroupScopeBudget budget;
    for(size_t i=0;i<TextureGroupScopeBudget::maxPerFrame;++i)check(budget.admit(7));
    check(!budget.admit(7)&&!budget.admit(6)&&budget.admit(8));
    budget.clear();check(budget.admit(1));
    std::cout<<"PASS region metadata, duplicate/invalid bounds, per-frame timing admission\n";
}
