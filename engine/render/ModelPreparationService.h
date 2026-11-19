// Generic, per-host asynchronous model preparation service.
// Worker tasks prepare CPU model data only; callers publish/upload on render thread.
#pragma once
#include "Model.h"
#include <algorithm>
#include <chrono>
#include <future>
#include <memory>
#include <string>
#include <utility>
#include <vector>
namespace aiverse::render {
class ModelPreparationService final {
public:
    struct PreparedModel {
        std::unique_ptr<render::Model> model;
        std::vector<std::string> warnings;
    };
    using Path=std::pair<std::string,std::string>;
    ~ModelPreparationService() {join();}
    ModelPreparationService()=default;
    ModelPreparationService(const ModelPreparationService&)=delete;
    ModelPreparationService& operator=(const ModelPreparationService&)=delete;
    void join() {if(pending_.valid())pending_.wait();}
    void reset() {
        join();
        pending_={};type_=-1;paths_.clear();cursor_=0;resource_.clear();
    }
    void begin(int type,bool allVariants) {
        reset();type_=type;allVariants_=allVariants;
    }
    bool matches(int type,bool allVariants) const noexcept {
        return type_==type && allVariants_==allVariants;
    }
    bool busy() const noexcept {return pending_.valid();}
    bool active() const noexcept {return type_>=0 || busy();}
    int type() const noexcept {return type_;}
    size_t cursor() const noexcept {return cursor_;}
    size_t size() const noexcept {return paths_.size();}
    float progress() const noexcept {
        return paths_.empty()?0.f:float(cursor_)/float(paths_.size());
    }
    const std::string& resource() const noexcept {return resource_;}
    const std::vector<Path>& paths() const noexcept {return paths_;}
    bool appendPath(const std::string& path,const std::string& key) {
        if(path.empty())return false;
        const Path value{path,key};
        if(std::find(paths_.begin(),paths_.end(),value)!=paths_.end())return false;
        paths_.push_back(value);return true;
    }
    const Path& current() const {return paths_.at(cursor_);}
    void advance() {++cursor_;}
    bool hasRemaining() const noexcept {return cursor_<paths_.size();}
    template<class Work>
    void launch(std::string resource,Work&& work) {
        join();resource_=std::move(resource);
        pending_=std::async(std::launch::async,std::forward<Work>(work));
    }
    bool ready() const {
        return pending_.valid() &&
            pending_.wait_for(std::chrono::seconds(0))==std::future_status::ready;
    }
    PreparedModel take() {return pending_.get();}
    void clearResource() {resource_.clear();}
private:
    int type_=-1;bool allVariants_=false;
    std::vector<Path> paths_;
    size_t cursor_=0;
    std::string resource_;
    std::future<PreparedModel> pending_;
};
} // namespace aiverse::render
