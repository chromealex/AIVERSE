#pragma once
#include "engine/sim/PersistentEcs.h"
#include "RegionWalFrame.h"

namespace aiverse::world {
// Opt-in durable resource snapshots. Native codecs encode presence as well as
// value, so deleting a resource needs no second operation or parallel store.
class DurableResourceTransaction {
public:
    using Registry=PersistentEcsRegistry;
    using Record=Registry::LoadedHash::Chunk;
    static constexpr size_t MaxFrame=RegionWalCodec::maxPayload;
private:
    static void put(std::vector<uint8_t>& b,uint64_t v,unsigned n){
        for(unsigned i=0;i<n;++i)b.push_back(uint8_t(v>>(8*i)));
    }
    static bool get(std::span<const uint8_t> b,size_t& p,uint64_t& v,unsigned n){
        if(n>b.size()-p)return false;
        v=0;for(unsigned i=0;i<n;++i)v|=uint64_t(b[p++])<<(8*i);return true;
    }
    static bool valid(const Record& r){
        if(r.kind!=Registry::Kind::Resource||!r.version||r.id.empty()||r.id.size()>128||
           r.bytes.empty()||r.bytes.size()>Registry::kMaxPayload)return false;
        for(unsigned char ch:r.id)if(!((ch>='a'&&ch<='z')||(ch>='A'&&ch<='Z')||
            (ch>='0'&&ch<='9')||ch=='.'||ch=='_'||ch=='-'||ch==':'))return false;
        return true;
    }
public:
    static bool encode(const std::vector<Record>& records,std::vector<uint8_t>& out){
        if(records.size()>Registry::kMaxSchemas)return false;
        std::vector<uint8_t> b;put(b,0x31545244,4);put(b,1,4);put(b,records.size(),4);
        std::string previous;
        for(const auto& r:records){
            if(!valid(r)||r.id<=previous||10+r.id.size()+r.bytes.size()>MaxFrame-b.size())return false;
            previous=r.id;
            put(b,r.id.size(),2);put(b,r.version,4);put(b,r.bytes.size(),4);
            b.insert(b.end(),r.id.begin(),r.id.end());b.insert(b.end(),r.bytes.begin(),r.bytes.end());
        }
        out.swap(b);return true;
    }
    static bool decode(std::span<const uint8_t> b,std::vector<Record>& out){
        if(b.size()>MaxFrame)return false;
        size_t p=0;uint64_t magic,version,count;
        if(!get(b,p,magic,4)||magic!=0x31545244||!get(b,p,version,4)||version!=1||
           !get(b,p,count,4)||count>Registry::kMaxSchemas||count>(b.size()-p)/12)return false;
        std::vector<Record> records;records.reserve(size_t(count));std::string previous;
        for(uint64_t i=0;i<count;++i){
            uint64_t n,v,len;
            if(!get(b,p,n,2)||!get(b,p,v,4)||!get(b,p,len,4)||
               !n||n>128||n>b.size()-p||!len||len>b.size()-p-n)return false;
            Record r{};r.kind=Registry::Kind::Resource;r.version=uint32_t(v);
            r.id.assign(reinterpret_cast<const char*>(b.data()+p),size_t(n));p+=size_t(n);
            r.bytes.assign(reinterpret_cast<const char*>(b.data()+p),size_t(len));p+=size_t(len);
            if(!valid(r)||r.id<=previous)return false;
            previous=r.id;records.push_back(std::move(r));
        }
        if(p!=b.size())return false;
        out.swap(records);return true;
    }
    // Selection must match checkpoint policy. Resource codecs must not contain
    // raw runtime Entity handles or publish external effects.
    static bool captureChanges(const Registry& registry,const std::vector<std::string>& selected,
        const World& before,const World& after,std::vector<Record>& out){
        if(selected.size()>Registry::kMaxSchemas)return false;
        auto ids=selected;std::sort(ids.begin(),ids.end());
        if(std::adjacent_find(ids.begin(),ids.end())!=ids.end())return false;
        std::vector<Record> staged;size_t used=12;
        for(const auto& id:ids){
            Record old{},next{};
            if(!registry.captureSchema(before,id,old)||!registry.captureSchema(after,id,next)||
               !valid(old)||!valid(next))return false;
            if(old.version==next.version&&old.bytes==next.bytes)continue;
            const size_t cost=10+next.id.size()+next.bytes.size();
            if(cost>MaxFrame-used)return false;
            used+=cost;staged.push_back(std::move(next));
        }
        out.swap(staged);return true;
    }
    struct Policy {
        const Registry* registry=nullptr;
        std::vector<std::string> selected;
        bool restore(World& world,const std::vector<Record>& records)const{
            if(records.empty())return true;
            if(!registry||selected.size()>Registry::kMaxSchemas)return false;
            auto ids=selected;std::sort(ids.begin(),ids.end());
            if(std::adjacent_find(ids.begin(),ids.end())!=ids.end())return false;
            std::string previous;size_t used=12;
            for(const auto& r:records){
                if(!valid(r)||r.id<=previous||!std::binary_search(ids.begin(),ids.end(),r.id))return false;
                const size_t cost=10+r.id.size()+r.bytes.size();
                if(cost>MaxFrame-used)return false;
                used+=cost;previous=r.id;
            }
            return registry->restoreSchemas(world,records);
        }
    };
};
}
