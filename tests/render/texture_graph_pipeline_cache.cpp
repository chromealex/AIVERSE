#include "engine/render/graph/TextureGraphPipelineCache.h"
#include <cstdlib>
#include <iostream>
#include <vector>
using namespace aiverse::render::texture_graph::gpu;
static void check(bool ok){if(!ok)std::abort();}
int main(){
    IdlePipelineCache<int,2> cache;
    std::vector<int> destroyed;
    auto destroy=[&](auto& e){destroyed.push_back(e.first);if(e.second)destroyed.push_back(e.second);};
    const PipelineCacheKey a{"metal","A",""},b{"metal","B","second"},c{"metal","C",""};
    cache.put({a,1,0},destroy);cache.put({b,2,3},destroy);
    check(cache.size()==2&&!cache.take({"vulkan","A",""})&&!cache.take({"metal","B","other"}));
    auto hit=cache.take(a);check(hit&&hit->first==1&&cache.size()==1);
    cache.put(std::move(*hit),destroy);cache.put({c,4,0},destroy);
    check((destroyed==std::vector<int>{2,3})&&!cache.take(b));
    cache.put({a,5,0},destroy);check((destroyed==std::vector<int>{2,3,1}));
    cache.put({{"metal",std::string(1024u*1024u+1,'x'),""},6,0},destroy);
    check(cache.size()==2&&destroyed.back()==6);
    cache.clear(destroy);check(cache.size()==0&&destroyed.size()==6&&destroyed[4]==4&&destroyed[5]==5);
    cache.put({a,7,0},destroy);cache.abandon();check(cache.size()==0&&destroyed.size()==6);
    std::cout<<"PASS: exact keys, LRU, duplicate, bounds, clear, abandon\n";
}
