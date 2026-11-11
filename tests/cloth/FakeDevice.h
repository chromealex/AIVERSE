#pragma once
#include "engine/cloth/ClothRender.h"
#include <cstring>
#include <map>
#include <stdexcept>
using namespace aiverse;
struct FakeDevice: gfx::Device {
    struct Texture {int w,h;std::vector<render::float4> data;};
    gfx::DeviceInfo info_;std::string error;uint32_t next=1,instances=0,indices=0;
    bool computeEnabled=false,inFrame=false,inCompute=false;unsigned dispatchCount=0;
    std::map<uint32_t,std::vector<uint8_t>> buffers;
    std::map<uint32_t,Texture> textures;
    gfx::TextureHandle bound[2];
    const gfx::DeviceInfo& info()const override{return info_;}
    const std::string& lastError()const override{return error;}
    gfx::BufferHandle createBuffer(const gfx::BufferDesc& d)override{auto id=next++;auto& b=buffers[id];b.resize(d.size);if(d.data)std::memcpy(b.data(),d.data,d.size);return {id};}
    void updateBuffer(gfx::BufferHandle h,const void* p,size_t size,size_t offset)override{auto& b=buffers.at(h.id);if(offset+size>b.size())throw std::runtime_error("buffer overflow");std::memcpy(b.data()+offset,p,size);}
    void destroyBuffer(gfx::BufferHandle h)override{buffers.erase(h.id);}
    gfx::TextureHandle createTexture(const gfx::TextureDesc& d)override{auto id=next++;textures.emplace(id,Texture{d.width,d.height,std::vector<render::float4>(size_t(d.width*d.height))});return {id};}
    void updateTexture(gfx::TextureHandle t,int x,int y,int w,int h,const void* p,int pitch)override{
        auto& dest=textures.at(t.id);if(x+w>dest.w||y+h>dest.h)throw std::runtime_error("texture overflow");
        for(int row=0;row<h;++row)std::memcpy(dest.data.data()+size_t((y+row)*dest.w+x),static_cast<const char*>(p)+row*pitch,size_t(w)*sizeof(render::float4));
    }
    void destroyTexture(gfx::TextureHandle t)override{textures.erase(t.id);}
    gfx::ShaderHandle createShader(const gfx::ShaderDesc& d)override{if(d.textureSlots!=2)throw std::runtime_error("missing instance texture");return {next++};}
    void destroyShader(gfx::ShaderHandle)override{}
    gfx::PipelineHandle createPipeline(const gfx::PipelineDesc&)override{return {next++};}
    void destroyPipeline(gfx::PipelineHandle)override{}
    void beginFrame(int,int)override{inFrame=true;}void endFrame()override{inFrame=false;}
    bool supportsCompute()const override{return computeEnabled;}
    gfx::ComputeHandle createCompute(const gfx::ComputeDesc&)override{return computeEnabled?gfx::ComputeHandle{next++}:gfx::ComputeHandle{};}
    bool beginCompute(gfx::ComputeHandle)override{inCompute=inFrame;return inCompute;}
    void dispatch(uint32_t,uint32_t)override{if(!inCompute)throw std::runtime_error("compute outside frame");++dispatchCount;}
    void endCompute()override{inCompute=false;}
    bool readBuffer(gfx::BufferHandle h,void* p,size_t size)override{if(inFrame||size>buffers.at(h.id).size())return false;std::memcpy(p,buffers.at(h.id).data(),size);return true;}
    void beginPass(const gfx::PassDesc&)override{}void endPass()override{}
    void setViewport(const gfx::Rect&)override{}void setScissor(const gfx::Rect&)override{}
    void setPipeline(gfx::PipelineHandle)override{}void setVertexBuffer(gfx::BufferHandle,size_t)override{}void setIndexBuffer(gfx::BufferHandle,size_t)override{}
    void setTexture(int slot,gfx::TextureHandle t)override{bound[slot]=t;}
    void setUniforms(int,const void*,size_t)override{}
    void draw(uint32_t,uint32_t)override{}
    void drawIndexed(uint32_t n,uint32_t,int32_t,uint32_t count)override{indices=n;instances=count;}
};
