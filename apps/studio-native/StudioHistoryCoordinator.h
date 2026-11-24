// Pure coordinator planning: exact owner suffixes, no implicit jumps.
// Existing-file transactions merge consecutive byte transitions, never overwrite
// an inconsistent intermediate state.
#pragma once
#include "apps/editor/core/ProjectFileEdits.h"
#include "apps/studio-native/StudioModelRepairBytes.h"
#include <map>
#include <set>
#include <array>
#include <algorithm>
namespace aiverse::studio {
struct HistoryBatchStep {std::string scope,id;};
struct HistoryBatchOwner {std::vector<std::string> ids;size_t position=0;};
inline std::vector<HistoryBatchStep> planHistoryBatch(const std::vector<HistoryBatchStep>& chronological,
    const std::map<std::string,HistoryBatchOwner>& owners,bool redo){
    if(chronological.empty()||chronological.size()>64)throw std::runtime_error("Select 1–64 tracked agent history steps");
    auto cursors=owners;auto ordered=chronological;
    if(!redo)std::reverse(ordered.begin(),ordered.end());
    std::set<std::pair<std::string,std::string>> unique;
    for(const auto& step:ordered){
        auto it=cursors.find(step.scope);
        if(it==cursors.end()||step.id.empty()||!unique.emplace(step.scope,step.id).second)
            throw std::runtime_error("Untracked or duplicate history step");
        auto& owner=it->second;
        if(owner.position>owner.ids.size()||(redo?owner.position==owner.ids.size():owner.position==0))
            throw std::runtime_error("History boundary unavailable");
        const auto index=redo?owner.position:owner.position-1;
        if(owner.ids[index]!=step.id)throw std::runtime_error("History contains intervening or evicted edits in "+step.scope);
        if(redo)++owner.position;else --owner.position;
    }
    return ordered;
}
class HistoryFileBatch {
    std::map<std::filesystem::path,editor::ProjectFileEdit> files_;
public:
    void append(const std::vector<editor::ProjectFileEdit>& edits){
        for(const auto& edit:edits){
            const auto path=std::filesystem::canonical(edit.path);
            auto it=files_.find(path);
            if(it==files_.end()){
                auto copy=edit;copy.path=path;files_.emplace(path,std::move(copy));
            }else{
                if(it->second.after!=edit.before||it->second.tailDigest!=edit.tailDigest||it->second.tailBytes!=edit.tailBytes)
                    throw std::runtime_error("Inconsistent history file chain: "+path.string());
                it->second.after=edit.after;
            }
        }
    }
    std::vector<editor::ProjectFileEdit> plan()const{
        std::vector<editor::ProjectFileEdit> result;
        for(const auto& [path,edit]:files_){
            if(!studioResourceMatches(path,edit))throw std::runtime_error("External file change: "+path.string());
            result.push_back(edit);
        }
        return result;
    }
};
}
