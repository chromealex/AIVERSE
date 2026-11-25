// GPU-only previous-scene depth history. The source buffer and Device are
// owned by the native renderer host; copy into the emitter before step().
#pragma once
#include "engine/gfx/Gfx.h"
#include "engine/render/Math3D.h"
#include <cmath>
namespace aiverse::spectra {
inline constexpr uint32_t kDepthCollisionSide=64;
inline constexpr size_t kDepthCollisionBytes=kDepthCollisionSide*kDepthCollisionSide*sizeof(float);
struct DepthCollisionSnapshot {
    gfx::Device* device=nullptr;
    gfx::BufferHandle depth;
    render::mat4 viewProj{},inverseViewProj{};
    render::float3 eye{};
    uint64_t fence=0; // submission identity, not CPU frame index
    bool zeroToOne=true,topDown=true;
    float maxSurfaceEdge=2.f; // reject depth discontinuities, metres
    bool valid()const {
        if(!device||!depth.valid()||!fence||!std::isfinite(maxSurfaceEdge)||
           maxSurfaceEdge<=0||maxSurfaceEdge>100)return false;
        for(float v:viewProj.m)if(!std::isfinite(v))return false;
        for(float v:inverseViewProj.m)if(!std::isfinite(v))return false;
        if(!std::isfinite(eye.x)||!std::isfinite(eye.y)||!std::isfinite(eye.z))return false;
        const auto product=viewProj*inverseViewProj;
        for(int y=0;y<4;++y)for(int x=0;x<4;++x)
            if(std::abs(product.at(y,x)-(x==y?1.f:0.f))>0.02f)return false;
        return true;
    }
};
// Metal producer. Native Vulkan producer can supply the same storage layout.
// Call capture after the scene depth writer ended and before the next frame;
// never read a currently attached depth texture. No readback or CPU pixels.
class MetalDepthCollisionHistory {
public:
    MetalDepthCollisionHistory()=default;
    MetalDepthCollisionHistory(const MetalDepthCollisionHistory&)=delete;
    MetalDepthCollisionHistory& operator=(const MetalDepthCollisionHistory&)=delete;
    // Explicit shutdown before Device destruction (outside encoders).
    bool capture(gfx::Device& device,gfx::TextureHandle depth,const render::mat4& viewProj,
                 render::float3 eye) {
        snapshot_={};
        if(device.info().backend!=gfx::Backend::Metal||!device.canUpdateSharedBuffers()||
           !device.gpuHealthy()||(device_&&device_!=&device))return false;
        gfx::TextureInfo info;
        if(!depth.valid()||!device.textureInfo(depth,info)||info.format!=gfx::PixelFormat::Depth||
           info.width<64||info.height<64)return false;
        DepthCollisionSnapshot next;
        next.device=&device;next.viewProj=viewProj;next.inverseViewProj=viewProj.inverse();
        next.eye=eye;next.fence=device.gpuSubmissionFence();
        next.zeroToOne=device.info().clipDepthZeroToOne;next.topDown=device.info().renderTargetFlipY;
        device_=&device;
        if(!compute_.valid()){
            static constexpr const char* shader=R"MSL(
#include <metal_stdlib>
using namespace metal;
kernel void cs_main(uint id [[thread_position_in_grid]],
    device float* output [[buffer(0)]],depth2d<float,access::read> depth [[texture(0)]]) {
    if(id>=4096u)return;
    uint2 xy=uint2(id%64u,id/64u);
    uint2 size=uint2(depth.get_width(),depth.get_height());
    uint2 at=min((xy*2u+1u)*size/128u,size-1u);
    output[id]=depth.read(at);
}
)MSL";
            compute_=device.createCompute({shader,nullptr,"SpectraFX depth history"});
        }
        if(!buffer_.valid()){
            gfx::BufferDesc desc;desc.type=gfx::BufferType::Storage;
            desc.size=kDepthCollisionBytes;desc.label="SpectraFX depth history 64x64";
            buffer_=device.createBuffer(desc);
        }
        next.depth=buffer_;
        if(!compute_.valid()||!next.valid()||!device.beginCompute(compute_))return false;
        device.setComputeBuffer(0,buffer_);
        device.setComputeTexture(0,depth);
        const bool ok=device.dispatchChecked(4096);
        device.endCompute();
        if(ok)snapshot_=next;
        return ok;
    }
    const DepthCollisionSnapshot& snapshot()const{return snapshot_;}
    void invalidate(){snapshot_={};}
    void shutdown(){
        snapshot_={};
        if(device_){
            if(compute_.valid())device_->destroyCompute(compute_);
            if(buffer_.valid())device_->destroyBuffer(buffer_);
        }
        compute_={};buffer_={};device_=nullptr;
    }
private:
    gfx::Device* device_=nullptr;
    gfx::ComputeHandle compute_;
    gfx::BufferHandle buffer_;
    DepthCollisionSnapshot snapshot_;
};
}
