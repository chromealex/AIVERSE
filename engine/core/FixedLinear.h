#pragma once
#include "FixedMatrix.h"
#include <algorithm>
namespace aiverse {
// Conservative raw bound permits intermediate products and subtraction.
inline bool fxLinearBounded(Fixed v){constexpr int64_t l=int64_t(1)<<30;return v.raw>=-l&&v.raw<=l;}
template<size_t N,size_t M> inline std::optional<std::array<Fixed,N*M>> fxLinearSolve(const FixedMatrix<N>& input,std::array<Fixed,N*M> rhs,Fixed pivotMin=Fixed::fromRaw(16)){
    static_assert(N>=1&&N<=16&&M>=1&&M<=16);
    if(pivotMin.raw<=0)return {};
    auto a=input;
    for(auto v:a.values)if(!fxLinearBounded(v))return {};
    for(auto v:rhs)if(!fxLinearBounded(v))return {};
    for(size_t k=0;k<N;++k){
        size_t p=k;for(size_t i=k+1;i<N;++i)if(fxAbs(a.at(i,k))>fxAbs(a.at(p,k)))p=i;
        if(fxAbs(a.at(p,k))<pivotMin)return {};
        for(size_t j=0;j<N;++j)std::swap(a.at(k,j),a.at(p,j));
        for(size_t j=0;j<M;++j)std::swap(rhs[k*M+j],rhs[p*M+j]);
        auto pivot=a.at(k,k);
        for(size_t j=k;j<N;++j){a.at(k,j)=a.at(k,j)/pivot;if(!fxLinearBounded(a.at(k,j)))return {};}
        for(size_t j=0;j<M;++j){rhs[k*M+j]=rhs[k*M+j]/pivot;if(!fxLinearBounded(rhs[k*M+j]))return {};}
        a.at(k,k)=1_fx;
        for(size_t i=0;i<N;++i)if(i!=k){
            auto factor=a.at(i,k);
            for(size_t j=k+1;j<N;++j){a.at(i,j)-=factor*a.at(k,j);if(!fxLinearBounded(a.at(i,j)))return {};}
            for(size_t j=0;j<M;++j){rhs[i*M+j]-=factor*rhs[k*M+j];if(!fxLinearBounded(rhs[i*M+j]))return {};}
            a.at(i,k)=0_fx;
        }
    }return rhs;
}
template<size_t N> inline std::optional<FixedMatrix<N>> fxMatrixInverse(const FixedMatrix<N>& a,Fixed pivotMin=Fixed::fromRaw(16)){
    auto x=fxLinearSolve<N,N>(a,FixedMatrix<N>::identity().values,pivotMin);
    if(!x)return {};return FixedMatrix<N>{*x};
}
template<size_t N> struct FixedLU {FixedMatrix<N> factors;std::array<size_t,N> permutation;bool odd=false;};
template<size_t N> inline std::optional<FixedLU<N>> fxLU(const FixedMatrix<N>& input,Fixed pivotMin=Fixed::fromRaw(16)){
    static_assert(N>=1&&N<=16);
    if(pivotMin.raw<=0)return {};
    FixedLU<N> out{input,{},false};auto& a=out.factors;
    for(auto v:a.values)if(!fxLinearBounded(v))return {};
    for(size_t i=0;i<N;++i)out.permutation[i]=i;
    for(size_t k=0;k<N;++k){
        size_t p=k;for(size_t i=k+1;i<N;++i)if(fxAbs(a.at(i,k))>fxAbs(a.at(p,k)))p=i;
        if(fxAbs(a.at(p,k))<pivotMin)return {};
        if(p!=k){for(size_t j=0;j<N;++j)std::swap(a.at(k,j),a.at(p,j));std::swap(out.permutation[k],out.permutation[p]);out.odd=!out.odd;}
        for(size_t i=k+1;i<N;++i){
            auto f=a.at(i,k)/a.at(k,k);if(!fxLinearBounded(f))return {};a.at(i,k)=f;
            for(size_t j=k+1;j<N;++j){a.at(i,j)-=f*a.at(k,j);if(!fxLinearBounded(a.at(i,j)))return {};}
        }
    }return out;
}
template<size_t N> inline std::optional<Fixed> fxDeterminant(const FixedMatrix<N>& a){
    auto lu=fxLU(a);if(!lu)return {};
    Fixed d=lu->odd?-1_fx:1_fx;
    for(size_t i=0;i<N;++i){d*=lu->factors.at(i,i);if(!fxLinearBounded(d))return {};}
    return d;
}
}
