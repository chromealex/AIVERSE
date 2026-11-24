#pragma once
#include "FixedQuaternion.h"
#include <array>
namespace aiverse {
template<size_t N> struct FixedMatrix {
    std::array<Fixed,N*N> values{};
    Fixed& at(size_t r,size_t c){return values[r*N+c];}
    Fixed at(size_t r,size_t c)const{return values[r*N+c];}
    static FixedMatrix identity(){FixedMatrix m;for(size_t i=0;i<N;++i)m.at(i,i)=1_fx;return m;}
};
using FixedMatrix3=FixedMatrix<3>;
using FixedMatrix4=FixedMatrix<4>;
template<size_t N> inline bool fxMatrixBounded(const FixedMatrix<N>& m){
    constexpr int64_t limit=int64_t(1)<<36;
    for(auto v:m.values)if(v.raw < -limit||v.raw>limit)return false;return true;
}
template<size_t N> inline std::optional<FixedMatrix<N>> fxMatrixMultiply(const FixedMatrix<N>& a,const FixedMatrix<N>& b){
    static_assert(N==3||N==4);
    if(!fxMatrixBounded(a)||!fxMatrixBounded(b))return {};
    FixedMatrix<N> out;
    for(size_t i=0;i<N;++i)for(size_t j=0;j<N;++j)for(size_t k=0;k<N;++k)out.at(i,j)+=a.at(i,k)*b.at(k,j);
    if(!fxMatrixBounded(out))return {};return out;
}
template<size_t N> inline FixedMatrix<N> fxMatrixTranspose(const FixedMatrix<N>& a){
    FixedMatrix<N> out;for(size_t i=0;i<N;++i)for(size_t j=0;j<N;++j)out.at(i,j)=a.at(j,i);return out;
}
inline std::optional<FixedMatrix4> fxMatrixTRS(Vec3 translation,FixedQuaternion rotation,Vec3 scale){
    auto basis=fxQuaternionBasis(rotation);if(!basis)return {};
    constexpr int64_t l=int64_t(1)<<35;
    for(auto v:{translation.x,translation.y,translation.z,scale.x,scale.y,scale.z})if(v.raw < -l||v.raw>l)return {};
    auto x=basis->right*scale.x,y=basis->up*scale.y,z=basis->forward*scale.z;
    FixedMatrix4 m=FixedMatrix4::identity();
    m.values={x.x,y.x,z.x,translation.x,x.y,y.y,z.y,translation.y,x.z,y.z,z.z,translation.z,0_fx,0_fx,0_fx,1_fx};
    return m;
}
inline std::optional<Vec3> fxTransformAffine(const FixedMatrix4& m,Vec3 v,bool point=true){
    if(!fxMatrixBounded(m)||m.at(3,0).raw||m.at(3,1).raw||m.at(3,2).raw||m.at(3,3)!=1_fx)return {};
    constexpr int64_t l=int64_t(1)<<36;
    for(auto c:{v.x,v.y,v.z})if(c.raw < -l||c.raw>l)return {};
    std::array<Fixed,3> out{};
    for(size_t i=0;i<3;++i)out[i]=m.at(i,0)*v.x+m.at(i,1)*v.y+m.at(i,2)*v.z+(point?m.at(i,3):Fixed{});
    return Vec3{out[0],out[1],out[2]};
}
// Inverse of a TRS transform without shear. Reject near-zero scales.
inline std::optional<FixedMatrix4> fxInverseTRS(Vec3 translation,FixedQuaternion rotation,Vec3 scale){
    auto basis=fxQuaternionBasis(rotation);if(!basis)return {};
    constexpr int64_t l=int64_t(1)<<35;
    for(auto v:{translation.x,translation.y,translation.z,scale.x,scale.y,scale.z})if(v.raw < -l||v.raw>l)return {};
    for(auto v:{scale.x,scale.y,scale.z})if(fxAbs(v).raw<256)return {};
    auto x=basis->right/scale.x,y=basis->up/scale.y,z=basis->forward/scale.z;
    FixedMatrix4 m=FixedMatrix4::identity();
    m.values={x.x,x.y,x.z,-x.dot(translation),y.x,y.y,y.z,-y.dot(translation),z.x,z.y,z.z,-z.dot(translation),0_fx,0_fx,0_fx,1_fx};
    if(!fxMatrixBounded(m))return {};return m;
}
}
