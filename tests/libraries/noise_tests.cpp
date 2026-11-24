#include <aiverselib/algorithms/Noise.h>
#include <cstdlib>
#include <iostream>

#define CHECK(...) do { if (!(__VA_ARGS__)) { std::cerr << "Noise check failed at line " << __LINE__ << ": " << #__VA_ARGS__ << '\n'; std::abort(); } } while(false)
using namespace aiverse::lib::algorithms;

template<std::size_t N> void verify() {
    std::array<int32_t,N> period{};
    std::array<int64_t,N> p{};
    for(std::size_t j=0;j<N;++j) { period[j]=int32_t(j+3); p[j]=-81001+int64_t(j)*35537; }
    for(uint64_t seed=0;seed<64;++seed) {
        auto v=periodicValueNoiseSample(p,seed,period);
        auto g=periodicGradientNoiseSample(p,seed,period);
        CHECK(v&&g);
        CHECK(v->value==*periodicValueNoise(p,seed,period));
        CHECK(g->value==*periodicGradientNoise(p,seed,period));
        CHECK(g==periodicGradientNoiseSample(p,seed,period));
        for(std::size_t axis=0;axis<N;++axis) {
            auto q=p; q[axis]+=int64_t(period[axis])*noiseOne;
            CHECK(v==periodicValueNoiseSample(q,seed,period));
            CHECK(g==periodicGradientNoiseSample(q,seed,period));
            auto lo=p,hi=p; lo[axis]-=256; hi[axis]+=256;
            auto a=periodicGradientNoiseSample(lo,seed,period);
            auto b=periodicGradientNoiseSample(hi,seed,period);
            CHECK(a&&b);
            const auto estimate=(b->value-a->value)*128;
            CHECK(std::abs(estimate-g->derivative[axis])<=1500);
        }
        for(auto mode:{NoiseFractalMode::FBm,NoiseFractalMode::Turbulence,NoiseFractalMode::Ridged}) {
            NoiseFractalOptions o{}; o.mode=mode; o.octaves=5;
            auto f=fractalNoiseSample(p,seed,period,o);
            CHECK(f&&f==fractalNoiseSample(p,seed,period,o));
            CHECK(f->value==*fractalNoise(p,seed,period,o));
            if(mode!=NoiseFractalMode::FBm) CHECK(f->value>=0);
            for(std::size_t axis=0;axis<N;++axis) {
                auto q=p; q[axis]-=int64_t(period[axis])*noiseOne;
                CHECK(f==fractalNoiseSample(q,seed,period,o));
            }
        }
        NoiseFractalOptions single{}; single.octaves=1;
        CHECK(fractalNoiseSample(p,seed,period,single)==g);
        single.basis=NoiseBasis::Value;
        CHECK(fractalNoiseSample(p,seed,period,single)==v);
        p[0]+=173;
    }
    std::array<int64_t,N> lattice{};
    CHECK(periodicGradientNoise(lattice,1,period)==0);
    auto invalid=p; invalid[0]=(int64_t(1)<<46)+1;
    CHECK(!periodicGradientNoiseSample(invalid,1,period));
    auto badPeriod=period; badPeriod[0]=-1;
    CHECK(!periodicValueNoiseSample(p,1,badPeriod));
    NoiseFractalOptions bad{}; bad.octaves=0;
    CHECK(!fractalNoiseSample(p,1,period,bad));
    bad.octaves=16; bad.lacunarity=8;
    CHECK(!fractalNoiseSample(p,1,period,bad));
    bad.octaves=2; bad.lacunarity=2;
    invalid[0]=(int64_t(1)<<46);
    CHECK(!fractalNoiseSample(invalid,1,period,bad));
    badPeriod=period; badPeriod[0]=100000000;
    CHECK(!fractalNoiseSample(p,1,badPeriod,bad));
    bad.gainQ16=-1;
    CHECK(!fractalNoiseSample(p,1,period,bad));
}
int main() {
    verify<2>();
    verify<3>();
    std::cout << "Noise stage 45 checks passed\n";
}
