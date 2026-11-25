#pragma once
#include "UnifiedBackupCatalog.h"
#include "RegionCheckpointRetention.h"
#include <set>

namespace aiverse::world {
class UnifiedCheckpointRetention {
    static bool matchesActive(const std::filesystem::path& dir,const RegionCheckpointManifest& active){
        std::error_code ec;
        const auto path=dir/"manifest.bin";
        if(!active.valid()||!std::filesystem::is_regular_file(std::filesystem::symlink_status(path,ec))||ec)return false;
        std::ifstream in(path,std::ios::binary);
        std::vector<uint8_t> bytes(RegionCheckpointManifestCodec::size);
        in.read(reinterpret_cast<char*>(bytes.data()),bytes.size());
        RegionCheckpointManifest current;
        return in&&in.peek()==std::char_traits<char>::eof()&&!in.bad()&&
            RegionCheckpointManifestCodec::decode(bytes,current)&&current.generation==active.generation&&
            current.identity==active.identity&&current.checkpointBytes==active.checkpointBytes;
    }
    static bool eligibleGenerations(const std::filesystem::path& dir,
        const RegionCheckpointManifest& active,size_t budget,
        const UnifiedCheckpointSections::Adapters& adapters,const UnifiedCheckpointSections::Factory& factory,
        const DurableComponentCodecs& codecs,const UnifiedWorldReplay::Materializer& materialize,
        const UnifiedWorldReplay::Validator& validate,const DurableResourceTransaction::Policy& resources,std::set<uint64_t>& out){
        if(!matchesActive(dir,active))return false;
        const auto catalog=UnifiedBackupCatalog::list(dir,active,budget,adapters,factory,codecs,materialize,validate,true,resources);
        if(!catalog.complete)return false;
        bool activeRecoverable=false;
        std::set<uint64_t> staged;
        for(const auto& entry:catalog.entries){
            if(entry.status!=UnifiedBackupStatus::Recoverable)continue;
            if(entry.active){activeRecoverable=true;continue;}
            // An older generation cannot include changes beyond the current checkpoint.
            // Never prune divergent history, orphan/future generations or damaged pairs.
            if(entry.generation<active.generation&&entry.lastSequence<=active.identity.sequence&&
               entry.lastRevision<=active.identity.revision)staged.insert(entry.generation);
        }
        if(!activeRecoverable||!matchesActive(dir,active))return false;
        out.swap(staged);return true;
    }
public:
    // Caller owns the lifetime world lock and a quiescent barrier. Verification
    // is deliberately expensive, and never modifies the source or live state.
    static RegionRetentionPlan plan(const std::filesystem::path& dir,const RegionCheckpointManifest& active,
        RegionRetentionPolicy policy,size_t budget,const UnifiedCheckpointSections::Adapters& adapters,
        const UnifiedCheckpointSections::Factory& factory,const DurableComponentCodecs& codecs,
        const UnifiedWorldReplay::Materializer& materialize,const UnifiedWorldReplay::Validator& validate={},const DurableResourceTransaction::Policy& resources={}){
        std::set<uint64_t> eligible;
        if(!eligibleGenerations(dir,active,budget,adapters,factory,codecs,materialize,validate,resources,eligible))return {};
        return RegionCheckpointRetention::plan(dir,active,policy,[&](uint64_t gen){return eligible.contains(gen);});
    }
    static RegionRetentionCleanup cleanup(const std::filesystem::path& dir,const RegionCheckpointManifest& active,
        RegionRetentionPolicy policy,size_t budget,const UnifiedCheckpointSections::Adapters& adapters,
        const UnifiedCheckpointSections::Factory& factory,const DurableComponentCodecs& codecs,
        const UnifiedWorldReplay::Materializer& materialize,const UnifiedWorldReplay::Validator& validate={},const DurableResourceTransaction::Policy& resources={}){
        std::set<uint64_t> eligible;
        if(!eligibleGenerations(dir,active,budget,adapters,factory,codecs,materialize,validate,resources,eligible))return {};
        return RegionCheckpointRetention::cleanup(dir,active,policy,[&](uint64_t gen){return eligible.contains(gen);});
    }
};
}
