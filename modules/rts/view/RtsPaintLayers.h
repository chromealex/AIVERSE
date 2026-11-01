#pragma once
#include "../../../engine/render/PaintLayer.h"
#include "../../../engine/render/Renderer3D.h"
namespace rts::game {
struct MapGrid; class RtsView;
class RtsPaintLayers {
public:
    void draw(render::Renderer3D&,gfx::Device&,const std::shared_ptr<render::PaintLayers>&,
              const MapGrid&,const RtsView&,const std::vector<uint8_t>* vision,double time,uint8_t viewer);
    void shutdown();
private:
    struct Layer {
        render::RenderTexture canvas;
        render::Mesh mesh;
        uint64_t meshKey=0;
        std::vector<float> value,target;
        uint64_t revision=0;
        std::vector<uint8_t> bytes;
        gfx::TextureHandle coast;   // categorical ground: signed shoreline distance (R = .5 + texels / 64)
        uint64_t seaKey=0;
    };
    gfx::Device* dev_=nullptr;
    gfx::ShaderHandle shader_,cellShader_;
    gfx::PipelineHandle pipe_,cellPipe_;
    void drawCells(render::Renderer3D&,gfx::Device&,render::PaintLayer&,const MapGrid&,const RtsView&,int id,double time);
    std::shared_ptr<render::PaintLayers> source_;
    std::map<int,Layer> layers_;
    std::vector<uint8_t> memory_;
    int w_=0,h_=0;
    uint8_t viewer_=0;
    uint64_t revision_=0;
    double lastTime_=-1;
};
}
