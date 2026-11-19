// Model streaming operations shared by native presentation hosts.
// The host chooses which authored asset tree to discover and provides
// a reader. Worker preparation touches CPU model data only.
#pragma once
#include "engine/data/Json.h"
#include "engine/render/Model.h"
#include "engine/render/Geometry.h"
#include "engine/render/Animation.h"
#include "RtsModelPreparationService.h"
#include <exception>
#include <functional>
#include <string>
#include <utility>

namespace aiverse::game::view_adapter {

template<class AlreadyCached,class Append>
void discoverModelPaths(const Json& source,bool geometry,
                        AlreadyCached&& cached,Append&& append) {
    const auto add=[&](const std::string& path) {
        if(path.empty())return;
        const std::string key=path+(geometry?"#geometry":"");
        if(!cached(key))append(path,key);
    };
    // Visit nested model descriptions, attachments and LOD/stage objects
    // in authored order; the provider/queue handles duplicate keys.
    std::function<void(const Json&)> visit=[&](const Json& value) {
        if(value.isObject()) {
            for(const auto& [name,child]:value.members()) {
                if(name=="model") {
                    if(child.isString())add(child.str());
                    for(const auto& choice:child.items())
                        if(choice.isString())add(choice.str());
                        else add(choice.getStr("file"));
                }
                visit(child);
            }
        } else {
            for(const auto& child:value.items())visit(child);
        }
    };
    visit(source);
}

struct ModelCpuOptions {
    bool geometry=false;
    bool animationBake=false;
};

inline RtsModelPreparationService::PreparedModel prepareModelCpu(
        const std::string& path,const render::FileReader& reader,
        const std::shared_ptr<render::ModelTextureCache>& textures,
        ModelCpuOptions options) {
    RtsModelPreparationService::PreparedModel result;
    try {
        result.model=render::loadModel(path,reader,&result.warnings,textures);
        if(result.model && options.geometry)
            render::prepareGeometry(*result.model);
        if(result.model && options.animationBake)
            render::prepareAnimationBake(*result.model);
        if(result.model)render::prepareModelUpload(*result.model);
    } catch(const std::exception& e) {
        result.warnings.push_back(e.what());
        result.model.reset();
    }
    return result;
}

} // namespace aiverse::game::view_adapter
