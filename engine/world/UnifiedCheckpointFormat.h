#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <vector>
#include <utility>
namespace aiverse::world {
class UnifiedCheckpointFormat {
public:
    enum class Kind:uint32_t {Ledger=1,Entities=2,Components=3,Resources=4};
    struct Section {Kind kind;uint32_t version=1;std::vector<uint8_t> bytes;};
    struct Snapshot {uint64_t sequence=0,revision=0;std::array<uint8_t,16> world{};std::vector<Section> sections;};
private:
    static void put(std::vector<uint8_t>& b,uint64_t v,unsigned n){for(unsigned i=0;i<n;++i)b.push_back(uint8_t(v>>(8*i)));}
    static bool get(std::span<const uint8_t> b,size_t& p,uint64_t& v,unsigned n){
        if(n>b.size()-p)return false;v=0;for(unsigned i=0;i<n;++i)v|=uint64_t(b[p++])<<(8*i);return true;
    }
    static bool valid(const Snapshot& s){
        bool nonzero=false;for(auto b:s.world)nonzero|=b!=0;
        if(!nonzero||s.sections.size()!=4)return false;
        for(size_t i=0;i<4;++i)if(uint32_t(s.sections[i].kind)!=i+1||!s.sections[i].version)return false;
        return true;
    }
public:
    static bool encode(const Snapshot& s,size_t budget,std::vector<uint8_t>& out){
        if(!valid(s)||budget<44)return false;
        std::vector<uint8_t> b;
        put(b,0x31504355,4);put(b,1,4);put(b,s.sequence,8);put(b,s.revision,8);
        b.insert(b.end(),s.world.begin(),s.world.end());put(b,4,4);
        for(const auto& section:s.sections){
            if(budget-b.size()<16||section.bytes.size()>budget-b.size()-16)return false;
            put(b,uint32_t(section.kind),4);put(b,section.version,4);put(b,section.bytes.size(),8);
            b.insert(b.end(),section.bytes.begin(),section.bytes.end());
        }
        out.swap(b);return true;
    }
    static bool decode(std::span<const uint8_t> b,size_t budget,Snapshot& out){
        if(b.size()>budget||b.size()<44)return false;
        Snapshot s;size_t p=0;uint64_t magic,version,count;
        if(!get(b,p,magic,4)||magic!=0x31504355||!get(b,p,version,4)||version!=1||
           !get(b,p,s.sequence,8)||!get(b,p,s.revision,8))return false;
        for(auto& v:s.world)v=b[p++];
        if(!get(b,p,count,4)||count!=4)return false;
        for(uint64_t i=1;i<=count;++i){
            uint64_t kind,ver,len;
            if(!get(b,p,kind,4)||kind!=i||!get(b,p,ver,4)||!ver||
               !get(b,p,len,8)||len>b.size()-p)return false;
            Section section{static_cast<Kind>(kind),uint32_t(ver),{}};
            section.bytes.assign(b.begin()+p,b.begin()+p+size_t(len));p+=size_t(len);
            s.sections.push_back(std::move(section));
        }
        if(p!=b.size()||!valid(s))return false;
        out=std::move(s);return true;
    }
};
}
