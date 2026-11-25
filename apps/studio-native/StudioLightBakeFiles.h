// Keep a completed bake visible until the replacement header is ready.
// The worker stages output beside the active map. The host publishes after
// checking that the map and job still match.
#pragma once
#include "apps/editor/core/ProjectDocumentJournal.h"
#include <chrono>
#include <map>

namespace aiverse::studio {
class StudioLightBakeFiles {
    using Path = std::filesystem::path;
public:
    StudioLightBakeFiles(const Path& project,const std::string& folder)
        :root_(std::filesystem::canonical(project)),folder_(folder) {
        const auto output=target(folder);
        if (output.extension()!=".bake") throw std::runtime_error("Invalid light bake folder");
        baseline_=previousHeader();
        const auto nonce=std::chrono::steady_clock::now().time_since_epoch().count();
        staging_=output.parent_path()/(".studio-lightbake-"+std::to_string(nonce));
        if (!std::filesystem::create_directory(staging_)) throw std::runtime_error("Cannot stage light bake");
    }
    ~StudioLightBakeFiles() {
        std::error_code ec;
        std::filesystem::remove_all(staging_,ec);
    }
    StudioLightBakeFiles(const StudioLightBakeFiles&)=delete;
    bool read(const std::string& name,std::string& bytes) const {
        if (!name.starts_with("content/")) return false;
        const auto file=target(name);
        std::ifstream in(file,std::ios::binary|std::ios::ate);
        if (!in) return false;
        const auto size=in.tellg();
        if (size<0 || size>256*1024*1024) return false;
        bytes.resize(size_t(size));
        in.seekg(0);
        return bool(in.read(bytes.data(),size));
    }
    bool stage(const std::string& name,const std::string& bytes) {
        (void)target(name);
        const Path rel(name);
        const bool sidecar=name.starts_with("content/") && rel.extension()==".lmuv";
        const bool payload=rel.parent_path()==folder_ && (rel.extension()==".png" || rel.filename()=="probes.bin");
        if (rel.parent_path()==folder_ && rel.filename()=="lightmap.json") {
            header_=Json::parse(bytes);
            return true;
        }
        if (!sidecar && !payload) return false;
        const auto staged=staging_/std::to_string(files_.size());
        editor::project_file_detail::write(staged,bytes);
        const auto key=rel.filename().string();
        if (payload) names_[key]=editor::project_file_detail::digest(bytes)+"-"+key;
        files_.push_back({staged,sidecar?rel:folder_/names_.at(key)});
        return true;
    }
    void publishRecoveredUvs() {
        if(baseline_.empty() || header_.isObject())throw std::runtime_error("Missing unchanged bake header");
        const auto output=target(folder_.generic_string());
        editor::project_file_detail::Lock lock(output/".studio-publish.lock");
        requireBaseline();
        for(const auto& [staged,relative]:files_) {
            if(relative.extension()!=".lmuv")throw std::runtime_error("Unexpected output for unchanged bake");
            editor::project_file_detail::replace(staged,target(relative.generic_string()));
        }
        // The header timestamp tells the Scene to reload recovered model UVs.
        const auto stagedHeader=staging_/"lightmap.json";
        editor::project_file_detail::write(stagedHeader,baseline_);
        editor::project_file_detail::replace(stagedHeader,target((folder_/"lightmap.json").generic_string()));
    }
    void publish() {
        if (!header_.isObject()) throw std::runtime_error("Missing completed light bake header");
        auto renameField=[&](Json& object,const char* field) {
            if(object.has(field)) object.set(field,Json::string(names_.at(object.getStr(field))));
        };
        auto index=header_;
        auto& pages=index.member("pages");
        for(size_t i=0;i<pages.size();++i) {
            renameField(pages.at(i),"lightmap");
            renameField(pages.at(i),"mask");
        }
        if(index.has("probes"))renameField(index.member("probes"),"file");
        const auto stagedHeader=staging_/"lightmap.json";
        editor::project_file_detail::write(stagedHeader,index.dump(2)+"\n");
        const auto output=target(folder_.generic_string());
        editor::project_file_detail::directory(output);
        editor::project_file_detail::Lock lock(output/".studio-publish.lock");
        requireBaseline();
        for(const auto& [staged,relative]:files_)
            editor::project_file_detail::replace(staged,target(relative.generic_string()));
        editor::project_file_detail::replace(stagedHeader,target((folder_/"lightmap.json").generic_string()));
        if (!baseline_.empty()) {
            Json previous;
            try { previous=Json::parse(baseline_); } catch(const std::exception&) { return; }
            auto retire=[&](const std::string& name){
                if(name.size()<70 || name[64]!='-' || name.substr(0,64).find_first_not_of("0123456789abcdef")!=std::string::npos ||
                   Path(name).filename()!=Path(name) || name.find_first_of("\\:")!=std::string::npos)return;
                for(const auto& [_,active]:names_)if(active==name)return;
                std::error_code ec;
                std::filesystem::remove(output/name,ec);
            };
            for(const auto& page:previous["pages"].items()){
                retire(page.getStr("lightmap"));retire(page.getStr("mask"));
            }
            retire(previous["probes"].getStr("file"));
        }
    }
private:
    void requireBaseline() const {
        if(previousHeader()!=baseline_)throw std::runtime_error("Baked output changed during calculation");
    }
    std::string previousHeader() const {
        const auto path=target((folder_/"lightmap.json").generic_string());
        if(!std::filesystem::exists(path))return {};
        std::ifstream in(path,std::ios::binary);
        if(!in)throw std::runtime_error("Cannot read previous bake header");
        return {std::istreambuf_iterator<char>(in),{}};
    }
    std::string baseline_;
    Path target(const std::string& name) const {
        const Path relative(name);
        if(name.empty() || name.find_first_of("\\:")!=std::string::npos || relative.is_absolute() || relative.has_root_name())
            throw std::runtime_error("Invalid light bake path");
        auto p=root_;
        for(const auto& part:relative) {
            if(part.empty() || part=="." || part=="..") throw std::runtime_error("Invalid light bake path");
            p/=part;
            if(std::filesystem::is_symlink(std::filesystem::symlink_status(p)))
                throw std::runtime_error("Symlink in light bake path");
        }
        return p;
    }
    Path root_,folder_,staging_;
    Json header_;
    std::map<std::string,std::string> names_;
    std::vector<std::pair<Path,Path>> files_;
};
}
