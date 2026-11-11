#pragma once
#include "Cloth.h"
#include <array>
#include "engine/gfx/Gfx.h"
namespace aiverse::cloth {
// All objects share a regular physical grid. GPU bilinear embedding of a denser visual grid.
// No self-collision, shadows, occlusion culling or hidden simulation LOD in this renderer.
class Renderer {
public:
    Renderer()=default;
    Renderer(const Renderer&)=delete;
    Renderer& operator=(const Renderer&)=delete;
    ~Renderer(){shutdown();}
    bool init(gfx::Device& device,int objects,int physicalCells,int renderCells=71);
    void upload(std::span<const Cloth> cloths,std::span<const size_t> representatives={}); // once per frame, after beginFrame, before pass
    void draw(const render::mat4& viewProjection); // inside the render pass
    void shutdown(); // before device destruction
    size_t uploadBytes()const{return uploadBytes_;}
    uint32_t triangles()const{return indices_/3*uint32_t(objects_);}
private:
    gfx::Device* device_=nullptr;
    gfx::ShaderHandle shader_;
    gfx::PipelineHandle pipeline_;
    // Metal RHI allows at most three frames in flight; never overwrite a texture still in use.
    std::array<gfx::TextureHandle,3> textures_{},instances_{};
    std::array<int,3> textureHeights_{};
    int slot_=0;
    gfx::BufferHandle vb_,ib_;
    int objects_=0,n_=0,columns_=0,width_=0,height_=0;
    uint32_t indices_=0;
    size_t uploadBytes_=0;
    std::vector<render::float4> pixels_,instanceData_;
    std::vector<size_t> tileFor_;
};
}
