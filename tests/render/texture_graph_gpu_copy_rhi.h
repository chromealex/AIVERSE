// Opt-in real GPU shader -> padded storage -> sampled texture -> diagnostic readback.
#pragma once
#include "engine/gfx/Gfx.h"
#include "engine/render/graph/TextureGraphKernelSource.h"
#include "engine/render/graph/TextureGraphMaterializedDispatch.h"
#include <array>
#include <cstdio>
#include <memory>
#include <stdexcept>

namespace aiverse::tests {
inline int textureGraphGpuCopyAcceptance(gfx::Device& dev,gfx::Backend backend){
    namespace gpu=render::texture_graph::gpu;
    auto require=[](bool ok,const char* why){if(!ok)throw std::runtime_error(why);};
    struct Capture {bool done=false;gfx::TextureReadbackResult result;};
    auto run=[&](int n,bool vector,gfx::PixelFormat format){
        const auto type=vector?gpu::KernelValue::Vector:gpu::KernelValue::Scalar;
        const auto pack=gpu::packMaterializedTextureKernel(type,n);
        const std::string value="float((tid%n+3u*(tid/n))%251u)/255.0f";
        const auto fill=gpu::materializedKernel(type,{},n,
            vector?"result[tid]=float4("+value+",0.25f,0.75f,1.0f);":"result[tid]="+value+";");
        const bool metal=backend==gfx::Backend::Metal;
        auto capture=std::make_shared<Capture>();
        gfx::BufferHandle input{},packed{};
        gfx::TextureHandle texture{},mipped{};
        gfx::ComputeHandle fillPipeline{},packPipeline{};
        bool frame=false;int result=1;
        try{
            dev.beginFrame(64,64);frame=true;
            gfx::BufferDesc bd;bd.type=gfx::BufferType::Storage;
            bd.size=size_t(n)*n*(vector?16u:4u);input=dev.createBuffer(bd);
            bd.size=pack.bufferBytes;packed=dev.createBuffer(bd);
            gfx::TextureDesc td;td.width=td.height=n;td.format=format;td.mipLevels=1;
            texture=dev.createTexture(td);
            td.mipLevels=2;mipped=dev.createTexture(td);
            auto create=[&](const gpu::Source& source){
                return dev.createCompute({metal?source.msl.c_str():nullptr,
                    metal?nullptr:source.glsl.c_str(),"GPU texture transfer acceptance"});
            };
            fillPipeline=create(fill);packPipeline=create(pack.source);
            require(input.valid()&&packed.valid()&&texture.valid()&&mipped.valid()&&
                fillPipeline.valid()&&packPipeline.valid(),"allocation or shader compilation failed");
            const std::array<gfx::BufferHandle,0> noInputs{};
            const std::array<gfx::BufferHandle,1> oneInput{input};
            require(gpu::dispatchMaterializedPass(dev,fillPipeline,input,
                std::span<const gfx::BufferHandle>(noInputs),uint32_t(n*n),metal),"GPU fill rejected");
            require(gpu::dispatchMaterializedPass(dev,packPipeline,packed,
                std::span<const gfx::BufferHandle>(oneInput),uint32_t(n*n),metal),"GPU pack rejected");
            auto wrong=pack.copy;wrong.sourceOffset=4;
            require(!dev.copyStorageBufferToTexture2D(packed,texture,wrong),"unaligned copy accepted");
            require(!dev.copyStorageBufferToTexture2D(packed,mipped,pack.copy),"multi-mip copy accepted");
            require(dev.copyStorageBufferToTexture2D(packed,texture,pack.copy),"GPU buffer-to-texture copy rejected");
            const auto fence=dev.gpuSubmissionFence();
            require(fence!=0,"missing copy fence");
            require(dev.readTextureAsync(texture,{},[capture](auto bytes){
                capture->result=std::move(bytes);capture->done=true;
            }),"diagnostic readback rejected");
            dev.endFrame();frame=false;
            require(dev.waitIdle(),"copy GPU idle failed");
            require(!dev.copyStorageBufferToTexture2D(packed,texture,pack.copy),"copy outside frame accepted");
            for(int i=0;i<12&&(!capture->done||dev.gpuCompletedFence()<fence);++i){
                dev.beginFrame(64,64);dev.endFrame();
                require(dev.waitIdle(),"readback GPU idle failed");
            }
            const auto& data=capture->result;
            require(capture->done&&dev.gpuCompletedFence()>=fence&&data.success()&&
                data.width==n&&data.height==n&&data.format==format&&data.pixels.size()==size_t(n)*n*4,
                "missing completed texture bytes");
            for(int y=0;y<n;++y)for(int x=0;x<n;++x){
                const auto v=uint8_t((x+3*y)%251);
                const auto k=size_t(y*n+x)*4;
                require(data.pixels[k]==v&&data.pixels[k+1]==(vector?64:v)&&
                    data.pixels[k+2]==(vector?191:v)&&data.pixels[k+3]==255,
                    "GPU copy pixel mismatch (row pitch, raw bytes, channels or orientation)");
            }
            std::printf("PASS GPU copy %dx%d, row=%zu, %s\n",n,n,pack.copy.rowPitchBytes,vector?"vector sRGB bytes":"scalar RGBA8");
            result=0;
        }catch(const std::exception& e){std::fprintf(stderr,"FAIL GPU copy: %s / %s\n",e.what(),dev.lastError().c_str());}
        if(frame)dev.endFrame();
        if(!dev.waitIdle()||!dev.gpuHealthy())return 1;
        if(fillPipeline.valid())dev.destroyCompute(fillPipeline);
        if(packPipeline.valid())dev.destroyCompute(packPipeline);
        if(input.valid())dev.destroyBuffer(input);
        if(packed.valid())dev.destroyBuffer(packed);
        if(texture.valid())dev.destroyTexture(texture);
        if(mipped.valid())dev.destroyTexture(mipped);
        return result;
    };
    if(dev.info().backend!=backend||!dev.supportsStorageBufferTextureCopy()){
        std::fprintf(stderr,"FAIL expected GPU copy backend unavailable\n");return 1;
    }
    std::printf("GPU copy device: %s\n",dev.info().name.c_str());
    if(run(65,false,gfx::PixelFormat::RGBA8))return 1;
    return run(128,true,gfx::PixelFormat::RGBA8_sRGB);
}
} // namespace aiverse::tests
