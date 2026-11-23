#pragma once
#include "RegionWalCheckpointIdentity.h"
#include <string>
namespace aiverse::world {
struct RegionCheckpointManifest {
    RegionWalCheckpointIdentity identity;
    uint64_t generation=0;
    uint64_t checkpointBytes=0;
    bool valid()const{return identity.valid()&&generation&&checkpointBytes;}
    std::string checkpointName()const{return "checkpoint-"+std::to_string(generation)+".bin";}
    std::string walName()const{return "wal-"+std::to_string(generation)+".bin";}
};
class RegionCheckpointManifestCodec {
public:
    static constexpr size_t size=92;
    static bool encode(const RegionCheckpointManifest& m,std::vector<uint8_t>& out){
        if(!m.valid())return false;
        std::vector<uint8_t> identity;
        if(!RegionWalCheckpointCodec::encode(m.identity,identity))return false;
        std::vector<uint8_t> b{'R','C','M',1};
        b.insert(b.end(),identity.begin(),identity.end());
        for(auto v:{m.generation,m.checkpointBytes})for(unsigned i=0;i<8;++i)b.push_back(uint8_t(v>>(8*i)));
        const auto crc=RegionWalCodec::checksum(b);
        for(unsigned i=0;i<4;++i)b.push_back(uint8_t(crc>>(8*i)));
        out.swap(b);return true;
    }
    static bool decode(std::span<const uint8_t> b,RegionCheckpointManifest& out){
        if(b.size()!=size||b[0]!='R'||b[1]!='C'||b[2]!='M'||b[3]!=1)return false;
        uint32_t crc=0;
        for(unsigned i=0;i<4;++i)crc|=uint32_t(b[88+i])<<(8*i);
        if(crc!=RegionWalCodec::checksum(b.first(88)))return false;
        RegionCheckpointManifest m;
        if(!RegionWalCheckpointCodec::decode(b.subspan(4,68),m.identity))return false;
        for(unsigned i=0;i<8;++i){
            m.generation|=uint64_t(b[72+i])<<(8*i);
            m.checkpointBytes|=uint64_t(b[80+i])<<(8*i);
        }
        if(!m.valid())return false;
        out=m;return true;
    }
};
}
