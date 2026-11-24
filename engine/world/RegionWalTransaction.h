#pragma once
#include "RegionEntityLedger.h"
#include "RegionWalFrame.h"
#include <bit>
namespace aiverse::world {
class RegionWalTransaction {
    static constexpr size_t prefix=16,row=29;
    static void put(std::vector<uint8_t>& b,uint64_t v,size_t n){for(size_t i=0;i<n;++i)b.push_back(uint8_t(v>>(8*i)));}
    static uint64_t get(std::span<const uint8_t> b,size_t& p,size_t n){uint64_t v=0;for(size_t i=0;i<n;++i)v|=uint64_t(b[p++])<<(8*i);return v;}
public:
    static constexpr size_t maxMutations=(RegionWalCodec::maxPayload-prefix)/row;
    static bool encode(const RegionEntityLedger::Transaction& tx,uint64_t sequence,RegionWalFrame& out){
        if(!tx.capturing())return false;
        return encodeCaptured(tx.initialRevision(),tx.view().lastRevision(),tx.mutations(),sequence,out);
    }
    // Exact chronological stream, never a coalesced latest-state journal.
    static bool encodeCaptured(uint64_t baseRevision,uint64_t finalRevision,
        std::span<const RegionEntityLedger::Mutation> changes,uint64_t sequence,RegionWalFrame& out){
        if(!sequence||changes.empty()||changes.size()>maxMutations)return false;
        RegionWalFrame f;f.sequence=sequence;
        auto& b=f.payload;b.reserve(prefix+row*changes.size());
        put(b,1,4);put(b,baseRevision,8);put(b,changes.size(),4);
        auto revision=baseRevision;
        for(const auto& [id,r]:changes){
            if(!id||r.live>1||revision==UINT64_MAX||r.revision!=++revision)return false;
            put(b,id,8);put(b,uint32_t(r.owner.x),4);put(b,uint32_t(r.owner.y),4);put(b,uint32_t(r.owner.z),4);
            put(b,r.revision,8);put(b,r.live,1);
        }
        if(revision!=finalRevision)return false;
        out=std::move(f);return true;
    }
    static bool replay(RegionEntityLedger& ledger,const RegionWalFrame& frame){
        std::span<const uint8_t> b=frame.payload;
        if(!frame.sequence||b.size()<prefix||b.size()>RegionWalCodec::maxPayload)return false;
        size_t p=0;
        if(get(b,p,4)!=1||get(b,p,8)!=ledger.lastRevision())return false;
        const auto n=get(b,p,4);
        if(!n||n>maxMutations||b.size()!=prefix+n*row)return false;
        RegionEntityLedger::Transaction tx(ledger);
        for(size_t i=0;i<n;++i){
            const auto id=get(b,p,8);
            Cell cell{};
            cell.x=std::bit_cast<int32_t>(uint32_t(get(b,p,4)));
            cell.y=std::bit_cast<int32_t>(uint32_t(get(b,p,4)));
            cell.z=std::bit_cast<int32_t>(uint32_t(get(b,p,4)));
            const auto rev=get(b,p,8),live=get(b,p,1);
            if(!id||live>1||ledger.lastRevision()==UINT64_MAX||rev!=ledger.lastRevision()+1)return false;
            const auto* old=tx.find(id);
            bool ok=false;
            if(!old){if(live)ok=tx.spawn(id,cell);}
            else if(old->live){
                if(live&&old->owner!=cell)ok=tx.transfer(id,old->owner,cell);
                else if(!live&&old->owner==cell)ok=tx.despawn(id,cell);
            }
            if(!ok||ledger.lastRevision()!=rev)return false;
        }
        tx.commit();return true;
    }
};
}
