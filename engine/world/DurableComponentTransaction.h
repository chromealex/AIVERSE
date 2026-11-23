#pragma once
#include "DurableComponentCodecs.h"
#include <span>
namespace aiverse::world {
class DurableComponentTransaction {
public:
    using Record=DurableComponentCodecs::Record;
    using Operation=DurableComponentCodecs::Operation;
    static constexpr size_t MaxFrame=16u*1024u*1024u;
    static constexpr uint32_t MaxRecords=1000000;
private:
    static void put(std::vector<uint8_t>& b,uint64_t v,unsigned n){
        for(unsigned i=0;i<n;++i)b.push_back(uint8_t(v>>(8*i)));
    }
    static bool get(std::span<const uint8_t> b,size_t& p,uint64_t& v,unsigned n){
        if(n>b.size()-p)return false;
        v=0;for(unsigned i=0;i<n;++i)v|=uint64_t(b[p++])<<(8*i);return true;
    }
    static bool valid(const Record& r){
        const auto& c=r.chunk;
        if(!c.owner||!c.version||c.schema.empty()||c.schema.size()>128||c.bytes.size()>DurableComponentCodecs::MaxBytes)return false;
        for(unsigned char ch:c.schema)if(!((ch>='a'&&ch<='z')||(ch>='A'&&ch<='Z')||(ch>='0'&&ch<='9')||ch=='.'||ch=='_'||ch=='-'||ch==':'))return false;
        return r.operation==Operation::Upsert||(r.operation==Operation::Remove&&c.bytes.empty());
    }
public:
    static bool encode(const std::vector<Record>& records,std::vector<uint8_t>& out){
        if(records.size()>MaxRecords)return false;
        std::vector<uint8_t> b;
        put(b,0x31544344,4);put(b,1,4);put(b,records.size(),4);
        for(const auto& r:records){
            if(!valid(r))return false;
            const auto& c=r.chunk;
            if(19+c.schema.size()+c.bytes.size()>MaxFrame-b.size())return false;
            put(b,uint8_t(r.operation),1);put(b,c.owner,8);put(b,c.version,4);
            put(b,c.schema.size(),2);put(b,c.bytes.size(),4);
            b.insert(b.end(),c.schema.begin(),c.schema.end());
            b.insert(b.end(),c.bytes.begin(),c.bytes.end());
        }
        out.swap(b);return true;
    }
    static bool decode(std::span<const uint8_t> b,std::vector<Record>& out){
        if(b.size()>MaxFrame)return false;
        size_t p=0;uint64_t magic,version,count;
        if(!get(b,p,magic,4)||magic!=0x31544344||!get(b,p,version,4)||version!=1||
           !get(b,p,count,4)||count>MaxRecords||count>(b.size()-p)/20)return false;
        std::vector<Record> records;records.reserve(size_t(count));
        for(uint64_t i=0;i<count;++i){
            uint64_t op,owner,ver,n,len;
            if(!get(b,p,op,1)||!get(b,p,owner,8)||!get(b,p,ver,4)||
               !get(b,p,n,2)||!get(b,p,len,4)||!n||n>128||n>b.size()-p||len>b.size()-p-n)return false;
            Record r;r.operation=static_cast<Operation>(op);r.chunk.owner=owner;r.chunk.version=uint32_t(ver);
            r.chunk.schema.assign(reinterpret_cast<const char*>(b.data()+p),size_t(n));p+=size_t(n);
            r.chunk.bytes.assign(reinterpret_cast<const char*>(b.data()+p),size_t(len));p+=size_t(len);
            if(!valid(r))return false;
            records.push_back(std::move(r));
        }
        if(p!=b.size())return false;
        out.swap(records);return true;
    }
};
}
