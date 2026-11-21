// Opt-in VIVID sculpt-pick benchmark. No threshold-based performance PASS:
// prints measured cold and warmed CPU times to JSON on the actual host.
// Never run implicitly as a CTest. No GPU renderer required.
#include "engine/character/vivid/DeformedSculptWorkspace.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

using namespace aiverse;
using namespace aiverse::render;
using namespace aiverse::vivid;

namespace {
using Clock=std::chrono::steady_clock;

double milliseconds(Clock::time_point a,Clock::time_point b) {
    return std::chrono::duration<double,std::milli>(b-a).count();
}

struct Result {
    std::size_t vertices=0,triangles=0,checks=0,boxes=0;
    double coldMs=0,medianWarmMs=0,referenceMs=0;
    bool parity=false;
};

Result run(int side,int warmIterations) {
    Model model;
    const auto vertexCount=std::size_t(side)*std::size_t(side);
    model.vertices.resize(vertexCount);
    for(int y=0;y<side;++y)for(int x=0;x<side;++x)
        model.vertices[std::size_t(y*side+x)].pos={
            float(x)/float(side-1),float(y)/float(side-1),0.f};
    // Checkerboard cells keep 1M-vertex input below the 1M-triangle
    // selected-mesh bound, with meaningful BVH traversal work.
    for(int y=0;y<side-1;++y)for(int x=0;x<side-1;++x) {
        if((x+y)&1)continue;
        const auto a=std::uint32_t(y*side+x),
                   b=a+1,c=a+std::uint32_t(side);
        model.indices.insert(model.indices.end(),{a,b,c,b,c+1,c});
    }
    MeshDef mesh;
    Primitive p;
    p.firstIndex=0;p.indexCount=std::uint32_t(model.indices.size());
    mesh.primitives.push_back(p);
    model.meshes.push_back(std::move(mesh));
    Node node;
    node.mesh=0;
    model.nodes.push_back(node);
    const auto morphs=SparseMorphLibrary::compile(vertexCount,{});
    const auto binding=FaceShapeBinding::compile(morphs,{});
    const std::array<mat4,1> pose{{mat4{}}};
    const FaceFrame face{};
    const float3 dir{0,0,-1};
    // Choose an existing checkerboard cell for every grid dimension.
    // A constant XY position can hit a checkerboard hole or an edge.
    int cx=(side-1)/2,cy=(side-1)/2;
    if((cx+cy)&1)--cx;
    const float3 origin{
        (float(cx)+0.25f)/float(side-1),
        (float(cy)+0.25f)/float(side-1),2.f};
    DeformedSculptWorkspace workspace;
    Result result;
    result.vertices=vertexCount;
    result.triangles=model.indices.size()/3u;
    const auto begin=Clock::now();
    const auto first=workspace.pick(model,0,pose,morphs,binding,face,
                                    origin,dir,vertexCount);
    const auto afterCold=Clock::now();
    result.coldMs=milliseconds(begin,afterCold);
    std::vector<double> warm;
    warm.reserve(std::size_t(warmIterations));
    SculptPick last=first;
    for(int j=0;j<warmIterations;++j) {
        const auto before=Clock::now();
        last=workspace.pick(model,0,pose,morphs,binding,face,
                            origin,dir,vertexCount);
        warm.push_back(milliseconds(before,Clock::now()));
    }
    std::sort(warm.begin(),warm.end());
    result.medianWarmMs=warm[warm.size()/2];
    const auto refStart=Clock::now();
    const auto reference=pickDeformedSculptVertex(
        model,0,pose,morphs,binding,face,origin,dir,vertexCount);
    result.referenceMs=milliseconds(refStart,Clock::now());
    result.parity=first.hit && reference.hit && last.hit &&
                  first.vertex==reference.vertex &&
                  last.vertex==reference.vertex &&
                  std::fabs(last.rayDistance-reference.rayDistance)<1.e-5f;
    result.checks=workspace.stats().testedTriangles;
    result.boxes=workspace.stats().visitedNodes;
    return result;
}
}

int main(int argc,char** argv) {
    try {
        int iterations=3;
        if(argc>2)throw std::invalid_argument("usage: sculpt_pick_bench [warm-clicks 1..32]");
        if(argc==2) {
            const std::string arg=argv[1];
            const auto value=std::stoi(arg);
            if(value<1||value>32||std::to_string(value)!=arg)
                throw std::invalid_argument("warm-clicks must be 1..32");
            iterations=value;
        }
        std::cout<<std::fixed<<std::setprecision(4);
        std::cout<<"{\n  \"workload\":\"VIVID CPU morph+skin+BVH picking\",\n";
        std::cout<<"  \"warm_clicks\":"<<iterations<<",\n";
        std::cout<<"  \"samples\":[\n";
        bool all=true;
        for(int n:{100,317,1000}) {
            auto r=run(n,iterations);
            all=all&&r.parity;
            if(n!=100)std::cout<<",\n";
            std::cout<<"    {\"vertices\":"<<r.vertices
                     <<",\"triangles\":"<<r.triangles
                     <<",\"cold_ms\":"<<r.coldMs
                     <<",\"warm_median_ms\":"<<r.medianWarmMs
                     <<",\"linear_reference_ms\":"<<r.referenceMs
                     <<",\"bvh_triangles_tested\":"<<r.checks
                     <<",\"bvh_nodes_visited\":"<<r.boxes
                     <<",\"parity\":"<<(r.parity?"true":"false")
                     <<"}";
        }
        std::cout<<"\n  ],\n  \"result\":\""<<(all?"PASS":"FAIL")
                 <<"\"\n}\n";
        return all?0:1;
    }catch(const std::exception& e) {
        std::cerr<<"VIVID sculpt pick benchmark failed: "<<e.what()<<"\n";
        return 2;
    }
}
