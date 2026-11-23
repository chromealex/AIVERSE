#pragma once
#include "RegionWalTransaction.h"
#include "DurableComponentTransaction.h"
namespace aiverse::world {
class UnifiedWorldTransaction {
public:
    using Record=DurableComponentTransaction::Record;
    struct Decoded {uint64_t baseRevision=0;RegionWalFrame ledger;std::vector<Record> components;};
private:
    static void put(std::vector<uint8_t>& b,uint64_t v,unsigned n){for(unsigned i=0;i<n;++i)b.push_back(uint8_t(v>>(8*i)));}
    static uint64_t get(std::span<const uint8_t> b,size_t& p,unsigned n){uint64_t v=0;for(unsigned i=0;i<n;++i)v|=uint64_t(b[p++])<<(8*i);return v;}
public:
    static bool encode(const RegionEntityLedger::Transaction& tx,const std::vector<Record>& records,uint64_t sequence,RegionWalFrame& out){
        if(!sequence||!tx.capturing()||(tx.mutations().empty()&&records.empty()))return false;
        RegionWalFrame ledger;
        if(!tx.mutations().empty()&&!RegionWalTransaction::encode(tx,sequence,ledger))return false;
        if(tx.mutations().empty()&&tx.initialRevision()!=tx.view().lastRevision())return false;
        std::vector<uint8_t> components;
        if(!DurableComponentTransaction::encode(records,components))return false;
        constexpr size_t header=24;
        if(ledger.payload.size()+components.size()>RegionWalCodec::maxPayload-header)return false;
        RegionWalFrame frame;frame.sequence=sequence;
        auto& b=frame.payload;
        put(b,0x31545755,4);put(b,1,4);put(b,tx.initialRevision(),8);
        put(b,ledger.payload.size(),4);put(b,components.size(),4);
        b.insert(b.end(),ledger.payload.begin(),ledger.payload.end());
        b.insert(b.end(),components.begin(),components.end());
        out=std::move(frame);return true;
    }
    static bool decode(const RegionWalFrame& frame,Decoded& out){
        std::span<const uint8_t> b=frame.payload;
        if(!frame.sequence||b.size()<24||b.size()>RegionWalCodec::maxPayload)return false;
        size_t p=0;if(get(b,p,4)!=0x31545755||get(b,p,4)!=1)return false;
        Decoded d;d.baseRevision=get(b,p,8);
        auto n=get(b,p,4),m=get(b,p,4);
        if(n>b.size()-p||m!=b.size()-p-n)return false;
        d.ledger.sequence=frame.sequence;
        d.ledger.payload.assign(b.begin()+p,b.begin()+p+n);p+=size_t(n);
        if(!DurableComponentTransaction::decode(b.subspan(p),d.components)||
           (d.ledger.payload.empty()&&d.components.empty()))return false;
        out=std::move(d);return true;
    }
    // Apply only to caller-owned private recovery state. Entity materialization
    // and allocator reconciliation are host responsibilities before component replay.
    static bool replayLedger(RegionEntityLedger& staged,const Decoded& d){
        if(staged.lastRevision()!=d.baseRevision)return false;
        return d.ledger.payload.empty()||RegionWalTransaction::replay(staged,d.ledger);
    }
};
}
