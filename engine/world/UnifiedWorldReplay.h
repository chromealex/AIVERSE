#pragma once
#include "UnifiedWorldTransaction.h"
#include <memory>
#include <functional>
namespace aiverse::world {
class UnifiedWorldReplay {
public:
    struct State {RegionEntityLedger ledger;World world;uint64_t sequence=0;};
    using Materializer=std::function<bool(World&,const RegionEntityLedger&,const RegionEntityLedger&)>;
    using Validator=std::function<bool(const State&)>;
    // Ownership swap publishes both halves together. Readers must be quiescent;
    // this is transactional publication, not a concurrent atomic pointer.
    static bool apply(std::unique_ptr<State>& current,const RegionWalFrame& frame,
        const DurableComponentCodecs& codecs,const Materializer& materialize,
        const Validator& validate={}){
        if(!current||!materialize||current->sequence==UINT64_MAX||
           frame.sequence!=current->sequence+1)return false;
        UnifiedWorldTransaction::Decoded decoded;
        if(!UnifiedWorldTransaction::decode(frame,decoded))return false;
        auto staged=std::make_unique<State>(*current);
        if(!UnifiedWorldTransaction::replayLedger(staged->ledger,decoded))return false;
        // Host reconciles spawns/despawns and region residency in private state.
        if(!materialize(staged->world,current->ledger,staged->ledger))return false;
        if(!codecs.restoreBatch(staged->world,staged->ledger,decoded.components))return false;
        staged->sequence=frame.sequence;
        if(validate&&!validate(*staged))return false;
        current.swap(staged);return true;
    }
};
}
