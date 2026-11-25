// Bounded Vulkan scope bookkeeping independent of Vulkan SDK/driver.
// Each slot owns one query pool; the caller proves fence completion before
// consuming records or resetting the pool. This policy never waits.
#pragma once
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace aiverse::gfx::vk {
struct ScopeTimingRecord {uint64_t tag=0;bool ended=false;};
class ScopeTimingFrame {
public:
    static constexpr uint32_t maxScopes=32,queryCount=maxScopes*2;
    bool begin(uint64_t tag,uint32_t& query)noexcept{
        if(!tag||active_||count_==maxScopes)return false;
        query=count_*2;records_[count_++]={tag,false};active_=true;return true;
    }
    bool end(uint32_t& query)noexcept{
        if(!active_)return false;
        query=(count_-1)*2+1;records_[count_-1].ended=true;active_=false;return true;
    }
    void abandonActive()noexcept{active_=false;}
    void clear()noexcept{count_=0;active_=false;}
    bool active()const noexcept{return active_;}
    uint32_t count()const noexcept{return count_;}
    const ScopeTimingRecord& record(uint32_t i)const noexcept{return records_[i];}
private:
    std::array<ScopeTimingRecord,maxScopes> records_{};
    uint32_t count_=0;bool active_=false;
};
inline double scopeMilliseconds(uint64_t begin,uint64_t end,uint32_t bits,double periodNs)noexcept{
    if(!bits||bits>64||!std::isfinite(periodNs)||periodNs<=0)return 0;
    const auto mask=bits==64?~uint64_t{0}:((uint64_t{1}<<bits)-1);
    const double ms=double((end-begin)&mask)*periodNs*1e-6;
    return std::isfinite(ms)&&ms>0?ms:0;
}
} // namespace aiverse::gfx::vk
