// GPU side of hierarchical wind masks (WindRig.h). One shared, first-party
// material ShaderGraph program for EVERY rigged object: graph texture 0 = the
// prototype's rig table, graph texture 1 = the global mask atlas. Uses the
// renderer's vertexOffset injection and SG_TEXEL<i> exact texel fetches; no
// new vertex streams, RHI buffers or per-object shader variants.
//
// The code mirrors wind::PackedWindRig::offset() line by line; keep both in
// sync (tests/wind_rig_test.cpp checks the CPU side and the packing).
#pragma once
#include "engine/render/WindRig.h"
#include "engine/render/graph/ShaderGraph.h"
#include "engine/render/Math3D.h"
#include <memory>

namespace aiverse::render::wind {

namespace shader_detail {
// %VEC%, %VEC3%, %VEC2%, %MAT3%, %INT% are replaced per language.
inline constexpr const char* kBody = R"(
%VEC% wr_h=SG_TEXEL0(0,0);
float wr_count=floor(wr_h.x*255.0+0.5)+floor(wr_h.y*255.0+0.5)*256.0;
float wr_unit=exp2(floor(wr_h.z*255.0+0.5)-20.0);
float wr_node=floor(sg_color.x*255.0+0.5)+floor(sg_color.y*255.0+0.5)*256.0;
float wr_flutter=sg_color.z;
%VEC3% wr_W=%VEC3%(I.anim.x,0.0,I.anim.y);
float wr_strength=length(wr_W);
%VEC3% wr_q=sg_object;
%VEC3% wr_off=%VEC3%(0.0,0.0,0.0);
for(int wr_l=0;wr_l<4;++wr_l){
  if(wr_node>=wr_count)break;
  int wr_b=1+int(wr_node)*4;
  %VEC% wr_n0=SG_TEXEL0(wr_b%64,wr_b/64);
  %VEC% wr_n1=SG_TEXEL0((wr_b+1)%64,(wr_b+1)/64);
  %VEC% wr_n2=SG_TEXEL0((wr_b+2)%64,(wr_b+2)/64);
  %VEC% wr_n3=SG_TEXEL0((wr_b+3)%64,(wr_b+3)/64);
  %VEC% wr_b0=floor(wr_n0*255.0+0.5);
  %VEC% wr_b1=floor(wr_n1*255.0+0.5);
  %VEC% wr_b2=floor(wr_n2*255.0+0.5);
  float wr_px=wr_b0.x+wr_b0.y*256.0; wr_px-=wr_px>=32768.0?65536.0:0.0;
  float wr_py=wr_b0.z+wr_b0.w*256.0; wr_py-=wr_py>=32768.0?65536.0:0.0;
  float wr_pz=wr_b1.x+wr_b1.y*256.0; wr_pz-=wr_pz>=32768.0?65536.0:0.0;
  %VEC3% wr_pivot=%VEC3%(wr_px,wr_py,wr_pz)*wr_unit;
  float wr_len=max((wr_b1.z+wr_b1.w*256.0)*wr_unit,0.01);
  %VEC2% wr_o=wr_n2.xy*2.0-1.0;
  %VEC3% wr_axis=%VEC3%(wr_o.x,1.0-abs(wr_o.x)-abs(wr_o.y),wr_o.y);
  if(wr_axis.y<0.0){
    %VEC2% wr_s=%VEC2%(wr_axis.x>=0.0?1.0:-1.0,wr_axis.z>=0.0?1.0:-1.0);
    wr_axis.xz=(1.0-abs(wr_axis.zx))*wr_s;
  }
  wr_axis=normalize(wr_axis);
  float wr_parent=wr_b2.z+wr_b2.w*256.0;
  int wr_mask=int(floor(wr_n3.x*255.0+0.5));
  float wr_stiff=wr_n3.y, wr_phase0=wr_n3.z, wr_flex=wr_n3.w*2.0;
  float wr_t=clamp(dot(wr_q-wr_pivot,wr_axis)/wr_len,0.0,1.0);
  float wr_f=wr_t*31.0;
  int wr_i0=int(floor(wr_f)); int wr_i1=min(wr_i0+1,31);
  int wr_a0=wr_mask*8+wr_i0/4; int wr_a1=wr_mask*8+wr_i1/4;
  %VEC% wr_m0=SG_TEXEL1(wr_a0%64,wr_a0/64);
  %VEC% wr_m1=SG_TEXEL1(wr_a1%64,wr_a1/64);
  float wr_m=mix(wr_m0[wr_i0&3],wr_m1[wr_i1&3],wr_f-float(wr_i0));
  %VEC3% wr_perp=wr_W-wr_axis*dot(wr_W,wr_axis);
  float wr_ph=I.anim.z*(0.6+1.4*wr_stiff)+wr_phase0*6.2831853;
  float wr_wave=0.62+0.38*sin(wr_ph)+I.anim.w*0.25*sin(wr_ph*2.13+1.7);
  %VEC3% wr_d=wr_perp*(wr_m*wr_flex*wr_wave);
  float wr_side=1.0-abs(wr_axis.y);
  wr_d.y+=wr_strength*wr_m*wr_flex*0.25*wr_side*sin(wr_ph*1.31+0.9);
  wr_d-=wr_axis*(dot(wr_d,wr_d)*0.5/wr_len);
  wr_off+=wr_d;
  wr_q=wr_pivot;
  wr_node=wr_parent;
}
%VEC3% wr_on=normalize(transpose(%MAT3%(sg_c0,sg_c1,sg_c2))*sg_normal+%VEC3%(0.0,1e-6,0.0));
float wr_fl=wr_flutter*wr_strength*0.08*sin(I.anim.z*6.7+dot(sg_object,%VEC3%(3.1,1.7,2.3)));
%VEC% wr_offset=%VEC%(wr_off+wr_on*wr_fl,0.0);
)";

inline std::string specialize(bool metal) {
    std::string s = kBody;
    const auto rep = [&](const std::string& from, const std::string& to) {
        for (size_t at = 0; (at = s.find(from, at)) != std::string::npos; at += to.size())
            s.replace(at, from.size(), to);
    };
    rep("%VEC3%", metal ? "float3" : "vec3");
    rep("%VEC2%", metal ? "float2" : "vec2");
    rep("%MAT3%", metal ? "float3x3" : "mat3");
    rep("%VEC%", metal ? "float4" : "vec4");
    return s;
}
} // namespace shader_detail

// Shared program for all rigged materials. Fragment colour is unchanged
// (sg_input); vertex colours carry wind data, so set vertexColors=false.
inline const std::shared_ptr<const graph::Program>& windRigGraph() {
    static const auto program = [] {
        auto p = std::make_shared<graph::Program>();
        p->domain = "material";
        p->minQuality = 0;
        p->textures = {"aiverse:wind-rig", "aiverse:wind-masks"};
        p->outputs["color"] = "sg_input";
        p->outputs["vertexOffset"] = "wr_offset";
        p->vertexGlsl = shader_detail::specialize(false);
        p->vertexMsl = shader_detail::specialize(true);
        return std::shared_ptr<const graph::Program>(std::move(p));
    }();
    return program;
}

// ---------------------------------------------------------- debug views
// Same vertex motion, fragment colour shows the rig. Selected globally with
// Renderer3D::setWindRigDebugView (editors); never used by games.
enum class WindDebugView : uint8_t { Off = 0, Parts = 1, Depth = 2, Weight = 3, Skeleton = 4 };

namespace shader_detail {
inline constexpr const char* kDebugBody = R"(
%VEC% wd_h=SG_TEXEL0(0,0);
float wd_count=floor(wd_h.x*255.0+0.5)+floor(wd_h.y*255.0+0.5)*256.0;
float wd_unit=exp2(floor(wd_h.z*255.0+0.5)-20.0);
float wd_node=floor(sg_color.x*255.0+0.5)+floor(sg_color.y*255.0+0.5)*256.0;
float wd_first=wd_node;
float wd_depth=0.0, wd_weight=0.0, wd_own=0.0;
%VEC3% wd_q=sg_object;
for(int wd_l=0;wd_l<4;++wd_l){
  if(wd_node>=wd_count)break;
  int wd_b=1+int(wd_node)*4;
  %VEC% wd_b0=floor(SG_TEXEL0(wd_b%64,wd_b/64)*255.0+0.5);
  %VEC% wd_b1=floor(SG_TEXEL0((wd_b+1)%64,(wd_b+1)/64)*255.0+0.5);
  %VEC% wd_n2=SG_TEXEL0((wd_b+2)%64,(wd_b+2)/64);
  %VEC% wd_n3=SG_TEXEL0((wd_b+3)%64,(wd_b+3)/64);
  float wd_px=wd_b0.x+wd_b0.y*256.0; wd_px-=wd_px>=32768.0?65536.0:0.0;
  float wd_py=wd_b0.z+wd_b0.w*256.0; wd_py-=wd_py>=32768.0?65536.0:0.0;
  float wd_pz=wd_b1.x+wd_b1.y*256.0; wd_pz-=wd_pz>=32768.0?65536.0:0.0;
  %VEC3% wd_pivot=%VEC3%(wd_px,wd_py,wd_pz)*wd_unit;
  float wd_len=max((wd_b1.z+wd_b1.w*256.0)*wd_unit,0.01);
  %VEC2% wd_o=wd_n2.xy*2.0-1.0;
  %VEC3% wd_axis=%VEC3%(wd_o.x,1.0-abs(wd_o.x)-abs(wd_o.y),wd_o.y);
  if(wd_axis.y<0.0){
    %VEC2% wd_s=%VEC2%(wd_axis.x>=0.0?1.0:-1.0,wd_axis.z>=0.0?1.0:-1.0);
    wd_axis.xz=(1.0-abs(wd_axis.zx))*wd_s;
  }
  wd_axis=normalize(wd_axis);
  float wd_parent=floor(wd_n2.z*255.0+0.5)+floor(wd_n2.w*255.0+0.5)*256.0;
  int wd_mask=int(floor(wd_n3.x*255.0+0.5));
  float wd_t=clamp(dot(wd_q-wd_pivot,wd_axis)/wd_len,0.0,1.0);
  float wd_f=wd_t*31.0;
  int wd_i0=int(floor(wd_f)); int wd_i1=min(wd_i0+1,31);
  int wd_a0=wd_mask*8+wd_i0/4; int wd_a1=wd_mask*8+wd_i1/4;
  float wd_m=mix(SG_TEXEL1(wd_a0%64,wd_a0/64)[wd_i0&3],SG_TEXEL1(wd_a1%64,wd_a1/64)[wd_i1&3],wd_f-float(wd_i0));
  if(wd_l==0)wd_own=wd_m;
  wd_weight+=wd_m*wd_n3.w;
  wd_depth+=1.0;
  wd_q=wd_pivot;
  wd_node=wd_parent;
}
%VEC3% wd_c=%VEC3%(0.35,0.35,0.35);
%MODE%
%VEC% wd_color=%VEC%(wd_c,1.0);
%VEC% wd_glow=%VEC%(wd_c*0.6,1.0);
)";

inline constexpr const char* kModeParts = R"(
if(wd_depth>0.0){
  %VEC3% wd_hue=%VEC3%(0.0,0.33,0.67)+fract(wd_first*0.61803398);
  wd_c=(0.5+0.5*cos(6.2831853*wd_hue))*(0.45+0.55*wd_own);
})";
inline constexpr const char* kModeDepth = R"(
%VEC3% wd_lv=wd_depth<0.5?%VEC3%(0.35,0.35,0.35):wd_depth<1.5?%VEC3%(0.62,0.38,0.18):
  wd_depth<2.5?%VEC3%(0.95,0.78,0.15):wd_depth<3.5?%VEC3%(0.2,0.85,0.35):%VEC3%(0.2,0.75,1.0);
wd_c=wd_lv*(0.35+0.65*wd_own);)";
inline constexpr const char* kModeWeight = R"(
float wd_k=clamp(wd_weight,0.0,1.0);
wd_c=wd_k<0.5?mix(%VEC3%(0.05,0.15,0.6),%VEC3%(0.1,0.85,0.3),wd_k*2.0):mix(%VEC3%(0.1,0.85,0.3),%VEC3%(1.0,0.2,0.05),wd_k*2.0-1.0);
if(wd_depth<0.5)wd_c=%VEC3%(0.3,0.3,0.3);)";

inline std::string specializeDebug(bool metal, WindDebugView view) {
    std::string mode = view == WindDebugView::Parts ? kModeParts
                     : view == WindDebugView::Depth ? kModeDepth : kModeWeight;
    std::string s = kDebugBody;
    s.replace(s.find("%MODE%"), 6, mode);
    const auto rep = [&](const std::string& from, const std::string& to) {
        for (size_t at = 0; (at = s.find(from, at)) != std::string::npos; at += to.size())
            s.replace(at, from.size(), to);
    };
    rep("%VEC3%", metal ? "float3" : "vec3");
    rep("%VEC2%", metal ? "float2" : "vec2");
    rep("%VEC%", metal ? "float4" : "vec4");
    return s;
}
} // namespace shader_detail

// Shared debug programs (Parts / Depth / Weight). Skeleton is drawn as lines
// by the host (windRigDebugLines) over the normal material.
inline std::shared_ptr<const graph::Program> windRigDebugGraph(WindDebugView view) {
    if (view != WindDebugView::Parts && view != WindDebugView::Depth && view != WindDebugView::Weight)
        return nullptr;
    static std::shared_ptr<const graph::Program> programs[3];
    auto& slot = programs[int(view) - 1];
    if (!slot) {
        auto p = std::make_shared<graph::Program>(*windRigGraph());
        p->glsl = shader_detail::specializeDebug(false, view);
        p->msl = shader_detail::specializeDebug(true, view);
        p->outputs["color"] = "wd_color";
        p->outputs["emission"] = "wd_glow";
        slot = std::move(p);
    }
    return slot;
}

// Scene-wide wind for every rigged draw that does not set its own vertex
// parameters (Renderer3D::setSceneWind). Wind is world m/s; the result is the
// object-local displacement the shader expects (I.anim convention).
struct SceneWind {
    float x = 0, z = 0;              // world wind, m/s
    float seconds = 0;               // presentation time
    float gust = 0;                  // 0..1
    float metresPerMps = 0.035f;     // displacement at mask 1, flex 1, per m/s
    bool enabled() const { return std::isfinite(x) && std::isfinite(z) && (x != 0 || z != 0); }
};

inline WindDrawParams windRigParamsForDraw(const SceneWind& w, const render::mat4& world) {
    WindDrawParams p;
    if (!w.enabled()) return p;
    // d_obj = M3^-1 * d_world; for rotation * uniform scale: R^T / s.
    const render::float3 c0{world.at(0, 0), world.at(1, 0), world.at(2, 0)};
    const render::float3 c2{world.at(0, 2), world.at(1, 2), world.at(2, 2)};
    const render::float3 c1{world.at(0, 1), world.at(1, 1), world.at(2, 1)};
    const float dx = w.x * w.metresPerMps, dz = w.z * w.metresPerMps;
    const auto local = [&](render::float3 c) {
        const float l2 = c.x * c.x + c.y * c.y + c.z * c.z;
        return l2 > 1e-12f ? (c.x * dx + c.z * dz) / l2 : 0.f;
    };
    p.windX = local(c0);
    p.windZ = local(c2);
    (void)c1;
    const float speed = std::sqrt(w.x * w.x + w.z * w.z);
    p.phase = float(std::fmod(double(w.seconds) * (1.2 + 0.05 * speed), 6.283185307179586 * 64.0));
    p.gust = std::clamp(w.gust, 0.f, 1.f);
    return p;
}

// Conservative per-draw bound for culling (ModelDraw::maxVertexOffsetMetres):
// |wind| * max flex (2) * wave (1.25) per level, 4 levels, + flutter.
inline float windRigMaxOffsetMetres(const WindDrawParams& w) {
    const float s = std::sqrt(w.windX * w.windX + w.windZ * w.windZ);
    return s * (2.f * 1.25f * 1.25f * float(kMaxLevels) + 0.08f) + 0.01f;
}

inline render::float4 windRigVertexParameters(const WindDrawParams& w) {
    return {w.windX, w.windZ, w.phase, w.gust};
}

} // namespace aiverse::render::wind
