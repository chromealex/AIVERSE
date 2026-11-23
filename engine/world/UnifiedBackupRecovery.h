#pragma once
#include "UnifiedWalSession.h"
namespace aiverse::world {
class UnifiedBackupRecovery {
public:
    using State=UnifiedWorldReplay::State;
    // Source must be quiescent and caller must hold its lifetime lock.
    static bool recover(const std::filesystem::path& dir,uint64_t generation,
        const std::array<uint8_t,16>& world,size_t budget,
        const UnifiedCheckpointSections::Adapters& adapters,const UnifiedCheckpointSections::Factory& factory,
        const DurableComponentCodecs& codecs,const UnifiedWorldReplay::Materializer& materialize,
        std::unique_ptr<State>& out,const UnifiedWorldReplay::Validator& validate={}){
        if(!generation)return false;
        RegionCheckpointManifest m;m.generation=generation;
        std::ifstream wal(dir/m.walName(),std::ios::binary);
        std::vector<uint8_t> prefix(4+RegionCheckpointManifestCodec::size);
        wal.read(reinterpret_cast<char*>(prefix.data()),prefix.size());
        if(!wal||prefix[0]!='U'||prefix[1]!='W'||prefix[2]!='T'||prefix[3]!='1'||
           !RegionCheckpointManifestCodec::decode(std::span<const uint8_t>(prefix).subspan(4),m)||
           m.generation!=generation||m.identity.world!=world)return false;
        std::unique_ptr<State> staged;RegionCheckpointManifest verified;
        if(!UnifiedCheckpointLoader::loadGeneration(dir,m,world,budget,adapters,factory,staged,verified,validate))return false;
        auto scan=RegionWalReader::scan(wal,staged->sequence,[&](const RegionWalFrame& frame){
            return UnifiedWorldReplay::apply(staged,frame,codecs,materialize,validate);
        });
        if(scan.status!=RegionWalScan::End)return false;
        out.swap(staged);return true;
    }
    static RegionWalWrite fork(const std::filesystem::path& source,uint64_t generation,
        const std::array<uint8_t,16>& world,const std::filesystem::path& destination,
        const std::array<uint8_t,16>& newWorld,size_t budget,
        const UnifiedCheckpointSections::Adapters& adapters,const UnifiedCheckpointSections::Factory& factory,
        const DurableComponentCodecs& codecs,const UnifiedWorldReplay::Materializer& materialize,
        const UnifiedWorldReplay::Validator& validate={}){
        if(world==newWorld)return RegionWalWrite::InvalidFrame;
        std::unique_ptr<State> recovered;
        if(!recover(source,generation,world,budget,adapters,factory,codecs,materialize,recovered,validate))
            return RegionWalWrite::NeedsRecovery;
        UnifiedWalSession session;
        return session.create(destination,newWorld,*recovered,adapters,budget);
    }
};
}
