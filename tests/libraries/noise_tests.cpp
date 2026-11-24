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

template<std::size_t N> void verifyComposition() {
    std::array<int64_t,N> p{};
    std::array<int32_t,N> period{};
    for(std::size_t j=0;j<N;++j) { p[j]=-32123+int64_t(j)*19673; period[j]=int32_t(j+3); }
    NoiseFractalOptions single{}; single.octaves=1;
    NoiseFractalOptions billow=single; billow.mode=NoiseFractalMode::Billow;
    NoiseWarpOptions warp{}; warp.carrier=single; warp.displacement=single;
    warp.strengthQ16=16384;
    for(uint64_t seed=0;seed<32;++seed) {
        auto scalar=fractalNoiseSample(p,seed,period,single);
        auto b=fractalNoiseSample(p,seed,period,billow);
        auto v=vectorNoiseSample(p,seed,period,single);
        auto values=vectorNoise(p,seed,period,single);
        auto w=domainWarpNoiseSample(p,seed,period,warp);
        auto c=curlNoise(p,seed,period,single);
        CHECK(scalar&&b&&v&&values&&w&&c);
        CHECK(b->value==2*std::abs(scalar->value)-noiseOne);
        const auto sign=(scalar->value>0)-(scalar->value<0);
        for(std::size_t j=0;j<N;++j) {
            CHECK(b->derivative[j]==2*sign*scalar->derivative[j]);
            CHECK((*values)[j]==(*v)[j].value);
            CHECK((*v)[j]==*fractalNoiseSample(p,deriveSeed(seed,0x4e4f495345564543ull,j),period,single));
            auto q=p; q[j]+=int64_t(period[j])*noiseOne;
            CHECK(v==vectorNoiseSample(q,seed,period,single));
            CHECK(w==domainWarpNoiseSample(q,seed,period,warp));
            CHECK(c==curlNoise(q,seed,period,single));
            CHECK(b==fractalNoiseSample(q,seed,period,billow));
            q=p; q[j]-=int64_t(period[j])*noiseOne;
            CHECK(w==domainWarpNoiseSample(q,seed,period,warp));
            auto lo=p,hi=p; lo[j]-=256; hi[j]+=256;
            auto a=domainWarpNoise(lo,seed,period,warp);
            auto z=domainWarpNoise(hi,seed,period,warp);
            CHECK(a&&z);
            CHECK(std::abs((*z-*a)*128-w->derivative[j])<=3000);
        }
        CHECK(w==domainWarpNoiseSample(p,seed,period,warp));
        CHECK(w->value==*domainWarpNoise(p,seed,period,warp));
        auto identity=warp; identity.strengthQ16=0; identity.displacement.octaves=0;
        CHECK(domainWarpNoiseSample(p,seed,period,identity)==scalar);
        if constexpr(N==2) {
            CHECK((*c)[0]==scalar->derivative[1]&&(*c)[1]==-scalar->derivative[0]);
        } else {
            CHECK((*c)[0]==(*v)[2].derivative[1]-(*v)[1].derivative[2]);
            CHECK((*c)[1]==(*v)[0].derivative[2]-(*v)[2].derivative[0]);
            CHECK((*c)[2]==(*v)[1].derivative[0]-(*v)[0].derivative[1]);
        }
        p[0]+=713;
    }
    auto bad=warp; bad.strengthQ16=8*noiseOne+1;
    CHECK(!domainWarpNoiseSample(p,1,period,bad));
    bad=warp; bad.displacement.octaves=0;
    CHECK(!domainWarpNoiseSample(p,1,period,bad));
    auto invalid=p; invalid[0]=(int64_t(1)<<46)+1;
    CHECK(!vectorNoiseSample(invalid,1,period,single));
    CHECK(!curlNoise(invalid,1,period,single));
    CHECK(!domainWarpNoiseSample(invalid,1,period,warp));
    auto badPeriod=period; badPeriod[0]=-1;
    CHECK(!vectorNoiseSample(p,1,badPeriod,single));
    CHECK(!curlNoise(p,1,badPeriod,single));
    CHECK(!domainWarpNoiseSample(p,1,badPeriod,warp));
    constexpr int64_t bound=int64_t(1)<<38;
    CHECK(noise_detail::chainProduct(bound,-bound)==-(int64_t(1)<<60));
    CHECK(noise_detail::chainProduct(-12345,67890)==-12345*int64_t(67890)/noiseOne);
    CHECK(!noise_detail::chainProduct(bound+1,1));
}

int main() {
    verify<2>();
    verify<3>();
    verifyComposition<2>();
    verifyComposition<3>();
    std::cout << "Noise stage 46 checks passed\n";
}
