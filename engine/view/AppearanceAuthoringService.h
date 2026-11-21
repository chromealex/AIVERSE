// Genre-neutral authored appearance resolver for native projects.
// No RTS/FPS/Editor rules, no animation clock, no GPU ownership. The
// caller supplies a project material library, texture-loader callback,
// animation-asset reader, and compile-time graph operations/traits.
#pragma once
#include "AppearancePacket.h"
#include "../render/AnimationAsset.h"
#include "../render/AnimationGraph.h"
#include "../render/graph/ShaderGraph.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace aiverse::view {

class AppearanceAuthoringService final {
public:
    using Warning=std::function<void(const std::string&)>;
    using TextureLoader=std::function<int(const std::string&)>;

    static Json resolveMaterial(const Json& library,const Json& ref,
                                const Warning& warn={},int depth=0) {
        if(depth>8) {
            if(warn)warn("materials: reference chain too deep (a loop of \"base\" / \"use\"?)");
            return Json::object();
        }
        const auto overlay=[](Json base,const Json& top,const char* skip) {
            for(const auto& [key,value]:top.members())
                if(key!=skip)base.set(key,value);
            return base;
        };
        if(ref.isString()) {
            const Json& entry=library[ref.str()];
            if(!entry.isObject()) {
                if(warn)warn("materials: unknown library material '"+ref.str()+"' (materials.json)");
                return Json::object();
            }
            return entry.has("base") ?
                overlay(resolveMaterial(library,entry["base"],warn,depth+1),
                        entry,"base") : entry;
        }
        if(!ref.isObject())return Json::object();
        if(ref.has("use"))
            return overlay(resolveMaterial(library,ref["use"],warn,depth+1),
                           ref,"use");
        return ref.has("base") ?
            overlay(resolveMaterial(library,ref["base"],warn,depth+1),
                    ref,"base") : ref;
    }

    static void applyMaterial(render::Material& mt,const Json& library,
                              const Json& ref,
                              const TextureLoader& loadTexture={},
                              const Warning& warn={}) {
        const Json o=resolveMaterial(library,ref,warn);
        const auto num=[](const Json& value,float fallback) {
            return value.isNumber()?float(value.toDouble(fallback)):fallback;
        };
        const auto vec3=[&](const Json& value,render::float3 fallback) {
            return value.size()>=3 ?
                render::float3{num(value[0],fallback.x),
                               num(value[1],fallback.y),
                               num(value[2],fallback.z)}:fallback;
        };
        const auto texture=[&](const std::string& source) {
            return loadTexture?loadTexture(source):-1;
        };
        if(o.getBool("replace")) {
            const auto name=mt.name;mt=render::Material{};mt.name=name;
        }
        const Json& col=o["color"];
        if(col.size()>=3)
            mt.baseColor={num(col[0],1),num(col[1],1),num(col[2],1),
                col.size()>3?num(col[3],1):mt.baseColor.w};
        if(o.has("shaderGraph")) {
            try {
                mt.shaderGraph=std::make_shared<render::graph::Program>(
                    render::graph::compile(o["shaderGraph"],"material"));
                for(size_t i=0;i<mt.shaderGraph->textures.size();++i)
                    mt.graphTextures[i]=texture(mt.shaderGraph->textures[i]);
            }catch(const std::exception& error) {
                if(warn)warn(error.what());
                mt.shaderGraph.reset();
            }
        }
        mt.metallic=num(o["metallic"],mt.metallic);
        mt.roughness=num(o["roughness"],mt.roughness);
        mt.thinTransmission=std::clamp(num(o["thinTransmission"],mt.thinTransmission),0.0f,1.0f);
        mt.shadowDiffusion=std::clamp(num(o["shadowDiffusion"],mt.shadowDiffusion),0.0f,1.0f);
        mt.sssStrength=std::clamp(num(o["sssStrength"],mt.sssStrength),0.0f,1.0f);
        mt.sssScale=std::clamp(num(o["sssScale"],mt.sssScale),0.0f,1.0f);
        mt.sssThickness=std::clamp(num(o["sssThickness"],mt.sssThickness),0.0f,1.0f);
        if(o.has("sssColor"))mt.sssColor=vec3(o["sssColor"],mt.sssColor);
        if(o.has("emissive"))mt.emissive=vec3(o["emissive"],mt.emissive);
        mt.emissiveStrength=num(o["emissiveStrength"],mt.emissiveStrength);
        if(o.has("alpha")) {
            const auto alpha=o.getStr("alpha");
            mt.alpha=alpha=="mask"?render::AlphaMode::Mask:
                     alpha=="blend"?render::AlphaMode::Blend:
                     render::AlphaMode::Opaque;
            if(alpha!="mask"&&alpha!="blend"&&alpha!="opaque"&&warn)
                warn("alpha must be opaque, mask or blend");
        }
        mt.alphaCutoff=num(o["cutoff"],mt.alphaCutoff);
        mt.doubleSided=o.getBool("doubleSided",mt.doubleSided);
        if(o.has("unlit"))
            mt.shading=o.getBool("unlit")?
                render::ShadingModel::Unlit:render::ShadingModel::Pbr;
        if(const Json& fixed=o["fixedLight"];fixed.size()==5) {
            render::float3 dir{num(fixed[0],0),num(fixed[1],1),
                               num(fixed[2],0)};
            const float length=render::length(dir);
            if(length>1e-6f) {
                dir=dir*(1.0f/length);
                mt.fixedLight={dir.x,dir.y,dir.z,1};
                mt.fixedShade={num(fixed[3],1),num(fixed[4],0)};
                mt.shading=render::ShadingModel::Unlit;
            }else if(warn)warn("fixedLight direction is zero");
        }else if(o.has("fixedLight")&&warn)
            warn("fixedLight is [x, y, z, ambient, diffuse]");
        mt.castShadow=o.getBool("castShadow",mt.castShadow);
        mt.transparent=o.getBool("transparent",mt.transparent);
        mt.receiveShadow=o.getBool("receiveShadow",mt.receiveShadow);
        mt.vertexColors=o.getBool("vertexColors",mt.vertexColors);
        if(o.has("team")) {
            const auto team=o.getStr("team");
            mt.team=team=="tint"?render::TeamMode::Tint:
                team=="mask"?render::TeamMode::Mask:
                team=="alpha"?render::TeamMode::BaseAlpha:
                team=="vertex"?render::TeamMode::VertexAlpha:
                render::TeamMode::None;
            if(team!="tint"&&team!="mask"&&team!="alpha"&&
               team!="vertex"&&team!="none"&&warn)
                warn("team must be tint, mask, alpha, vertex or none");
        }
        mt.teamStrength=num(o["teamStrength"],mt.teamStrength);
        if(o.has("teamMask")) {
            mt.teamTex=texture(o.getStr("teamMask"));
            if(mt.team==render::TeamMode::None ||
               mt.team==render::TeamMode::Tint)
                mt.team=render::TeamMode::Mask;
        }
        if(o.has("texture"))mt.baseTex=texture(o.getStr("texture"));
        if(o.has("normalMap"))mt.normalTex=texture(o.getStr("normalMap"));
        if(o.has("emissiveMap")) {
            mt.emissiveTex=texture(o.getStr("emissiveMap"));
            if(!o.has("emissive"))mt.emissive={1,1,1};
        }
        if(o.has("metallicRoughnessMap"))
            mt.mrTex=texture(o.getStr("metallicRoughnessMap"));
        if(o.has("occlusionMap"))
            mt.occlusionTex=texture(o.getStr("occlusionMap"));
        mt.occlusionStrength=num(o["occlusionStrength"],mt.occlusionStrength);
        mt.normalScale=num(o["normalScale"],mt.normalScale);
        if(const Json& uv=o["uvScale"];uv.size()==2)
            mt.uvScale={num(uv[0],1),num(uv[1],1)};
        if(const Json& uv=o["uvOffset"];uv.size()==2)
            mt.uvOffset={num(uv[0],0),num(uv[1],0)};
    }

    static std::shared_ptr<const render::AnimationGraph> compileGraph(
            const render::Model& model,const Json& authored,
            const render::FileReader& reader={},
            const render::AnimationOperations& operations={},
            const render::AnimationTraits& traits={},
            const render::AnimationGraphs& named={}) {
        Json definition=authored;
        if(authored.isString()) {
            const auto& source=authored.str();
            std::string text;
            if(!reader || !reader(source,text))
                throw std::runtime_error("animationGraph file not found: "+source);
            definition=render::parseAnimationAsset(text);
        }
        return render::AnimationGraph::compile(model,definition,
                                               operations,traits,named);
    }

    // The service creates only immutable programs; it never starts a
    // second AnimationGraphInstance/pose clock. The owning View uses
    // AppearanceSpec graph+parameters and advances it exactly once.
};

} // namespace aiverse::view
