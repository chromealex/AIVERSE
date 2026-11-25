#pragma once
#include "RegionWalTransaction.h"
#include "DurableComponentTransaction.h"
#include "DurableResourceTransaction.h"
#include "NativeRestartState.h"
namespace aiverse::world {
class UnifiedWorldTransaction {
public:
    using Record=DurableComponentTransaction::Record;
    struct Decoded {uint64_t baseRevision=0;RegionWalFrame ledger;std::vector<Record> components;std::vector<DurableResourceTransaction::Record> resources;std::string restart;};
private:
    static void put(std::vector<uint8_t>& b,uint64_t v,unsigned n){for(unsigned i=0;i<n;++i)b.push_back(uint8_t(v>>(8*i)));}
    static uint64_t get(std::span<const uint8_t> b,size_t& p,unsigned n){uint64_t v=0;for(unsigned i=0;i<n;++i)v|=uint64_t(b[p++])<<(8*i);return v;}
public:
    // Preserve byte-identical version 1 for transactions without resources.
    static bool encode(const RegionEntityLedger::Transaction& tx,const std::vector<Record>& records,uint64_t sequence,RegionWalFrame& out){
        return encode(tx,records,{},sequence,out);
    }
    static bool encode(const RegionEntityLedger::Transaction& tx,const std::vector<Record>& records,
        const std::vector<DurableResourceTransaction::Record>& resources,uint64_t sequence,RegionWalFrame& out){
        if(!tx.capturing())return false;
        return encodeCaptured(tx.initialRevision(),tx.view().lastRevision(),tx.mutations(),records,resources,sequence,out);
    }
    static bool encodeCaptured(uint64_t baseRevision,uint64_t finalRevision,
        std::span<const RegionEntityLedger::Mutation> mutations,const std::vector<Record>& records,
        const std::vector<DurableResourceTransaction::Record>& resources,uint64_t sequence,RegionWalFrame& out,
        const std::string& restart={}){
        if(!sequence||(mutations.empty()&&records.empty()&&resources.empty()&&restart.empty()))return false;
        RegionWalFrame ledger;
        if(!mutations.empty()&&!RegionWalTransaction::encodeCaptured(baseRevision,finalRevision,mutations,sequence,ledger))return false;
        if(mutations.empty()&&baseRevision!=finalRevision)return false;
        std::vector<uint8_t> components,resourceBytes;
        if(!DurableComponentTransaction::encode(records,components))return false;
        const uint32_t version=!restart.empty()?3:(!resources.empty()?2:1);
        if(version>=2&&!DurableResourceTransaction::encode(resources,resourceBytes))return false;
        NativeRestartState::Data metadata;
        if(version==3&&!NativeRestartState::decode(restart,metadata))return false;
        const size_t header=version==3?32:(version==2?28:24);
        if(ledger.payload.size()>RegionWalCodec::maxPayload-header||
           components.size()>RegionWalCodec::maxPayload-header-ledger.payload.size()||
           resourceBytes.size()>RegionWalCodec::maxPayload-header-ledger.payload.size()-components.size()||
           restart.size()>RegionWalCodec::maxPayload-header-ledger.payload.size()-components.size()-resourceBytes.size())return false;
        RegionWalFrame frame;frame.sequence=sequence;
        auto& b=frame.payload;
        put(b,0x31545755,4);put(b,version,4);put(b,baseRevision,8);
        put(b,ledger.payload.size(),4);put(b,components.size(),4);
        if(version>=2)put(b,resourceBytes.size(),4);
        if(version==3)put(b,restart.size(),4);
        b.insert(b.end(),ledger.payload.begin(),ledger.payload.end());
        b.insert(b.end(),components.begin(),components.end());
        b.insert(b.end(),resourceBytes.begin(),resourceBytes.end());
        b.insert(b.end(),restart.begin(),restart.end());
        out=std::move(frame);return true;
    }
    static bool decode(const RegionWalFrame& frame,Decoded& out){
        std::span<const uint8_t> b=frame.payload;
        if(!frame.sequence||b.size()<24||b.size()>RegionWalCodec::maxPayload)return false;
        size_t p=0;if(get(b,p,4)!=0x31545755)return false;
        const auto version=get(b,p,4);
        if(version<1||version>3)return false;
        if((version==2&&b.size()<28)||(version==3&&b.size()<32))return false;
        Decoded d;d.baseRevision=get(b,p,8);
        auto n=get(b,p,4),m=get(b,p,4),r=version>=2?get(b,p,4):uint64_t(0);
        auto s=version==3?get(b,p,4):uint64_t(0);
        if(n>b.size()-p||m>b.size()-p-n||r>b.size()-p-n-m||s!=b.size()-p-n-m-r)return false;
        d.ledger.sequence=frame.sequence;
        d.ledger.payload.assign(b.begin()+p,b.begin()+p+n);p+=size_t(n);
        if(!DurableComponentTransaction::decode(b.subspan(p,size_t(m)),d.components))return false;
        p+=size_t(m);
        if(version>=2&&!DurableResourceTransaction::decode(b.subspan(p,size_t(r)),d.resources))return false;
        if(version==2&&d.resources.empty())return false;
        p+=size_t(r);
        if(version==3){
            if(!s||s>NativeRestartState::MaxBytes)return false;
            d.restart.assign(reinterpret_cast<const char*>(b.data()+p),size_t(s));
            NativeRestartState::Data metadata;
            if(!NativeRestartState::decode(d.restart,metadata))return false;
        }
        if(d.ledger.payload.empty()&&d.components.empty()&&d.resources.empty()&&d.restart.empty())return false;
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
