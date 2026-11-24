#pragma once
#include "Fixed.h"
#include <optional>
#include <cstddef>
namespace aiverse {
inline bool fxNumericalBounded(Fixed x){constexpr int64_t l=int64_t(1)<<30;return x.raw>=-l&&x.raw<=l;}
// Callbacks return optional<Fixed>; invalid samples abort the calculation.
template<class Fn> inline std::optional<Fixed> fxIntegrateTrapezoid(Fn f,Fixed a,Fixed b,size_t steps=128){
    if(!fxNumericalBounded(a)||!fxNumericalBounded(b)||!steps||steps>4096)return {};
    auto prev=f(a);if(!prev||!fxNumericalBounded(*prev))return {};
    Fixed sum{},x=a;
    for(size_t i=1;i<=steps;++i){
        auto next=a+(b-a)*Fixed::fromRatio(i,steps);
        auto y=f(next);if(!y||!fxNumericalBounded(*y))return {};
        sum+=((*prev+*y)/2)*(next-x);x=next;prev=y;
    }return sum;
}
struct FixedRoot {Fixed value;bool converged;};
template<class Fn> inline std::optional<FixedRoot> fxBisect(Fn f,Fixed a,Fixed b,Fixed tolerance=Fixed::fromRaw(1),size_t budget=64){
    if(!fxNumericalBounded(a)||!fxNumericalBounded(b)||a>b||tolerance.raw<=0||!budget||budget>1024)return {};
    auto x=f(a),y=f(b);if(!x||!y)return {};
    if(!x->raw)return FixedRoot{a,true};if(!y->raw)return FixedRoot{b,true};
    if((x->raw<0)==(y->raw<0))return {};
    for(size_t i=0;i<budget;++i){
        auto m=a+(b-a)/2;
        if(b.raw-a.raw<=tolerance.raw||m==a||m==b)return FixedRoot{m,true};
        auto z=f(m);if(!z)return {};
        if(!z->raw)return FixedRoot{m,true};
        if((z->raw<0)==(x->raw<0)){a=m;x=z;}else b=m;
    }return FixedRoot{a+(b-a)/2,false};
}
enum class FixedIntegrator {Euler,Midpoint,RungeKutta4};
// Scalar ODE y'=f(t,y), one step. Bounds apply to intermediate states.
template<class Fn> inline std::optional<Fixed> fxOdeStep(Fn f,Fixed t,Fixed y,Fixed dt,FixedIntegrator mode){
    if(!fxNumericalBounded(t)||!fxNumericalBounded(y)||!fxNumericalBounded(dt)||dt.raw<0||dt>1_fx)return {};
    auto sample=[&](Fixed x,Fixed v)->std::optional<Fixed>{
        if(!fxNumericalBounded(x)||!fxNumericalBounded(v))return {};
        auto k=f(x,v);if(!k||!fxNumericalBounded(*k))return {};return k;
    };
    auto a=sample(t,y);if(!a)return {};
    Fixed out;
    if(mode==FixedIntegrator::Euler)out=y+dt*(*a);
    else{
        auto b=sample(t+dt/2,y+dt*(*a)/2);if(!b)return {};
        if(mode==FixedIntegrator::Midpoint)out=y+dt*(*b);
        else{
            auto c=sample(t+dt/2,y+dt*(*b)/2);if(!c)return {};
            auto d=sample(t+dt,y+dt*(*c));if(!d)return {};
            out=y+dt*((*a+*b*2+*c*2+*d)/6);
        }
    }
    if(!fxNumericalBounded(out))return {};return out;
}
struct FixedVerletState {Fixed position,previous;};
inline std::optional<FixedVerletState> fxVerletStep(FixedVerletState s,Fixed acceleration,Fixed dt){
    if(!fxNumericalBounded(s.position)||!fxNumericalBounded(s.previous)||!fxNumericalBounded(acceleration)||dt.raw<0||dt>1_fx)return {};
    auto p=s.position*2-s.previous+acceleration*(dt*dt);
    if(!fxNumericalBounded(p))return {};return FixedVerletState{p,s.position};
}
}
