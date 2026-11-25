#pragma once
#include "UnifiedCheckpointSections.h"
namespace aiverse::world {
class UnifiedCheckpointPublisher {
public:
    static bool identity(const RegionCheckpointManifest& m,std::vector<uint8_t>& out){
        std::vector<uint8_t> bytes;
        if(!RegionCheckpointManifestCodec::encode(m,bytes))return false;
        bytes.insert(bytes.begin(),{'U','W','T','1'});out.swap(bytes);return true;
    }
    // Caller holds lifetime lock, finalized barrier and has stopped old writer.
    static RegionCheckpointPublishResult publish(const std::filesystem::path& dir,
        const UnifiedWorldReplay::State& state,const std::array<uint8_t,16>& world,
        uint64_t generation,const UnifiedCheckpointSections::Adapters& adapters,
        size_t budget,RegionCheckpointManifest& out){
        std::vector<uint8_t> bytes;
        if(!UnifiedCheckpointSections::capture(state,world,adapters,budget,bytes))
            return RegionCheckpointPublishResult::Invalid;
        RegionCheckpointManifest m;
        m.generation=generation;m.identity.world=world;m.identity.sequence=state.sequence;
        m.identity.revision=state.ledger.lastRevision();
        m.identity.digest=pack::sha256(bytes.data(),bytes.size());m.checkpointBytes=bytes.size();
        auto result=RegionCheckpointPublisher::publish(dir,m,bytes,true);
        if(result==RegionCheckpointPublishResult::Ok)out=m;
        return result;
    }
};
}
