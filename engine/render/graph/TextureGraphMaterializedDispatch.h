// Ordered typed-buffer dispatch adapter. The caller owns GPU lifetime and fences.
#pragma once
#include <cstddef>
#include <cstdint>
#include <span>

namespace aiverse::render::texture_graph::gpu {
template<class Device,class Pipeline,class Buffer>
bool dispatchMaterializedPass(Device& device,Pipeline pipeline,Buffer output,
        std::span<const Buffer> inputs,uint32_t pixels,bool metal){
    if(!pipeline.valid()||!output.valid()||inputs.size()>3||!pixels||!device.gpuHealthy())return false;
    for(const auto& input:inputs)if(!input.valid()||input==output)return false;
    if(!device.beginCompute(pipeline))return false;
    bool encoded=false;
    try{
        device.setComputeBuffer(0,output);
        for(std::size_t i=0;i<inputs.size();++i)device.setComputeBuffer(int(i+1),inputs[i]);
        encoded=device.dispatchChecked(pixels);
    }catch(...){device.endCompute();throw;}
    device.endCompute();
    return encoded&&device.gpuHealthy()&&(!metal||device.computeDispatchSucceeded());
}
} // namespace aiverse::render::texture_graph::gpu
