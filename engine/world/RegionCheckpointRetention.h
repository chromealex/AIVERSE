#pragma once
#include "RegionCheckpointManifest.h"
#include <filesystem>
#include <map>
#include <fstream>
#include <string_view>
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
struct RegionRetentionCleanup {
    bool ok=false;
    RegionRetentionPlan plan;
    std::vector<uint64_t> removed;
    uint64_t failedGeneration=0;
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
    // Caller owns the lifetime world lock and a quiescent save barrier.
    // Partial removal is reported; orphan files are deliberately retained.
    static RegionRetentionCleanup cleanup(const std::filesystem::path& dir,
        const RegionCheckpointManifest& active,RegionRetentionPolicy policy){
        RegionRetentionCleanup result;
        std::ifstream in(dir/"manifest.bin",std::ios::binary);
        std::vector<uint8_t> bytes(RegionCheckpointManifestCodec::size);
        in.read(reinterpret_cast<char*>(bytes.data()),bytes.size());
        RegionCheckpointManifest current;
        if(!in||in.peek()!=std::char_traits<char>::eof()||in.bad()||
           !RegionCheckpointManifestCodec::decode(bytes,current)||
           current.identity!=active.identity||current.generation!=active.generation||
           current.checkpointBytes!=active.checkpointBytes)return result;
        in.close();
        result.plan=plan(dir,current,policy);
        if(!result.plan.valid)return result;
        result.removed.reserve(result.plan.removeGenerations.size());
        for(auto gen:result.plan.removeGenerations){
            if(gen>=current.generation){result.failedGeneration=gen;return result;}
            RegionCheckpointManifest old;old.generation=gen;
            auto cp=dir/old.checkpointName(),wal=dir/old.walName();
            std::error_code ec;
            if(!std::filesystem::is_regular_file(std::filesystem::symlink_status(cp,ec))||ec||
               !std::filesystem::is_regular_file(std::filesystem::symlink_status(wal,ec))||ec){
                result.failedGeneration=gen;return result;
            }
            if(!std::filesystem::remove(wal,ec)||ec||
               !std::filesystem::remove(cp,ec)||ec){
                result.failedGeneration=gen;return result;
            }
            result.removed.push_back(gen);
        }
        result.ok=true;return result;
    }

};
}
