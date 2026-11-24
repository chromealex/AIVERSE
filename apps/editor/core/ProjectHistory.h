// Project document history, independent of map snapshots and UI.
// Stores only bounded, owner-generated exact file plans (including dependencies).
#pragma once
#include "ProjectFileEdits.h"
#include <algorithm>
#include <cstdint>
#include <cstddef>
#include <vector>
#include <string>

namespace aiverse::editor {
class ProjectHistory {
public:
    static constexpr size_t MaxSteps=64, MaxBytes=16u*1024u*1024u;
    struct Step { std::string label; std::vector<ProjectFileEdit> files; size_t bytes=0; };
    const std::vector<Step>& steps() const noexcept {return steps_;}
    size_t position() const noexcept {return position_;}
    size_t bytes() const noexcept {return bytes_;}
    uint64_t version() const noexcept {return version_;}
    bool canUndo() const noexcept{return position_>0;}
    bool canRedo() const noexcept{return position_<steps_.size();}
    const Step& next(bool redo) const {
        if(redo?!canRedo():!canUndo())throw std::runtime_error(redo?"No project edit to redo":"No project edit to undo");
        return steps_[redo?position_:position_-1];
    }
    void advance(bool redo) noexcept {if(redo)++position_;else --position_;++version_;}
    void reset() noexcept {steps_.clear();position_=bytes_=0;++version_;}
    // Called only after the owner has successfully published the plan.
    // An untracked write or oversized step is a history barrier, never a gap.
    void record(std::vector<ProjectFileEdit>&& files,const std::string& label) noexcept {
        if(std::none_of(files.begin(),files.end(),[](const auto& f){return f.before!=f.after;}))return;
        if(label.empty()){reset();return;}
        try {
            size_t n=sizeof(Step)+std::min<size_t>(label.size(),256);
            auto add=[&](size_t size){if(size>MaxBytes-n)throw std::length_error("Project history budget");n+=size;};
            for(const auto& f:files){add(sizeof(ProjectFileEdit));add(f.path.native().size()*sizeof(std::filesystem::path::value_type));add(f.before.size());add(f.after.size());}
            while(steps_.size()>position_){bytes_-=steps_.back().bytes;steps_.pop_back();}
            while(!steps_.empty()&&(steps_.size()>=MaxSteps||n>MaxBytes-bytes_)){
                bytes_-=steps_.front().bytes;steps_.erase(steps_.begin());--position_;
            }
            steps_.push_back({label.substr(0,256),std::move(files),n});
            bytes_+=n;position_=steps_.size();++version_;
        }catch(...){reset();}
    }
private:
    std::vector<Step> steps_;
    size_t position_=0,bytes_=0;
    uint64_t version_=0;
};
} // namespace aiverse::editor
