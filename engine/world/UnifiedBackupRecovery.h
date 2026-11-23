#pragma once
#include "UnifiedBackupRead.h"
#include "UnifiedWalSession.h"
namespace aiverse::world {
class UnifiedBackupRecovery : public UnifiedBackupRead {
public:
    static RegionWalWrite fork(const std::filesystem::path& source,uint64_t generation,
        const std::array<uint8_t,16>& world,const std::filesystem::path& destination,
        const std::array<uint8_t,16>& newWorld,size_t budget,
        const UnifiedCheckpointSections::Adapters& adapters,const UnifiedCheckpointSections::Factory& factory,
        const DurableComponentCodecs& codecs,const UnifiedWorldReplay::Materializer& materialize,
        const UnifiedWorldReplay::Validator& validate={},const DurableResourceTransaction::Policy& resources={}){
        if(world==newWorld)return RegionWalWrite::InvalidFrame;
        std::unique_ptr<State> recovered;
        if(!recover(source,generation,world,budget,adapters,factory,codecs,materialize,recovered,validate,resources))
            return RegionWalWrite::NeedsRecovery;
        UnifiedWalSession session;
        return session.create(destination,newWorld,*recovered,adapters,budget);
    }
};
}
