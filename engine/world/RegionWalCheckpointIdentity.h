#pragma once
#include "RegionWalFrame.h"
#include <array>
#include <algorithm>
namespace aiverse::world {
// Host supplies a unique world/branch identity and checkpoint content digest.
// Digest calculation and durable checkpoint publication belong to the save owner.
struct RegionWalCheckpointIdentity {
    std::array<uint8_t,16> world{};
    std::array<uint8_t,32> digest{};
    uint64_t sequence=0, revision=0;
    bool operator==(const RegionWalCheckpointIdentity&)const=default;
    bool valid()const {
        return std::any_of(world.begin(),world.end(),[](auto b){return b!=0;}) &&
               std::any_of(digest.begin(),digest.end(),[](auto b){return b!=0;});
    }
};
class RegionWalCheckpointCodec {
public:
    static constexpr size_t size=68;
    static bool encode(const RegionWalCheckpointIdentity& id,std::vector<uint8_t>& out){
        if(!id.valid())return false;
        std::vector<uint8_t> b{'R','W','C',1};
        b.insert(b.end(),id.world.begin(),id.world.end());
        b.insert(b.end(),id.digest.begin(),id.digest.end());
        for(auto v:{id.sequence,id.revision})for(unsigned i=0;i<8;++i)b.push_back(uint8_t(v>>(8*i)));
        out.swap(b);return true;
    }
    static bool decode(std::span<const uint8_t> b,RegionWalCheckpointIdentity& out){
        if(b.size()!=size||b[0]!='R'||b[1]!='W'||b[2]!='C'||b[3]!=1)return false;
        RegionWalCheckpointIdentity id;
        std::copy_n(b.begin()+4,16,id.world.begin());
        std::copy_n(b.begin()+20,32,id.digest.begin());
        for(unsigned i=0;i<8;++i){
            id.sequence|=uint64_t(b[52+i])<<(8*i);
            id.revision|=uint64_t(b[60+i])<<(8*i);
        }
        if(!id.valid())return false;
        out=id;return true;
    }
};
}
