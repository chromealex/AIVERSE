#pragma once
#include "RegionCheckpointBackup.h"
#include <charconv>
#include <string_view>
namespace aiverse::world {
struct RegionBackupEntry {
    uint64_t generation=0;
    bool active=false;
    RegionCheckpointLoadResult recovery;
};
struct RegionBackupCatalogResult {
    bool complete=false;
    std::vector<RegionBackupEntry> entries;
};
class RegionBackupCatalog {
public:
    // Caller holds world session lock. Full validation is opt-in and bounded
    // per checkpoint; metadata-only listing does not certify recoverability.
    static RegionBackupCatalogResult list(const std::filesystem::path& dir,
        const RegionCheckpointManifest& active,uint64_t maxBytes,bool validate=false){
        RegionBackupCatalogResult result;
        std::error_code ec;
        std::filesystem::directory_iterator it(dir,ec),end;
        for(;!ec&&it!=end;it.increment(ec)){
            if(!std::filesystem::is_regular_file(it->symlink_status(ec))){if(ec)break;continue;}
            auto name=it->path().filename().string();
            if(!name.starts_with("wal-")||!name.ends_with(".bin"))continue;
            auto digits=std::string_view(name).substr(4);digits.remove_suffix(4);
            uint64_t gen=0;
            auto parsed=std::from_chars(digits.data(),digits.data()+digits.size(),gen);
            if(parsed.ec!=std::errc{}||parsed.ptr!=digits.data()+digits.size()||!gen||std::to_string(gen)!=digits)continue;
            RegionBackupEntry entry;entry.generation=gen;entry.active=gen==active.generation;
            if(validate){
                RegionEntityLedger staged;
                entry.recovery=RegionCheckpointBackup::recover(dir,gen,active.identity.world,maxBytes,staged);
            }else{
                std::ifstream in(it->path(),std::ios::binary);
                std::vector<uint8_t> bytes(RegionCheckpointManifestCodec::size);
                in.read(reinterpret_cast<char*>(bytes.data()),bytes.size());
                auto& m=entry.recovery.manifest;
                if(!in||!RegionCheckpointManifestCodec::decode(bytes,m)||m.generation!=gen)
                    entry.recovery.status=RegionCheckpointLoadStatus::InvalidManifest;
                else if(m.identity.world!=active.identity.world)
                    entry.recovery.status=RegionCheckpointLoadStatus::WrongWorld;
            }
            result.entries.push_back(std::move(entry));
        }
        std::sort(result.entries.begin(),result.entries.end(),[](const auto& a,const auto& b){return a.generation>b.generation;});
        result.complete=!ec;return result;
    }
};
}
