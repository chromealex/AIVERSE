// Shared deterministic integer-centimetre area scale arithmetic.
// Used by Studio's numeric preview and atomic MapDoc batch commit.
#pragma once
#include <cmath>
#include <cstdint>
#include <limits>
namespace aiverse::evolve::scale_math {
struct AxisResult { std::int64_t center=0,half=0; };
inline bool transform(std::int64_t center,std::int64_t half,
                      std::int64_t pivot,std::int64_t numerator,
                      std::int64_t denominator,bool individual,
                      AxisResult& result){
    if(half<0||numerator<1||denominator<1||
       numerator>1000000||denominator>1000000)return false;
    const long double h=(long double)half*numerator/denominator;
    const long double p=individual?(long double)center:(long double)pivot;
    const long double c=p+((long double)center-p)*numerator/denominator;
    constexpr auto limit=(long double)std::numeric_limits<std::int64_t>::max()/4;
    if(!std::isfinite(h)||!std::isfinite(c)||
       std::abs(h)>limit||std::abs(c)>limit)return false;
    result={std::llround(c),std::llround(h)};
    return true;
}
} // namespace aiverse::evolve::scale_math
