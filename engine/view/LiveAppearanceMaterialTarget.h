// Genre-independent optimistic identity for a live material edit.
// No Model or GPU ownership; hosts resolve the actual target at commit time.
#pragma once
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>

namespace aiverse::view {
struct LiveAppearanceMaterialTarget {
    uint64_t publication=0,version=0;
    size_t material=0;
    bool matches(uint64_t actualPublication,uint64_t actualVersion,size_t materialCount)const noexcept {
        return publication!=0&&version!=0&&publication==actualPublication&&
            version==actualVersion&&material<materialCount&&
            version!=std::numeric_limits<uint64_t>::max();
    }
    uint64_t nextVersion()const noexcept {
        return version&&version!=std::numeric_limits<uint64_t>::max()?version+1:0;
    }
};
// Decimal strings avoid loss of 64-bit publication IDs over JSON/JS.
inline uint64_t parseAppearanceIdentity(std::string_view value)noexcept {
    if(value.empty()||value.size()>20||value.front()=='0')return 0;
    uint64_t result=0;
    const auto parsed=std::from_chars(value.data(),value.data()+value.size(),result);
    return parsed.ec==std::errc{}&&parsed.ptr==value.data()+value.size()?result:0;
}
} // namespace aiverse::view
