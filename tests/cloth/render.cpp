#include "engine/cloth/ClothRender.h"
#include <cstring>
#include <map>
#include <stdexcept>
#include <iostream>
using namespace aiverse;
struct FakeDevice: gfx::Device {
    struct Texture {int w,h;std::vector<render::float4> data;};
    gfx::DeviceInfo info_;std::string error;uint32_t next=1,instances=0,indices=0;
    std::map<uint32_t,Texture> textures;
    gfx::TextureHandle bound[2];
    const gfx::DeviceInfo& info()const override{return info_;}
    const std::string& lastError()const override{return error;}
    gfx::BufferHandle createBuffer(const gfx::BufferDesc&)override{return {next++};}
    void updateBuffer(gfx::BufferHandle,const void*,size_t,size_t)override{}
    void destroyBuffer(gfx::BufferHandle)override{}
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
    void beginFrame(int,int)override{}void endFrame()override{}
    void beginPass(const gfx::PassDesc&)override{}void endPass()override{}
    void setViewport(const gfx::Rect&)override{}void setScissor(const gfx::Rect&)override{}
    void setPipeline(gfx::PipelineHandle)override{}void setVertexBuffer(gfx::BufferHandle,size_t)override{}void setIndexBuffer(gfx::BufferHandle,size_t)override{}
    void setTexture(int slot,gfx::TextureHandle t)override{bound[slot]=t;}
    void setUniforms(int,const void*,size_t)override{}
    void draw(uint32_t,uint32_t)override{}
    void drawIndexed(uint32_t n,uint32_t,int32_t,uint32_t count)override{indices=n;instances=count;}
};
void check(bool b,const char* s){if(!b)throw std::runtime_error(s);}
int main(){try{
    FakeDevice d;cloth::Settings settings;settings.cells=2;auto grid=std::make_shared<cloth::Grid>(settings);std::vector<cloth::Cloth> objects;
    for(int i=0;i<4;++i)objects.emplace_back(grid,cloth::Vec{float(i)*4,0,0});
    cloth::Renderer r;check(r.init(d,4,2,4),"renderer init");size_t sources[]={0,0,2,2};uint32_t handles[4]{};
    for(int f=0;f<4;++f){r.upload(objects,sources);r.draw({});handles[f]=d.bound[0].id;
        check(d.instances==4&&d.indices==96,"instanced triangle count");
        auto& metadata=d.textures.at(d.bound[1].id).data;for(int i=0;i<4;++i){check(metadata[size_t(i)].x==i*4,"lost world origin");check(metadata[size_t(i)].w==i/2,"wrong source tile");}
        check(r.uploadBytes()==(6*3+4)*16,"shared upload budget");
    }
    check(handles[0]!=handles[1]&&handles[1]!=handles[2]&&handles[0]!=handles[2]&&handles[0]==handles[3],"texture ring reuse");
    r.upload(objects);r.draw({});check(r.uploadBytes()==(6*6+4)*16,"split texture did not grow");
    auto& metadata=d.textures.at(d.bound[1].id).data;for(int i=0;i<4;++i)check(metadata[size_t(i)].w==i,"split instance mapping");
    r.shutdown();check(d.textures.empty(),"texture leak");std::cout<<"cloth render: group atlas, transforms, split growth and frame texture ring passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
