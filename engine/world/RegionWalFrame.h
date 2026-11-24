#pragma once
#include <cstdint>
#include <cstddef>
#include <span>
#include <vector>
namespace aiverse::world {
struct RegionWalFrame {
    uint64_t sequence=0;
    std::vector<uint8_t> payload;
};
enum class RegionWalRead { Complete, Incomplete, Corrupt };
class RegionWalCodec {
public:
    static constexpr size_t headerSize=24, maxPayload=16*1024*1024;
    static uint32_t checksum(std::span<const uint8_t> bytes) {
        uint32_t crc=0xffffffffu;
        for(auto b:bytes){crc^=b;for(int i=0;i<8;++i)crc=(crc>>1)^(0xedb88320u & (0u-(crc&1u)));}
        return ~crc;
    }
    static bool encode(const RegionWalFrame& f,std::vector<uint8_t>& out) {
        if(!f.sequence || f.payload.size()>maxPayload)return false;
        std::vector<uint8_t> b;
        b.reserve(headerSize+f.payload.size());
        put(b,0x314c4157u,4);put(b,1,4);put(b,f.sequence,8);put(b,f.payload.size(),4);
        put(b,0,4);
        b.insert(b.end(),f.payload.begin(),f.payload.end());
        const auto crc=checksum(b);
        for(size_t i=0;i<4;++i)b[20+i]=uint8_t(crc>>(8*i));
        out.swap(b);return true;
    }
    static RegionWalRead decode(std::span<const uint8_t> b,RegionWalFrame& out,size_t& consumed) {
        consumed=0;
        if(b.size()<headerSize)return RegionWalRead::Incomplete;
        if(get(b,0,4)!=0x314c4157u || get(b,4,4)!=1 || !get(b,8,8))return RegionWalRead::Corrupt;
        const auto n=get(b,16,4);
        if(n>maxPayload)return RegionWalRead::Corrupt;
        if(b.size()<headerSize+n)return RegionWalRead::Incomplete;
        std::vector<uint8_t> copy(b.begin(),b.begin()+headerSize+n);
        const auto expected=get(b,20,4);
        for(size_t i=20;i<24;++i)copy[i]=0;
        if(checksum(copy)!=expected)return RegionWalRead::Corrupt;
        RegionWalFrame f;f.sequence=get(b,8,8);
        f.payload.assign(b.begin()+headerSize,b.begin()+headerSize+n);
        out=std::move(f);consumed=headerSize+n;return RegionWalRead::Complete;
    }
private:
    static void put(std::vector<uint8_t>& b,uint64_t v,size_t n){for(size_t i=0;i<n;++i)b.push_back(uint8_t(v>>(8*i)));}
    static uint64_t get(std::span<const uint8_t> b,size_t p,size_t n){uint64_t v=0;for(size_t i=0;i<n;++i)v|=uint64_t(b[p+i])<<(8*i);return v;}
};
}
