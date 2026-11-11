// Renderer3D: models (PBR materials, GPU skinning, alpha modes) — shader permutations, culling,
// sorting and the per-draw uniform blocks. See ModelShaders.h for the uniform layouts.
#include "Renderer3D.h"
#include "ModelShaders.h"
#include "Animation.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace aiverse::render {

namespace {
struct MaterialUniforms { float base[4]; float emissive[4]; float pbr[4]; float misc[4]; float uv0[4]; float uv1[4]; float light[4]; };
constexpr size_t kJointFloats = size_t(kMaxPaletteJoints) * 12; // 3 rows of vec4 per joint
static_assert(kJointFloats * 4 <= 4096, "joint palette must fit a 4 KB uniform block");

constexpr uint32_t kKeySkinned = 1, kKeyNormalMap = 2, kKeyAlphaMask = 4, kKeyShadow = 8, kKeyBaked = 64, kKeyMorph = 128,
                   kKeyInstanced = 256, kKeyOutline = 512;
constexpr int kMaxInstances = 32; // 128-byte instance records in a 4 KB uniform block

bool transparentItem(uint32_t flags) { return (flags & kDrawTransparent) != 0; }

// AABB (in `xf` space) against a clip-space frustum: false only when all corners are outside one plane.
bool boxVisible(const mat4& clip, float3 mn, float3 mx) {
    int out[6] = {0, 0, 0, 0, 0, 0};
    for (int c = 0; c < 8; ++c) {
        float4 p = clip * float4{c & 1 ? mx.x : mn.x, c & 2 ? mx.y : mn.y, c & 4 ? mx.z : mn.z, 1};
        out[0] += p.x < -p.w; out[1] += p.x > p.w;
        out[2] += p.y < -p.w; out[3] += p.y > p.w;
        out[4] += p.z < -p.w; out[5] += p.z > p.w;
    }
    for (int i = 0; i < 6; ++i) if (out[i] == 8) return false;
    return true;
}
} // namespace

MaterialQuality Renderer3D::defaultMaterialQualityFor(const gfx::Device& dev, bool mobile) {
    if (const char* q = std::getenv("AIVERSE_MATERIALS")) { // testing override: low|medium|high
        std::string v = q;
        return v == "low" ? MaterialQuality::Low : v == "medium" ? MaterialQuality::Medium : MaterialQuality::High;
    }
    const std::string& n = dev.info().name;
    if (mobile) {
        for (const char* weak : {"Mali-4", "Mali-T", "Adreno (TM) 3", "Adreno (TM) 4", "Adreno (TM) 50", "PowerVR SGX", "PowerVR Rogue GE"})
            if (n.find(weak) != std::string::npos) return MaterialQuality::Low;
        return MaterialQuality::Medium;
    }
    return MaterialQuality::High;
}

void Renderer3D::initModels() {
    auto tex1x1 = [&](uint32_t rgbaPx, const char* label) {
        gfx::TextureDesc td; td.width = td.height = 1; td.data = &rgbaPx; td.repeat = true; td.label = label;
        return dev_->createTexture(td);
    };
    white_ = tex1x1(0xffffffffu, "white");
    black_ = tex1x1(0xff000000u, "black");
    flatNormal_ = tex1x1(0xffff8080u, "flat normal");
    jointData_.reserve(kJointFloats * 64);
}

void Renderer3D::shutdownModels() {
    modelError_.clear();
    for (auto& [k, p] : modelPipes_) dev_->destroyPipeline(p);
    for (auto& [k, s] : modelShaders_) if (s.shader.valid()) dev_->destroyShader(s.shader);
    modelPipes_.clear(); modelShaders_.clear();
    for (auto t : {white_, black_, flatNormal_}) dev_->destroyTexture(t);
}

void Renderer3D::setMaterialQuality(MaterialQuality q) {
    if (q == matQuality_) return;
    matQuality_ = q;
    if (!dev_) return;
    for (auto& [k, p] : modelPipes_) dev_->destroyPipeline(p);
    for (auto& [k, s] : modelShaders_) if (s.shader.valid()) dev_->destroyShader(s.shader);
    modelPipes_.clear(); modelShaders_.clear();
}

gfx::PipelineHandle Renderer3D::modelPipeline(const ModelItem& it, int phase, bool instanced) {
    const bool shadow = phase == 0, outline = phase == 3, depthAfter = phase == 4;
    const bool skinned = it.joints >= 0, baked = it.baked, morph = it.morph >= 0;
    const bool mask = !outline && it.mat->alpha == AlphaMode::Mask;
    const bool normalMap = phase != 0 && !outline && it.mat->normalTex >= 0 && matQuality_ != MaterialQuality::Low;
    const bool blend = outline || (!shadow && !depthAfter && (it.mat->alpha == AlphaMode::Blend || it.tint.w < 0.99f || transparentItem(it.flags)));
    const bool twoSided = !outline && it.mat->doubleSided;
    const uint32_t shaderKey = (skinned ? kKeySkinned : 0u) | (normalMap ? kKeyNormalMap : 0u) | (mask ? kKeyAlphaMask : 0u) |
                               (shadow ? kKeyShadow : 0u) | (baked ? kKeyBaked : 0u) | (morph ? kKeyMorph : 0u) |
                               (instanced ? kKeyInstanced : 0u) | (outline ? kKeyOutline : 0u);
    const bool fadeDepth = blend && !outline && it.mat->alpha != AlphaMode::Blend && (it.flags & kDrawFadeDepth) && !transparentItem(it.flags);
    const uint32_t pipeKey = shaderKey | (blend ? 16u : 0u) | (twoSided ? 32u : 0u) | (fadeDepth ? 64u : 0u) | (depthAfter ? 1024u : 0u);
    const auto selectedGraph=it.mat->shaderGraph && int(matQuality_)>=it.mat->shaderGraph->minQuality?it.mat->shaderGraph:nullptr;
    const auto* graph = selectedGraph.get();
    const ModelShaderKey skey{shaderKey, selectedGraph};
    const ModelShaderKey pkey{pipeKey, selectedGraph};
    for (auto& [k, p] : modelPipes_) if (k == pkey) return p;

    ShaderEntry* entry = nullptr;
    for (auto& [k, s] : modelShaders_) if (k == skey) entry = &s;
    if (!entry) {
        std::string defs;
        defs += "#define AIVERSE_INST " + std::to_string(instanced ? kMaxInstances : 1) + "\n";
        if (skinned) defs += "#define SKINNED 1\n";
        if (baked) defs += "#define BAKED 1\n";
        if (morph) defs += "#define MORPH 1\n";
        if (normalMap) defs += "#define NORMALMAP 1\n";
        if (mask) defs += "#define ALPHAMASK 1\n";
        if (outline) defs += "#define OUTLINE 1\n";
        defs += "#define AIVERSE_Q " + std::to_string(int(matQuality_)) + "\n";
        std::string vs = defs + (shadow ? shaders::kModelShadowVS : shaders::kModelVS);
        std::string fs = defs + (shadow ? shaders::kModelShadowFS : shaders::kModelFS);
        std::string msl = defs + (shadow ? shaders::kModelShadowMSL : shaders::kModelMSL);
        if (graph) {
            auto replace = [](std::string& source,const std::string& marker,const std::string& text) {
                const auto at=source.find(marker);if(at!=std::string::npos)source.replace(at,marker.size(),text);
            };
            auto inject = [&](std::string& source,bool metal,bool vertexOnly) {
                const std::string v=metal?"float4":"vec4", v3=metal?"float3":"vec3";
                if(!vertexOnly) {
                    std::string declarations, args, macros;
                    for(size_t i=0;i<graph->textures.size();++i) {
                        const auto slot=std::to_string(11+i), index=std::to_string(i);
                        declarations+="uniform sampler2D tex"+slot+";\n";
                        args+=", texture2d<float> tex"+slot+" [[texture("+slot+")]], sampler s"+slot+" [[sampler("+slot+")]]";
                        macros+="#define SG_TEXTURE"+index+"(uv) "+(metal?"tex"+slot+".sample(s"+slot+",uv)":"texture(tex"+slot+",uv)")+"\n";
                        macros+="#define SG_TEXTURE_LOD"+index+"(uv,lod) "+(metal?"tex"+slot+".sample(s"+slot+",uv,level(lod))":"textureLod(tex"+slot+",uv,lod)")+"\n";
                    }
                    if(metal) {
                        const auto f=source.find("fragment ");const auto at=source.find(") {",f);
                        if(at!=std::string::npos)source.insert(at,args);
                    } else source=declarations+source;
                    const auto uv=metal?"in.uv":"v_uv", wp=metal?"in.wpos":"v_wpos", np=metal?"in.nrm":"v_nrm";
                    std::string code="\n#define SG_SAMPLE(uv) "+std::string(metal?"tex1.sample(s1,uv)":"texture(tex1,uv)")+"\n"+macros;
                    code+=std::string(metal?"float2":"vec2")+" sg_uv="+uv+"; "+v+" sg_input=base;\n";
                    code+=v3+" sg_world="+wp+", sg_object="+(metal?"in.opos":"v_opos")+", sg_normal="+np+", sg_view="+(metal?"f.camPos.xyz":"u_camPos.xyz")+"-"+wp+";\n";
                    code+=v+" sg_color="+(metal?"in.col":"v_col")+"; float sg_time="+(metal?"f.camPos.w":"u_camPos.w")+";\n";
                    // Projected world position gives the same normalized screen coordinates in colour/shadow passes.
                    code+=v+" sg_clip="+(metal?"f.viewProj":"u_viewProj")+"*"+v+"("+wp+",1.0); "+(metal?"float2":"vec2")+" sg_screen=sg_clip.xy/max(abs(sg_clip.w),0.00001)*0.5+0.5;\n";
                    code+=graph::statements(*graph,metal)+"base="+graph->outputs.at("color")+";\n#undef SG_SAMPLE\n";
                    replace(source,"// SG_COLOR",code);
                    std::string surface;
                    for(auto name:{"roughness","metallic","emission","normal"}) {
                        auto it=graph->outputs.find(name);if(it==graph->outputs.end())continue;
                        const std::string key=name;
                        if(key=="normal") surface+="n="+it->second+".xyz/max(length("+it->second+".xyz),0.00001);\n";
                        else surface+=(key=="roughness"?"rough":key=="emission"?"emis":"metallic")+std::string(" = ")+it->second+(key=="emission"?".rgb;\n":".r;\n");
                    }
                    replace(source,"// SG_SURFACE",surface);
                }
                if(vertexOnly || metal) {
                    const std::string pos=metal?"d.p":"p", normal=metal?"d.n":"n";
                    std::string code="{\n"+std::string(metal?"float2 sg_uv=uvTransform(m,in.uv);":"vec2 sg_uv=uvTransform(a_uv);");
                    code+=v3+" sg_object="+pos+".xyz, sg_world=(I.model*"+pos+").xyz;\n";
                    code+=v3+" sg_c0=I.model[0].xyz,sg_c1=I.model[1].xyz,sg_c2=I.model[2].xyz;\n";
                    code+=v3+" sg_normal="+(metal?"float3x3":"mat3")+"(cross(sg_c1,sg_c2),cross(sg_c2,sg_c0),cross(sg_c0,sg_c1))*"+normal+"*(dot(sg_c0,cross(sg_c1,sg_c2))<0.0?-1.0:1.0);\n";
                    code+=v3+" sg_view="+(metal?"f.camPos.xyz":"u_camPos.xyz")+"-sg_world; "+v+" sg_color="+(metal?"in.col":"a_col")+"; float sg_time="+(metal?"f.camPos.w":"u_camPos.w")+";\n";
                    std::string args,declarations,macros;
                    for(size_t i=0;i<graph->textures.size();++i) {
                        const auto slot=std::to_string(11+i),index=std::to_string(i);
                        declarations+="uniform sampler2D tex"+slot+";\n";
                        args+=", texture2d<float> tex"+slot+" [[texture("+slot+")]], sampler s"+slot+" [[sampler("+slot+")]]";
                        macros+="#define SG_TEXTURE_LOD"+index+"(uv,lod) "+(metal?"tex"+slot+".sample(s"+slot+",uv,level(lod))":"textureLod(tex"+slot+",uv,lod)")+"\n";
                    }
                    if(metal)replace(source,"vs_main(VERTEX_ARGS)","vs_main(VERTEX_ARGS"+args+")");else source=declarations+source;
                    code+=macros;
                    code+=metal?graph->vertexMsl:graph->vertexGlsl;
                    if(auto it=graph->outputs.find("vertexOffset");it!=graph->outputs.end())code+=pos+".xyz += "+it->second+".xyz;\n";
                    if(auto it=graph->outputs.find("vertexNormal");it!=graph->outputs.end())code+=normal+" = "+it->second+".xyz;\n";
                    code+="}\n";replace(source,"// SG_VERTEX",code);
                }
            };
            inject(vs,false,true);inject(fs,false,false);inject(msl,true,false);
        }
        gfx::ShaderDesc sd;
        sd.glslVertex = vs.c_str(); sd.glslFragment = fs.c_str(); sd.msl = msl.c_str();
        sd.uniformSlots = shadow ? 5 : 6; sd.textureSlots = graph && !graph->textures.empty() ? 11+int(graph->textures.size()) : shadow ? 9 : 11; sd.label = shadow ? "model shadow" : "model"; // U5/tex9: lights, tex10: height field
        ShaderEntry e;
        e.shader = dev_->createShader(sd);
        if (!e.shader.valid()) {
            e.failed = true;
            modelError_ = "model shader (" + defs + "): " + dev_->lastError();
            std::fprintf(stderr, "%s\n", modelError_.c_str());
        }
        modelShaders_.push_back({skey, e});
        entry = &modelShaders_.back().second;
    }
    gfx::PipelineHandle ph;
    if (!entry->failed) {
        gfx::PipelineDesc pd;
        pd.shader = entry->shader;
        pd.attrs[0] = {0, gfx::VertexFormat::Float3, 0};
        pd.attrs[1] = {1, gfx::VertexFormat::Float3, 12};
        pd.attrs[2] = {2, gfx::VertexFormat::Float2, 24};
        pd.attrs[3] = {3, gfx::VertexFormat::Float4, 32};
        pd.attrs[4] = {4, gfx::VertexFormat::UByte4N, 48};
        pd.attrs[5] = {5, gfx::VertexFormat::UByte4, 52};
        pd.attrs[6] = {6, gfx::VertexFormat::UByte4N, 56};
        pd.attrCount = 7;
        pd.stride = sizeof(ModelVertex);
        pd.indexType = gfx::IndexType::UInt32;
        pd.cull = outline ? gfx::CullMode::Front : twoSided ? gfx::CullMode::None : gfx::CullMode::Back;
        if (shadow) {
            pd.depthOnly = true;
            pd.depthBiasConstant = 2.0f; pd.depthBiasSlope = 2.5f;
        } else if (blend) {
            pd.blend = gfx::BlendMode::Alpha;
            pd.depthWrite = fadeDepth;
        } else if (depthAfter) {
            pd.colorWrite = false;
        }
        pd.label = shadow ? "model shadow" : outline ? "model outline" : depthAfter ? "model depth" : "model";
        ph = dev_->createPipeline(pd);
        if (!ph.valid()) modelError_ = "model pipeline: " + dev_->lastError();
    }
    modelPipes_.push_back({pkey, ph});
    return ph;
}

void Renderer3D::drawModel(const ModelDraw& d) {
    const Model* m = d.model;
    if (!m || !m->vb.valid() || m->nodes.empty()) return;
    // whole-model culling with the bind-pose bounds (padded: animation moves limbs outside them)
    float3 c = (m->boundsMin + m->boundsMax) * 0.5f, e = (m->boundsMax - m->boundsMin) * 0.75f + float3{0.1f, 0.1f, 0.1f};
    const auto& graphMaterials=d.materials && d.materials->size()>=m->materials.size()?*d.materials:m->materials;
    const bool displaced=std::any_of(graphMaterials.begin(),graphMaterials.end(),[this](const Material& mat){return mat.shaderGraph && int(matQuality_)>=mat.shaderGraph->minQuality && mat.shaderGraph->outputs.count("vertexOffset");});
    const bool inView = displaced || boxVisible(viewProj_ * d.world, c - e, c + e);
    const bool inShadow = quality_ != ShadowQuality::Off && !(d.flags & kDrawNoCastShadow) && d.tint.w >= 0.99f && d.dissolve <= 0 &&
                          (displaced || boxVisible(lightViewProj_ * d.world, c - e, c + e));
    if (!inView && !inShadow) return;
    const std::vector<mat4>& world = d.nodeWorld && d.nodeWorld->size() == m->nodes.size() ? *d.nodeWorld : m->bindWorld;
    const std::vector<Material>& mats = d.materials && d.materials->size() >= m->materials.size() ? *d.materials : m->materials;
    const bool usePose = d.pose && d.pose->size() == m->nodes.size();
    float4 anim{0, 0, 0, 0};
    const bool canBake = d.baked && m->baked.tex.valid();
    if (canBake) {
        const BakedSample& b = *d.baked;
        anim = {float(bakedRow(*m, b.clipA, b.timeA, b.loopA)), float(bakedRow(*m, b.clipB, b.timeB, b.loopB)), std::clamp(b.mix, 0.0f, 1.0f), 0};
    }
    for (int n : m->order) {
        const Node& node = m->nodes[size_t(n)];
        if (node.mesh < 0) continue;
        for (const Primitive& p : m->meshes[size_t(node.mesh)].primitives) {
            if (p.indexCount == 0) continue;
            int mi = p.material;
            if (d.variant >= 0 && d.variant < int(p.variantMaterial.size()) && p.variantMaterial[size_t(d.variant)] >= 0) mi = p.variantMaterial[size_t(d.variant)];
            if (d.nodeMaterial && size_t(n) < d.nodeMaterial->size() && (*d.nodeMaterial)[size_t(n)] >= 0 && &mats == d.materials) mi = (*d.nodeMaterial)[size_t(n)];
            ModelItem it;
            it.geometry = d.geometry;
            it.firstIndex = p.firstIndex; it.indexCount = p.indexCount;
            size_t pi = 0;
            for (int mesh = 0; mesh < node.mesh; ++mesh) pi += m->meshes[size_t(mesh)].primitives.size();
            it.geometryPrimitive = pi + size_t(&p - m->meshes[size_t(node.mesh)].primitives.data());
            it.model = m; it.prim = &p; it.mat = &mats[size_t(std::clamp(mi, 0, int(mats.size()) - 1))];
            it.tint = d.tint; it.team = d.team; it.flash = d.flash; it.flags = d.flags | (mats[size_t(std::clamp(mi, 0, int(mats.size()) - 1))].transparent ? uint32_t(kDrawTransparent) : 0u);
            it.dissolve = d.dissolve; it.teamGlow = d.teamGlow; it.outline = d.outline;
            it.inView = inView; it.inShadow = inShadow && it.mat->castShadow && it.mat->alpha != AlphaMode::Blend && !transparentItem(it.flags);
            it.joints = -1; it.baked = false; it.anim = anim; it.morph = -1;
            const bool skinnedPrim = node.skin >= 0 && !p.jointPalette.empty();
            if (skinnedPrim && canBake && node.skin == m->baked.skin) {
                it.baked = true;
                it.world = d.world;
            } else if (skinnedPrim) {
                // joint matrices are in model space: the mesh node transform is ignored (glTF skinning rule)
                paletteMatrices(*m, m->skins[size_t(node.skin)], p, world, palette_);
                it.joints = int32_t(jointData_.size());
                jointData_.resize(jointData_.size() + kJointFloats, 0.0f);
                float* dst = jointData_.data() + it.joints;
                for (size_t j = 0; j < palette_.size() && j < size_t(kMaxPaletteJoints); ++j)
                    for (int r = 0; r < 3; ++r)
                        for (int col = 0; col < 4; ++col) dst[j * 12 + size_t(r) * 4 + size_t(col)] = palette_[j].at(r, col);
                it.world = d.world;
            } else {
                it.world = d.world * world[size_t(n)];
            }
            if (p.morphTargets > 0 && m->morphTex.valid()) {
                const float* w = usePose ? (*d.pose)[size_t(n)].w : node.weights;
                bool any = false;
                for (int k = 0; k < p.morphTargets; ++k) any |= w[k] != 0;
                if (any) {
                    it.morph = int32_t(morphData_.size());
                    morphData_.insert(morphData_.end(), w, w + kMaxMorphTargets);
                }
            }
            float3 wc = it.world.transformPoint((p.boundsMin + p.boundsMax) * 0.5f);
            // sort distance: ortho cameras along the view direction (Unity's TransparencySortMode.Default), else from the eye
            it.depth = cam_.ortho ? dot(wc - camPos_, normalize(cam_.target - camPos_)) : length(wc - camPos_);
            modelItems_.push_back(it);
        }
    }
}

void Renderer3D::drawModelBatch(const uint32_t* items, uint32_t count, int phase) {
    const ModelItem& it = modelItems_[items[0]];
    const bool instanced = count > 1;
    gfx::PipelineHandle ph = modelPipeline(it, phase, instanced);
    if (!ph.valid()) return;
    if (!(ph == currentPipe_)) {
        dev_->setPipeline(ph); currentPipe_ = ph;
        if (phase != 0) { lights.bind(5, 9); bindTrace(10); }   // U5 / tex9: dynamic lights; tex10: height field (traced terrain shadows)
    }
    // per-instance block (an instanced shader declares 32 entries: the block is always uploaded whole)
    const uint32_t slots = instanced ? uint32_t(kMaxInstances) : 1u;
    instData_.assign(size_t(slots) * 32, 0.0f);
    for (uint32_t i = 0; i < count; ++i) {
        const ModelItem& x = modelItems_[items[i]];
        float* d = instData_.data() + size_t(i) * 32;
        std::memcpy(d, x.world.m, 16 * sizeof(float));
        d[16] = x.tint.x; d[17] = x.tint.y; d[18] = x.tint.z; d[19] = x.tint.w;
        if (phase == 3) { d[20] = x.outline.x; d[21] = x.outline.y; d[22] = x.outline.z; d[23] = 0; }
        else { d[20] = x.team.x; d[21] = x.team.y; d[22] = x.team.z; d[23] = x.flash; }
        d[24] = (x.mat->receiveShadow && !(x.flags & kDrawNoReceiveShadow)) ? 1.0f : 0.0f;
        d[25] = x.dissolve;
        d[26] = x.outline.w;
        d[27] = phase == 3 ? 0.0f : x.teamGlow;
        d[28] = x.anim.x; d[29] = x.anim.y; d[30] = x.anim.z;
    }
    dev_->setUniforms(0, instData_.data(), instData_.size() * sizeof(float));
    const Material& mt = *it.mat;
    const bool newModel = !lastItem_ || lastItem_->model != it.model;
    if (!lastItem_ || lastItem_->mat != it.mat || newModel) {
        MaterialUniforms mu{};
        mu.base[0] = mt.baseColor.x; mu.base[1] = mt.baseColor.y; mu.base[2] = mt.baseColor.z; mu.base[3] = mt.baseColor.w;
        mu.emissive[0] = mt.emissive.x * mt.emissiveStrength; mu.emissive[1] = mt.emissive.y * mt.emissiveStrength; mu.emissive[2] = mt.emissive.z * mt.emissiveStrength;
        mu.pbr[0] = mt.metallic; mu.pbr[1] = mt.roughness; mu.pbr[2] = mt.normalScale; mu.pbr[3] = mt.occlusionStrength;
        mu.misc[0] = mt.alpha == AlphaMode::Mask ? mt.alphaCutoff : 0.0f;
        mu.misc[1] = float(int(mt.team)); mu.misc[2] = mt.teamStrength; mu.misc[3] = mt.vertexColors ? 1.0f : 0.0f;
        mu.uv0[0] = mt.uvOffset.x; mu.uv0[1] = mt.uvOffset.y; mu.uv0[2] = mt.uvScale.x; mu.uv0[3] = mt.uvScale.y;
        mu.uv1[0] = mt.uvRotation; mu.uv1[1] = mt.shading == ShadingModel::Unlit ? 1.0f : 0.0f;
        mu.uv1[2] = mt.fixedShade.x; mu.uv1[3] = mt.fixedShade.y;
        mu.light[0] = mt.fixedLight.x; mu.light[1] = mt.fixedLight.y; mu.light[2] = mt.fixedLight.z; mu.light[3] = mt.fixedLight.w;
        dev_->setUniforms(2, &mu, sizeof mu);
        auto tex = [&](int idx, gfx::TextureHandle def) {
            return idx >= 0 && idx < int(it.model->images.size()) && it.model->images[size_t(idx)].tex.valid() ? it.model->images[size_t(idx)].tex : def;
        };
        dev_->setTexture(1, tex(mt.baseTex, white_));
        if(mt.shaderGraph)for(size_t i=0;i<mt.shaderGraph->textures.size();++i)dev_->setTexture(11+int(i),tex(mt.graphTextures[i],white_));
        if (phase != 0) {
            dev_->setTexture(2, tex(mt.mrTex, white_));
            dev_->setTexture(3, tex(mt.normalTex, flatNormal_));
            dev_->setTexture(4, tex(mt.occlusionTex, white_));
            dev_->setTexture(5, tex(mt.emissiveTex, white_));
            dev_->setTexture(6, tex(mt.teamTex, black_));
        }
    }
    if (newModel) {
        dev_->setTexture(7, it.model->baked.tex.valid() ? it.model->baked.tex : white_);
        dev_->setTexture(8, it.model->morphTex.valid() ? it.model->morphTex : white_);
    }
    if (it.joints >= 0) dev_->setUniforms(3, jointData_.data() + it.joints, kJointFloats * sizeof(float));
    else if (it.baked && (!lastItem_ || !lastItem_->baked || lastItem_->prim != it.prim)) {
        float pal[80] = {};
        for (size_t j = 0; j < it.prim->jointPalette.size() && j < 80; ++j) pal[j] = float(it.prim->jointPalette[j]);
        dev_->setUniforms(3, pal, sizeof pal);
    }
    if (it.morph >= 0) {
        float mo[16] = {};
        std::memcpy(mo, morphData_.data() + it.morph, 8 * sizeof(float));
        mo[8] = float(it.prim->morphTexel); mo[9] = float(it.prim->firstVertex); mo[10] = float(it.prim->vertexCount); mo[11] = float(it.prim->morphTargets);
        mo[12] = float(it.model->morphTexWidth);
        dev_->setUniforms(4, mo, sizeof mo);
    }
    dev_->setVertexBuffer(it.model->vb);
    dev_->setIndexBuffer(it.model->ib);
    dev_->drawIndexed(it.indexCount, it.firstIndex, 0, count);
    lastItem_ = &it;
    ++drawCalls_;
}

void Renderer3D::renderModels(int phase) {
    if (modelItems_.empty()) return;
    order_.clear();
    for (uint32_t i = 0; i < modelItems_.size(); ++i) {
        const ModelItem& it = modelItems_[i];
        const bool blended = it.mat->alpha == AlphaMode::Blend || it.tint.w < 0.99f || transparentItem(it.flags);
        bool take = phase == 0 ? it.inShadow : phase == 3 ? it.inView && it.outline.w > 0 : phase == 4 ? it.inView && transparentItem(it.flags) :
                    it.inView && (phase == 2) == blended;
        // Reflection has its own view, never consume main-view occlusion there.
        if (phase == 1 && !geometryMainView_) take = !blended && geometryBoxVisible(viewProj_ * it.world, it.prim->boundsMin, it.prim->boundsMax);
        if (take && phase != 0 && geometryMainView_ && it.geometry.occlusion && it.joints < 0 && !it.baked && it.morph < 0 && it.outline.w == 0 &&
            geometryOcclusion_.hidden(viewProj_ * it.world, it.prim->boundsMin, it.prim->boundsMax)) {
            if (phase == 1 || phase == 2) ++geometryStats_.occluded;
            take = false;
        }
        if (take) order_.push_back(i);
    }
    if (order_.empty()) return;
    if (phase == 2) { // back to front
        std::sort(order_.begin(), order_.end(), [&](uint32_t a, uint32_t b) { return modelItems_[a].depth > modelItems_[b].depth; });
    } else {          // group by state so identical primitives end up next to each other (instancing)
        std::sort(order_.begin(), order_.end(), [&](uint32_t a, uint32_t b) {
            const ModelItem &x = modelItems_[a], &y = modelItems_[b];
            if ((x.joints >= 0) != (y.joints >= 0)) return x.joints < y.joints;
            if (x.baked != y.baked) return x.baked < y.baked;
            if (x.mat != y.mat) return x.mat < y.mat;
            if (x.prim != y.prim) return x.prim < y.prim;
            return a < b;
        });
    }
    currentPipe_ = {};
    lastItem_ = nullptr;
    auto batchable = [&](const ModelItem& x) { return x.joints < 0 && x.morph < 0; };
    auto sameBatch = [&](const ModelItem& x, const ModelItem& y) {
        const bool bx = x.mat->alpha == AlphaMode::Blend || x.tint.w < 0.99f, by = y.mat->alpha == AlphaMode::Blend || y.tint.w < 0.99f;
        return x.prim == y.prim && x.mat == y.mat && x.baked == y.baked && bx == by &&
               (x.flags & (kDrawFadeDepth | kDrawTransparent)) == (y.flags & (kDrawFadeDepth | kDrawTransparent));
    };
    for (size_t i = 0; i < order_.size();) {
        size_t j = i + 1;
        if (batchable(modelItems_[order_[i]]))
            while (j < order_.size() && j - i < size_t(kMaxInstances) && batchable(modelItems_[order_[j]]) &&
                   sameBatch(modelItems_[order_[i]], modelItems_[order_[j]])) ++j;
        drawGeometryBatch(order_.data() + i, uint32_t(j - i), phase);
        i = j;
    }
}

void Renderer3D::prepareOcclusion() {
    geometryOcclusion_.clear(dev_->info().clipDepthZeroToOne);
    uint32_t budget = 8192;
    for (const auto& it : modelItems_) {
        if (!budget) break;
        if (!it.inView || !it.geometry.occluder || !it.model->geometry || it.joints >= 0 || it.baked || it.morph >= 0 ||
            it.mat->alpha != AlphaMode::Opaque || it.mat->transparent || it.mat->shaderGraph ||
            it.tint.w < 1 || it.dissolve > 0 || transparentItem(it.flags)) continue;
        for (const auto& t : it.model->geometry->occluders[it.geometryPrimitive]) {
            geometryOcclusion_.triangle(viewProj_ * it.world, t, it.mat->doubleSided);
            ++geometryStats_.occluderTriangles;
            if (!--budget) break;
        }
    }
}
void Renderer3D::drawGeometryBatch(const uint32_t* items, uint32_t count, int phase) {
    auto& it = modelItems_[items[0]];
    const bool eligible = count == 1 && phase == 1 && geometryMainView_ && it.geometry.clusters && it.model->geometry &&
        it.joints < 0 && !it.baked && it.morph < 0 && it.outline.w == 0 && it.dissolve == 0;
    if (!eligible) { drawModelBatch(items,count,phase); return; }
    const auto& clusters = it.model->geometry->primitives[it.geometryPrimitive];
    if (clusters.empty() || clusters.size() > 8192 || geometryStats_.clustersTested + clusters.size() > 16384) { drawModelBatch(items,count,phase); return; }
    const mat4 clip = viewProj_ * it.world;
    const auto first = it.firstIndex, indices = it.indexCount;
    std::vector<std::pair<uint32_t,uint32_t>> runs;
    uint32_t culled = 0, reduced = 0;
    for (const auto& c : clusters) {
        ++geometryStats_.clustersTested;
        if (!geometryBoxVisible(clip,c.min,c.max) || (it.geometry.occlusion && geometryOcclusion_.hidden(clip,c.min,c.max))) {
            ++culled; continue;
        }
        // The occlusion buffer contains source triangles, so an occluder must render exact geometry.
        const bool coarse = !it.geometry.occluder && c.coarseCount < c.indexCount && geometryErrorPixels(clip,c.min,c.max,c.error,sceneW_,sceneH_) <= it.geometry.errorPixels;
        if (coarse) ++reduced;
        const auto begin = coarse ? c.coarseFirst : c.firstIndex, length = coarse ? c.coarseCount : c.indexCount;
        if (!runs.empty() && runs.back().first + runs.back().second == begin) runs.back().second += length;
        else runs.push_back({begin,length});
    }
    if (runs.size() > 256 || (!culled && !reduced)) { drawModelBatch(items,count,phase); return; }
    geometryStats_.clustersCulled += culled; geometryStats_.coarseClusters += reduced;
    for (auto [begin,length] : runs) { it.firstIndex = begin; it.indexCount = length; drawModelBatch(items,count,phase); }
    it.firstIndex = first; it.indexCount = indices;
}
} // namespace aiverse::render
