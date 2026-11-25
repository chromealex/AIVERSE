#include "engine/render/graph/TextureGraphOutputReuse.h"
#include <cstdlib>
#include <iostream>
using namespace aiverse::render::texture_graph::gpu;
static void check(bool value){if(!value)std::abort();}
int main(){
    const std::vector<std::vector<size_t>> oe{{},{},{0},{1}};
    const std::vector<size_t> oo{0,1,2,3};
    std::vector<std::string> nk{"a","b","normal","color"};
    const std::map<std::string,size_t> named{{"normal",2},{"color",3}};
    const auto before=outputDependencyKeys(oe,oo,nk,named,"32:mips");
    nk[0]="changed";const auto after=outputDependencyKeys(oe,oo,nk,named,"32:mips");
    check(!unchangedOutput(before,after,"normal")&&unchangedOutput(before,after,"color"));
    check(!unchangedOutput(before,outputDependencyKeys(oe,oo,nk,named,"32:single"),"color"));
    check(!unchangedOutput({},after,"color"));
    check(unchangedOutput(after,after,"normal"));
    nk[0]=std::string(outputKeyBudget+1,'x');check(outputDependencyKeys(oe,oo,nk,named,"32:mips").empty());
    std::cout<<"PASS output closure, independent branch, configuration, cold cache, no-op, key limit\n";
}
