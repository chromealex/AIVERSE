#pragma once
#include "FixedLinear.h"
namespace aiverse {
template<size_t R,size_t C> struct FixedQR {
    std::array<Fixed,R*C> q{};
    FixedMatrix<C> r;
};
// Modified Gram-Schmidt, thin QR, full column rank required.
template<size_t R,size_t C> inline std::optional<FixedQR<R,C>> fxQR(const std::array<Fixed,R*C>& a,Fixed pivotMin=Fixed::fromRaw(256)){
    static_assert(R>=C&&C>=1&&R<=64&&C<=16);
    if(pivotMin.raw<=0)return {};
    for(auto x:a)if(!fxLinearBounded(x))return {};
    FixedQR<R,C> out;
    for(size_t j=0;j<C;++j){
        std::array<Fixed,R> v{};
        for(size_t i=0;i<R;++i)v[i]=a[i*C+j];
        for(size_t k=0;k<j;++k){
            Fixed d{};for(size_t i=0;i<R;++i)d+=out.q[i*C+k]*v[i];
            if(!fxLinearBounded(d))return {};out.r.at(k,j)=d;
            for(size_t i=0;i<R;++i){v[i]-=out.q[i*C+k]*d;if(!fxLinearBounded(v[i]))return {};}
        }
        Fixed norm{};for(auto x:v)norm+=x*x;
        auto n=fxSqrt(norm);if(n<pivotMin||!fxLinearBounded(n))return {};
        out.r.at(j,j)=n;
        for(size_t i=0;i<R;++i){out.q[i*C+j]=v[i]/n;if(!fxLinearBounded(out.q[i*C+j]))return {};}
    }return out;
}
template<size_t R,size_t C> inline std::optional<std::array<Fixed,C>> fxLeastSquares(const std::array<Fixed,R*C>& a,const std::array<Fixed,R>& b){
    for(auto v:b)if(!fxLinearBounded(v))return {};
    auto qr=fxQR<R,C>(a);if(!qr)return {};
    std::array<Fixed,C> x{};
    for(size_t j=0;j<C;++j){for(size_t i=0;i<R;++i)x[j]+=qr->q[i*C+j]*b[i];if(!fxLinearBounded(x[j]))return {};}
    for(size_t i=C;i-->0;){
        for(size_t j=i+1;j<C;++j){x[i]-=qr->r.at(i,j)*x[j];if(!fxLinearBounded(x[i]))return {};}
        x[i]=x[i]/qr->r.at(i,i);if(!fxLinearBounded(x[i]))return {};
    }return x;
}
template<size_t N> struct FixedEigenEstimate {Fixed value,residual;std::array<Fixed,N> vector;bool converged;};
// Symmetric matrix, caller seed must overlap dominant eigenspace.
template<size_t N> inline std::optional<FixedEigenEstimate<N>> fxPowerIteration(const FixedMatrix<N>& a,std::array<Fixed,N> x,size_t budget=64,Fixed tolerance=Fixed::fromRaw(64)){
    static_assert(N>=1&&N<=16);
    if(!budget||budget>1024||tolerance.raw<=0)return {};
    for(size_t i=0;i<N;++i)for(size_t j=0;j<N;++j)if(!fxLinearBounded(a.at(i,j))||a.at(i,j)!=a.at(j,i))return {};
    auto normalize=[](std::array<Fixed,N>& v){
        Fixed n{};for(auto c:v){if(!fxLinearBounded(c))return false;n+=c*c;}
        n=fxSqrt(n);if(n.raw<256)return false;
        for(auto& c:v)c=c/n;return true;
    };
    if(!normalize(x))return {};
    FixedEigenEstimate<N> out{};
    for(size_t step=0;step<budget;++step){
        std::array<Fixed,N> y{};
        for(size_t i=0;i<N;++i){for(size_t j=0;j<N;++j)y[i]+=a.at(i,j)*x[j];if(!fxLinearBounded(y[i]))return {};}
        Fixed lambda{},norm{};for(size_t i=0;i<N;++i){lambda+=x[i]*y[i];norm+=x[i]*x[i];}
        if(norm.raw<256)return {};lambda=lambda/norm;if(!fxLinearBounded(lambda))return {};
        Fixed residual{};for(size_t i=0;i<N;++i)residual=fxMax(residual,fxAbs(y[i]-x[i]*lambda));
        out={lambda,residual,x,residual<=tolerance};if(out.converged)return out;
        if(!normalize(y))return {};x=y;
    }return out;
}
}
