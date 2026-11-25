#pragma once
#include "FixedInterpolation.h"
#include <vector>
#include <algorithm>
#include <initializer_list>
namespace aiverse {
inline bool fxCurveBounded(Vec3 p){
    constexpr int64_t l=int64_t(1)<<30;
    for(auto v:{p.x,p.y,p.z})if(v.raw < -l||v.raw>l)return false;
    return true;
}
inline std::optional<Vec3> fxHermite(Vec3 a,Vec3 b,Vec3 ta,Vec3 tb,Fixed t){
    for(auto p:{a,b,ta,tb})if(!fxCurveBounded(p))return {};
    t=fxClamp(t,0_fx,1_fx);auto t2=t*t,t3=t2*t;
    return a*(t3*2-t2*3+1_fx)+ta*(t3-t2*2+t)+b*(t2*3-t3*2)+tb*(t3-t2);
}
inline std::optional<Vec3> fxHermiteDerivative(Vec3 a,Vec3 b,Vec3 ta,Vec3 tb,Fixed t){
    for(auto p:{a,b,ta,tb})if(!fxCurveBounded(p))return {};
    t=fxClamp(t,0_fx,1_fx);auto t2=t*t;
    return a*(t2*6-t*6)+ta*(t2*3-t*4+1_fx)+b*(t*6-t2*6)+tb*(t2*3-t*2);
}
inline std::optional<Vec3> fxCatmullRom(Vec3 a,Vec3 b,Vec3 c,Vec3 d,Fixed t){
    for(auto p:{a,b,c,d})if(!fxCurveBounded(p))return {};
    auto half=Fixed::fromRatio(1,2);
    return fxHermite(b,c,(c-a)*half,(d-b)*half,t);
}
inline std::optional<Vec3> fxUniformBSpline(Vec3 a,Vec3 b,Vec3 c,Vec3 d,Fixed t){
    for(auto p:{a,b,c,d})if(!fxCurveBounded(p))return {};
    t=fxClamp(t,0_fx,1_fx);auto t2=t*t,t3=t2*t,u=1_fx-t;
    return (a*(u*u*u)+b*(t3*3-t2*6+4_fx)+c*(t3*(-3)+t2*3+t*3+1_fx)+d*t3)/6_fx;
}
struct FixedArcTable {std::vector<Fixed> cumulative;};
template<class Curve> inline std::optional<FixedArcTable> fxSampleArcLength(Curve curve,size_t segments=128){
    if(!segments||segments>4096)return {};
    auto prev=curve(0_fx);if(!prev||!fxCurveBounded(*prev))return {};
    FixedArcTable out;out.cumulative.push_back(0_fx);
    for(size_t i=1;i<=segments;++i){
        auto p=curve(Fixed::fromRatio(i,segments));if(!p||!fxCurveBounded(*p))return {};
        out.cumulative.push_back(out.cumulative.back()+(*p-*prev).length());prev=p;
    }return out;
}
inline std::optional<Fixed> fxArcParameter(const FixedArcTable& table,Fixed distance){
    auto& a=table.cumulative;if(a.size()<2||a.size()>4097||a.front().raw!=0||!std::is_sorted(a.begin(),a.end())||a.back().raw>(int64_t(1)<<46))return {};
    if(distance.raw<=0||a.back().raw==0)return 0_fx;
    if(distance>=a.back())return 1_fx;
    auto i=size_t(std::lower_bound(a.begin(),a.end(),distance)-a.begin());
    auto t=(distance-a[i-1])/(a[i]-a[i-1]);
    return (Fixed::fromInt(i-1)+t)/Fixed::fromInt(a.size()-1);
}
}
