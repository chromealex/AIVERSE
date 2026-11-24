#pragma once
#include "UnifiedBackupRead.h"
#include <algorithm>
#include <charconv>
#include <string_view>

namespace aiverse::world {
enum class UnifiedBackupStatus { MetadataOnly, Recoverable, InvalidMetadata, WrongWorld, MissingCheckpoint, RecoveryFailed };
struct UnifiedBackupEntry {
    uint64_t generation=0;
    bool active=false;
    uint64_t lastSequence=0,lastRevision=0; // valid only when Recoverable
    RegionCheckpointManifest manifest;
    UnifiedBackupStatus status=UnifiedBackupStatus::InvalidMetadata;
};
struct UnifiedBackupCatalogResult {
    bool complete=false;
    std::vector<UnifiedBackupEntry> entries;
};

class UnifiedBackupCatalog {
public:
    // Caller holds the lifetime world lock and a quiescent save barrier.
    // MetadataOnly is not evidence of recoverability. Full verification stages
    // private state; adapters and materializers must not publish side effects.
    static UnifiedBackupCatalogResult list(const std::filesystem::path& dir,
        const RegionCheckpointManifest& active,size_t budget,
        const UnifiedCheckpointSections::Adapters& adapters,
        const UnifiedCheckpointSections::Factory& factory,
        const DurableComponentCodecs& codecs,const UnifiedWorldReplay::Materializer& materialize,
        const UnifiedWorldReplay::Validator& validate={},bool verify=false,const DurableResourceTransaction::Policy& resources={}){
        UnifiedBackupCatalogResult result;
        if(!active.valid())return result;
        std::error_code ec;
        std::filesystem::directory_iterator it(dir,ec),end;
        for(;!ec&&it!=end;it.increment(ec)){
            const auto status=it->symlink_status(ec);
            if(ec)break;
            if(!std::filesystem::is_regular_file(status))continue;
            const auto name=it->path().filename().string();
            if(!name.starts_with("wal-")||!name.ends_with(".bin"))continue;
            auto digits=std::string_view(name).substr(4);digits.remove_suffix(4);
            uint64_t generation=0;
            auto parsed=std::from_chars(digits.data(),digits.data()+digits.size(),generation);
            if(parsed.ec!=std::errc{}||parsed.ptr!=digits.data()+digits.size()||
               !generation||std::to_string(generation)!=digits)continue;
            UnifiedBackupEntry entry;
            entry.generation=generation;entry.active=generation==active.generation;
            std::ifstream in(it->path(),std::ios::binary);
            std::vector<uint8_t> prefix(4+RegionCheckpointManifestCodec::size);
            in.read(reinterpret_cast<char*>(prefix.data()),prefix.size());
            auto& m=entry.manifest;
            if(!in||prefix[0]!='U'||prefix[1]!='W'||prefix[2]!='T'||prefix[3]!='1'||
               !RegionCheckpointManifestCodec::decode(std::span<const uint8_t>(prefix).subspan(4),m)||
               m.generation!=generation){
                entry.status=UnifiedBackupStatus::InvalidMetadata;
            }else if(m.identity.world!=active.identity.world){
                entry.status=UnifiedBackupStatus::WrongWorld;
            }else if(entry.active&&(m.identity!=active.identity||m.checkpointBytes!=active.checkpointBytes)){
                entry.status=UnifiedBackupStatus::InvalidMetadata;
            }else{
                std::error_code cpError;
                auto cp=dir/m.checkpointName();
                auto cpStatus=std::filesystem::symlink_status(cp,cpError);
                if(cpError||!std::filesystem::is_regular_file(cpStatus)){
                    entry.status=UnifiedBackupStatus::MissingCheckpoint;
                }else{
                    const auto bytes=std::filesystem::file_size(cp,cpError);
                    if(cpError||bytes!=m.checkpointBytes){
                        entry.status=UnifiedBackupStatus::InvalidMetadata;
                    }else if(!verify){
                        entry.status=UnifiedBackupStatus::MetadataOnly;
                    }else{
                        std::unique_ptr<UnifiedWorldReplay::State> recovered;
                        entry.status=UnifiedBackupRead::recover(dir,generation,active.identity.world,
                            budget,adapters,factory,codecs,materialize,recovered,validate,resources)
                            ?UnifiedBackupStatus::Recoverable:UnifiedBackupStatus::RecoveryFailed;
                        if(entry.status==UnifiedBackupStatus::Recoverable){
                            entry.lastSequence=recovered->sequence;
                            entry.lastRevision=recovered->ledger.lastRevision();
                        }
                    }
                }
            }
            result.entries.push_back(std::move(entry));
        }
        std::sort(result.entries.begin(),result.entries.end(),[](const auto& a,const auto& b){
            return a.generation>b.generation;
        });
        result.complete=!ec;return result;
    }
};
}
