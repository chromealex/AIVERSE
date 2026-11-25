#pragma once
#include "NoiseSimd.h"
#include <functional>
#include <limits>
#include <span>

namespace aiverse::lib::algorithms {
namespace noise_batch_detail {
// Pointer total ordering also covers distinct allocations. No integer-address
// arithmetic or relational comparisons of unrelated raw pointers.
template<class A,class B> inline bool overlaps(std::span<A> a,std::span<B> b) {
    if(a.empty()||b.empty()) return false;
    const std::less<const void*> less{};
    return less(a.data(),b.data()+b.size())&&less(b.data(),a.data()+a.size());
}
} // namespace noise_batch_detail

// Borrowed, read-only input. SoA axes may alias each other; all must have
// equal length. Outputs are typed contiguous spans (AoS for compound results).
template<std::size_t N> class NoiseBatchPoints {
    static_assert(N==2||N==3);
    using Point=std::array<int64_t,N>;
    bool soa_=false;
    std::span<const Point> aos_{};
    std::array<std::span<const int64_t>,N> axes_{};
public:
    static NoiseBatchPoints aos(std::span<const Point> points) {
        NoiseBatchPoints v; v.aos_=points; return v;
    }
    static NoiseBatchPoints soa(std::array<std::span<const int64_t>,N> axes) {
        NoiseBatchPoints v; v.soa_=true; v.axes_=axes; return v;
    }
    std::size_t size() const {return soa_?axes_[0].size():aos_.size();}
    bool valid() const {
        if(soa_) for(auto axis:axes_) if(axis.size()!=size()) return false;
        return true;
    }
    Point point(std::size_t i) const {
        if(!soa_) return aos_[i];
        Point p{}; for(std::size_t j=0;j<N;++j) p[j]=axes_[j][i];
        return p;
    }
    template<class T> bool overlaps(std::span<T> output) const {
        if(!soa_) return noise_batch_detail::overlaps(aos_,output);
        for(auto axis:axes_) if(noise_batch_detail::overlaps(axis,output)) return true;
        return false;
    }
};

enum class NoiseBatchPolicy { Atomic, MarkInvalid };
enum class NoiseBatchStatus { Complete, InvalidShape, InvalidOptions, Overlap, InvalidElement, Partial };
struct NoiseBatchReport {
    NoiseBatchStatus status=NoiseBatchStatus::Complete;
    std::size_t written=0;
    std::size_t invalidCount=0;
    std::size_t firstInvalid=std::numeric_limits<std::size_t>::max();
    NoiseBatchBackend backend=NoiseBatchBackend::Scalar; // actual executed SIMD kernel, if any
    bool complete() const {return status==NoiseBatchStatus::Complete;}
};

enum class NoiseScalarKind { Value, Gradient, Simplex, SimplexSkew, Fractal, DomainWarp };
struct NoiseScalarBatchOptions {
    NoiseScalarKind kind=NoiseScalarKind::Fractal;
    NoiseFractalOptions fractal{};
    NoiseWarpOptions warp{};
    NoiseBatchBackend backend=NoiseBatchBackend::Auto; // only Value values have a SIMD kernel
};

namespace noise_batch_detail {
template<std::size_t N> inline bool validFractal(std::array<int32_t,N> period,NoiseFractalOptions options) {
    // Delegate all settings, scaled-period and frequency bounds to the scalar
    // owner. At the origin coordinates cannot exceed scaled bounds. No warp
    // probe is used, since its derivative limits are point-dependent.
    return fractalNoiseSample(std::array<int64_t,N>{},0,period,options).has_value();
}
template<std::size_t N> inline bool validScalar(std::array<int32_t,N> period,NoiseScalarBatchOptions options) {
    if(!noise_simd_detail::validBackend(options.backend)||!noiseValid(std::array<int64_t,N>{},period)) return false;
    switch(options.kind) {
        case NoiseScalarKind::Value: case NoiseScalarKind::Gradient: case NoiseScalarKind::SimplexSkew:
            return true;
        case NoiseScalarKind::Simplex:
            for(auto p:period) if(p) return false;
            return true;
        case NoiseScalarKind::Fractal: return validFractal(period,options.fractal);
        case NoiseScalarKind::DomainWarp:
            return options.warp.strengthQ16>=-8*noiseOne&&options.warp.strengthQ16<=8*noiseOne&&
                validFractal(period,options.warp.carrier)&&
                (options.warp.strengthQ16==0||validFractal(period,options.warp.displacement));
    }
    return false;
}
template<std::size_t N> inline std::optional<NoiseSample<N>> scalarSample(
    std::array<int64_t,N> p,uint64_t seed,std::array<int32_t,N> period,NoiseScalarBatchOptions options) {
    switch(options.kind) {
        case NoiseScalarKind::Value: return periodicValueNoiseSample(p,seed,period);
        case NoiseScalarKind::Gradient: return periodicGradientNoiseSample(p,seed,period);
        case NoiseScalarKind::Simplex: return simplexNoiseSample(p,seed);
        case NoiseScalarKind::SimplexSkew: return simplexSkewNoiseSample(p,seed,period);
        case NoiseScalarKind::Fractal: return fractalNoiseSample(p,seed,period,options.fractal);
        case NoiseScalarKind::DomainWarp: return domainWarpNoiseSample(p,seed,period,options.warp);
    }
    return {};
}
template<std::size_t N> inline std::optional<int64_t> scalarValue(
    std::array<int64_t,N> p,uint64_t seed,std::array<int32_t,N> period,NoiseScalarBatchOptions options) {
    if(options.kind==NoiseScalarKind::Value) return periodicValueNoise(p,seed,period);
    auto s=scalarSample(p,seed,period,options);
    if(!s) return {};
    return s->value;
}

// Atomic performs a read-only validation pass, then a deterministic write
// pass. MarkInvalid is single-pass and requires a mask for nonempty inputs.
// Inputs and options must remain stable throughout the synchronous call.
// No allocation, no partial writes for invalid shape/config/overlap.
template<std::size_t N,class T> inline NoiseBatchStatus validate(
    const NoiseBatchPoints<N>& points,std::span<T> output,std::span<uint8_t> mask,
    NoiseBatchPolicy policy,bool validOptions) {
    const auto count=points.size();
    if(!points.valid()||output.size()!=count||(!mask.empty()&&mask.size()!=count)||
        (policy==NoiseBatchPolicy::MarkInvalid&&mask.size()!=count))
        return NoiseBatchStatus::InvalidShape;
    if(policy!=NoiseBatchPolicy::Atomic&&policy!=NoiseBatchPolicy::MarkInvalid)
        return NoiseBatchStatus::InvalidOptions;
    if(!validOptions) return NoiseBatchStatus::InvalidOptions;
    if(points.overlaps(output)||points.overlaps(mask)||overlaps(output,mask))
        return NoiseBatchStatus::Overlap;
    return NoiseBatchStatus::Complete;
}
inline void recordInvalid(NoiseBatchReport& report,std::size_t i) {
    if(report.invalidCount++==0) report.firstInvalid=i;
}
template<std::size_t N,class T,class Eval> inline NoiseBatchReport run(
    const NoiseBatchPoints<N>& points,std::span<T> output,std::span<uint8_t> mask,
    NoiseBatchPolicy policy,bool validOptions,Eval eval) {
    const auto status=validate(points,output,mask,policy,validOptions);
    if(status!=NoiseBatchStatus::Complete) return {status};
    const auto count=points.size();
    NoiseBatchReport report{};
    auto invalid=[&](std::size_t i) {recordInvalid(report,i);};
    if(policy==NoiseBatchPolicy::Atomic) {
        for(std::size_t i=0;i<count;++i) if(!eval(points.point(i))) invalid(i);
        if(report.invalidCount) {
            report.status=NoiseBatchStatus::InvalidElement;
            return report;
        }
    }
    for(std::size_t i=0;i<count;++i) {
        auto v=eval(points.point(i));
        if(v) {
            output[i]=*v; ++report.written;
            if(!mask.empty()) mask[i]=1;
        } else {
            invalid(i);
            mask[i]=0; // MarkInvalid always supplies a full mask.
        }
    }
    if(report.invalidCount) report.status=NoiseBatchStatus::Partial;
    return report;
}

// Full SIMD blocks may contain invalid lanes under MarkInvalid. Substitute
// safe points for them before the kernel; never write their output slots.
template<std::size_t N,std::size_t L,class Interpolate> inline std::size_t valueBlocks(
    const NoiseBatchPoints<N>& points,std::span<int64_t> output,std::span<uint8_t> mask,
    uint64_t seed,std::array<int32_t,N> period,NoiseBatchBackend backend,
    NoiseBatchReport& report,Interpolate interpolate) {
    std::size_t i=0;
    for(;points.size()-i>=L;i+=L) {
        noise_simd_detail::ValueBlock<N,L> block{};
        std::array<bool,L> valid{};
        bool any=false;
        for(std::size_t lane=0;lane<L;++lane) {
            auto p=points.point(i+lane);
            valid[lane]=noiseValid(p,period);
            if(valid[lane]) {block.prepare(lane,p,seed,period);any=true;}
            else recordInvalid(report,i+lane);
        }
        if(any) {interpolate(block);report.backend=backend;}
        for(std::size_t lane=0;lane<L;++lane) {
            if(valid[lane]) {output[i+lane]=block.corners[0][lane];++report.written;}
            if(!mask.empty()) mask[i+lane]=uint8_t(valid[lane]);
        }
    }
    return i;
}
template<std::size_t N> inline NoiseBatchReport runValue(
    const NoiseBatchPoints<N>& points,std::span<int64_t> output,uint64_t seed,
    std::array<int32_t,N> period,NoiseScalarBatchOptions options,
    NoiseBatchPolicy policy,std::span<uint8_t> mask) {
    const auto status=validate(points,output,mask,policy,validScalar(period,options));
    if(status!=NoiseBatchStatus::Complete) return {status};
    NoiseBatchReport report{};
    // For the value kernel, point validity is exactly noiseValid. No need to
    // hash or interpolate twice to honor the all-or-nothing contract.
    if(policy==NoiseBatchPolicy::Atomic) {
        for(std::size_t i=0;i<points.size();++i)
            if(!noiseValid(points.point(i),period)) recordInvalid(report,i);
        if(report.invalidCount) {report.status=NoiseBatchStatus::InvalidElement;return report;}
    }
    std::size_t i=0;
    switch(noise_simd_detail::select(options.backend,points.size())) {
#if defined(AIVERSE_NOISE_HAS_AVX2)
        case NoiseBatchBackend::AVX2:
            i=valueBlocks<N,4>(points,output,mask,seed,period,NoiseBatchBackend::AVX2,report,noise_simd_detail::interpolateAvx2<N>);break;
#endif
#if defined(AIVERSE_NOISE_HAS_SSE2)
        case NoiseBatchBackend::SSE2:
            i=valueBlocks<N,2>(points,output,mask,seed,period,NoiseBatchBackend::SSE2,report,noise_simd_detail::interpolateSse2<N>);break;
#endif
#if defined(AIVERSE_NOISE_HAS_NEON)
        case NoiseBatchBackend::NEON:
            i=valueBlocks<N,2>(points,output,mask,seed,period,NoiseBatchBackend::NEON,report,noise_simd_detail::interpolateNeon<N>);break;
#endif
        default: break;
    }
    for(;i<points.size();++i) {
        auto v=periodicValueNoise(points.point(i),seed,period);
        if(v) {output[i]=*v;++report.written;if(!mask.empty()) mask[i]=1;}
        else {recordInvalid(report,i);if(!mask.empty()) mask[i]=0;}
    }
    if(report.invalidCount) report.status=NoiseBatchStatus::Partial;
    return report;
}
} // namespace noise_batch_detail

// The same seed/period/options apply to every point. Output element i
// corresponds to input i, independent of layout and partitioning.
template<std::size_t N> inline NoiseBatchReport noiseBatchSamples(
    const NoiseBatchPoints<N>& points,std::span<NoiseSample<N>> output,uint64_t seed,
    std::array<int32_t,N> period={},NoiseScalarBatchOptions options={},
    NoiseBatchPolicy policy=NoiseBatchPolicy::Atomic,std::span<uint8_t> mask={}) {
    return noise_batch_detail::run(points,output,mask,policy,noise_batch_detail::validScalar(period,options),
        [&](auto p){return noise_batch_detail::scalarSample(p,seed,period,options);});
}
template<std::size_t N> inline NoiseBatchReport noiseBatchValues(
    const NoiseBatchPoints<N>& points,std::span<int64_t> output,uint64_t seed,
    std::array<int32_t,N> period={},NoiseScalarBatchOptions options={},
    NoiseBatchPolicy policy=NoiseBatchPolicy::Atomic,std::span<uint8_t> mask={}) {
    if(options.kind==NoiseScalarKind::Value) return noise_batch_detail::runValue(points,output,seed,period,options,policy,mask);
    return noise_batch_detail::run(points,output,mask,policy,noise_batch_detail::validScalar(period,options),
        [&](auto p){return noise_batch_detail::scalarValue(p,seed,period,options);});
}
template<std::size_t N> inline NoiseBatchReport noiseBatchCellular(
    const NoiseBatchPoints<N>& points,std::span<CellularDistance> output,uint64_t seed,
    std::array<int32_t,N> period={},NoiseBatchPolicy policy=NoiseBatchPolicy::Atomic,std::span<uint8_t> mask={}) {
    return noise_batch_detail::run(points,output,mask,policy,noiseValid(std::array<int64_t,N>{},period),
        [&](auto p){return cellularNoise(p,seed,period);});
}
template<std::size_t N> inline NoiseBatchReport noiseBatchVectorSamples(
    const NoiseBatchPoints<N>& points,std::span<NoiseVectorSample<N>> output,uint64_t seed,
    std::array<int32_t,N> period={},NoiseFractalOptions options={},
    NoiseBatchPolicy policy=NoiseBatchPolicy::Atomic,std::span<uint8_t> mask={}) {
    return noise_batch_detail::run(points,output,mask,policy,noise_batch_detail::validFractal(period,options),
        [&](auto p){return vectorNoiseSample(p,seed,period,options);});
}
template<std::size_t N> inline NoiseBatchReport noiseBatchVectors(
    const NoiseBatchPoints<N>& points,std::span<std::array<int64_t,N>> output,uint64_t seed,
    std::array<int32_t,N> period={},NoiseFractalOptions options={},
    NoiseBatchPolicy policy=NoiseBatchPolicy::Atomic,std::span<uint8_t> mask={}) {
    return noise_batch_detail::run(points,output,mask,policy,noise_batch_detail::validFractal(period,options),
        [&](auto p){return vectorNoise(p,seed,period,options);});
}
template<std::size_t N> inline NoiseBatchReport noiseBatchCurl(
    const NoiseBatchPoints<N>& points,std::span<std::array<int64_t,N>> output,uint64_t seed,
    std::array<int32_t,N> period={},NoiseFractalOptions options={},
    NoiseBatchPolicy policy=NoiseBatchPolicy::Atomic,std::span<uint8_t> mask={}) {
    return noise_batch_detail::run(points,output,mask,policy,noise_batch_detail::validFractal(period,options),
        [&](auto p){return curlNoise(p,seed,period,options);});
}
} // namespace aiverse::lib::algorithms
