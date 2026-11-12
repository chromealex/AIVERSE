// Host-only worker for autolod.py. All generated indices address the original vertices.
#include "meshoptimizer.h"
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
template<class T> void read(std::ifstream& f,T* p,size_t n){
    f.read(reinterpret_cast<char*>(p),std::streamsize(n*sizeof(T)));
    if(!f)throw std::runtime_error("truncated mesh input");
}
int main(int argc,char** argv){try{
    if(argc!=3)throw std::runtime_error("usage: aiverse_lod_simplify input.bin output.bin");
    std::ifstream in(argv[1],std::ios::binary);uint32_t h[4];read(in,h,4);
    const size_t vertices=h[0],count=h[1],attributes=h[2];
    if(!vertices||vertices>100000000||!count||count%3||count>300000000||attributes>16||h[3]!=0x31444f4c)
        throw std::runtime_error("invalid mesh header");
    float params[2];read(in,params,2);
    if(!std::isfinite(params[0])||!std::isfinite(params[1])||params[0]<=0||params[0]>=1||params[1]<=0||params[1]>1)
        throw std::runtime_error("invalid simplification limits");
    const auto bodyStart=in.tellg();in.seekg(0,std::ios::end);const auto fileEnd=in.tellg();in.seekg(bodyStart);
    const uint64_t expected=(uint64_t(vertices)*(3+attributes)+attributes+count)*4;
    if(fileEnd<bodyStart||uint64_t(fileEnd-bodyStart)!=expected)throw std::runtime_error("invalid mesh payload size");
    std::vector<float> positions(vertices*3),attrs(vertices*attributes),weights(attributes);
    std::vector<unsigned int> indices(count),result(count);
    read(in,positions.data(),positions.size());read(in,attrs.data(),attrs.size());read(in,weights.data(),weights.size());read(in,indices.data(),indices.size());
    for(float v:positions)if(!std::isfinite(v))throw std::runtime_error("nonfinite position");
    for(float v:attrs)if(!std::isfinite(v))throw std::runtime_error("nonfinite attribute");
    for(float v:weights)if(!std::isfinite(v)||v<0)throw std::runtime_error("invalid weight");
    for(auto v:indices)if(v>=vertices)throw std::runtime_error("index out of range");
    float error=0;size_t target=size_t(double(count)*params[0])/3*3;
    size_t size=meshopt_simplifyWithAttributes(result.data(),indices.data(),count,positions.data(),vertices,12,
        attributes?attrs.data():nullptr,attributes*4,attributes?weights.data():nullptr,attributes,nullptr,
        target,params[1],meshopt_SimplifyLockBorder,&error);
    meshopt_optimizeVertexCache(result.data(),result.data(),size,vertices);
    std::ofstream out(argv[2],std::ios::binary);uint32_t n=uint32_t(size);
    out.write(reinterpret_cast<const char*>(&n),4);out.write(reinterpret_cast<const char*>(&error),4);
    out.write(reinterpret_cast<const char*>(result.data()),std::streamsize(size*4));
    if(!out)throw std::runtime_error("cannot write simplified indices");
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
