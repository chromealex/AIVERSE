#include "engine/gfx/Gfx.h"
#include <cstdio>
#include <cstdlib>
using namespace aiverse::gfx;
namespace {
void check(bool value,const char* what) {
    if(!value){std::fprintf(stderr,"CPU rendering check failed: %s\n",what);std::exit(1);}
}
struct Vertex {float clip[4];float rgba[4];};
}
int main() {
    auto device=createCpuDevice();
    check(device->info().backend==Backend::Cpu,"factory");
    ShaderDesc shader;
    check(!device->createShader(shader).valid(),"unsupported shader");
    shader.cpu=CpuShader::ClipColor;
    PipelineDesc p;p.shader=device->createShader(shader);p.stride=sizeof(Vertex);p.attrCount=2;
    p.attrs[0]={0,VertexFormat::Float4,0};p.attrs[1]={1,VertexFormat::Float4,16};
    auto pipe=device->createPipeline(p);
    check(pipe.valid(),"triangle pipeline");
    const Vertex vertices[]={
        {{-2,-1,0,1},{1,0,0,1}},{{1,-1,0,1},{1,0,0,1}},{{0,1,0,1},{1,0,0,1}},
        {{-2,-1,.5f,1},{0,1,0,1}},{{1,-1,.5f,1},{0,1,0,1}},{{0,1,.5f,1},{0,1,0,1}}};
    auto vb=device->createBuffer({BufferType::Vertex,Usage::Immutable,sizeof(vertices),vertices});
    uint16_t indices[]={0,1,2};
    auto ib=device->createBuffer({BufferType::Index,Usage::Immutable,sizeof(indices),indices});
    TextureDesc td;td.width=16;td.height=16;td.renderTarget=true;
    auto color=device->createTexture(td);
    td.format=PixelFormat::Depth;
    auto depth=device->createTexture(td);
    device->beginFrame(16,16);
    auto render=[&](TextureHandle c, TextureHandle z){
        PassDesc pass;pass.colorTarget=c;pass.depthTarget=z;
        pass.clearColor[0]=pass.clearColor[1]=pass.clearColor[2]=0;
        device->beginPass(pass);device->setPipeline(pipe);device->setVertexBuffer(vb);
        device->setIndexBuffer(ib);device->setScissor({2,0,12,16});
        device->drawIndexed(3);device->drawIndexed(3,0,3);device->endPass();
    };
    bool offscreen=false,frame=false;
    auto verify=[](int w,int h,const std::vector<uint8_t>& rgba){
        check(w==16 && h==16 && rgba.size()==1024,"dimensions");
        auto at=[&](int x,int y,int ch){return rgba[size_t(y*16+x)*4+ch];};
        check(at(8,8,0)==255 && at(8,8,1)==0,"depth occlusion");
        check(at(2,12,0)==255 && !at(1,12,0),"clipping and scissor");
        check(!at(13,0,0),"background");
    };
    render(color,depth);
    check(device->captureTexture(color,[&](auto... a){verify(a...);offscreen=true;}),"capture texture");
    render({},{});
    device->captureNextFrame([&](auto... a){verify(a...);frame=true;});
    device->endFrame();check(offscreen && frame,"callbacks");
    struct Textured {float clip[4],color[4],uv[2];};
    const Textured quad[]={
        {{-1,-1,0,1},{1,1,1,1},{0,0}},{{3,-1,0,1},{1,1,1,1},{2,0}},
        {{-1,3,0,1},{1,1,1,1},{0,2}}};
    const uint8_t texels[]={255,0,0,255,0,255,0,255};
    TextureDesc image;image.width=2;image.height=1;image.data=texels;image.filter=Filter::Nearest;
    auto sampled=device->createTexture(image);
    shader.cpu=CpuShader::ClipColorTexture;shader.textureSlots=1;p.shader=device->createShader(shader);
    p.attrCount=3;p.stride=sizeof(Textured);p.attrs[2]={2,VertexFormat::Float2,32};
    auto tp=device->createPipeline(p);
    auto tv=device->createBuffer({BufferType::Vertex,Usage::Immutable,sizeof(quad),quad});
    check(tp.valid() && sampled.valid(),"textured pipeline");
    device->beginFrame(4,1);device->beginPass({});device->setPipeline(tp);device->setVertexBuffer(tv);
    device->setTexture(0,sampled);device->draw(3);device->endPass();
    bool textured=false;
    device->captureNextFrame([&](int,int,std::vector<uint8_t> rgba){
        check(rgba.size()==16 && rgba[0]==255 && rgba[4]==255 && rgba[9]==255 && rgba[13]==255,
              "clipped UV interpolation and nearest sampling");
        textured=true;
    });
    device->endFrame();check(textured,"textured frame captured");
}
