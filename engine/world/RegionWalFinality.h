#pragma once
#include "RegionWalCommit.h"
namespace aiverse::world {
// Host-owned, never an ECS snapshot resource. Only finalized authoritative
// transactions may enter this session; speculative ticks must not append.
class RegionWalFinality {
    RegionWalWriter writer_;
    uint64_t revision_=0;
    bool ready_=false;
public:
    RegionWalWrite openFinalized(const std::filesystem::path& path,
        uint64_t checkpointSequence,uint64_t recoveredSequence,
        const RegionEntityLedger& recovered,bool repair=false,std::span<const uint8_t> identity={}){
        ready_=false;
        auto result=writer_.open(path,checkpointSequence,repair,identity);
        if(result!=RegionWalWrite::Ok)return result;
        if(writer_.lastSequence()!=recoveredSequence){
            writer_.close();return RegionWalWrite::NeedsRecovery;
        }
        revision_=recovered.lastRevision();ready_=true;return RegionWalWrite::Ok;
    }
    RegionWalWrite commitFinalized(RegionEntityLedger::Transaction& tx){
        if(!ready_||tx.initialRevision()!=revision_)return RegionWalWrite::NeedsRecovery;
        auto result=commitRegionWal(tx,writer_);
        if(result==RegionWalWrite::Ok)revision_=tx.view().lastRevision();
        else ready_=false;
        return result;
    }
    bool ready()const{return ready_;}
    uint64_t revision()const{return revision_;}
    uint64_t sequence()const{return writer_.lastSequence();}
    void close(){ready_=false;writer_.close();}
};
}
