#pragma once
#include "RegionWalWriter.h"
#include "RegionWalTransaction.h"
namespace aiverse::world {
// Exclusive authoritative host barrier. On any failure abandon tx;
// an I/O error may mean the frame reached disk: stop the host and recover.
inline RegionWalWrite commitRegionWal(RegionEntityLedger::Transaction& tx,RegionWalWriter& writer) {
    if(!tx.capturing())return RegionWalWrite::InvalidFrame;
    if(tx.mutations().empty()){
        if(tx.initialRevision()!=tx.view().lastRevision())return RegionWalWrite::InvalidFrame;
        tx.commit();return RegionWalWrite::Ok;
    }
    if(writer.lastSequence()==UINT64_MAX)return RegionWalWrite::InvalidSequence;
    RegionWalFrame frame;
    if(!RegionWalTransaction::encode(tx,writer.lastSequence()+1,frame))return RegionWalWrite::InvalidFrame;
    const auto result=writer.append(frame);
    if(result==RegionWalWrite::Ok)tx.commit();
    return result;
}
// Recover into a private ledger; publish only after a clean end or an
// explicitly accepted incomplete tail. Does not repair files or restore ECS.
inline RegionWalScanResult recoverRegionWal(std::istream& input,uint64_t checkpointSequence,
    RegionEntityLedger& ledger,bool acceptIncompleteTail=false) {
    RegionEntityLedger staged=ledger;
    auto result=RegionWalReader::scan(input,checkpointSequence,[&](const RegionWalFrame& frame){
        return RegionWalTransaction::replay(staged,frame);
    });
    if(result.status==RegionWalScan::End ||
       (acceptIncompleteTail&&result.status==RegionWalScan::Incomplete))
        ledger=std::move(staged);
    return result;
}
}
