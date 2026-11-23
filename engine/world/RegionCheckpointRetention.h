#pragma once
#include "RegionCheckpointManifest.h"
#include <filesystem>
#include <map>
#include <charconv>
#include <limits>
namespace aiverse::world {
struct RegionRetentionPolicy {
    size_t keepPrevious=2;
    uint64_t maxBytes=0; // zero = unlimited; active generation always protected
};
struct RegionRetentionPlan {
    bool valid=false, overBudget=false;
    uint64_t retainedBytes=0;
    std::vector<uint64_t> removeGenerations;
};
class RegionCheckpointRetention {
public:
    // Run under world session lock. Only complete older pairs are eligible.
    // Orphans, symlinks, unknown names and future generations are never deleted.
    static RegionRetentionPlan plan(const std::filesystem::path& dir,
        const RegionCheckpointManifest& active,RegionRetentionPolicy policy){
        RegionRetentionPlan result;
        if(!active.valid())return result;
        struct Pair {bool checkpoint=false,wal=false;uint64_t bytes=0;};
        std::map<uint64_t,Pair> pairs;
        std::error_code ec;
        std::filesystem::directory_iterator it(dir,ec),end;
        for(;!ec&&it!=end;it.increment(ec)){
            auto status=it->symlink_status(ec);if(ec)break;
            if(!std::filesystem::is_regular_file(status))continue;
            const auto name=it->path().filename().string();
            bool cp=name.starts_with("checkpoint-");
            if(!cp&&!name.starts_with("wal-"))continue;
            if(!name.ends_with(".bin"))continue;
            auto digits=std::string_view(name).substr(cp?11:4);
            digits.remove_suffix(4);
            uint64_t gen=0;
            auto parsed=std::from_chars(digits.data(),digits.data()+digits.size(),gen);
            if(parsed.ec!=std::errc{}||parsed.ptr!=digits.data()+digits.size()||!gen||std::to_string(gen)!=digits)continue;
            auto size=it->file_size(ec);if(ec)break;
            auto& p=pairs[gen];
            if(size>UINT64_MAX-p.bytes||size>UINT64_MAX-result.retainedBytes)return {};
            p.bytes+=size;result.retainedBytes+=size;
            if(cp)p.checkpoint=true;else p.wal=true;
        }
        if(ec)return {};
        auto current=pairs.find(active.generation);
        if(current==pairs.end()||!current->second.checkpoint||!current->second.wal)return {};
        size_t previous=0;
        for(const auto& [gen,p]:pairs)if(gen<active.generation&&p.checkpoint&&p.wal)++previous;
        for(const auto& [gen,p]:pairs){
            if(gen>=active.generation||!p.checkpoint||!p.wal)continue;
            if(previous<=policy.keepPrevious&&(!policy.maxBytes||result.retainedBytes<=policy.maxBytes))break;
            // keepPrevious is a minimum backup count even under byte pressure.
            if(previous<=policy.keepPrevious)break;
            result.removeGenerations.push_back(gen);result.retainedBytes-=p.bytes;--previous;
        }
        result.overBudget=policy.maxBytes&&result.retainedBytes>policy.maxBytes;
        result.valid=true;return result;
    }
};
}
