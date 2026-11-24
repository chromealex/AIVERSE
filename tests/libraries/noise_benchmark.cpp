// Standalone native benchmark; no engine or third-party dependencies.
#include "aiverselib/algorithms/NoiseBatch.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>

using namespace aiverse::lib::algorithms;
using Clock=std::chrono::steady_clock;
constexpr std::array backends{NoiseBatchBackend::Scalar,NoiseBatchBackend::Auto,
    NoiseBatchBackend::SSE2,NoiseBatchBackend::AVX2,NoiseBatchBackend::NEON};
const char* name(NoiseBatchBackend b){
    switch(b){
        case NoiseBatchBackend::Scalar:return "Scalar";
        case NoiseBatchBackend::Auto:return "Auto";
        case NoiseBatchBackend::SSE2:return "SSE2";
        case NoiseBatchBackend::AVX2:return "AVX2";
        case NoiseBatchBackend::NEON:return "NEON";
    }
    return "unknown";
}
uint64_t next(uint64_t& s){
    s=s*6364136223846793005ULL+1442695040888963407ULL;
    return s;
}
uint64_t checksum(const std::vector<int64_t>& values,const std::vector<uint8_t>& mask){
    uint64_t h=14695981039346656037ULL;
    for(size_t i=0;i<values.size();++i){
        h=(h^uint64_t(values[i]))*1099511628211ULL;
        h=(h^mask[i])*1099511628211ULL;
    }
    return h;
}
volatile uint64_t observed=0;
template<size_t N> void bench(size_t count,bool soa,bool periodic,NoiseBatchPolicy policy){
    using Point=std::array<int64_t,N>;
    std::vector<Point> points(count);
    std::array<std::vector<int64_t>,N> axes;
    for(auto& a:axes)a.resize(count);
    uint64_t rng=0x19842fd9ULL;
    for(size_t i=0;i<count;++i)for(size_t j=0;j<N;++j)
        axes[j][i]=points[i][j]=int64_t((next(rng)>>16)%134217728ULL)-67108864;
    std::array<std::span<const int64_t>,N> spans;
    for(size_t j=0;j<N;++j)spans[j]=axes[j];
    const auto input=soa?NoiseBatchPoints<N>::soa(spans):NoiseBatchPoints<N>::aos(points);
    std::array<int32_t,N> period{};
    if(periodic)for(size_t j=0;j<N;++j)period[j]=int32_t(31+2*j);
    std::vector<int64_t> reference(count),out(count);
    std::vector<uint8_t> referenceMask(count),mask(count);
    NoiseScalarBatchOptions opts{};
    opts.kind=NoiseScalarKind::Value;
    opts.backend=NoiseBatchBackend::Scalar;
    constexpr uint64_t seed=0xe0b1c2d3a4ULL;
    auto checkReport=[&](const NoiseBatchReport& report){
        if(!report.complete()||report.written!=count||report.invalidCount)
            throw std::runtime_error("unexpected batch report");
    };
    checkReport(noiseBatchValues(input,reference,seed,period,opts,policy,referenceMask));
    const auto expected=checksum(reference,referenceMask);
    constexpr size_t trials=7;
    const size_t repeats=std::max<size_t>(1,65536/count);
    std::array<std::array<double,trials>,backends.size()> elapsed{};
    std::array<NoiseBatchBackend,backends.size()> actual{};
    std::array<bool,backends.size()> enabled{};
    for(size_t k=0;k<backends.size();++k){
        enabled[k]=noiseBatchBackendAvailable(backends[k]);
        if(!enabled[k])continue;
        opts.backend=backends[k];
        auto report=noiseBatchValues(input,out,seed,period,opts,policy,mask);
        checkReport(report);
        actual[k]=report.backend;
        if(out!=reference||mask!=referenceMask)throw std::runtime_error("warmup parity");
    }
    // Rotate backend order each trial to reduce systematic thermal/order bias.
    for(size_t t=0;t<trials;++t)for(size_t j=0;j<backends.size();++j){
        const size_t k=(j+t)%backends.size();
        if(!enabled[k])continue;
        opts.backend=backends[k];
        const auto start=Clock::now();
        for(size_t i=0;i<repeats;++i){
            auto report=noiseBatchValues(input,out,seed,period,opts,policy,mask);
            checkReport(report);
            if(report.backend!=actual[k])throw std::runtime_error("backend drift");
            // Make every batch observable without including a full checksum in timing.
            observed=uint64_t(out[i%count]);
        }
        const auto end=Clock::now();
        elapsed[k][t]=std::chrono::duration<double,std::nano>(end-start).count()/
            (double(count)*double(repeats));
        if(out!=reference||mask!=referenceMask||checksum(out,mask)!=expected)
            throw std::runtime_error("timed parity");
    }
    for(size_t k=0;k<backends.size();++k){
        std::cout<<N<<','<<(soa?"SoA":"AoS")<<','<<count<<','<<periodic<<','
            <<(policy==NoiseBatchPolicy::Atomic?"Atomic":"MarkInvalid")<<','<<name(backends[k])<<',';
        if(!enabled[k]){std::cout<<"unavailable,0,0,0,0,"<<expected<<'\n';continue;}
        auto v=elapsed[k];std::sort(v.begin(),v.end());
        std::cout<<name(actual[k])<<','<<repeats<<','<<v.front()<<','<<v[trials/2]<<','
            <<v.back()<<','<<expected<<'\n';
    }
}
int main(int argc,char** argv){
    try{
#if defined(__clang__)
        std::cout<<"# compiler=Clang "<<__clang_version__<<'\n';
#elif defined(__GNUC__)
        std::cout<<"# compiler=GCC "<<__VERSION__<<'\n';
#elif defined(_MSC_VER)
        std::cout<<"# compiler=MSVC "<<_MSC_VER<<'\n';
#endif
        std::cout<<"# hardware="<<(argc>1?argv[1]:"unspecified (pass CPU/OS as argv[1])")<<'\n';
        std::cout<<"# warmup=1 batch; trials=7; valid points only; ns/sample includes report checks and observable write\n";
        std::cout<<"dimension,layout,count,periodic,policy,requested,actual,repeats,min_ns,median_ns,max_ns,checksum\n";
        std::cout<<std::fixed<<std::setprecision(3);
        for(size_t n:std::array<size_t,3>{17,1024,65536})
            for(bool soa:{false,true})for(bool periodic:{false,true})
                for(auto policy:{NoiseBatchPolicy::Atomic,NoiseBatchPolicy::MarkInvalid}){
                    bench<2>(n,soa,periodic,policy);bench<3>(n,soa,periodic,policy);
                }
        return EXIT_SUCCESS;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return EXIT_FAILURE;}
}
