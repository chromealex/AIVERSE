#include <map>
#include "HumanMaterials.h"
#include "HumanAppearance.h"
#include <algorithm>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "HumanAppearanceBake.h"

namespace aiverse::vivid {
namespace {
Json vector4(float a,float b,float c,float d=1.0f) {
    auto x=Json::array();
    for(float v:{a,b,c,d})x.push(Json::number(double(v),6));
    return x;
}
struct Nodes {
    Json graph=Json::object(), list=Json::array();
    int serial=0;
    std::string add(const char* op,
                    std::initializer_list<std::string> inputs={},
                    Json value={}) {
        const std::string name="h"+std::to_string(serial++);
        auto item=Json::object();
        item.set("id",Json::string(name));
        item.set("op",Json::string(op));
        if(inputs.size()) {
            auto source=Json::array();
            for(const auto& input:inputs)source.push(Json::string(input));
            item.set("inputs",std::move(source));
        }
        if(!value.isNull())item.set("value",std::move(value));
        list.push(std::move(item));
        return name;
    }
    // Identical constants share one node (graphs are capped at 128 nodes).
    std::map<std::string,std::string> constants;
    std::string cached(Json value) {
        const auto key=value.dump();
        if(auto it=constants.find(key);it!=constants.end())return it->second;
        return constants[key]=add("constant",{},std::move(value));
    }
    std::string constant(float scalar) {return cached(Json::number(double(scalar),6));}
    std::string color(const std::array<float,3>& rgb) {return cached(vector4(rgb[0],rgb[1],rgb[2]));}
    std::string vec(float a,float b,float c,float d) {return cached(vector4(a,b,c,d));}
    std::string scalarOp(const char* op,std::string input) {
        return add(op,{std::move(input)});
    }
    std::string binary(const char* op,std::string a,std::string b) {
        return add(op,{std::move(a),std::move(b)});
    }
    std::string ternary(const char* op,std::string a,std::string b,std::string c) {
        return add(op,{std::move(a),std::move(b),std::move(c)});
    }
    std::shared_ptr<const render::graph::Program> finish(
        const std::string& output, const std::string& roughness,
        bool high=false) {
        graph.set("version",Json::number(std::int64_t(1)));
        graph.set("domain",Json::string("material"));
        graph.set("nodes",std::move(list));
        if(high)graph.set("minQuality",Json::string("high"));
        auto outs=Json::object();
        outs.set("color",Json::string(output));
        outs.set("roughness",Json::string(roughness));
        graph.set("outputs",std::move(outs));
        return std::make_shared<const render::graph::Program>(
            render::graph::compile(graph,"material"));
    }
};
render::Material basic(const char* name,std::array<float,3> color,float rough) {
    render::Material m;
    m.name=name;
    m.baseColor={color[0],color[1],color[2],1.0f};
    m.roughness=rough;
    m.metallic=0;
    m.vertexColors=false;
    return m;
}
std::shared_ptr<const render::graph::Program> skinGraph(const HumanSkin& d) {
    Nodes g;
    const auto input=g.add("input");
    const auto uv=g.add("uv");
    const auto normal=g.add("normal");
    const auto view=g.add("viewDirection");
    std::array<float,3> pigment;
    for(int i=0;i<3;++i)
        pigment[std::size_t(i)]=std::clamp(d.melaninColor[std::size_t(i)]*
                    (1.04f-d.melanin*0.22f),0.02f,1.0f);
    const auto base=g.binary("multiply",input,g.color(pigment));
    const auto poreFreq=g.constant(42.0f+85.0f*d.poreScale);
    const auto poreUv=g.binary("multiply",uv,poreFreq);
    const auto noise=g.scalarOp("gradientNoise",poreUv);
    const auto poreOffset=g.binary("subtract",noise,g.constant(0.5f));
    const auto pore=g.binary("multiply",poreOffset,g.constant(0.06f*d.poreScale));
    const auto wrinkleUv=g.binary("multiply",uv,
                                  g.constant(20.0f+60.0f*d.wrinkleStrength));
    const auto wrinkleNoise=g.scalarOp("gradientNoise",wrinkleUv);
    const auto wrinkle=g.binary("multiply",
                     g.binary("subtract",wrinkleNoise,g.constant(0.5f)),
                     g.constant(0.085f*d.wrinkleStrength));
    const auto skinNoise=g.binary("multiply",base,
             g.binary("add",g.constant(1.0f),g.binary("add",pore,wrinkle)));
    const auto fresnel=g.ternary("fresnel",normal,view,g.constant(2.4f));
    const auto blood=g.color({0.17f*d.hemoglobin,
                             0.035f*d.hemoglobin,0.023f*d.hemoglobin});
    // Broad hemoglobin tint at grazing angles plus shallow diffuse warmth.
    // This is an artist-controlled surface approximation, not screen-space SSS.
    const auto scatter=g.binary("multiply",fresnel,
                    g.binary("multiply",blood,g.constant(d.scatteringMm/6.0f)));
    const auto shallow=g.binary("multiply",blood,
        g.constant(std::clamp(d.scatteringMm/6.0f,0.0f,1.0f)*0.12f));
    const auto skinColor=g.scalarOp("clamp",g.binary("add",skinNoise,
                                    g.binary("add",scatter,shallow)));
    const auto finalRoughness=g.constant(std::clamp(
            d.roughness*(1.0f-0.55f*d.oiliness-0.22f*d.wetness),0.04f,1.0f));
    return g.finish(skinColor,finalRoughness);
}
std::shared_ptr<const render::graph::Program> irisGraph(const HumanEyes& d) {
    Nodes g;
    const auto uv=g.add("uv");
    const auto origin=g.add("constant",{},vector4(0.5f,0.5f,0,0));
    const auto radius=g.scalarOp("length3",g.binary("subtract",uv,origin));
    const auto pupilRadius=std::max(0.02f,d.irisRadius*d.pupil*0.5f);
    const auto irisRadius=std::max(0.10f,d.irisRadius*0.49f);
    const auto irisMask=g.scalarOp("oneMinus",
        g.ternary("smoothstep",g.constant(irisRadius-0.012f),
                  g.constant(irisRadius+0.012f),radius));
    const auto pupilMask=g.scalarOp("oneMinus",
        g.ternary("smoothstep",g.constant(pupilRadius-0.01f),
                  g.constant(pupilRadius+0.01f),radius));
    const auto pupil=g.color({0.008f,0.009f,0.012f});
    const auto sclera=g.color({0.92f,0.89f-d.scleraRedness*0.12f,
                              0.86f-d.scleraRedness*0.18f});
    const auto fibreNoise=g.scalarOp("gradientNoise",
                       g.binary("multiply",uv,g.constant(85.0f)));
    // Layered iris: fine radial fibers plus a lower-frequency crypt band.
    // The radial term is centered on the actual pupil UV rather than
    // absolute UV, preventing the texture from drifting with iris scaling.
    const auto radialUv=g.binary("subtract",uv,origin);
    const auto radialNoise=g.scalarOp("gradientNoise",
        g.binary("multiply",radialUv,
                 g.add("constant",{},vector4(108.0f,108.0f,0,0))));
    const auto cryptNoise=g.scalarOp("gradientNoise",
        g.binary("multiply",radialUv,g.constant(24.0f)));
    const auto fiberVariation=g.binary("add",g.constant(0.72f),
        g.binary("add",g.binary("multiply",fibreNoise,g.constant(0.20f)),
            g.binary("add",g.binary("multiply",radialNoise,g.constant(0.14f)),
                          g.binary("multiply",cryptNoise,g.constant(0.10f)))));
    const auto irisColor=g.binary("multiply",g.color(d.irisColor),fiberVariation);
    const auto iris=g.ternary("mix",sclera,irisColor,irisMask);
    const auto eye=g.ternary("mix",iris,pupil,pupilMask);
    const auto limbalOuter=g.scalarOp("oneMinus",
        g.ternary("smoothstep",g.constant(irisRadius-0.055f),
                  g.constant(irisRadius-0.015f),radius));
    const auto limbal=g.binary("subtract",irisMask,limbalOuter);
    const auto darkening=g.binary("multiply",limbal,
                          g.constant(0.25f*d.limbus));
    const auto result=g.scalarOp("clamp",
                   g.binary("multiply",eye,
                       g.binary("subtract",g.constant(1.0f),darkening)));
    return g.finish(result,g.constant(0.19f),true);
}
std::shared_ptr<const render::graph::Program> hairGraph(const HumanGroom& d) {
    Nodes g;
    const auto uv=g.add("uv");
    const auto strands=g.scalarOp("gradientNoise",
        g.binary("multiply",uv,g.add("constant",{},
                                   vector4(65.0f,6.0f,0,0))));
    const auto variation=g.binary("add",g.constant(0.62f),
        g.binary("multiply",strands,g.constant(0.45f)));
    const auto facing=g.ternary("fresnel",g.add("normal"),
                        g.add("viewDirection"),g.constant(4.0f));
    // A second longitudinal low-frequency modulation softens the repeated
    // glints on cards while retaining the dedicated renderer Hair BRDF.
    const auto longitudinal=g.scalarOp("gradientNoise",
        g.binary("multiply",uv,
                 g.add("constant",{},vector4(3.0f,17.0f,0,0))));
    const auto glint=g.binary("multiply",facing,
        g.binary("add",g.constant(0.10f),
            g.binary("multiply",longitudinal,g.constant(0.16f))));
    const auto highlight=g.binary("add",variation,glint);
    const auto melaninDarkening=g.constant(1.0f-0.38f*d.melanin);
    const auto color=g.scalarOp("clamp",g.binary("multiply",
             g.binary("multiply",g.color(d.color),melaninDarkening),highlight));
    // Keep sampled coverage through both colour and shadow graph evaluation.
    // Procedural RGB must not turn imported alpha-masked cards opaque.
    const auto covered=g.binary("setAlpha",color,g.scalarOp("alpha",g.add("input")));
    return g.finish(covered,g.constant(std::clamp(d.roughness,0.05f,1.0f)));
}
// Mobile/medium-specific graphs share the full ShaderGraph compiler, while
// reducing nodes rather than hiding costly effects behind a runtime branch.
std::shared_ptr<const render::graph::Program> compactSkinGraph(
        const HumanSkin& d,HumanMaterialQuality quality) {
    Nodes g;
    std::array<float,3> pigment{};
    for(int i=0;i<3;++i)
        pigment[std::size_t(i)]=std::clamp(d.melaninColor[std::size_t(i)]*
                    (1.04f-d.melanin*0.22f),0.02f,1.0f);
    const auto base=g.binary("multiply",g.add("input"),g.color(pigment));
    const auto rough=g.constant(std::clamp(
        d.roughness*(1.0f-0.55f*d.oiliness-0.22f*d.wetness),0.04f,1.0f));
    if(quality==HumanMaterialQuality::Low)return g.finish(base,rough);
    const auto fresnel=g.ternary("fresnel",g.add("normal"),
                                g.add("viewDirection"),g.constant(2.4f));
    const auto blood=g.color({0.17f*d.hemoglobin,
                             0.035f*d.hemoglobin,0.023f*d.hemoglobin});
    const auto scatter=g.binary("multiply",fresnel,
        g.binary("multiply",blood,g.constant(d.scatteringMm/8.0f)));
    return g.finish(g.scalarOp("clamp",g.binary("add",base,scatter)),rough);
}
std::shared_ptr<const render::graph::Program> compactIrisGraph(
        const HumanEyes& d,HumanMaterialQuality quality) {
    Nodes g;
    const auto uv=g.add("uv");
    const auto center=g.add("constant",{},vector4(0.5f,0.5f,0,0));
    const auto radius=g.scalarOp("length3",g.binary("subtract",uv,center));
    const auto irisRadius=std::max(0.10f,d.irisRadius*0.49f);
    const auto pupilRadius=std::max(0.02f,d.irisRadius*d.pupil*0.5f);
    const auto irisMask=g.scalarOp("oneMinus",g.ternary("smoothstep",
        g.constant(irisRadius-0.012f),g.constant(irisRadius+0.012f),radius));
    const auto pupilMask=g.scalarOp("oneMinus",g.ternary("smoothstep",
        g.constant(pupilRadius-0.010f),g.constant(pupilRadius+0.010f),radius));
    const auto sclera=g.color({0.92f,0.89f-d.scleraRedness*0.12f,
                              0.86f-d.scleraRedness*0.18f});
    auto iris=g.color(d.irisColor);
    if(quality==HumanMaterialQuality::Medium) {
        const auto grain=g.scalarOp("gradientNoise",
            g.binary("multiply",uv,g.constant(43.0f)));
        iris=g.binary("multiply",iris,
            g.binary("add",g.constant(0.86f),
                         g.binary("multiply",grain,g.constant(0.20f))));
    }
    const auto colored=g.ternary("mix",sclera,iris,irisMask);
    const auto eye=g.ternary("mix",colored,
                               g.color({0.008f,0.009f,0.012f}),pupilMask);
    return g.finish(eye,g.constant(0.19f));
}
std::shared_ptr<const render::graph::Program> compactHairGraph(
        const HumanGroom& d,HumanMaterialQuality quality) {
    Nodes g;
    const auto darkness=g.constant(1.0f-0.38f*d.melanin);
    auto color=g.binary("multiply",g.color(d.color),darkness);
    if(quality==HumanMaterialQuality::Medium) {
        const auto grain=g.scalarOp("gradientNoise",
            g.binary("multiply",g.add("uv"),
                g.add("constant",{},vector4(65.0f,6.0f,0,0))));
        color=g.binary("multiply",color,g.binary("add",
            g.constant(0.75f),g.binary("multiply",grain,g.constant(0.30f))));
    }
    // Keep sampled coverage through both colour and shadow graph evaluation.
    // Procedural RGB must not turn imported alpha-masked cards opaque.
    const auto covered=g.binary("setAlpha",color,g.scalarOp("alpha",g.add("input")));
    return g.finish(covered,g.constant(std::clamp(d.roughness,0.05f,1.0f)));
}
void enforceGraphBudget(const render::Material& material,
                        HumanMaterialQuality quality) {
    if(!material.shaderGraph)return;
    const auto budget=humanMaterialBudget(quality);
    if(material.shaderGraph->nodes>budget.maxNodes ||
       material.shaderGraph->samples>budget.maxSamples)
        throw std::runtime_error("VIVID material graph exceeded quality budget");
}
} // namespace

render::Material humanMaterial(HumanSurface surface,const HumanDNA& dna,
                               HumanMaterialQuality quality) {
    dna.validate();
    render::Material m;
    switch(surface) {
    case HumanSurface::Skin:
        m=basic("VIVID skin",dna.skin.melaninColor,dna.skin.roughness);
        m.shaderGraph=quality==HumanMaterialQuality::High
            ?skinGraph(dna.skin):compactSkinGraph(dna.skin,quality);
        m.shading=render::ShadingModel::Skin;
        m.humanProfile={dna.skin.scatteringMm/6.0f,dna.skin.wetness};
        m.roughness=std::clamp(dna.skin.roughness-dna.skin.oiliness*0.23f,0.04f,1.0f);
        break;
    case HumanSurface::Iris:
        m=basic("VIVID layered iris",dna.eyes.irisColor,0.19f);
        m.shaderGraph=quality==HumanMaterialQuality::High
            ?irisGraph(dna.eyes):compactIrisGraph(dna.eyes,quality);
        m.shading=render::ShadingModel::Eye;
        m.humanProfile={dna.eyes.corneaIor,dna.eyes.tearFilm};
        break;
    case HumanSurface::Sclera:
        // The sclera is not pure white. Small blood-vessel absorption is
        // driven by authored DNA and must affect the actual PBR material.
        m=basic("VIVID sclera",{
            0.92f,
            std::clamp(0.89f-0.12f*dna.eyes.scleraRedness,0.0f,1.0f),
            std::clamp(0.86f-0.18f*dna.eyes.scleraRedness,0.0f,1.0f)},0.24f);
        break;
    case HumanSurface::Cornea:
        m=basic("VIVID cornea",{1.0f,1.0f,1.0f},0.04f);
        m.alpha=render::AlphaMode::Blend;
        m.baseColor.w=std::clamp(0.07f+0.09f*dna.eyes.tearFilm,0.01f,0.22f);
        m.castShadow=false;
        m.doubleSided=true;
        m.shading=render::ShadingModel::Eye;
        m.humanProfile={dna.eyes.corneaIor,dna.eyes.tearFilm};
        break;
    case HumanSurface::Hair:
        m=basic("VIVID groom",dna.groom.color,dna.groom.roughness);
        m.shaderGraph=quality==HumanMaterialQuality::High
            ?hairGraph(dna.groom):compactHairGraph(dna.groom,quality);
        m.shading=render::ShadingModel::Hair;
        m.humanProfile={0.65f+dna.groom.clump*0.30f,dna.groom.roughness};
        m.doubleSided=true;
        break;
    case HumanSurface::Eyelash:
        m=basic("VIVID eyelash",dna.groom.color,0.52f);
        m.doubleSided=true;
        m.shading=render::ShadingModel::Hair;
        m.humanProfile={0.35f,0.55f};
        break;
    case HumanSurface::Teeth:
        m=basic("VIVID teeth",{0.88f,0.83f,0.72f},0.25f);
        break;
    }
    if(quality==HumanMaterialQuality::Low &&
       (surface==HumanSurface::Skin || surface==HumanSurface::Iris ||
        surface==HumanSurface::Cornea || surface==HumanSurface::Hair ||
        surface==HumanSurface::Eyelash))
        m.shading=render::ShadingModel::Pbr;
    enforceGraphBudget(m,quality);
    return m;
}
render::Material humanMaterial(HumanSurface surface,const HumanDNA& dna) {
    return humanMaterial(surface,dna,HumanMaterialQuality::High);
}
render::Material humanMaterialFromSource(HumanSurface surface,
                                        const HumanDNA& dna,
                                        const render::Material& source,
                                        HumanMaterialQuality quality) {
    auto dst=humanMaterial(surface,dna,quality);
    // The imported texture array remains in the owning render::Model; its
    // integer indices are still valid after remapping the material factors.
    dst.baseTex=source.baseTex;
    dst.mrTex=source.mrTex;
    dst.normalTex=source.normalTex;
    dst.occlusionTex=source.occlusionTex;
    dst.emissiveTex=source.emissiveTex;
    dst.teamTex=source.teamTex;
    dst.normalScale=source.normalScale;
    dst.occlusionStrength=source.occlusionStrength;
    dst.uvScale=source.uvScale;
    dst.uvOffset=source.uvOffset;
    dst.uvRotation=source.uvRotation;
    dst.emissive=source.emissive;
    dst.emissiveStrength=source.emissiveStrength;
    dst.name=source.name;
    if(surface==HumanSurface::Hair || surface==HumanSurface::Eyelash) {
        dst.doubleSided=true;
        dst.alpha=source.alpha;   // preserve artist alpha-masked hair cards
        dst.alphaCutoff=source.alphaCutoff;
        dst.transparent=source.transparent;
        dst.baseColor.w=source.baseColor.w;
    }
    // Cornea always retains the DNA tear-film transparent material; imported
    // alpha modes do not turn the corneal shell into an opaque depth writer.
    return dst;
}

render::Material humanMaterialFromSource(HumanSurface surface,
                                        const HumanDNA& dna,
                                        const render::Material& source) {
    return humanMaterialFromSource(surface,dna,source,
                                   HumanMaterialQuality::High);
}
HumanSurfaceMaterials humanMaterials(const HumanDNA& dna,
                                     HumanMaterialQuality quality) {
    return {humanMaterial(HumanSurface::Skin,dna,quality),
            humanMaterial(HumanSurface::Iris,dna,quality),
            humanMaterial(HumanSurface::Sclera,dna,quality),
            humanMaterial(HumanSurface::Cornea,dna,quality),
            humanMaterial(HumanSurface::Hair,dna,quality),
            humanMaterial(HumanSurface::Eyelash,dna,quality),
            humanMaterial(HumanSurface::Teeth,dna,quality)};
}
HumanSurfaceMaterials humanMaterials(const HumanDNA& dna) {
    return humanMaterials(dna,HumanMaterialQuality::High);
}

static void appearanceImpl(render::Model& model,const HumanAppearance& look,int bakeSize) {
    // Validate the entire recipe and all mappings before modifying the candidate.
    (void)HumanAppearance::fromJson(look.toJson());
    for(const auto& [name,role]:look.materials)
        if(model.findMaterial(name)<0)throw std::invalid_argument("Appearance material missing: "+name);
    auto materials=model.materials;
    for(const auto& [name,role]:look.materials) {
        auto& m=materials[std::size_t(model.findMaterial(name))];
        const auto value=[&](const char* key){return look.get(key);};
        Nodes g;
        const auto input=g.add("input"),uv=g.add("uv");
        auto alpha=g.scalarOp("alpha",input),color=input;
        float rough=.65f;
        auto c=[&](float x){return g.constant(x);};
        auto mul=[&](std::string a,std::string b){return g.binary("multiply",a,b);};
        auto add=[&](std::string a,std::string b){return g.binary("add",a,b);};
        auto sub=[&](std::string a,std::string b){return g.binary("subtract",a,b);};
        auto mix=[&](std::string a,std::string b,std::string t){return g.ternary("mix",a,b,t);};
        auto rgb=[&](const char* a,const char* b,const char* d){return g.color({value(a),value(b),value(d)});};
        auto smooth=[&](float a,float b,std::string x){return g.ternary("smoothstep",c(a),c(b),x);};
        m.shading=render::ShadingModel::Pbr; // imported PBR, no extra fixed glint lobes
        m.metallic=0;
        if(role=="skin" || role=="cloth") {
            const bool skin=role=="skin";
            const std::string group=skin?"skin.":"cloth.";
            rough=look.get(group+"roughness");
            if(skin)rough=std::clamp(rough-value("skin.oiliness")*.2f-value("skin.wetness")*.3f,.15f,1.f);
            m.normalScale=look.get(group+"normal");
            auto luminance=g.binary("dot3",input,g.color({.2126f,.7152f,.0722f}));
            color=mix(luminance,input,c(look.get(group+"saturation")));
            color=mul(color,c(std::exp2(look.get(group+"exposure"))));
            if(!skin)color=mul(color,rgb("cloth.tintR","cloth.tintG","cloth.tintB"));
            if(skin) {
                m.occlusionStrength=value("skin.cavity");
                color=mix(g.color({.45f,.3f,.22f}),color,c(value("skin.texture")));
                color=add(mul(sub(color,c(.18f)),c(value("skin.contrast"))),c(.18f));
                color=mul(color,rgb("skin.tintR","skin.tintG","skin.tintB"));
                color=mul(color,g.color({1.f+value("skin.redness")*.4f,1.f-value("skin.redness")*.18f,1.f-value("skin.redness")*.2f}));
                const auto noise=g.scalarOp("gradientNoise",mul(uv,c(value("skin.freckleScale"))));
                const auto freckles=smooth(1.f-value("skin.freckleDensity")*.45f,1.f,noise);
                color=mul(color,sub(c(1),mul(freckles,c(value("skin.freckles")*value("skin.freckleTone")))));
                const auto pores=g.scalarOp("gradientNoise",mul(uv,c(value("skin.poreScale"))));
                color=mul(color,add(c(1),mul(sub(pores,c(.5f)),c(.12f*value("skin.pores")))));
                // Makeup layers: soft UV ellipses (mask 1 inside, 0 outside).
                auto ellipse=[&](float u,float v,float w,float h){
                    const auto d=g.scalarOp("length3",mul(sub(uv,g.vec(u,v,0,1)),g.vec(1.f/w,1.f/h,0,0)));
                    return sub(c(1),smooth(.55f,1.f,d));};
                const float fu=value("makeup.faceU");
                if(value("makeup.stubble")>0) {
                    const float sz=value("makeup.stubbleSize");
                    const auto dots=g.binary("step",c(1.f-value("makeup.stubbleDensity")*.5f),g.scalarOp("gradientNoise",mul(uv,c(900))));
                    color=mul(color,sub(c(1),mul(mul(dots,ellipse(fu,value("makeup.stubbleV"),sz*1.4f,sz)),c(value("makeup.stubble")*.65f))));
                }
                if(value("makeup.shadow")>0) {
                    const float off=value("makeup.shadowOffset"),v=value("makeup.shadowV"),w=value("makeup.shadowW"),h=value("makeup.shadowH");
                    const auto m2=g.scalarOp("clamp",add(ellipse(fu-off,v,w,h),ellipse(fu+off,v,w,h)));
                    color=mix(color,mul(color,rgb("makeup.shadowR","makeup.shadowG","makeup.shadowB")),mul(m2,c(value("makeup.shadow"))));
                }
                if(value("makeup.blush")>0) {
                    const float off=value("makeup.blushOffset"),v=value("makeup.blushV"),r=value("makeup.blushSize");
                    const auto m2=g.scalarOp("clamp",add(ellipse(fu-off,v,r,r),ellipse(fu+off,v,r,r)));
                    color=mix(color,mul(color,mul(rgb("makeup.blushR","makeup.blushG","makeup.blushB"),c(1.5f))),mul(m2,c(value("makeup.blush")*.6f)));
                }
                if(value("makeup.lips")>0)
                    color=mix(color,rgb("makeup.lipR","makeup.lipG","makeup.lipB"),mul(ellipse(fu,value("makeup.lipV"),value("makeup.lipW"),value("makeup.lipH")),c(value("makeup.lips")*.85f)));
            }
        } else if(role=="hair") {
            rough=value("hair.roughness");
            // Prepared companion atlas stores linear root position and strand seed
            // in sRGB-encoded R/G, coverage in A. No arbitrary UV-root guess.
            m.baseColor={1,1,1,m.baseColor.w};
            const auto root=g.scalarOp("red",input),seed=g.scalarOp("green",input);
            const float mel=value("hair.melanin"),red=value("hair.redness");
            const auto pigment=g.color({std::exp(-4.6f*mel),std::exp(-6.f*mel)*(1-.55f*red),std::exp(-7.f*mel)*(1-.75f*red)});
            color=mul(pigment,rgb("hair.dyeR","hair.dyeG","hair.dyeB"));
            color=mul(color,add(c(1.f-value("hair.variation")*.5f),mul(seed,c(value("hair.variation")))));
            const auto gray=g.binary("step",c(1.00001f-value("hair.gray")),seed);
            color=mix(color,c(value("hair.grayLightness")),gray);
            color=mul(color,sub(c(1),mul(sub(c(1),root),c(value("hair.rootDarkening")))));
            const auto tips=mul(smooth(value("hair.ombreStart"),value("hair.ombreStart")+value("hair.ombreSoftness"),root),c(value("hair.ombre")));
            color=mix(color,rgb("hair.tipR","hair.tipG","hair.tipB"),tips);
            const auto strands=g.binary("step",c(1.00001f-value("hair.highlights")),g.scalarOp("fract",mul(seed,c(7.13f))));
            color=mix(color,rgb("hair.tipR","hair.tipG","hair.tipB"),mul(strands,c(value("hair.highlightStrength"))));
            alpha=g.scalarOp("clamp",mul(alpha,c(value("hair.coverage"))));
            if(m.alpha==render::AlphaMode::Mask)m.alphaCutoff=value("hair.cutoff");
        } else if(role=="lashes") {
            rough=value("lashes.roughness");
            alpha=g.scalarOp("clamp",mul(alpha,c(value("lashes.coverage"))));
        } else if(role=="eyes") {
            rough=value("eyes.roughness");
            m.shading=render::ShadingModel::Eye;
            m.humanProfile={value("eyes.corneaIor"),value("eyes.tearFilm")};
            const bool right=name.find("Right")!=std::string::npos || name.find("right")!=std::string::npos;
            m.eyeOptics={value(right?"eyes.rightU":"eyes.leftU"),value("eyes.centerV"),value("eyes.irisSize"),value("eyes.depth")};
            const auto center=g.add("constant",{},vector4(m.eyeOptics.x,m.eyeOptics.y,0,1));
            const auto radius=g.scalarOp("length3",sub(uv,center));
            const float ir=value("eyes.irisSize"),pr=value("eyes.pupilSize");
            const auto iris=smooth(ir-.006f,ir+.006f,radius);
            const auto pupil=smooth(pr-.004f,pr+.004f,radius);
            const auto detail=g.binary("dot3",input,g.color({.2126f,.7152f,.0722f}));
            const auto texture=mix(c(1),g.ternary("clampRange",mul(detail,c(10)),c(.15f),c(2.2f)),c(value("eyes.irisTexture")));
            color=mul(rgb("eyes.irisR","eyes.irisG","eyes.irisB"),texture);
            color=mul(color,sub(c(1),mul(smooth(ir-value("eyes.limbusWidth"),ir,radius),c(value("eyes.limbus")))));
            const float light=value("eyes.scleraLightness"),red=value("eyes.redness");
            color=mix(color,g.color({light,light*(1-.3f*red),light*(1-.4f*red)}),iris);
            color=mix(c(.003f),color,pupil);
        }
        m.roughness=rough;
        m.shaderGraph=g.finish(g.binary("setAlpha",g.scalarOp("clamp",color),alpha),c(rough));
        if(bakeSize)detail::bakeMaterial(model,m,g.graph,bakeSize);
    }
    model.materials=std::move(materials);
    model.markMaterialChanged();
}

void applyHumanAppearance(render::Model& model,const HumanAppearance& look) {appearanceImpl(model,look,0);}
void bakeHumanAppearance(render::Model& model,const HumanAppearance& look,int size) {
    if(size<16||size>4096)throw std::invalid_argument("Bake resolution must be 16..4096");
    appearanceImpl(model,look,size);
}

void applyHumanBody(const render::Model& model,const HumanAppearance& look,
                    std::span<render::mat4> world) {
    if(world.size()!=model.nodes.size())throw std::invalid_argument("Body pose size mismatch");
    bool changed=false;
    for(const auto& f:appearanceFields)if(std::string_view(f.key).starts_with("body.")&&look.get(f.key)!=1.f)changed=true;
    if(!changed)return;
    const auto a=model.boundsMin,b=model.boundsMax;
    const float h=b.y-a.y;if(!(h>1e-5f))throw std::invalid_argument("Body bounds have no height");
    const float cx=(a.x+b.x)*.5f,cz=(a.z+b.z)*.5f;
    auto field=[&](render::float3 p) {
        const float y=std::clamp((p.y-a.y)/h,0.f,1.f);
        auto bell=[&](float center,float width){const float d=(y-center)/width;return std::exp(-d*d);};
        const float width=look.get("body.width")*(1+(look.get("body.hips")-1)*bell(.49f,.10f)
          +(look.get("body.waist")-1)*bell(.61f,.07f)+(look.get("body.chest")-1)*bell(.73f,.08f)
          +(look.get("body.shoulders")-1)*bell(.82f,.06f)+(look.get("body.head")-1)*bell(.94f,.06f));
        const float yp=(p.y-a.y)/h;
        const float shapedY=yp+(look.get("body.legs")-1)*std::min(yp,.52f)
          +(look.get("body.torso")-1)*std::clamp(yp-.52f,0.f,.34f);
        return render::float3{cx+(p.x-cx)*width,a.y+h*shapedY*look.get("body.height"),
          cz+(p.z-cz)*look.get("body.depth")*(1+(look.get("body.head")-1)*bell(.94f,.06f))};
    };
    // Warp only actual skin joints (not mesh owner nodes as well: that would
    // apply deformation twice). All duplicate clothing skeletons are included.
    std::vector<bool> joint(world.size(),false);
    for(const auto& skin:model.skins)for(int j:skin.joints)if(j>=0&&size_t(j)<joint.size())joint[size_t(j)]=true;
    const float eps=h*.0001f;
    for(size_t i=0;i<world.size();++i) {
        const auto& node=model.nodes[i];
        const bool rigid=node.mesh>=0 && node.skin<0 && size_t(node.mesh)<model.meshes.size();
        if(!joint[i]&&!rigid)continue;
        render::float3 local{};
        if(rigid) {
            const auto& mesh=model.meshes[size_t(node.mesh)];
            local={(mesh.boundsMin.x+mesh.boundsMax.x)*.5f,(mesh.boundsMin.y+mesh.boundsMax.y)*.5f,(mesh.boundsMin.z+mesh.boundsMax.z)*.5f};
        }
        auto& m=world[i];
        const render::float3 p{m.m[12]+m.m[0]*local.x+m.m[4]*local.y+m.m[8]*local.z,
            m.m[13]+m.m[1]*local.x+m.m[5]*local.y+m.m[9]*local.z,
            m.m[14]+m.m[2]*local.x+m.m[6]*local.y+m.m[10]*local.z};
        const auto q=field(p);
        for(int axis=0;axis<3;++axis) {
            const render::float3 d{m.m[axis*4],m.m[axis*4+1],m.m[axis*4+2]};
            const auto r=field({p.x+d.x*eps,p.y+d.y*eps,p.z+d.z*eps});
            m.m[axis*4]=(r.x-q.x)/eps;m.m[axis*4+1]=(r.y-q.y)/eps;m.m[axis*4+2]=(r.z-q.z)/eps;
        }
        m.m[12]=q.x-m.m[0]*local.x-m.m[4]*local.y-m.m[8]*local.z;
        m.m[13]=q.y-m.m[1]*local.x-m.m[5]*local.y-m.m[9]*local.z;
        m.m[14]=q.z-m.m[2]*local.x-m.m[6]*local.y-m.m[10]*local.z;
    }
}
} // namespace aiverse::vivid
