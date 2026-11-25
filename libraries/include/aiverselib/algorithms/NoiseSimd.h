#pragma once
#include "Noise.h"

// SIMD is optional, with baseline-safe runtime AVX2 selection on GCC/Clang.
// AIVERSE_NOISE_DISABLE_SIMD is a build-wide opt-out (same in every TU).
#if !defined(AIVERSE_NOISE_DISABLE_SIMD) && (defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP>=2)) && (defined(__i386__) || defined(__x86_64__) || defined(_M_IX86) || defined(_M_X64))
# include <immintrin.h>
# define AIVERSE_NOISE_HAS_SSE2 1
# if (defined(__GNUC__) || defined(__clang__)) && !defined(_MSC_VER)
#  define AIVERSE_NOISE_HAS_AVX2 1
# endif
#elif !defined(AIVERSE_NOISE_DISABLE_SIMD) && defined(__aarch64__) && defined(__ARM_NEON)
# include <arm_neon.h>
# define AIVERSE_NOISE_HAS_NEON 1
#endif

namespace aiverse::lib::algorithms {
enum class NoiseBatchBackend { Auto, Scalar, SSE2, AVX2, NEON };
inline bool noiseBatchBackendAvailable(NoiseBatchBackend backend) {
    switch(backend) {
        case NoiseBatchBackend::Auto: case NoiseBatchBackend::Scalar: return true;
        case NoiseBatchBackend::SSE2:
#if defined(AIVERSE_NOISE_HAS_SSE2)
            return true; // SSE2 is a compile-target baseline.
#else
            return false;
#endif
        case NoiseBatchBackend::AVX2:
#if defined(AIVERSE_NOISE_HAS_AVX2)
            return __builtin_cpu_supports("avx2"); // CPU and OS vector-state support.
#else
            return false;
#endif
        case NoiseBatchBackend::NEON:
#if defined(AIVERSE_NOISE_HAS_NEON)
            return true; // Advanced SIMD is a compile-target baseline.
#else
            return false;
#endif
    }
    return false;
}
namespace noise_simd_detail {
inline bool validBackend(NoiseBatchBackend b) {
    switch(b) {
        case NoiseBatchBackend::Auto: case NoiseBatchBackend::Scalar: case NoiseBatchBackend::SSE2:
        case NoiseBatchBackend::AVX2: case NoiseBatchBackend::NEON: return true;
    }
    return false;
}
inline NoiseBatchBackend select(NoiseBatchBackend b,std::size_t count) {
    if(b==NoiseBatchBackend::Auto) {
        if(count>=4&&noiseBatchBackendAvailable(NoiseBatchBackend::AVX2)) return NoiseBatchBackend::AVX2;
        if(count>=2&&noiseBatchBackendAvailable(NoiseBatchBackend::NEON)) return NoiseBatchBackend::NEON;
        if(count>=2&&noiseBatchBackendAvailable(NoiseBatchBackend::SSE2)) return NoiseBatchBackend::SSE2;
        return NoiseBatchBackend::Scalar;
    }
    const auto width=b==NoiseBatchBackend::AVX2?4u:2u;
    return b!=NoiseBatchBackend::Scalar&&count>=width&&noiseBatchBackendAvailable(b)?b:NoiseBatchBackend::Scalar;
}

// Each lane uses the canonical scalar hash, wrapping, floor and fade. Only
// interpolation is vectorized. Values and differences fit signed 32 bits;
// the product needs 64 bits. Never round negative products toward -infinity.
template<std::size_t N,std::size_t L> struct ValueBlock {
    std::array<std::array<int64_t,L>,N> fade{};
    std::array<std::array<int64_t,L>,std::size_t(1)<<N> corners{};
    void prepare(std::size_t lane,std::array<int64_t,N> p,uint64_t seed,std::array<int32_t,N> period) {
        std::array<int64_t,N> t{}; std::array<int64_t,std::size_t(1)<<N> values{};
        noise_detail::valueNoiseCorners(p,seed,period,t,values);
        for(std::size_t j=0;j<N;++j) fade[j][lane]=t[j];
        for(std::size_t i=0;i<values.size();++i) corners[i][lane]=values[i];
    }
};
#if defined(AIVERSE_NOISE_HAS_SSE2)
inline __m128i lerpSse2(__m128i a,__m128i b,__m128i t) {
    const auto d=_mm_sub_epi64(b,a);
    const auto sign=_mm_srai_epi32(_mm_shuffle_epi32(d,_MM_SHUFFLE(3,3,1,1)),31);
    const auto magnitude=_mm_sub_epi64(_mm_xor_si128(d,sign),sign);
    const auto q=_mm_srli_epi64(_mm_mul_epu32(magnitude,t),16);
    return _mm_add_epi64(a,_mm_sub_epi64(_mm_xor_si128(q,sign),sign));
}
template<std::size_t N> inline void interpolateSse2(ValueBlock<N,2>& b) {
    for(std::size_t j=0;j<N;++j) {
        const auto t=_mm_loadu_si128(reinterpret_cast<const __m128i*>(b.fade[j].data()));
        const auto stride=std::size_t(1)<<j;
        for(std::size_t i=0;i<b.corners.size();i+=2*stride) {
            auto a=_mm_loadu_si128(reinterpret_cast<const __m128i*>(b.corners[i].data()));
            auto z=_mm_loadu_si128(reinterpret_cast<const __m128i*>(b.corners[i+stride].data()));
            _mm_storeu_si128(reinterpret_cast<__m128i*>(b.corners[i].data()),lerpSse2(a,z,t));
        }
    }
}
#endif
#if defined(AIVERSE_NOISE_HAS_AVX2)
__attribute__((target("avx2"))) inline __m256i lerpAvx2(__m256i a,__m256i b,__m256i t) {
    const auto d=_mm256_sub_epi64(b,a);
    const auto sign=_mm256_cmpgt_epi64(_mm256_setzero_si256(),d);
    const auto magnitude=_mm256_sub_epi64(_mm256_xor_si256(d,sign),sign);
    const auto q=_mm256_srli_epi64(_mm256_mul_epu32(magnitude,t),16);
    return _mm256_add_epi64(a,_mm256_sub_epi64(_mm256_xor_si256(q,sign),sign));
}
template<std::size_t N> __attribute__((target("avx2"))) inline void interpolateAvx2(ValueBlock<N,4>& b) {
    for(std::size_t j=0;j<N;++j) {
        const auto t=_mm256_loadu_si256(reinterpret_cast<const __m256i*>(b.fade[j].data()));
        const auto stride=std::size_t(1)<<j;
        for(std::size_t i=0;i<b.corners.size();i+=2*stride) {
            auto a=_mm256_loadu_si256(reinterpret_cast<const __m256i*>(b.corners[i].data()));
            auto z=_mm256_loadu_si256(reinterpret_cast<const __m256i*>(b.corners[i+stride].data()));
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(b.corners[i].data()),lerpAvx2(a,z,t));
        }
    }
}
#endif
#if defined(AIVERSE_NOISE_HAS_NEON)
inline int64x2_t lerpNeon(int64x2_t a,int64x2_t b,int64x2_t t) {
    const auto d=vsubq_s64(b,a);
    const auto product=vmull_s32(vmovn_s64(d),vmovn_s64(t));
    const auto bias=vandq_s64(vshrq_n_s64(product,63),vdupq_n_s64(noiseOne-1));
    return vaddq_s64(a,vshrq_n_s64(vaddq_s64(product,bias),16));
}
template<std::size_t N> inline void interpolateNeon(ValueBlock<N,2>& b) {
    for(std::size_t j=0;j<N;++j) {
        const auto t=vld1q_s64(b.fade[j].data());
        const auto stride=std::size_t(1)<<j;
        for(std::size_t i=0;i<b.corners.size();i+=2*stride) {
            auto a=vld1q_s64(b.corners[i].data());
            auto z=vld1q_s64(b.corners[i+stride].data());
            vst1q_s64(b.corners[i].data(),lerpNeon(a,z,t));
        }
    }
}
#endif
} // namespace noise_simd_detail
} // namespace aiverse::lib::algorithms
