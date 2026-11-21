#pragma once
#include "engine/render/Renderer3D.h"
#include "modules/rts/view/RtsTerrainShaders.h"
namespace aiverse::test {
// Real production terrain shader and vertex ABI, with an asset-free receiver.
struct TerrainGiReceiver {
    gfx::BufferHandle vertices{};
    gfx::ShaderHandle shaders[4]{};
    gfx::PipelineHandle pipelines[4]{};
    bool compact=false;
    bool initialize(gfx::Device& d) {
        compact=d.info().backend==gfx::Backend::GL33 || d.info().backend==gfx::Backend::GLES3;
        struct Vertex {float x,y,z,nx,ny,nz;uint32_t color;float morph,pad;};
        static_assert(sizeof(Vertex)==36);
        const Vertex data[]={{-100,-100,0,0,0,1,0xffffffff,0,0},{100,-100,0,0,0,1,0xffffffff,0,0},
            {100,100,0,0,0,1,0xffffffff,0,0},{-100,-100,0,0,0,1,0xffffffff,0,0},
            {100,100,0,0,0,1,0xffffffff,0,0},{-100,100,0,0,0,1,0xffffffff,0,0}};
        gfx::BufferDesc buffer;buffer.data=data;buffer.size=sizeof(data);vertices=d.createBuffer(buffer);
        if(!vertices.valid())return false;
        for(int i=0;i<4;++i) {
            const bool gi=i>=2;
            const int tier=compact || (i%2)==0 ? 0:2;
            const std::string def="#define TERRAIN_TIER "+std::to_string(tier)+
                "\n#define TERRAIN_DETAIL 0\n#define TERRAIN_GL_NORMAL_ATLAS 0\n"+
                (gi?"#define DYNAMIC_GI 1\n#define DYNAMIC_GI_TERRAIN 1\n":"")+
                (gi&&compact?"#define DYNAMIC_GI_TERRAIN_COMPACT 1\n":"");
            const auto vs=def+game::shaders::kTerrainVS,fs=def+game::shaders::kTerrainFS,msl=def+game::shaders::kTerrainMSL;
            gfx::ShaderDesc shader;shader.glslVertex=vs.c_str();shader.glslFragment=fs.c_str();shader.msl=msl.c_str();
            shader.uniformSlots=gi?7:4;shader.textureSlots=compact?16:gi?22:21;shader.label="terrain GI acceptance";
            shaders[i]=d.createShader(shader);if(!shaders[i].valid())return false;
            gfx::PipelineDesc p;p.shader=shaders[i];p.stride=sizeof(Vertex);p.attrCount=4;p.cull=gfx::CullMode::None;
            p.attrs[0]={0,gfx::VertexFormat::Float3,0};p.attrs[1]={1,gfx::VertexFormat::Float3,12};
            p.attrs[2]={2,gfx::VertexFormat::UByte4N,24};p.attrs[3]={3,gfx::VertexFormat::Float2,28};
            pipelines[i]=d.createPipeline(p);if(!pipelines[i].valid())return false;
        }
        return true;
    }
    bool draw(gfx::Device& d,render::Renderer3D& r,bool low,bool enabled) {
        const bool gi=enabled && r.dynamicGiOverlayEnabled();
        d.setPipeline(pipelines[(gi?2:0)+(low?0:1)]);
        r.bindLitFrame();
        if(!compact){r.bindTrace(18);r.bindBakedTerrain(3,19,20);}
        float uniforms[60]={};uniforms[0]=1e8f;uniforms[1]=2e8f;
        d.setUniforms(0,uniforms,sizeof(uniforms));
        if(gi && !r.bindDynamicGiForCustomAt(compact?8:21))return false;
        d.setVertexBuffer(vertices);d.draw(6);return true;
    }
    void shutdownAfterGpuIdle(gfx::Device& d) {
        for(auto p:pipelines)if(p.valid())d.destroyPipeline(p);
        for(auto s:shaders)if(s.valid())d.destroyShader(s);
        if(vertices.valid())d.destroyBuffer(vertices);
        *this={};
    }
};
}
