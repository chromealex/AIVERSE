// Editor-owned, window-independent source watcher for visual assets.
// Runs only on the editor/presentation thread; no renderer or ECS state.
// Changing projects resets the watch baseline and every pending request.
#pragma once
#include "modules/rts/view/RtsModelSourceDependencies.h"
#include "modules/rts/view/RtsModelResourceCache.h"
#include "engine/render/Model.h"
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::editor {

class VisualAssetDependencyService final {
public:
    using Clock = std::filesystem::file_time_type;
    using Impact = game::view_adapter::ModelSourceDependencyIndex::Impact;
    using ModelSourceKind = game::view_adapter::ModelSourceKind;

    void reset() {
        project_.clear();
        elapsed_=0;
        stamps_.clear();
        converted_.clear();
        pending_.clear();
        dependencies_.clear();
    }

    // Return true once per scan interval, independent of editor-window
    // visibility. No GPU/device operations or file writes happen here.
    bool due(const std::string& project,double dt) {
        if(project_!=project) {
            reset();
            project_=project;
        }
        if(project.empty())return false;
        elapsed_+=dt;
        if(elapsed_<1.0)return false;
        elapsed_=0;
        return true;
    }

    template<class Types,class Convert,class Log>
    void scan(const std::string& project,const Types& types,
              const Json& materialLibrary,
              const game::view_adapter::RtsModelResourceCache& resources,
              Convert&& convert,Log&& log) {
        dependencies_.rebuild(types,materialLibrary);
        for(const auto& [key,model]:resources.models()) {
            if(key.rfind("shape:",0)==0)continue;
            const auto source=
                game::view_adapter::RtsModelResourceCache::sourceForKey(key);
            dependencies_.addResidentModel(source);
            if(model)for(const auto& img:model->images)
                if(!img.textureKey.empty())
                    dependencies_.addResidentTexture(source,img.textureKey);
        }

        std::set<std::string> observed;
        for(const auto& [file,entry]:dependencies_.sources()) {
            if(file.empty())continue;
            const std::filesystem::path path(file);
            if(!allowedRelative(path))continue;
            observed.insert(file);
            const auto src=std::filesystem::path(project)/path;
            std::error_code ec;
            const auto timestamp=std::filesystem::last_write_time(src,ec);
            const std::optional<Clock> stamp=
                ec?std::nullopt:std::optional<Clock>(timestamp);
            bool changed=false;
            auto [it,inserted]=stamps_.try_emplace(file,stamp);
            if(!inserted && it->second!=stamp) {
                it->second=stamp;
                changed=true; // includes deletion and missing-file recovery
                log("визуальный источник изменён: "+file);
            }
            if(stamp && entry.has(ModelSourceKind::Model) &&
               !render::isNativeModelFormat(file)) {
                const auto side=std::filesystem::path(src.string()+".glb");
                std::error_code sideError;
                const bool exists=std::filesystem::exists(side,sideError);
                const auto modified=exists?
                    std::filesystem::last_write_time(side,sideError):Clock{};
                if((!exists || sideError || modified<*stamp) &&
                   converted_[src.string()]!=stamp) {
                    converted_[src.string()]=stamp;
                    if(convert(src.string()))changed=true;
                }
            }
            if(changed)pending_.insert(file);
        }
        for(auto it=stamps_.begin();it!=stamps_.end();) {
            if(!observed.contains(it->first))it=stamps_.erase(it);
            else ++it;
        }
    }

    bool pending() const noexcept{return !pending_.empty();}
    const std::set<std::string>& changedSources() const noexcept {
        return pending_;
    }
    const game::view_adapter::ModelSourceDependencyIndex& index() const noexcept {
        return dependencies_;
    }
    Impact impact() const {return dependencies_.resolve(pending_);}
    void acknowledge() {pending_.clear();}

private:
    static bool allowedRelative(const std::filesystem::path& path) {
        if(path.empty() || path.is_absolute())return false;
        for(const auto& segment:path)
            if(segment==".." || segment==".")return false;
        return true;
    }

    std::string project_;
    double elapsed_=0;
    std::map<std::string,std::optional<Clock>> stamps_;
    std::map<std::string,std::optional<Clock>> converted_;
    std::set<std::string> pending_;
    game::view_adapter::ModelSourceDependencyIndex dependencies_;
};

} // namespace aiverse::editor
