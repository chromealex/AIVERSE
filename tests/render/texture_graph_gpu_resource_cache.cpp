#include "engine/render/graph/TextureGraphGpuResourceCache.h"
#include <cstdlib>
#include <iostream>
using aiverse::render::texture_graph::gpu::RetiredBufferSet;
static void check(bool ok){if(!ok)std::abort();}
int main(){
    RetiredBufferSet<int> pool;
    std::vector<int> destroyed;
    auto destroy=[&](auto& e){destroyed.insert(destroyed.end(),e.handles.begin(),e.handles.end());};
    pool.put({{4,16,4},{1,2,3}},24,destroy);
    check(pool.bytes()==24&&!pool.take({16,4,4})&&!pool.take({4,16}));
    auto hit=pool.take({4,16,4});check(hit&&hit->handles==std::vector<int>({1,2,3})&&pool.bytes()==0);
    check(!pool.take({4,16,4}));
    pool.put(std::move(*hit),24,destroy);
    pool.put({{25},{4}},24,destroy);check(pool.bytes()==24&&destroyed==std::vector<int>{4});
    pool.put({{4,4},{5}},24,destroy);check(pool.bytes()==24&&destroyed.back()==5);
    pool.put({{8},{6}},24,destroy);check(pool.bytes()==8&&destroyed==std::vector<int>({4,5,1,2,3}));
    pool.clear(destroy);check(pool.bytes()==0&&destroyed.back()==6);
    pool.put({{8},{7}},24,destroy);pool.abandon();check(pool.bytes()==0&&destroyed.back()==6);
    check(!RetiredBufferSet<int>::admissible({0},24));
    check(!RetiredBufferSet<int>::admissible(std::vector<size_t>(34,1),128));
    check(!RetiredBufferSet<int>::admissible({24,1},24));
    std::cout<<"PASS: exact layout, exclusive take, replacement, budget, clear, abandon\n";
}
