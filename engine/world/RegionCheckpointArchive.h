#pragma once
#include "RegionCheckpointLoad.h"
#include "RegionCheckpointPublish.h"
#include "engine/pack/Sha256.h"
namespace aiverse::world {
class RegionCheckpointArchive {
public:
    static bool encode(const RegionEntityLedger& ledger,std::vector<uint8_t>& out){
        // Archive writer accepts mutable references but does not modify the ledger.
        std::string bytes;
        Archive archive(&bytes);
        auto copy=ledger;
        archive(copy);
        if(!archive.ok())return false;
        std::vector<uint8_t> result(bytes.begin(),bytes.end());
        out.swap(result);return true;
    }
    static bool decode(std::span<const uint8_t> bytes,RegionEntityLedger& out){
        if(bytes.empty())return false;
        const std::string data(reinterpret_cast<const char*>(bytes.data()),bytes.size());
        Archive archive(data);
        RegionEntityLedger staged;
        archive(staged);
        if(!archive.ok()||archive.remaining()!=0)return false;
        out=std::move(staged);return true;
    }
    static bool verify(std::span<const uint8_t> bytes,const std::array<uint8_t,32>& digest){
        return pack::sha256(bytes.data(),bytes.size())==digest;
    }
    static RegionCheckpointPublishResult publish(const std::filesystem::path& dir,
        RegionCheckpointManifest manifest,const RegionEntityLedger& ledger){
        std::vector<uint8_t> bytes;
        if(!encode(ledger,bytes))return RegionCheckpointPublishResult::Invalid;
        manifest.identity.revision=ledger.lastRevision();
        manifest.identity.digest=pack::sha256(bytes.data(),bytes.size());
        manifest.checkpointBytes=bytes.size();
        return RegionCheckpointPublisher::publish(dir,manifest,bytes);
    }
    static RegionCheckpointLoadResult load(const std::filesystem::path& dir,
        const std::array<uint8_t,16>& world,uint64_t maxBytes,RegionEntityLedger& ledger,
        bool acceptIncomplete=false){
        return RegionCheckpointLoader::load(dir,world,maxBytes,ledger,verify,decode,acceptIncomplete);
    }
};
}
