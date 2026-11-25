// Sparse CPU ownership index for GPU particle slots. No RHI dependencies.
// Pages are 64 slots, only occupied pages are visited. Stable ascending
// iteration preserves the legacy free-list/checkpoint ordering.
#pragma once
#include <bit>
#include <cstdint>
#include <cstddef>
#include <set>
#include <vector>
#include <cassert>
namespace aiverse::spectra {
class ActiveParticlePages {
public:
    void reset(uint32_t capacity){
        capacity_=capacity;
        masks_.assign((size_t(capacity)+63)/64,0);
        occupied_.clear();count_=0;
    }
    bool insert(uint32_t slot){
        if(slot>=capacity_)return false;
        const uint32_t page=slot/64;
        const uint64_t bit=uint64_t(1)<<(slot%64);
        auto& mask=masks_[page];
        if(mask&bit)return false;
        if(!mask)occupied_.insert(page);
        mask|=bit;++count_;return true;
    }
    bool erase(uint32_t slot){
        if(slot>=capacity_)return false;
        const uint32_t page=slot/64;
        const uint64_t bit=uint64_t(1)<<(slot%64);
        auto& mask=masks_[page];
        if(!(mask&bit))return false;
        mask&=~bit;--count_;
        if(!mask)occupied_.erase(page);
        return true;
    }
    bool contains(uint32_t slot) const{
        return slot<capacity_&&(masks_[slot/64]&(uint64_t(1)<<(slot%64)))!=0;
    }
    uint32_t size()const{return count_;}
    size_t occupiedPages()const{return occupied_.size();}
    // Callback may erase the currently visited slot, but must not add slots
    // or erase other pages. Increment iterator before invoking it so the last
    // bit of the current page can disappear without invalidating traversal.
    template<class Fn> void forEach(Fn&& fn) const{
        for(auto it=occupied_.begin();it!=occupied_.end();){
            const auto page=*it++;
            uint64_t bits=masks_[page];
            while(bits){
                const uint32_t offset=uint32_t(std::countr_zero(bits));
                bits&=bits-1;
                fn(page*64+offset);
            }
        }
    }
private:
    uint32_t capacity_=0,count_=0;
    std::vector<uint64_t> masks_;
    std::set<uint32_t> occupied_;
};
}
