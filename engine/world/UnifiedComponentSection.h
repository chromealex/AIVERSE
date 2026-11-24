#pragma once
#include "DurableComponentTransaction.h"
#include "UnifiedCheckpointSections.h"
#include <set>
namespace aiverse::world {
class UnifiedComponentSection {
public:
    using Record=DurableComponentTransaction::Record;
    // Pages reuse DCT1; the checkpoint itself is not limited to a WAL frame.
    static bool encode(const std::vector<Record>& records,size_t budget,std::vector<uint8_t>& out){
        if(budget<8)return false;
        std::vector<uint8_t> bytes{'C','P','S','1',0,0,0,0};
        uint32_t pages=0;
        std::vector<Record> page;
        size_t used=12;
        std::set<std::pair<RegionEntityId,std::string>> keys;
        auto flush=[&](){
            if(page.empty())return true;
            std::vector<uint8_t> payload;
            if(!DurableComponentTransaction::encode(page,payload)||pages==UINT32_MAX||
               budget-bytes.size()<4||payload.size()>budget-bytes.size()-4)return false;
            for(unsigned i=0;i<4;++i)bytes.push_back(uint8_t(payload.size()>>(8*i)));
            bytes.insert(bytes.end(),payload.begin(),payload.end());
            ++pages;page.clear();used=12;return true;
        };
        for(const auto& r:records){
            if(r.operation!=DurableComponentCodecs::Operation::Upsert||
               r.chunk.schema.size()>128||r.chunk.bytes.size()>DurableComponentCodecs::MaxBytes||
               !keys.emplace(r.chunk.owner,r.chunk.schema).second)return false;
            size_t n=19+r.chunk.schema.size()+r.chunk.bytes.size();
            if(n>DurableComponentTransaction::MaxFrame-12)return false;
            if(n>DurableComponentTransaction::MaxFrame-used||page.size()==DurableComponentTransaction::MaxRecords)
                if(!flush())return false;
            page.push_back(r);used+=n;
        }
        if(!flush())return false;
        for(unsigned i=0;i<4;++i)bytes[4+i]=uint8_t(pages>>(8*i));
        out.swap(bytes);return true;
    }
    static UnifiedCheckpointSections::Adapter adapter(const DurableComponentCodecs& codecs,size_t budget){
        return {1,[&codecs,budget](const UnifiedWorldReplay::State& s,std::vector<uint8_t>& out){
            std::vector<Record> records;
            return codecs.captureAll(s.world,s.ledger,budget,records)&&encode(records,budget,out);
        },[&codecs,budget](UnifiedWorldReplay::State& s,uint32_t version,std::span<const uint8_t> bytes){
            return version==1&&restore(bytes,budget,s.world,s.ledger,codecs);
        }};
    }
    // Restore into private checkpoint state only. On failure discard it.
    static bool restore(std::span<const uint8_t> bytes,size_t budget,World& staged,
        const RegionEntityLedger& ledger,const DurableComponentCodecs& codecs){
        if(bytes.size()>budget||bytes.size()<8||bytes[0]!='C'||bytes[1]!='P'||bytes[2]!='S'||bytes[3]!='1')return false;
        size_t p=4;
        auto read=[&](uint32_t& v){if(bytes.size()-p<4)return false;v=0;for(unsigned i=0;i<4;++i)v|=uint32_t(bytes[p++])<<(8*i);return true;};
        uint32_t pages;if(!read(pages)||pages>(bytes.size()-p)/16)return false;
        PersistentEntityReferences refs;if(!refs.rebuild(staged,ledger))return false;
        std::set<std::pair<RegionEntityId,std::string>> keys;
        for(uint32_t i=0;i<pages;++i){
            uint32_t n;if(!read(n)||n>bytes.size()-p)return false;
            std::vector<Record> records;
            if(!DurableComponentTransaction::decode(bytes.subspan(p,n),records)||records.empty())return false;
            p+=n;
            for(const auto& r:records){
                if(r.operation!=DurableComponentCodecs::Operation::Upsert||
                   !keys.emplace(r.chunk.owner,r.chunk.schema).second||
                   !codecs.restoreStaged(staged,refs,r.chunk))return false;
            }
        }
        return p==bytes.size()&&codecs.rebuildDerived(staged,ledger);
    }
};
}
