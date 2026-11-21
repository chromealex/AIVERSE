#include "LocalFog.h"
#include "LocalFogShaders.h"
#include "Renderer3D.h"
#include "Shaders.h"
namespace aiverse::render {
namespace {
bool finite(float3 v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
bool validate(FogRegion& r){
    if((r.shape!=FogShape::Sphere&&r.shape!=FogShape::Obb)||!finite(r.center)||
       std::max({std::abs(r.center.x),std::abs(r.center.y),std::abs(r.center.z)})>1000000.f||
       !std::isfinite(r.feather)||r.feather<0||r.feather>50000)return false;
    if(r.shape==FogShape::Sphere){if(!std::isfinite(r.radius)||r.radius<=0||r.radius>50000)return false;r.orientation={};r.halfExtents={r.radius,r.radius,r.radius};}
    else {if(!finite(r.halfExtents)||std::min({r.halfExtents.x,r.halfExtents.y,r.halfExtents.z})<=0||std::max({r.halfExtents.x,r.halfExtents.y,r.halfExtents.z})>50000)return false;
        const auto q=r.orientation;const double n=double(q.x)*q.x+double(q.y)*q.y+double(q.z)*q.z+double(q.w)*q.w;
        if(!std::isfinite(n)||n<1e-12)return false;
        const float inv=float(1/std::sqrt(n));r.orientation={q.x*inv,q.y*inv,q.z*inv,q.w*inv};}
    return true;
}
bool validStructure(const FogStructure& s){
    return std::isfinite(s.amount)&&s.amount>=0&&s.amount<=1&&finite(s.scale)&&
        std::min({s.scale.x,s.scale.y,s.scale.z})>=.1f&&std::max({s.scale.x,s.scale.y,s.scale.z})<=50000&&
        std::isfinite(s.coverage)&&s.coverage>=0&&s.coverage<=1&&std::isfinite(s.softness)&&s.softness>=.01f&&s.softness<=1&&
        std::isfinite(s.heightBase)&&std::abs(s.heightBase)<=1000000&&std::isfinite(s.heightFalloff)&&s.heightFalloff>=0&&s.heightFalloff<=100&&
        std::isfinite(s.heightVariation)&&s.heightVariation>=0&&s.heightVariation<=50000&&
        std::isfinite(s.warp)&&s.warp>=0&&s.warp<=2&&std::isfinite(s.edgeErosion)&&s.edgeErosion>=0&&s.edgeErosion<=1&&finite(s.wind)&&std::max({std::abs(s.wind.x),std::abs(s.wind.y),std::abs(s.wind.z)})<=1000;
}
float phase(float wind,float scale,double seconds){
    // Periodic 256-cell noise lets the clock wrap without a phase seam or overflow.
    if(!std::isfinite(seconds)||wind==0)return 0;
    return float(-std::fmod(seconds,256.0*scale/std::abs(wind))*wind/scale);
}
float noiseHash(int x,int y,int z){
    uint32_t h=(uint32_t(x)&255u)*1597334677u+(uint32_t(y)&255u)*3812015801u+(uint32_t(z)&255u)*2798796415u;
    h=(h^(h>>16u))*2246822519u;h=h^(h>>13u);return float(h&65535u)/65535.f;
}
float noise(float3 p){
    const int x=int(std::floor(p.x)),y=int(std::floor(p.y)),z=int(std::floor(p.z));
    auto smooth=[](float v){return v*v*(3-2*v);};
    const float fx=smooth(p.x-x),fy=smooth(p.y-y),fz=smooth(p.z-z);
    auto mix=[](float a,float b,float t){return a+(b-a)*t;};
    return mix(mix(mix(noiseHash(x,y,z),noiseHash(x+1,y,z),fx),mix(noiseHash(x,y+1,z),noiseHash(x+1,y+1,z),fx),fy),
        mix(mix(noiseHash(x,y,z+1),noiseHash(x+1,y,z+1),fx),mix(noiseHash(x,y+1,z+1),noiseHash(x+1,y+1,z+1),fx),fy),fz);
}
float structureWeight(const FogStructure& s,float3 p,double seconds,float boundary){
    float3 q{p.x/s.scale.x+phase(s.wind.x,s.scale.x,seconds),p.y/s.scale.y+phase(s.wind.y,s.scale.y,seconds),p.z/s.scale.z+phase(s.wind.z,s.scale.z,seconds)};
    const float crest=s.heightVariation>0?s.heightVariation*(2*noise({q.x,19,q.z})-1):0;
    const float height=std::exp(-std::max(p.y-s.heightBase-crest,0.f)*s.heightFalloff);
    if(s.amount==0)return height;
    if(s.warp>0)q=q+float3{noise(q+float3{5,19,37})-.5f,noise(q+float3{31,7,13})-.5f,noise(q+float3{17,43,3})-.5f}*s.warp;
    const float n=.57f*noise(q)+.28f*noise(q*2.f+float3{17,31,7})+.15f*noise(q*4.f+float3{43,11,29});
    const float t=std::clamp((n-(1-s.coverage)-(1-boundary)*s.edgeErosion)/s.softness,0.f,1.f);
    return height*(1-s.amount+s.amount*t*t*(3-2*t));
}
template<class T>bool put(std::array<T,8>& entries,T v){
    for(auto& e:entries)if(e.id==v.id){e=v;return true;}
    for(auto& e:entries)if(!e.id){e=v;return true;}return false;
}
template<class T>bool erase(std::array<T,8>& entries,uint32_t id){
    if(!id)return false;for(auto& e:entries)if(e.id==id){e={};return true;}return false;
}
struct Uniforms {mat4 invVP,lightVP;float4 px,sun,sunColor,params;
    float4 centers[16],axesX[16],axesY[16],axesZ[16],material[16],edge[16];
    float4 noiseScale[8],noiseShape[8],noiseOffset[8],noiseDetail[8];float4 lighting;};
}
bool LocalFogScene::upsert(FogVolume v){
    return v.id&&validate(v.region)&&validStructure(v.structure)&&std::isfinite(v.density)&&v.density>=0&&v.density<=100&&
        finite(v.color)&&std::min({v.color.x,v.color.y,v.color.z})>=0&&std::max({v.color.x,v.color.y,v.color.z})<=1&&put(volumes_,v);
}
bool LocalFogScene::upsert(FogReveal v){
    return v.id&&validate(v.region)&&std::isfinite(v.strength)&&v.strength>=0&&v.strength<=1&&put(reveals_,v);
}
bool LocalFogScene::removeVolume(uint32_t id){return erase(volumes_,id);}
bool LocalFogScene::removeReveal(uint32_t id){return erase(reveals_,id);}
float fogRegionWeight(const FogRegion& region,float3 point){
    FogRegion r=region;if(!validate(r)||!finite(point))return 0;
    const auto p=point-r.center;float sdf=length(p)-r.radius;
    if(r.shape==FogShape::Obb){const auto q=r.orientation;
        const float3 d{std::abs(dot(p,q.rotate({1,0,0})))-r.halfExtents.x,std::abs(dot(p,q.rotate({0,1,0})))-r.halfExtents.y,std::abs(dot(p,q.rotate({0,0,1})))-r.halfExtents.z};
        sdf=length(float3{std::max(d.x,0.f),std::max(d.y,0.f),std::max(d.z,0.f)})+std::min(std::max({d.x,d.y,d.z}),0.f);}
    if(r.feather==0)return sdf<=0?1.f:0.f;
    const float t=std::clamp((sdf+r.feather)/r.feather,0.f,1.f);return 1-t*t*(3-2*t);
}
float LocalFogScene::densityAt(float3 p,double seconds)const{
    float density=0,reveal=0;for(const auto& v:volumes_)if(v.id){const float w=fogRegionWeight(v.region,p);if(w>0)density+=v.density*w*structureWeight(v.structure,p,seconds,w);}
    for(const auto& r:reveals_)if(r.id)reveal=std::max(reveal,r.strength*fogRegionWeight(r.region,p));
    return density*(1-reveal);
}
void LocalFog::shutdown(gfx::Device& d){
    if(pipe_.valid())d.destroyPipeline(pipe_);if(shader_.valid())d.destroyShader(shader_);
    pipe_={};shader_={};failed_=false;error_.clear();
}
bool LocalFog::draw(gfx::Device& d,const PostFrame& f,const LocalFogSettings& settings,const LocalFogScene& scene,int quality,
    gfx::TextureHandle source,gfx::TextureHandle depth,gfx::TextureHandle target,gfx::TextureHandle scratchDepth,gfx::BufferHandle triangle,double seconds){
    if(!settings.enabled||quality<2||failed_||!source.valid()||!depth.valid()||!target.valid()||!scratchDepth.valid()||source==target||depth==scratchDepth)return false;
    bool any=false;for(const auto& v:scene.volumes())any|=v.id&&v.density>0;if(!any)return false;
    if(!pipe_.valid()) {
        const std::string fields="mat4 invVP;mat4 lightVP;vec4 px;vec4 sun;vec4 sunColor;vec4 params;vec4 centers[16];vec4 axesX[16];vec4 axesY[16];vec4 axesZ[16];vec4 material[16];vec4 edge[16];vec4 noiseScale[8];vec4 noiseShape[8];vec4 noiseOffset[8];vec4 noiseDetail[8];vec4 lighting;";
        const std::string uv="#define UV(p) (px.z>.5?vec2((p).x,1.0-(p).y):(p))\n";
        const std::string vs="layout(location=0) in vec2 a_pos;out vec2 v_screen;void main(){v_screen=a_pos*.5+.5;gl_Position=vec4(a_pos,0,1);}";
        const std::string fs="layout(std140) uniform U0{"+fields+"};\nuniform sampler2D tex0;uniform highp sampler2D tex1;uniform highp sampler2D tex2;\nin vec2 v_screen;out vec4 o_color;\n"
            "#define COLOR(p) texture(tex0,p)\n#define DEPTH(p) texture(tex1,p).r\n#define RETURN(c) {o_color=(c);return;}\n"
            AIVERSE_GLSL_SUN_FILTER("tex2")
            "#define SHADOW(p,z) sunCompare(p,z)\n"+uv+localfog::helpers+"void main(){vec2 s=v_screen;"+localfog::body+"}";
        const std::string msl="#include <metal_stdlib>\nusing namespace metal;\n#define mat4 float4x4\n#define vec2 float2\n#define vec3 float3\n#define vec4 float4\nstruct U0{"+fields+"};\n"
            "struct VIn{float2 pos [[attribute(0)]];};struct VOut{float4 pos [[position]];float2 screen;};\n"
            "vertex VOut vs_main(VIn in [[stage_in]]){VOut o;o.pos=float4(in.pos,0,1);o.screen=in.pos*.5+.5;return o;}\n"
            AIVERSE_MSL_SUN_FILTER
            "#define COLOR(p) tex0.sample(s0,p)\n#define DEPTH(p) tex1.sample(s1,p)\n#define SHADOW(p,z) sunCompare(tex2,p,z)\n#define RETURN(c) return (c)\n"+uv+localfog::helpers+
            "fragment float4 fs_main(VOut in [[stage_in]],constant U0& u [[buffer(1)]],texture2d<float> tex0 [[texture(0)]],sampler s0 [[sampler(0)]],depth2d<float> tex1 [[texture(1)]],sampler s1 [[sampler(1)]],depth2d<float> tex2 [[texture(2)]]){"
            "const mat4 invVP=u.invVP,lightVP=u.lightVP;const vec4 px=u.px,sun=u.sun,sunColor=u.sunColor,params=u.params,lighting=u.lighting;constant vec4 *centers=u.centers,*axesX=u.axesX,*axesY=u.axesY,*axesZ=u.axesZ,*material=u.material,*edge=u.edge,*noiseScale=u.noiseScale,*noiseShape=u.noiseShape,*noiseOffset=u.noiseOffset,*noiseDetail=u.noiseDetail;vec2 s=in.screen;"+localfog::body+"}";
        gfx::ShaderDesc desc;desc.glslVertex=vs.c_str();desc.glslFragment=fs.c_str();desc.msl=msl.c_str();
        desc.uniformSlots=1;desc.textureSlots=3;desc.label="local volumetric fog";
        shader_=d.createShader(desc);
        if(shader_.valid()) {
            gfx::PipelineDesc p;p.shader=shader_;p.attrs[0]={0,gfx::VertexFormat::Float2,0};
            p.attrCount=1;p.stride=8;p.indexType=gfx::IndexType::None;p.cull=gfx::CullMode::None;
            p.depthTest=p.depthWrite=false;p.colorFormat=gfx::PixelFormat::RGBA16F;
            p.label="local fog HDR";pipe_=d.createPipeline(p);
        }
        if(!pipe_.valid()){failed_=true;error_=d.lastError();return false;}
    }
    auto bounded=[](float v,float fallback,float lo,float hi){return std::isfinite(v)?std::clamp(v,lo,hi):fallback;};
    Uniforms u{};u.invVP=f.viewProj.inverse();u.lightVP=f.lightViewProj;
    u.px={1.f/std::max(f.width,1),1.f/std::max(f.height,1),d.info().renderTargetFlipY?1.f:0.f,d.info().clipDepthZeroToOne?1.f:2.f};
    const auto sun=normalize(f.lighting.sunDir);u.sun={sun.x,sun.y,sun.z,bounded(settings.anisotropy,.3f,-.9f,.9f)};
    const float intensity=std::max(f.lighting.sunIntensity,0.f);u.sunColor={f.lighting.sunColor.x*intensity,f.lighting.sunColor.y*intensity,f.lighting.sunColor.z*intensity,0};
    u.params={bounded(settings.maxDistance,1000,1,50000),bounded(settings.ambient,.5f,0,10),f.sunShadow.valid()?1.f:0.f,quality==2?32.f:64.f};
    for(const auto& v:scene.volumes())if(v.id&&(v.structure.amount>0||v.structure.heightFalloff>0)){u.params.w=quality==2?48.f:96.f;break;}
    u.lighting={bounded(settings.selfShadow,0,0,1),bounded(settings.shadowDistance,30,.1f,1000),quality==2?4.f:6.f,0};
    auto region=[&](int i,const FogRegion& r){const auto q=r.orientation;
        const auto x=q.rotate({1,0,0}),y=q.rotate({0,1,0}),z=q.rotate({0,0,1});
        u.centers[i]={r.center.x,r.center.y,r.center.z,r.shape==FogShape::Obb?1.f:0.f};
        u.axesX[i]={x.x,x.y,x.z,r.shape==FogShape::Sphere?r.radius:r.halfExtents.x};
        u.axesY[i]={y.x,y.y,y.z,r.halfExtents.y};u.axesZ[i]={z.x,z.y,z.z,r.halfExtents.z};u.edge[i]={r.feather,0,0,0};};
    for(int i=0;i<8;++i){const auto& v=scene.volumes()[i];if(v.id){region(i,v.region);u.material[i]={v.color.x,v.color.y,v.color.z,v.density};
            const auto& n=v.structure;u.noiseDetail[i]={n.heightVariation,n.warp,n.edgeErosion,0};u.noiseScale[i]={n.scale.x,n.scale.y,n.scale.z,n.amount};
            u.noiseShape[i]={n.coverage,n.softness,n.heightBase,n.heightFalloff};
            u.noiseOffset[i]={phase(n.wind.x,n.scale.x,seconds),phase(n.wind.y,n.scale.y,seconds),phase(n.wind.z,n.scale.z,seconds),0};}
        const auto& r=scene.reveals()[i];if(r.id){region(i+8,r.region);u.material[i+8]={0,0,0,r.strength};}}
    gfx::PassDesc pass;pass.colorTarget=target;pass.depthTarget=scratchDepth;pass.clearColorEnabled=pass.clearDepthEnabled=false;
    d.beginPass(pass);d.setViewport({0,0,f.width,f.height});d.setPipeline(pipe_);d.setUniforms(0,&u,sizeof u);
    d.setTexture(0,source);d.setTexture(1,depth);d.setTexture(2,f.sunShadow.valid()?f.sunShadow:depth);
    d.setVertexBuffer(triangle);d.draw(3);d.endPass();return true;
}
}
