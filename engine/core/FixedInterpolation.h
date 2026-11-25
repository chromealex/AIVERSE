#pragma once
#include "Fixed.h"
#include <optional>
namespace aiverse {
// Bounded endpoints prevent signed overflow in differences and interpolation.
inline std::optional<Fixed> fxLerpBounded(Fixed a,Fixed b,Fixed t){
    constexpr int64_t limit=int64_t(1)<<60;
    if(a.raw < -limit||a.raw>limit||b.raw < -limit||b.raw>limit)return {};
    t=fxClamp(t,Fixed{},Fixed::fromInt(1));
    return a+(b-a)*t;
}
enum class FixedEase {Linear,InQuad,OutQuad,InOutQuad,SmoothStep,SmootherStep};
inline Fixed fxEase(Fixed t,FixedEase mode){
    auto one=Fixed::fromInt(1);t=fxClamp(t,Fixed{},one);
    switch(mode){
    case FixedEase::InQuad:return t*t;
    case FixedEase::OutQuad:return one-(one-t)*(one-t);
    case FixedEase::InOutQuad:return t.raw<Fixed::kOne/2?t*t*2:one-(one-t)*(one-t)*2;
    case FixedEase::SmoothStep:return fxClamp(t*t*(Fixed::fromInt(3)-t*2),Fixed{},one);
    case FixedEase::SmootherStep:return fxClamp(t*t*t*(t*(t*6-Fixed::fromInt(15))+Fixed::fromInt(10)),Fixed{},one);
    default:return t;
    }
}
inline std::optional<Fixed> fxBezierBounded(Fixed a,Fixed b,Fixed c,Fixed d,Fixed t){
    auto ab=fxLerpBounded(a,b,t),bc=fxLerpBounded(b,c,t),cd=fxLerpBounded(c,d,t);
    if(!ab||!bc||!cd)return {};
    auto x=fxLerpBounded(*ab,*bc,t),y=fxLerpBounded(*bc,*cd,t);
    if(!x||!y)return {};return fxLerpBounded(*x,*y,t);
}
inline std::optional<Vec3> fxLerpBounded(Vec3 a,Vec3 b,Fixed t){
    auto x=fxLerpBounded(a.x,b.x,t),y=fxLerpBounded(a.y,b.y,t),z=fxLerpBounded(a.z,b.z,t);
    if(!x||!y||!z)return {};return Vec3{*x,*y,*z};
}
}
