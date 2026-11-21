// Shared appearance-choice policy for scene/model authoring in any genre.
// The plan contains CPU-only authored data; the host resolves GPU textures.
#pragma once
#include "AppearanceAuthoringService.h"
#include <algorithm>
#include <cctype>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::view {

struct AppearanceChoicePlan final {
    struct ExtraMaterial {
        std::string name;
        Json reference;
    };
    // Multiple material layers preserve authored "*" then named override order
    // (including "replace" semantics), without merging those distinct passes.
    std::vector<std::vector<Json>> materialLayers;
    std::vector<ExtraMaterial> extraMaterials;
    std::vector<int> nodeMaterial;
    int variant=-1;
};

class AppearanceChoiceAuthoring final {
public:
    using Warning=AppearanceAuthoringService::Warning;

    static bool globMatchCi(const std::string& pattern,const std::string& name) {
        size_t p=0,t=0,star=std::string::npos,mark=0;
        auto eq=[](char a,char b){
            return std::tolower(static_cast<unsigned char>(a))==
                   std::tolower(static_cast<unsigned char>(b));
        };
        while(t<name.size()) {
            if(p<pattern.size() && (pattern[p]=='?' || eq(pattern[p],name[t]))) {
                ++p;++t;
            } else if(p<pattern.size() && pattern[p]=='*') {
                star=p++;mark=t;
            } else if(star!=std::string::npos) {
                p=star+1;t=++mark;
            } else return false;
        }
        while(p<pattern.size()&&pattern[p]=='*')++p;
        return p==pattern.size();
    }

    static AppearanceChoicePlan resolve(const render::Model& model,
                                         const Json& authored,
                                         const Warning& warn={}) {
        AppearanceChoicePlan plan;
        plan.materialLayers.resize(model.materials.size());
        const Json& overrides=authored["materials"];
        const Json& all=overrides["*"];
        if(all.isObject() || all.isString()) {
            for(auto& layers:plan.materialLayers)layers.push_back(all);
        }
        for(const auto& [name,reference]:overrides.members()) {
            if(name=="*")continue;
            const int index=model.findMaterial(name);
            if(index<0) {
                if(warn)warn("materials: '"+name+"' not found in "+model.path);
                continue;
            }
            plan.materialLayers[size_t(index)].push_back(reference);
        }

        for(const auto& [pattern,reference]:authored["assign"].members()) {
            const int extraIndex=int(model.materials.size()+plan.extraMaterials.size());
            bool matched=false;
            // Descendants inherit the nearest/later authored node assignment.
            // A visited bit prevents broken imported parent/child cycles.
            std::vector<uint8_t> visited(model.nodes.size(),0);
            std::vector<int> stack;
            for(size_t i=0;i<model.nodes.size();++i)
                if(globMatchCi(pattern,model.nodes[i].name))
                    stack.push_back(int(i));
            while(!stack.empty()) {
                const int n=stack.back();stack.pop_back();
                if(n<0 || size_t(n)>=model.nodes.size() || visited[size_t(n)])
                    continue;
                visited[size_t(n)]=1;
                for(int child:model.nodes[size_t(n)].children)
                    stack.push_back(child);
                if(model.nodes[size_t(n)].mesh<0)continue;
                if(plan.nodeMaterial.empty())
                    plan.nodeMaterial.assign(model.nodes.size(),-1);
                plan.nodeMaterial[size_t(n)]=extraIndex;
                matched=true;
            }
            if(matched)
                plan.extraMaterials.push_back({
                    reference.isString()?reference.str():pattern,reference});
            else if(warn)
                warn("assign: no node (with meshes under it) matches '"+
                     pattern+"' in "+model.path);
        }
        if(const auto variant=authored.getStr("variant");!variant.empty()) {
            plan.variant=model.findVariant(variant);
            if(plan.variant<0 && warn)
                warn("variant '"+variant+"' not found in "+model.path);
        }
        return plan;
    }
};

} // namespace aiverse::view
