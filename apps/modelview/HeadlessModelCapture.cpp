// Render a bind-pose model thumbnail without initializing a window or graphics driver.
#include "HeadlessModelCapture.h"
#include "engine/gfx/Gfx.h"
#ifdef _WIN32
#include "engine/gfx/d3d/Direct3D.h"
#endif
#include "engine/render/CameraPng.h"
#include "engine/render/Model.h"
#include <cstdio>
#include <fstream>
#include <sstream>

namespace {
using namespace aiverse;
using namespace aiverse::render;
struct Vertex { float4 clip, color; };

bool read(const std::string& path, std::string& data) {
    std::ifstream file(path,std::ios::binary);
    if(!file)return false;
    std::ostringstream bytes;
    bytes << file.rdbuf();
    if(file.bad())return false;
    data=bytes.str();
    return true;
}
bool capture(gfx::Device& device,const Model& model,const std::string& output) {
    gfx::ShaderDesc shader;
    shader.cpu=gfx::CpuShader::ClipColor;
#ifdef _WIN32
    shader.hlsl11=R"(
        struct Input {float4 pos:TEXCOORD0;float4 color:TEXCOORD1;};
        struct Out {float4 pos:SV_Position;float4 color:COLOR0;};
        Out vs_main(Input i){Out o;o.pos=i.pos;o.color=i.color;return o;}
        float4 ps_main(Out i):SV_Target{return i.color;}
    )";
    shader.hlsl9=R"(
        struct Input {float4 pos:TEXCOORD0;float4 color:TEXCOORD1;};
        struct Out {float4 pos:POSITION;float4 color:COLOR0;};
        Out vs_main(Input i){Out o;o.pos=i.pos;o.color=i.color;return o;}
        float4 ps_main(float4 color:COLOR0):COLOR0{return color;}
    )";
#endif
    gfx::PipelineDesc pipeline;
    pipeline.shader=device.createShader(shader);
    pipeline.attrs[0]={0,gfx::VertexFormat::Float4,offsetof(Vertex,clip)};
    pipeline.attrs[1]={1,gfx::VertexFormat::Float4,offsetof(Vertex,color)};
    pipeline.attrCount=2; pipeline.stride=sizeof(Vertex); pipeline.indexType=gfx::IndexType::UInt32;
    pipeline.cull=gfx::CullMode::None;
    auto program=device.createPipeline(pipeline);
    if(!program.valid())return false;
    constexpr int size=512;
    Camera camera;
    const float radius=std::max(.001f,length(model.boundsMax-model.boundsMin)*.5f);
    camera.target=(model.boundsMax+model.boundsMin)*.5f;
    camera.distance=radius/std::sin(camera.fovY*.5f)*1.05f;
    camera.zNear=std::max(radius*.001f,camera.distance-radius*1.1f);
    camera.zFar=camera.distance+radius*1.1f;
    camera.pitch=.3f;camera.yaw=.55f;
    const auto vp=camera.proj(1,device.info().clipDepthZeroToOne)*camera.view();
    auto ib=device.createBuffer({gfx::BufferType::Index,gfx::Usage::Immutable,model.indices.size()*sizeof(uint32_t),model.indices.data()});
    if(!ib.valid())return false;
    device.beginFrame(size,size);
    device.beginPass({});
    device.setPipeline(program);
    device.setIndexBuffer(ib);
    for(int id:model.order) {
        const auto& node=model.nodes.at(id);
        if(node.mesh<0)continue;
        const auto& transform=model.bindWorld.at(id);
        const auto mvp=vp*transform;
        const auto normalTransform=transform.normalMatrix();
        const auto& mesh=model.meshes.at(node.mesh);
        for(const auto& prim:mesh.primitives) {
            if(!prim.indexCount)continue;
            if(prim.firstIndex>model.indices.size() || prim.indexCount>model.indices.size()-prim.firstIndex)return false;
            auto begin=model.indices.begin()+prim.firstIndex;
            auto [low,high]=std::minmax_element(begin,begin+prim.indexCount);
            if(*high>=model.vertices.size() || *low>INT32_MAX)return false;
            const size_t count=size_t(*high)-*low+1;
            const auto& material=model.materials.at(prim.material);
            std::vector<Vertex> vertices;
            vertices.reserve(count);
            for(size_t j=0;j<count;++j) {
                const auto& source=model.vertices.at(size_t(*low)+j);
                float4 color=material.baseColor;
                if(material.vertexColors) {
                    color.x*=float(source.color&255)/255;
                    color.y*=float((source.color>>8)&255)/255;
                    color.z*=float((source.color>>16)&255)/255;
                }
                const auto normal=normalize(normalTransform.transformDir(source.nrm));
                const float light=.32f+.68f*std::max(0.f,dot(normal,normalize({.3f,.8f,.5f})));
                color.x*=light;color.y*=light;color.z*=light;color.w=1;
                vertices.push_back({mvp*float4{source.pos.x,source.pos.y,source.pos.z,1},color});
            }
            auto vb=device.createBuffer({gfx::BufferType::Vertex,gfx::Usage::Immutable,
                                         sizeof(Vertex)*vertices.size(),vertices.data()});
            if(!vb.valid())return false;
            device.setVertexBuffer(vb);
            device.drawIndexed(prim.indexCount,prim.firstIndex,-int32_t(*low));
            device.destroyBuffer(vb);
        }
    }
    device.endPass();
    bool written=false;
    device.captureNextFrame([&](int w,int h,std::vector<uint8_t> rgba){
        written=cameraWritePngRgba8(output,w,h,rgba);
    });
    device.endFrame();
    return written && device.lastError().empty();
}
}
namespace aiverse::modelview {
int captureHeadlessModel(const std::string& backend,const std::string& path,const std::string& output) {
    try {
        std::unique_ptr<gfx::Device> device;
        if(backend=="cpu")device=gfx::createCpuDevice();
#ifdef _WIN32
        else if(backend=="d3d9" || backend=="d3d11" || backend=="d3d11-warp") {
            std::string error;
            auto version=backend=="d3d9"?gfx::Direct3DVersion::D3D9:gfx::Direct3DVersion::D3D11;
            device=gfx::createDirect3DDevice(version,512,512,nullptr,backend=="d3d11-warp",error);
            if(!device){std::fprintf(stderr,"%s\n",error.c_str());return 1;}
        }
#endif
        else {std::fprintf(stderr,"Unavailable capture backend: %s\n",backend.c_str());return 2;}
        auto model=render::loadModel(path,read);
        if(!capture(*device,*model,output)) {
            std::fprintf(stderr,"Model capture failed: %s\n",device->lastError().c_str());
            return 1;
        }
    }catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}
    return 0;
}
}
