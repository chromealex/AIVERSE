#include "ClothRender.h"
#include "ClothShaders.h"
#include <algorithm>
namespace aiverse::cloth {
bool Renderer::init(gfx::Device& d,int objects,int physicalCells,int renderCells){
    shutdown();
    if(objects<1||objects>2048||physicalCells<2||physicalCells>128||renderCells<2||renderCells>128)return false;
    device_=&d;objects_=objects;n_=physicalCells+1;columns_=int(std::ceil(std::sqrt(float(objects))));
    width_=columns_*n_;height_=((objects+columns_-1)/columns_)*n_;
    if(width_>4096||height_>4096){shutdown();return false;}
    std::vector<render::float2> vertices;std::vector<uint32_t> indices;int m=renderCells+1;
    for(int y=0;y<m;++y)for(int x=0;x<m;++x)vertices.push_back({float(x)/renderCells,float(y)/renderCells});
    for(int y=0;y<renderCells;++y)for(int x=0;x<renderCells;++x){uint32_t a=uint32_t(y*m+x),b=a+1,c=a+uint32_t(m),e=c+1;indices.insert(indices.end(),{a,c,b,b,c,e});}
    indices_=uint32_t(indices.size());
    vb_=d.createBuffer({gfx::BufferType::Vertex,gfx::Usage::Immutable,vertices.size()*sizeof(vertices[0]),vertices.data(),"cloth UV grid"});
    ib_=d.createBuffer({gfx::BufferType::Index,gfx::Usage::Immutable,indices.size()*sizeof(indices[0]),indices.data(),"cloth triangles"});
    pixels_.resize(size_t(width_*height_));
    gfx::TextureDesc t;t.width=width_;t.height=height_;t.format=gfx::PixelFormat::RGBA32F;t.filter=gfx::Filter::Nearest;t.label="cloth simulation positions";
    t.height=n_;
    for(size_t i=0;i<textures_.size();++i){textures_[i]=d.createTexture(t);textureHeights_[i]=n_;}
    t.width=objects_;t.height=1;t.label="cloth instance transforms";
    for(auto& texture:instances_)texture=d.createTexture(t);
    instanceData_.resize(size_t(objects_));tileFor_.resize(size_t(objects_));
    gfx::ShaderDesc sh;sh.glslVertex=kVertex;sh.glslFragment=kFragment;sh.msl=kMetal;sh.textureSlots=2;sh.label="cloth bilinear embedding";shader_=d.createShader(sh);
    if(!shader_.valid()){shutdown();return false;}
    gfx::PipelineDesc p;p.shader=shader_;p.attrs[0]={0,gfx::VertexFormat::Float2,0};p.attrCount=1;p.stride=sizeof(render::float2);p.indexType=gfx::IndexType::UInt32;p.cull=gfx::CullMode::None;
    pipeline_=d.createPipeline(p);
    if(!vb_.valid()||!ib_.valid()||!pipeline_.valid()){shutdown();return false;}
    for(size_t i=0;i<textures_.size();++i)if(!textures_[i].valid()||!instances_[i].valid()){shutdown();return false;}
    return true;
}
void Renderer::upload(std::span<const Cloth> cloths,std::span<const size_t> representatives){
    if(!device_||cloths.size()!=size_t(objects_)||(!representatives.empty()&&representatives.size()!=cloths.size()))return;
    auto source=[&](size_t i){return representatives.empty()?i:representatives[i];};
    for(size_t i=0;i<cloths.size();++i)if(cloths[i].positions().size()!=size_t(n_*n_)||source(i)>=cloths.size()||source(source(i))!=source(i))return;
    size_t groups=0;
    for(size_t i=0;i<cloths.size();++i)if(source(i)==i)tileFor_[i]=groups++;
    int rows=int((groups+size_t(columns_)-1)/size_t(columns_))*n_;
    slot_=(slot_+1)%int(textures_.size());
    if(textureHeights_[size_t(slot_)]<rows){
        gfx::TextureDesc t;t.width=width_;t.height=rows;t.format=gfx::PixelFormat::RGBA32F;t.filter=gfx::Filter::Nearest;t.label="cloth simulation positions";
        auto grown=device_->createTexture(t);if(!grown.valid())return;
        device_->destroyTexture(textures_[size_t(slot_)]);textures_[size_t(slot_)]=grown;textureHeights_[size_t(slot_)]=rows;
    }
    for(size_t i=0;i<cloths.size();++i){size_t tile=tileFor_[source(i)];auto origin=cloths[i].origin();instanceData_[i]={origin.x,origin.y,origin.z,float(tile)};
        if(source(i)!=i)continue;
        auto p=cloths[i].positions();int tx=int(tile%size_t(columns_))*n_,ty=int(tile/size_t(columns_))*n_;
        for(int y=0;y<n_;++y)for(int x=0;x<n_;++x){auto v=p[size_t(y*n_+x)];pixels_[size_t((ty+y)*width_+tx+x)]={v.x,v.y,v.z,1};}
    }
    int uploadWidth=std::min(columns_,int(groups))*n_;
    device_->updateTexture(textures_[size_t(slot_)],0,0,uploadWidth,rows,pixels_.data(),width_*int(sizeof(pixels_[0])));
    device_->updateTexture(instances_[size_t(slot_)],0,0,objects_,1,instanceData_.data(),objects_*int(sizeof(instanceData_[0])));
    uploadBytes_=size_t(uploadWidth*rows+objects_)*sizeof(render::float4);
}
void Renderer::draw(const render::mat4& vp){if(!device_)return;
    struct Uniforms {render::mat4 vp;render::float4 config;}u{vp,{float(n_),float(columns_),0,0}};
    device_->setPipeline(pipeline_);device_->setVertexBuffer(vb_);device_->setIndexBuffer(ib_);device_->setTexture(0,textures_[size_t(slot_)]);device_->setTexture(1,instances_[size_t(slot_)]);device_->setUniforms(0,&u,sizeof(u));device_->drawIndexed(indices_,0,0,uint32_t(objects_));
}
void Renderer::shutdown(){
    if(!device_)return;
    device_->destroyPipeline(pipeline_);device_->destroyShader(shader_);
    for(auto t:textures_)device_->destroyTexture(t);
    for(auto t:instances_)device_->destroyTexture(t);
    device_->destroyBuffer(vb_);device_->destroyBuffer(ib_);device_=nullptr;
    pipeline_={};shader_={};textures_={};instances_={};textureHeights_={};vb_={};ib_={};slot_=0;uploadBytes_=0;
    pixels_.clear();instanceData_.clear();tileFor_.clear();
}
}
