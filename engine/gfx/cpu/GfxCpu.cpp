// Offscreen reference rasterizer. Device calls execute immediately on the render thread.
#include "engine/gfx/Gfx.h"
#include <array>
#include <cmath>
#include <cstring>
#include <unordered_map>

namespace aiverse::gfx {
namespace {
using Vec = std::array<double, 4>;
struct Vertex { Vec clip, color; std::array<double,2> uv{}; };
struct ScreenVertex { double x, y, depth, inverseW; Vec color; std::array<double,2> uv; };

double edge(const ScreenVertex& a, const ScreenVertex& b, double x, double y) {
    return (b.x-a.x)*(y-a.y)-(b.y-a.y)*(x-a.x);
}
bool topLeft(const ScreenVertex& a, const ScreenVertex& b) {
    return b.y < a.y || (a.y == b.y && b.x > a.x);
}
double planeDistance(const Vertex& v, int plane) {
    return v.clip[3] + (plane%2 ? -1 : 1)*v.clip[plane/2];
}
Vertex between(const Vertex& a, const Vertex& b, double t) {
    Vertex v;
    for (int k=0; k<4; ++k) {
        v.clip[k]=a.clip[k]+(b.clip[k]-a.clip[k])*t;
        v.color[k]=a.color[k]+(b.color[k]-a.color[k])*t;
    }
    for(int k=0;k<2;++k)v.uv[k]=a.uv[k]+(b.uv[k]-a.uv[k])*t;
    return v;
}
uint8_t unorm(double x) {
    return uint8_t(std::lround(std::clamp(x, 0.0, 1.0)*255));
}
bool sizeValid(int width, int height) {
    return width>0 && height>0 && width<=8192 && height<=8192 && uint64_t(width)*height<=16777216;
}
struct Surface {
    int width=0,height=0;
    std::vector<uint8_t> color;
    std::vector<float> depth;
    bool attachment=false,repeat=false;
    Filter filter=Filter::Linear;

    Vec sample(double u,double v) const {
        auto coordinate=[&](double value,int extent){
            value=repeat?value-std::floor(value):std::clamp(value,0.,1.);
            return value*extent-.5;
        };
        auto texel=[&](int x,int y,int channel){
            auto index=[&](int i,int n){return repeat?(i+n)%n:std::clamp(i,0,n-1);};
            return color[(size_t(index(y,height))*width+index(x,width))*4+channel]/255.;
        };
        const double x=coordinate(u,width),y=coordinate(v,height);
        const auto ix=int(std::floor(x)),iy=int(std::floor(y));
        const double fx=x-ix,fy=y-iy;
        auto lerp=[](double a,double b,double t){return a+(b-a)*t;};
        Vec c{};
        for(int k=0;k<4;++k) {
            if(filter==Filter::Nearest)c[k]=texel(int(std::floor(x+.5)),int(std::floor(y+.5)),k);
            else {
                const double upper=lerp(texel(ix,iy,k),texel(ix+1,iy,k),fx);
                const double lower=lerp(texel(ix,iy+1,k),texel(ix+1,iy+1,k),fx);
                c[k]=lerp(upper,lower,fy);
            }
        }
        return c;
    }
};
struct CpuPipeline { PipelineDesc layout; CpuShader shader; };
struct Buffer { BufferType type; Usage usage; std::vector<uint8_t> bytes; };

class CpuDevice final : public Device {
    DeviceInfo deviceInfo{Backend::Cpu,false,true,"AIVERSE CPU"};
    std::string error;
    uint32_t nextId=1;
    std::unordered_map<uint32_t,Buffer> buffers;
    std::unordered_map<uint32_t,Surface> textures;
    std::unordered_map<uint32_t,CpuShader> shaders;
    std::unordered_map<uint32_t,CpuPipeline> pipelines;
    TextureHandle sampler{};
    Surface screen;
    Surface* color=nullptr;
    Surface* depth=nullptr;
    bool frame=false,pass=false;
    Rect viewport{},scissor{};
    BufferHandle vb{},ib{};
    size_t vbOffset=0,ibOffset=0;
    PipelineHandle pipeline{};
    CaptureFn capture;
public:
    const DeviceInfo& info() const override { return deviceInfo; }
    const std::string& lastError() const override { return error; }
    bool supports(PixelFormat f) const override { return f==PixelFormat::RGBA8 || f==PixelFormat::Depth; }
    BufferHandle createBuffer(const BufferDesc& d) override {
        if (!d.size || (d.type!=BufferType::Vertex && d.type!=BufferType::Index)) {
            error="CPU device requires a nonempty vertex or index buffer";return {};
        }
        Buffer b{d.type,d.usage,std::vector<uint8_t>(d.size)};
        if(d.data) std::memcpy(b.bytes.data(),d.data,d.size);
        const uint32_t id=nextId++;
        buffers.emplace(id,std::move(b));
        return {id};
    }
    void updateBuffer(BufferHandle h,const void* data,size_t size,size_t offset) override {
        auto it=buffers.find(h.id);
        if(it==buffers.end() || it->second.usage!=Usage::Dynamic || !data ||
           size>it->second.bytes.max_size() || offset>it->second.bytes.max_size()-size) {
            error="invalid CPU buffer update";return;
        }
        auto& bytes=it->second.bytes;
        if(offset+size>bytes.size()) bytes.resize(offset+size);
        std::memcpy(bytes.data()+offset,data,size);
    }
    void destroyBuffer(BufferHandle h) override { buffers.erase(h.id); }
    TextureHandle createTexture(const TextureDesc& d) override {
        if(validateTextureDesc(d)!=TextureDescError::None || !sizeValid(d.width,d.height) || !supports(d.format) ||
           (d.format==PixelFormat::Depth && !d.renderTarget) ||
           d.dimension!=TextureDimension::Texture2D || d.depth!=1 || d.arrayLayers!=1 ||
           d.sampleCount!=1 || d.mipLevels>1 || d.levelCount || d.storage || d.mipmaps || d.compare) {
            error="CPU device requires a single-level RGBA8 image or depth attachment";return {};
        }
        Surface s; s.width=d.width; s.height=d.height;s.attachment=d.renderTarget;s.filter=d.filter;s.repeat=d.repeat;
        const size_t count=size_t(s.width)*s.height;
        if(d.format==PixelFormat::Depth) {
            s.depth.resize(count,1);
            if(d.data) std::memcpy(s.depth.data(),d.data,count*sizeof(float));
        } else {
            s.color.resize(count*4);
            if(d.data) std::memcpy(s.color.data(),d.data,s.color.size());
        }
        uint32_t id=nextId++;
        textures.emplace(id,std::move(s));
        return {id};
    }
    void updateTexture(TextureHandle h,int x,int y,int w,int ht,const void* data,int pitch) override {
        auto it=textures.find(h.id);
        if(it==textures.end() || it->second.color.empty() || !data || x<0 || y<0 || w<=0 || ht<=0 ||
           x>it->second.width-w || y>it->second.height-ht || (pitch && pitch<w*4)) {
            error="invalid CPU texture update";return;
        }
        auto& s=it->second;
        if(!pitch)pitch=w*4;
        for(int j=0;j<ht;++j)
            std::memcpy(&s.color[(size_t(y+j)*s.width+x)*4],static_cast<const uint8_t*>(data)+size_t(j)*pitch,size_t(w)*4);
    }
    void destroyTexture(TextureHandle h) override {
        auto it=textures.find(h.id);
        if(it==textures.end())return;
        if(color==&it->second || depth==&it->second)endPass();
        if(sampler.id==h.id)sampler={};
        textures.erase(it);
    }
    bool textureInfo(TextureHandle h,TextureInfo& out) const override {
        auto it=textures.find(h.id);
        if(it==textures.end())return false;
        out={it->second.width,it->second.height,it->second.color.empty()?PixelFormat::Depth:PixelFormat::RGBA8};return true;
    }
    ShaderHandle createShader(const ShaderDesc& d) override {
        if((d.cpu!=CpuShader::ClipColor || d.textureSlots!=0) &&
           (d.cpu!=CpuShader::ClipColorTexture || d.textureSlots!=1)) {error="missing native CPU shader implementation";return {};}
        uint32_t id=nextId++; shaders.emplace(id,d.cpu);return {id};
    }
    void destroyShader(ShaderHandle h) override {shaders.erase(h.id);}
    PipelineHandle createPipeline(const PipelineDesc& d) override {
        auto shader=shaders.find(d.shader.id);
        const bool textured=shader!=shaders.end() && shader->second==CpuShader::ClipColorTexture;
        if(shader==shaders.end() || d.primitive!=Primitive::Triangles || d.attrCount!=(textured?3:2) ||
           (textured && (d.attrs[2].location!=2 || d.attrs[2].format!=VertexFormat::Float2 || d.stride<8 || d.attrs[2].offset>d.stride-8)) ||
           d.attrs[0].location!=0 || d.attrs[0].format!=VertexFormat::Float4 ||
           d.attrs[1].location!=1 || d.attrs[1].format!=VertexFormat::Float4 ||
           d.stride<16 || d.attrs[0].offset>d.stride-16 || d.attrs[1].offset>d.stride-16 ||
           d.depthOnly || d.vertexPerInstance || d.colorFormat!=PixelFormat::RGBA8 ||
           d.colorAttachmentCount!=1 || d.sampleCount!=1 || d.depthBiasConstant || d.depthBiasSlope ||
           (d.blend!=BlendMode::Opaque && d.blend!=BlendMode::Alpha)) {
            error="unsupported CPU triangle pipeline";return {};
        }
        auto config=d;config.label=nullptr;
        uint32_t id=nextId++;pipelines.emplace(id,CpuPipeline{config,shader->second});return {id};
    }
    void destroyPipeline(PipelineHandle p) override {pipelines.erase(p.id);}
    void beginFrame(int w,int h) override {
        if(frame || !sizeValid(w,h)){error="invalid CPU framebuffer";return;}
        if(w!=fbW_ || h!=fbH_) {
            Surface fresh;
            fresh.width=w;fresh.height=h;
            fresh.color.resize(size_t(w)*h*4);fresh.depth.resize(size_t(w)*h,1);
            screen=std::move(fresh);fbW_=w;fbH_=h;
        }
        frame=true;
    }
    void endFrame() override {
        if(!frame || pass){error="invalid CPU endFrame";return;}
        frame=false;
        auto fn=std::move(capture);capture={};
        if(fn)fn(fbW_,fbH_,screen.color);
    }
    void captureNextFrame(CaptureFn fn) override {capture=std::move(fn);}
    bool captureTexture(TextureHandle h,CaptureFn fn) override {
        auto it=textures.find(h.id);
        if(pass || !fn || it==textures.end() || it->second.color.empty())return false;
        fn(it->second.width,it->second.height,it->second.color);return true;
    }
    void beginPass(const PassDesc& d) override {
        if(!frame || pass){error="invalid CPU beginPass";return;}
        auto target=[&](TextureHandle h)->Surface*{
            if(!h.valid())return nullptr;
            auto it=textures.find(h.id);
            return it==textures.end()?nullptr:&it->second;
        };
        auto* c=target(d.colorTarget);auto* z=target(d.depthTarget);
        if(!d.colorTarget.valid() && !d.depthTarget.valid())c=z=&screen;
        if((d.colorTarget.valid() && !c) || (d.depthTarget.valid() && !z) ||
           (c && (c->color.empty() || (c!=&screen && !c->attachment))) || (z && z->depth.empty()) ||
           (c && z && (c->width!=z->width || c->height!=z->height))) {
            error="invalid CPU render attachments";return;
        }
        auto* s=c?c:z;
        viewport={0,0,s->width,s->height};scissor={};pipeline={};
        color=c;depth=z;pass=true;
        if(color && d.clearColorEnabled) {
            std::array<uint8_t,4> rgba{};
            for(int k=0;k<4;++k)rgba[k]=unorm(d.clearColor[k]);
            for(size_t i=0;i<color->color.size();i+=4)std::memcpy(color->color.data()+i,rgba.data(),4);
        }
        if(depth && d.clearDepthEnabled)std::fill(depth->depth.begin(),depth->depth.end(),d.clearDepth);
    }
    void endPass() override {pass=false;color=depth=nullptr;}
    void setViewport(const Rect& r) override {viewport=r;}
    void setScissor(const Rect& r) override {scissor=r;}
    void setPipeline(PipelineHandle p) override {pipeline=p;}
    void setVertexBuffer(BufferHandle b,size_t offset) override {vb=b;vbOffset=offset;}
    void setIndexBuffer(BufferHandle b,size_t offset) override {ib=b;ibOffset=offset;}
    void setTexture(int slot,TextureHandle t) override {
        if(slot!=0 || (t.valid() && (!textures.count(t.id) || textures.at(t.id).color.empty()))) {
            error="invalid CPU texture binding";return;
        }
        sampler=t;
    }
    void setUniforms(int,const void*,size_t) override {error="clip/color CPU shader has no uniforms";}
    void draw(uint32_t n,uint32_t first) override {submit(n,first,0,false);}
    void drawIndexed(uint32_t n,uint32_t first,int32_t base,uint32_t instances) override {
        if(!instances)return;
        if(instances!=1){error="CPU pipeline does not support instancing";return;}
        submit(n,first,base,true);
    }
private:
    void submit(uint32_t n,uint32_t first,int32_t base,bool indexed) {
        auto p=pipelines.find(pipeline.id);
        auto v=buffers.find(vb.id);
        if(!pass || p==pipelines.end() || v==buffers.end() || v->second.type!=BufferType::Vertex) {
            error="CPU draw missing a pipeline or vertex buffer";return;
        }
        const auto& desc=p->second.layout;
        const Surface* source=nullptr;
        if(p->second.shader==CpuShader::ClipColorTexture) {
            auto tex=textures.find(sampler.id);
            if(tex==textures.end() || &tex->second==color){error="missing or active CPU texture";return;}
            source=&tex->second;
        }
        const auto& bytes=v->second.bytes;
        if(vbOffset>bytes.size()) {error="invalid CPU vertex buffer offset";return;}
        const size_t capacity=(bytes.size()-vbOffset)/desc.stride;
        const size_t indexSize=desc.indexType==IndexType::UInt16?2:4;
        const uint8_t* indices=nullptr;
        if(indexed) {
            auto i=buffers.find(ib.id);
            if(desc.indexType==IndexType::None || i==buffers.end() || i->second.type!=BufferType::Index ||
               ibOffset>i->second.bytes.size() || uint64_t(first)+n>(i->second.bytes.size()-ibOffset)/indexSize){
                error="invalid CPU index range";return;
            }
            indices=i->second.bytes.data()+ibOffset+size_t(first)*indexSize;
        } else if(uint64_t(first)+n>capacity){error="invalid CPU vertex range";return;}
        for(uint64_t i=0;i+2<n;i+=3) {
            std::array<Vertex,3> tri{};
            for(size_t j=0;j<3;++j) {
                int64_t id=int64_t(first)+i+j;
                if(indices) {
                    uint32_t value=0;
                    if(indexSize==2) {uint16_t small;std::memcpy(&small,indices+(i+j)*2,2);value=small;}
                    else std::memcpy(&value,indices+(i+j)*4,4);
                    id=int64_t(value)+base;
                }
                if(id<0 || uint64_t(id)>=capacity){error="CPU index exceeds vertex buffer";return;}
                const uint8_t* vertex=bytes.data()+vbOffset+size_t(id)*desc.stride;
                if(source) {
                    float uv[2];std::memcpy(uv,vertex+desc.attrs[2].offset,8);
                    if(!std::isfinite(uv[0]) || !std::isfinite(uv[1])){error="non-finite CPU UV";return;}
                    tri[j].uv={uv[0],uv[1]};
                }
                for(int a=0;a<2;++a){
                    float values[4];std::memcpy(values,vertex+desc.attrs[a].offset,16);
                    for(int k=0;k<4;++k) {
                        if(!std::isfinite(values[k])){error="non-finite CPU vertex";return;}
                        (a==0?tri[j].clip:tri[j].color)[k]=values[k];
                    }
                }
            }
            clipAndRasterize(tri,desc,source);
        }
    }
    void clipAndRasterize(const std::array<Vertex,3>& tri,const PipelineDesc& p,const Surface* source) {
        // Clipping a convex polygon by one plane adds at most one vertex.
        std::array<Vertex,10> polygon{},scratch{};
        std::copy(tri.begin(),tri.end(),polygon.begin());
        int count=3;
        for(int plane=0;plane<6 && count>=3;++plane) {
            int out=0;
            auto prev=polygon[count-1];double prevD=planeDistance(prev,plane);
            for(int i=0;i<count;++i){
                auto curr=polygon[i];double d=planeDistance(curr,plane);
                if((prevD<0 && d>0)||(prevD>0 && d<0))
                    scratch[out++]=between(prev,curr,prevD/(prevD-d));
                if(d>=0)scratch[out++]=curr;
                prev=curr;prevD=d;
            }
            count=out;polygon.swap(scratch);
        }
        std::array<ScreenVertex,10> points{};
        for(int i=0;i<count;++i) {
            auto& v=polygon[i];
            if(v.clip[3]<=0)return;
            double w=1/v.clip[3];
            points[i]={viewport.x+(v.clip[0]*w+1)*viewport.w*.5,
                       viewport.y+(1-v.clip[1]*w)*viewport.h*.5,
                       (v.clip[2]*w+1)*.5,w,v.color,v.uv};
        }
        for(int i=1;i+1<count;++i)rasterize(points[0],points[i],points[i+1],p,source);
    }
    void rasterize(ScreenVertex a,ScreenVertex b,ScreenVertex c,const PipelineDesc& p,const Surface* source) {
        double area=edge(a,b,c.x,c.y);
        if(!area || (p.cull==CullMode::Back && area>0) || (p.cull==CullMode::Front && area<0))return;
        if(area<0){std::swap(b,c);area=-area;}
        auto& dst=color?*color:*depth;
        const auto bound=[](double x,int max){return int(std::clamp(x,0.0,double(max)));};
        int x0=bound(std::floor(std::min({a.x,b.x,c.x})),dst.width);
        int x1=bound(std::ceil(std::max({a.x,b.x,c.x})),dst.width);
        int y0=bound(std::floor(std::min({a.y,b.y,c.y})),dst.height);
        int y1=bound(std::ceil(std::max({a.y,b.y,c.y})),dst.height);
        if(scissor.w){
            x0=std::max(x0,scissor.x);y0=std::max(y0,scissor.y);
            x1=std::min(x1,bound(double(scissor.x)+scissor.w,dst.width));
            y1=std::min(y1,bound(double(scissor.y)+scissor.h,dst.height));
        }
        const bool include[]={topLeft(b,c),topLeft(c,a),topLeft(a,b)};
        for(int y=y0;y<y1;++y)for(int x=x0;x<x1;++x) {
            double t[]={edge(b,c,x+.5,y+.5),edge(c,a,x+.5,y+.5),edge(a,b,x+.5,y+.5)};
            if(t[0]<0 || t[1]<0 || t[2]<0 || (!t[0]&&!include[0]) ||
               (!t[1]&&!include[1]) || (!t[2]&&!include[2]))continue;
            const size_t pixel=size_t(y)*dst.width+x;
            float z=float((a.depth*t[0]+b.depth*t[1]+c.depth*t[2])/area);
            if(depth && p.depthTest && z>depth->depth[pixel])continue;
            if(depth && p.depthWrite)depth->depth[pixel]=z;
            if(!color || !p.colorWrite)continue;
            t[0]*=a.inverseW;t[1]*=b.inverseW;t[2]*=c.inverseW;
            const double sum=t[0]+t[1]+t[2];
            Vec src;
            for(int k=0;k<4;++k)
                src[k]=std::clamp((a.color[k]*t[0]+b.color[k]*t[1]+c.color[k]*t[2])/sum,0.,1.);
            if(source) {
                auto interpolate=[&](int k){return (a.uv[k]*t[0]+b.uv[k]*t[1]+c.uv[k]*t[2])/sum;};
                const auto texel=source->sample(interpolate(0),interpolate(1));
                for(int k=0;k<4;++k)src[k]*=texel[k];
            }
            uint8_t* rgba=&color->color[pixel*4];
            for(int k=0;k<4;++k){
                double v=src[k];
                if(p.blend==BlendMode::Alpha)
                    v=(k==3?v:v*src[3])+rgba[k]/255.*(1-src[3]);
                rgba[k]=unorm(v);
            }
        }
    }
};
}
std::unique_ptr<Device> createCpuDevice() {return std::make_unique<CpuDevice>();}
} // namespace aiverse::gfx
