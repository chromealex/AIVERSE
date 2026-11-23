#pragma once
#include "RegionCheckpointArchive.h"
namespace aiverse::world {
class RegionCheckpointBackup {
public:
    // Caller holds source world session lock. Never modifies source files.
    static RegionCheckpointLoadResult recover(const std::filesystem::path& dir,
        uint64_t generation,const std::array<uint8_t,16>& world,uint64_t maxBytes,
        RegionEntityLedger& output,bool acceptIncomplete=false){
        RegionCheckpointLoadResult failure;
        failure.status=RegionCheckpointLoadStatus::InvalidManifest;
        if(!generation)return failure;
        RegionCheckpointManifest selected;selected.generation=generation;
        std::ifstream in(dir/selected.walName(),std::ios::binary);
        std::vector<uint8_t> bytes(RegionCheckpointManifestCodec::size);
        in.read(reinterpret_cast<char*>(bytes.data()),bytes.size());
        if(!in||!RegionCheckpointManifestCodec::decode(bytes,selected)||
           selected.generation!=generation)return failure;
        in.close();
        return RegionCheckpointLoader::loadGeneration(dir,selected,world,maxBytes,output,
            RegionCheckpointArchive::verify,RegionCheckpointArchive::decode,acceptIncomplete);
    }
};
}
