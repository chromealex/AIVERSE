// Compiles the hierarchical wind GLSL (vertex motion + debug fragments) with
// the vendored glslang front end in the renderer's SG_VERTEX/SG_COLOR context.
#include "glslang/Public/ShaderLang.h"
#include "glslang/Public/ResourceLimits.h"
#include "engine/render/WindRigShader.h"
#include <cstdio>
static bool compile(const char* label, const std::string& src, int version, EProfile profile, EShLanguage stage = EShLangVertex) {
    glslang::TShader sh(stage);
    const char* s = src.c_str();
    sh.setStrings(&s, 1);
    const bool ok = sh.parse(GetDefaultResources(), version, profile, false, true, EShMsgDefault);
    std::printf("%s: %s\n%s", label, ok ? "OK" : "FAILED", ok ? "" : sh.getInfoLog());
    return ok;
}
int main() {
    glslang::InitializeProcess();
    const auto& g = aiverse::render::wind::windRigGraph();
    // Same host context the renderer injects (Renderer3DModels.cpp SG_VERTEX).
    const std::string body = std::string(R"(
uniform sampler2D tex13;
uniform sampler2D tex14;
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_nrm;
layout(location = 4) in vec4 a_col;
struct Inst { mat4 model; vec4 anim; };
uniform U1 { Inst u_inst[1]; };
#define I u_inst[0]
out vec3 v_out;
void main() {
  vec4 p = vec4(a_pos, 1.0); vec3 n = a_nrm;
  {
  vec2 sg_uv = vec2(0.0);
  vec3 sg_object=p.xyz, sg_world=(I.model*p).xyz;
  vec3 sg_c0=I.model[0].xyz,sg_c1=I.model[1].xyz,sg_c2=I.model[2].xyz;
  vec3 sg_normal=mat3(cross(sg_c1,sg_c2),cross(sg_c2,sg_c0),cross(sg_c0,sg_c1))*n;
  vec3 sg_view=-sg_world; vec4 sg_color=a_col; float sg_time=0.0;
#define SG_TEXTURE_LOD0(uv,lod) textureLod(tex13,uv,lod)
#define SG_TEXEL0(x,y) texelFetch(tex13,ivec2(x,y),0)
#define SG_TEXTURE_LOD1(uv,lod) textureLod(tex14,uv,lod)
#define SG_TEXEL1(x,y) texelFetch(tex14,ivec2(x,y),0)
)") + g->vertexGlsl + R"(
  p.xyz += wr_offset.xyz;
  }
  gl_Position = I.model * p; v_out = n;
}
)";
    bool ok = compile("GLSL 330 core", "#version 330 core\n" + body, 330, ECoreProfile);
    ok = compile("GLSL ES 300", "#version 300 es\nprecision highp float;\nprecision highp int;\n" + body, 300, EEsProfile) && ok;
    ok = compile("Vulkan-style GLSL 450", "#version 450\n" + body, 450, ECoreProfile) && ok;
    using aiverse::render::wind::WindDebugView;
    for (auto v : {WindDebugView::Parts, WindDebugView::Depth, WindDebugView::Weight}) {
        const auto d = aiverse::render::wind::windRigDebugGraph(v);
        const std::string frag = std::string(R"(
uniform sampler2D tex13;
uniform sampler2D tex14;
in vec3 v_opos; in vec4 v_col; in vec3 v_wpos; in vec3 v_nrm; in vec2 v_uv;
out vec4 o_color;
void main() {
  vec4 base = vec4(1.0); vec3 emis = vec3(0.0);
  {
#define SG_TEXEL0(x,y) texelFetch(tex13,ivec2(x,y),0)
#define SG_TEXEL1(x,y) texelFetch(tex14,ivec2(x,y),0)
  vec2 sg_uv=v_uv; vec4 sg_input=base;
  vec3 sg_world=v_wpos, sg_object=v_opos, sg_normal=v_nrm, sg_view=-v_wpos;
  vec4 sg_color=v_col; float sg_time=0.0;
)") + d->glsl + "\n  base=" + d->outputs.at("color") + ";\n  emis=" + d->outputs.at("emission") + ".rgb;\n  }\n  o_color=vec4(base.rgb+emis,1.0);\n}\n";
        ok = compile("debug fragment GLSL 330", "#version 330 core\n" + frag, 330, ECoreProfile, EShLangFragment) && ok;
        ok = compile("debug fragment ES 300", "#version 300 es\nprecision highp float;\nprecision highp int;\n" + frag, 300, EEsProfile, EShLangFragment) && ok;
    }
    glslang::FinalizeProcess();
    return ok ? 0 : 1;
}
