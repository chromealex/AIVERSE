// Render a bind-pose model thumbnail without initializing a window or graphics driver.
#include "HeadlessModelFrame.h"
#include "engine/gfx/Gfx.h"
#ifdef _WIN32
#include "engine/gfx/d3d/Direct3D.h"
#endif
#include <filesystem>
#include "engine/render/Model.h"
#include <cstdio>
#include <fstream>
#include <sstream>

namespace {
using namespace aiverse;
using namespace aiverse::render;
struct Vertex { float4 clip, color; float2 uv; };

bool readModelBytes(const std::string& path, std::string& data) {
    std::ifstream file(std::filesystem::path(std::u8string(path.begin(), path.end())),std::ios::binary);
    if(!file)return false;
    std::ostringstream bytes;
    bytes << file.rdbuf();
    if(file.bad())return false;
    data=bytes.str();
    return true;
}
bool capture(gfx::Device& device,const Model& model,int width,int height,float yaw,float pitch,std::vector<uint8_t>& pixels) {
    gfx::ShaderDesc shader;
    shader.cpu=gfx::CpuShader::ClipColorTexture;shader.textureSlots=1;
#ifdef _WIN32
    shader.hlsl11=R"(
        struct Input {float4 pos:TEXCOORD0;float4 color:TEXCOORD1;float2 uv:TEXCOORD2;};
        struct Out {float4 pos:SV_Position;float4 color:COLOR0;float2 uv:TEXCOORD0;};
        Out vs_main(Input i){Out o;o.pos=i.pos;o.color=i.color;o.uv=i.uv;return o;}
        Texture2D baseColor:register(t0);SamplerState baseSampler:register(s0);
        float4 ps_main(Out i):SV_Target{return i.color*baseColor.Sample(baseSampler,i.uv);}
    )";
    shader.hlsl9=R"(
        struct Input {float4 pos:TEXCOORD0;float4 color:TEXCOORD1;float2 uv:TEXCOORD2;};
        struct Out {float4 pos:POSITION;float4 color:COLOR0;float2 uv:TEXCOORD0;};
        Out vs_main(Input i){Out o;o.pos=i.pos;o.color=i.color;o.uv=i.uv;return o;}
        sampler2D baseColor:register(s0);
        float4 ps_main(float4 color:COLOR0,float2 uv:TEXCOORD0):COLOR0{return color*tex2D(baseColor,uv);}
    )";
#endif
    gfx::PipelineDesc pipeline;
    pipeline.shader=device.createShader(shader);
    if(!pipeline.shader.valid())return false;
    pipeline.attrs[0]={0,gfx::VertexFormat::Float4,offsetof(Vertex,clip)};
    pipeline.attrs[1]={1,gfx::VertexFormat::Float4,offsetof(Vertex,color)};
    pipeline.attrs[2]={2,gfx::VertexFormat::Float2,offsetof(Vertex,uv)};
    pipeline.attrCount=3; pipeline.stride=sizeof(Vertex); pipeline.indexType=gfx::IndexType::UInt32;
    pipeline.cull=gfx::CullMode::None;
    auto program=device.createPipeline(pipeline);
    if(!program.valid())return false;
    const uint32_t white=0xffffffff;
    gfx::TextureDesc plain;plain.width=plain.height=1;plain.data=&white;
    const auto fallback=device.createTexture(plain);
    if(!fallback.valid())return false;
    std::vector<gfx::TextureHandle> textures(model.images.size());
    for(size_t i=0;i<model.images.size();++i) {
        const auto& image=model.images[i];
        const auto& rgba=image.sharedRgba?*image.sharedRgba:image.rgba;
        if(image.width<=0 || image.height<=0 || uint64_t(image.width)*image.height*4!=rgba.size())continue;
        gfx::TextureDesc td;td.width=image.width;td.height=image.height;td.repeat=image.repeat;
        td.filter=image.nearest?gfx::Filter::Nearest:gfx::Filter::Linear;td.data=rgba.data();
        textures[i]=device.createTexture(td);
        if(!textures[i].valid())return false;
    }
    const float aspect=float(width)/float(height);
    Camera camera;
    const float radius=std::max(.001f,length(model.boundsMax-model.boundsMin)*.5f);
    camera.target=(model.boundsMax+model.boundsMin)*.5f;
    camera.distance=radius/std::sin(std::atan(std::tan(camera.fovY*.5f)*std::min(aspect,1.f)))*1.05f;
    camera.zNear=std::max(radius*.001f,camera.distance-radius*1.1f);
    camera.zFar=camera.distance+radius*1.1f;
    camera.pitch=pitch;camera.yaw=yaw;
    const auto vp=camera.proj(aspect,device.info().clipDepthZeroToOne)*camera.view();
    auto ib=device.createBuffer({gfx::BufferType::Index,gfx::Usage::Immutable,model.indices.size()*sizeof(uint32_t),model.indices.data()});
    if(!ib.valid())return false;
    device.beginFrame(width,height);
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
            auto texture=fallback;
            if(material.baseTex>=0 && size_t(material.baseTex)<textures.size()) {
                texture=textures[size_t(material.baseTex)];
                if(!texture.valid())return false;
            }
            device.setTexture(0,texture);
            const float cosAngle=std::cos(material.uvRotation),sinAngle=std::sin(material.uvRotation);
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
                const float u=source.uv.x*material.uvScale.x,v=source.uv.y*material.uvScale.y;
                const float2 uv{cosAngle*u-sinAngle*v+material.uvOffset.x,
                                sinAngle*u+cosAngle*v+material.uvOffset.y};
                vertices.push_back({mvp*float4{source.pos.x,source.pos.y,source.pos.z,1},color,uv});
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
    device.captureNextFrame([&](int w,int h,std::vector<uint8_t> rgba){
        if(w==width && h==height) pixels=std::move(rgba);
    });
    device.endFrame();
    return pixels.size()==size_t(width)*height*4 && device.lastError().empty();
}
}

namespace aiverse::render {
bool headlessModelBackendAvailable(const std::string& backend) {
#ifdef _WIN32
    if(backend=="d3d9" || backend=="d3d11" || backend=="d3d11-warp")return true;
#endif
    return backend=="cpu";
}
std::unique_ptr<Model> loadHeadlessModel(const std::string& path) {
    return loadModel(path,readModelBytes);
}
std::vector<uint8_t> renderHeadlessModelFrame(const Model& model,const std::string& backend,
                                              int width,int height,float yaw,float pitch) {
    if (width<1 || height<1 || width>4096 || height>4096 || !std::isfinite(yaw) || !std::isfinite(pitch))
        throw std::invalid_argument("invalid frame dimensions (1..4096) or camera angle");
    std::unique_ptr<gfx::Device> device;
    if(backend=="cpu") device=gfx::createCpuDevice();
#ifdef _WIN32
    else if(headlessModelBackendAvailable(backend)) {
        std::string error;
        const auto version=backend=="d3d9"?gfx::Direct3DVersion::D3D9:gfx::Direct3DVersion::D3D11;
        device=gfx::createDirect3DDevice(version,width,height,nullptr,backend=="d3d11-warp",error);
        if(!device)throw std::runtime_error(error);
    }
#endif
    else throw std::invalid_argument("unavailable capture backend: "+backend);
    std::vector<uint8_t> pixels;
    if(!capture(*device,model,width,height,yaw,pitch,pixels))
        throw std::runtime_error("model capture failed: "+device->lastError());
    return pixels;
}
}
