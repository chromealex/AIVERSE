#pragma once
#include "BigNumber.h"
#include <limits>

namespace aiverse::lib::strategy {
// One discrete tick: next[i] = constant[i] + sum(matrix[i][j] * current[j]).
// Simultaneous, nonnegative, uncapped affine production. Not a consumption/capacity model.
struct ProductionLaw {
    std::vector<std::vector<BigNumber>> matrix;
    std::vector<BigNumber> constant;
};
struct OfflineState {uint64_t tick=0;std::vector<BigNumber> amounts;bool operator==(const OfflineState&)const=default;};
inline bool valid(const ProductionLaw& law) {
    const size_t n=law.constant.size();
    if(n==0||n>16||law.matrix.size()!=n) return false;
    for(const auto& r:law.matrix) if(r.size()!=n) return false;
    return true;
}
inline bool valid(const OfflineState& s){return !s.amounts.empty()&&s.amounts.size()<=16;}
namespace offline_detail {
using Matrix=std::vector<std::vector<BigNumber>>;
inline std::optional<Matrix> multiply(const Matrix& a,const Matrix& b) {
    const size_t n=a.size();Matrix out(n,std::vector<BigNumber>(n));
    for(size_t i=0;i<n;++i) for(size_t k=0;k<n;++k) {
        if(a[i][k].zero()) continue;
        for(size_t j=0;j<n;++j) {
            if(b[k][j].zero()) continue;
            auto p=a[i][k].times(b[k][j]);if(!p)return {};
            auto s=out[i][j].plus(*p);if(!s)return {};out[i][j]=std::move(*s);
        }
    }
    return out;
}
}
inline bool advanceOffline(const ProductionLaw& law,OfflineState& state,uint64_t now,uint64_t maxTicks=1000000000) {
    if(!valid(law)||!valid(state)||state.amounts.size()!=law.constant.size()||now<state.tick||now-state.tick>maxTicks) return false;
    uint64_t steps=now-state.tick;if(!steps)return true;
    const size_t n=state.amounts.size()+1;
    offline_detail::Matrix power(n,std::vector<BigNumber>(n)),result=power;
    for(size_t i=0;i<n;++i)result[i][i]=BigNumber{1};
    for(size_t i=0;i<n-1;++i){for(size_t j=0;j<n-1;++j)power[i][j]=law.matrix[i][j];power[i][n-1]=law.constant[i];}
    power[n-1][n-1]=BigNumber{1};
    while(steps) {
        if(steps&1){auto p=offline_detail::multiply(result,power);if(!p)return false;result=std::move(*p);}
        steps>>=1;
        if(steps){auto p=offline_detail::multiply(power,power);if(!p)return false;power=std::move(*p);}
    }
    auto out=state;
    for(size_t i=0;i<n-1;++i){auto value=result[i][n-1];for(size_t j=0;j<n-1;++j){auto p=result[i][j].times(state.amounts[j]);if(!p)return false;auto s=value.plus(*p);if(!s)return false;value=std::move(*s);}out.amounts[i]=std::move(value);}
    out.tick=now;state=std::move(out);return true;
}
}
