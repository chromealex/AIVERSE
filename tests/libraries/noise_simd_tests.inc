// Included by the existing library_noise fixture.
template<std::size_t N> void verifySimd() {
    using Point=std::array<int64_t,N>;
    constexpr std::size_t count=65;
    constexpr int64_t limit=int64_t(1)<<46;
    constexpr auto sentinel=INT64_MIN;
    std::array<Point,count> p{};
    std::array<std::array<int64_t,count>,N> axes{};
    std::array<std::span<const int64_t>,N> axisSpans{};
    std::array<int64_t,count> scalar{},actual{};
    std::array<uint8_t,count> mask{};
    Pcg32 rng(0x53494d44);
    NoiseScalarBatchOptions o{};o.kind=NoiseScalarKind::Value;
    const std::array backends={NoiseBatchBackend::Scalar,NoiseBatchBackend::SSE2,
        NoiseBatchBackend::AVX2,NoiseBatchBackend::NEON,NoiseBatchBackend::Auto};
    for(unsigned pass=0;pass<64;++pass) {
        const auto seed=rng.next64();
        std::array<int32_t,N> period{};
        for(std::size_t j=0;j<N;++j) {
            period[j]=pass%4==0?0:pass%4==1?1:pass%4==2?int32_t(j+3):100000000;
            for(std::size_t i=0;i<count;++i) {
                const auto edge=int64_t(i%4);
                p[i][j]=axes[j][i]=i<4?(-limit+edge):i<8?(limit-edge):
                    i<12?(-noiseOne+edge):i<16?(noiseOne-edge):
                    int64_t(*rng.below(uint64_t(2*limit+1)))-limit;
            }
            axisSpans[j]=axes[j];
        }
        auto aos=NoiseBatchPoints<N>::aos(p),soa=NoiseBatchPoints<N>::soa(axisSpans);
        o.backend=NoiseBatchBackend::Scalar;
        auto reference=noiseBatchValues<N>(aos,scalar,seed,period,o);
        CHECK(reference.complete()&&reference.backend==NoiseBatchBackend::Scalar);
        for(std::size_t i=0;i<count;++i) {
            CHECK(scalar[i]==*periodicValueNoise(p[i],seed,period));
            CHECK(scalar[i]==periodicValueNoiseSample(p[i],seed,period)->value);
        }
        for(auto backend:backends) {
            o.backend=backend;
            auto expected=noise_simd_detail::select(backend,count);
            auto r=noiseBatchValues<N>(aos,actual,seed,period,o);
            CHECK(r.complete()&&r.written==count&&r.backend==expected&&actual==scalar);
            mask.fill(99);
            r=noiseBatchValues<N>(soa,actual,seed,period,o,NoiseBatchPolicy::MarkInvalid,mask);
            CHECK(r.complete()&&r.backend==expected&&actual==scalar);
            for(auto m:mask) CHECK(m==1);
            // Exercise all sub-vector lengths, exact widths, odd tails and zero.
            for(std::size_t n=0;n<=9;++n) {
                auto sub=NoiseBatchPoints<N>::aos(std::span<const Point>(p).first(n));
                actual.fill(sentinel);
                r=noiseBatchValues<N>(sub,std::span<int64_t>(actual).first(n),seed,period,o);
                CHECK(r.complete()&&r.written==n&&r.backend==noise_simd_detail::select(backend,n));
                for(std::size_t i=0;i<count;++i) CHECK(actual[i]==(i<n?scalar[i]:sentinel));
            }
            p[0][0]=INT64_MIN;p[4][0]=INT64_MAX;p[count-1][0]=limit+1;
            actual.fill(sentinel);mask.fill(99);
            r=noiseBatchValues<N>(aos,actual,seed,period,o,NoiseBatchPolicy::Atomic,mask);
            CHECK(r.status==NoiseBatchStatus::InvalidElement&&r.written==0&&r.invalidCount==3&&r.firstInvalid==0&&r.backend==NoiseBatchBackend::Scalar);
            for(std::size_t i=0;i<count;++i) CHECK(actual[i]==sentinel&&mask[i]==99);
            r=noiseBatchValues<N>(aos,actual,seed,period,o,NoiseBatchPolicy::MarkInvalid,mask);
            CHECK(r.status==NoiseBatchStatus::Partial&&r.written==count-3&&r.invalidCount==3&&r.firstInvalid==0&&r.backend==expected);
            for(std::size_t i=0;i<count;++i) {
                const bool valid=i!=0&&i!=4&&i!=count-1;
                CHECK(actual[i]==(valid?scalar[i]:sentinel)&&mask[i]==uint8_t(valid));
            }
            p[0][0]=axes[0][0];p[4][0]=axes[0][4];p[count-1][0]=axes[0][count-1];
        }
    }
    // All-invalid full vector groups never report a SIMD invocation.
    for(auto& q:p) q[0]=INT64_MAX;
    auto aos=NoiseBatchPoints<N>::aos(p);
    o.backend=NoiseBatchBackend::Auto; actual.fill(sentinel); mask.fill(99);
    auto r=noiseBatchValues<N>(aos,actual,1,{},o,NoiseBatchPolicy::MarkInvalid,mask);
    CHECK(r.status==NoiseBatchStatus::Partial&&r.written==0&&r.invalidCount==count&&r.backend==NoiseBatchBackend::Scalar);
    for(std::size_t i=0;i<count;++i) CHECK(actual[i]==sentinel&&mask[i]==0);
    o.backend=static_cast<NoiseBatchBackend>(255);mask.fill(99);
    r=noiseBatchValues<N>(aos,actual,1,{},o,NoiseBatchPolicy::MarkInvalid,mask);
    CHECK(r.status==NoiseBatchStatus::InvalidOptions);
    for(std::size_t i=0;i<count;++i) CHECK(actual[i]==sentinel&&mask[i]==99);
    // Unsupported kernels still use scalar, even for a requested SIMD backend.
    p.fill(Point{});o.backend=NoiseBatchBackend::AVX2;o.kind=NoiseScalarKind::Gradient;
    CHECK(noiseBatchValues<N>(aos,actual,1,{},o).backend==NoiseBatchBackend::Scalar);
    std::array<NoiseSample<N>,count> samples{};o.kind=NoiseScalarKind::Value;
    CHECK(noiseBatchSamples<N>(aos,samples,1,{},o).backend==NoiseBatchBackend::Scalar);
}
