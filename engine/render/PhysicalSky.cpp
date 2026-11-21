#include "PhysicalSky.h"
#include "PhysicalSkyShaders.h"
#include "Renderer3D.h"
#include "Shaders.h"

namespace aiverse::render {
namespace {
struct Uniforms {
    mat4 invVP,lightVP;
    float4 px,world,sun,sunColor,air,layer,weather,budget,options;
};
}
void PhysicalSky::shutdown(gfx::Device& d) {
    if(pipe_.valid())d.destroyPipeline(pipe_);
    if(shader_.valid())d.destroyShader(shader_);
    pipe_={};shader_={};failed_=false;error_.clear();
}
bool PhysicalSky::draw(gfx::Device& d,const PostFrame& f,const PhysicalSkySettings& input,
                       int quality,double seconds,gfx::TextureHandle source,
                       gfx::TextureHandle depth,gfx::TextureHandle target,
                       gfx::TextureHandle scratchDepth,gfx::BufferHandle triangle,const float2* displacementMeters) {
    if(!input.enabled||quality<=0||failed_||!source.valid()||!depth.valid()||
       !target.valid()||!scratchDepth.valid()||source==target||depth==scratchDepth)return false;
    if(!pipe_.valid()) {
        const std::string fields="mat4 invVP;mat4 lightVP;vec4 px;vec4 world;vec4 sun;vec4 sunColor;vec4 air;vec4 layer;vec4 weather;vec4 budget;vec4 options;";
        const std::string uv="#define UV(p) (px.z>.5?vec2((p).x,1.0-(p).y):(p))\n";
        const std::string vs="layout(location=0) in vec2 a_pos;out vec2 v_screen;void main(){v_screen=a_pos*.5+.5;gl_Position=vec4(a_pos,0,1);}";
        const std::string fs="layout(std140) uniform U0{"+fields+"};\nuniform sampler2D tex0;uniform highp sampler2D tex1;uniform highp sampler2D tex2;\nin vec2 v_screen;out vec4 o_color;\n"
            "#define COLOR(p) texture(tex0,p)\n#define DEPTH(p) texture(tex1,p).r\n#define RETURN(c) {o_color=(c);return;}\n"
            AIVERSE_GLSL_SUN_FILTER("tex2")
            "#define SHADOW(p,z) sunCompare(p,z)\n"+uv+physicalsky::helpers+"void main(){vec2 s=v_screen;"+physicalsky::body+"}";
        const std::string msl="#include <metal_stdlib>\nusing namespace metal;\n#define mat4 float4x4\n#define vec2 float2\n#define vec3 float3\n#define vec4 float4\nstruct U0{"+fields+"};\n"
            "struct VIn{float2 pos [[attribute(0)]];};struct VOut{float4 pos [[position]];float2 screen;};\n"
            "vertex VOut vs_main(VIn in [[stage_in]]){VOut o;o.pos=float4(in.pos,0,1);o.screen=in.pos*.5+.5;return o;}\n"
            AIVERSE_MSL_SUN_FILTER
            "#define COLOR(p) tex0.sample(s0,p)\n#define DEPTH(p) tex1.sample(s1,p)\n#define SHADOW(p,z) sunCompare(tex2,p,z)\n#define RETURN(c) return (c)\n"+uv+physicalsky::helpers+
            "fragment float4 fs_main(VOut in [[stage_in]],constant U0& u [[buffer(1)]],texture2d<float> tex0 [[texture(0)]],sampler s0 [[sampler(0)]],depth2d<float> tex1 [[texture(1)]],sampler s1 [[sampler(1)]],depth2d<float> tex2 [[texture(2)]]){"
            "const mat4 invVP=u.invVP,lightVP=u.lightVP;const vec4 px=u.px,world=u.world,sun=u.sun,sunColor=u.sunColor,air=u.air,layer=u.layer,weather=u.weather,budget=u.budget,options=u.options;vec2 s=in.screen;"+physicalsky::body+"}";
        gfx::ShaderDesc desc;desc.glslVertex=vs.c_str();desc.glslFragment=fs.c_str();desc.msl=msl.c_str();
        desc.uniformSlots=1;desc.textureSlots=3;desc.label="physical sky and volumetric clouds";
        shader_=d.createShader(desc);
        if(shader_.valid()) {
            gfx::PipelineDesc p;p.shader=shader_;p.attrs[0]={0,gfx::VertexFormat::Float2,0};
            p.attrCount=1;p.stride=8;p.indexType=gfx::IndexType::None;p.cull=gfx::CullMode::None;
            p.depthTest=p.depthWrite=false;p.colorFormat=gfx::PixelFormat::RGBA16F;
            p.label="physical sky HDR";pipe_=d.createPipeline(p);
        }
        if(!pipe_.valid()){failed_=true;error_=d.lastError();return false;}
    }
    const auto s=sanitizePhysicalSky(input);const auto b=physicalSkyBudget(quality);
    const auto sun=normalize(f.lighting.sunDir);
    // Bound coordinates during long sessions; wrap only after 65536 weather cells.
    auto drift=[&](float speed){return float(std::fmod((std::isfinite(seconds)?seconds:0.0)*speed,double(s.cloudScale)*65536.0));};
    const auto offset=[&](float v){return std::isfinite(v)?-std::fmod(v,s.cloudScale*65536.f):0.f;};
    const float dx=displacementMeters?offset(displacementMeters->x):drift(s.windX);
    const float dz=displacementMeters?offset(displacementMeters->y):drift(s.windZ);
    const float intensity=std::max(f.lighting.sunIntensity,0.f);
    Uniforms u{f.viewProj.inverse(),f.lightViewProj,
        {1.f/std::max(f.width,1),1.f/std::max(f.height,1),d.info().renderTargetFlipY?1.f:0.f,d.info().clipDepthZeroToOne?1.f:2.f},
        {s.metersPerUnit,s.seaLevel,0,0},
        {sun.x,sun.y,sun.z,s.anisotropy},
        {f.lighting.sunColor.x*intensity,f.lighting.sunColor.y*intensity,f.lighting.sunColor.z*intensity,0},
        {s.rayleigh,s.mie,s.sunRadiance,0},
        {s.cloudBase,s.cloudThickness,s.cloudScale,s.clouds?s.coverage:0.f},
        {dx,dz,s.extinction,s.haze},
        {float(b.atmosphereSteps),float(b.cloudSteps),float(b.lightSteps),float(b.raySteps)},
        {s.godRays?1.f:0.f,s.rayDistance,f.sunShadow.valid()?1.f:0.f,0}};
    gfx::PassDesc pass;pass.colorTarget=target;pass.depthTarget=scratchDepth;
    pass.clearColorEnabled=pass.clearDepthEnabled=false;
    d.beginPass(pass);d.setViewport({0,0,f.width,f.height});d.setPipeline(pipe_);
    d.setUniforms(0,&u,sizeof u);d.setTexture(0,source);d.setTexture(1,depth);
    // A valid depth binding is required even when scene shadows are disabled.
    d.setTexture(2,f.sunShadow.valid()?f.sunShadow:depth);
    d.setVertexBuffer(triangle);d.draw(3);d.endPass();return true;
}
}
