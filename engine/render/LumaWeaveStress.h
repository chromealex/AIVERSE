// Reproducible source workloads for profiling LumaWeave with 100/500/1000
// moving sources. No hidden source truncation or simulated GPU timings.
// This creates real registry snapshots and CPU cluster/quality measurements;
// actual Metal GPU duration must come from native command-buffer diagnostics.
#pragma once
#include "LumaWeaveQuality.h"
#include "LumaWeaveGpuPacket.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <utility>
#include <vector>

namespace aiverse::render {

struct LumaStressScene {
    uint32_t sourceCount=0,frame=0;
    std::vector<LumaLight> lights;
    LumaClusterConfig clusters{};
};

struct LumaStressProfile {
    uint32_t sourceCount=0,frame=0;
    size_t clusterCells=0,clusterReferences=0,clusterOverflows=0;
    size_t gpuPacketBytes=0;
    size_t fastReceiverQueries=0,slowReceiverQueries=0;
    uint64_t candidateVisits=0;
    double registryCpuMs=0,clustersCpuMs=0,packetCpuMs=0;
    LumaWeaveDecision selected{};
    bool gpuActuallyMeasured=false;
    double gpuFrameMs=0;
};

class LumaWeaveStress {
public:
    static constexpr uint32_t kCounts[3]={100,500,1000};
    static constexpr uint32_t kMaxFrames=10000;

    static LumaStressScene generate(uint32_t count,uint32_t frame) {
        if((count!=100 && count!=500 && count!=1000) || frame>kMaxFrames)
            throw std::invalid_argument("LumaWeave stress count/frame must be 100/500/1000");
        LumaStressScene result;
        result.sourceCount=count;
        result.frame=frame;
        result.lights.reserve(count);
        const uint32_t width=uint32_t(std::ceil(std::sqrt(double(count))));
        const double elapsed=double(frame)/60.0;
        for(uint32_t i=0;i<count;++i) {
            const uint32_t ix=i%width, iz=i/width;
            const double shift=3.2*std::sin(elapsed*.7+double(i)*.377);
            const double lift=2.0+1.2*std::cos(elapsed*.35+double(i)*.61);
            LumaLight light;
            light.key=1000000u+uint64_t(i);
            light.kind=i%7==0?LumaLightKind::Spot:LumaLightKind::Point;
            light.position={float(-100.0+double(ix)*6.0+shift),
                            float(lift),
                            float(-100.0+double(iz)*6.0+shift*.3)};
            light.direction={0,-1,0};
            light.color={float(.4+.6*std::sin(double(i)*.71)*std::sin(double(i)*.71)),
                         float(.35+.65*std::sin(double(i)*.37)*std::sin(double(i)*.37)),
                         float(.25+.75*std::cos(double(i)*.89)*std::cos(double(i)*.89))};
            light.radius=float(8+(i%8)*2);
            light.intensity=3.0f+float(i%6)*.6f;
            light.importanceBias=1.0f;
            light.castsShadows=i%5!=0;
            light.contributesToGi=false; // direct-only stress; no hidden GI rebuild
            if(light.kind==LumaLightKind::Spot) {
                light.cosOuter=.7f;
                light.cosInner=.86f;
            }
            result.lights.push_back(light);
        }
        result.clusters.origin={-128,-16,-128};
        result.clusters.cellSize={8,8,8};
        result.clusters.nx=32;result.clusters.ny=4;result.clusters.nz=32;
        result.clusters.maxFastPerCell=48;
        result.clusters.maxFastReferences=1u<<19;
        return result;
    }

    static LumaStressProfile cpuProfile(uint32_t count,uint32_t frame,
                                        uint32_t viewWidth=1920,
                                        uint32_t viewHeight=1080,
                                        LumaWeaveQuality quality=LumaWeaveQuality::Balanced) {
        using Clock=std::chrono::steady_clock;
        const auto scenario=generate(count,frame);
        LumaWeaveRegistry sources;
        auto t0=Clock::now();
        (void)sources.publish(scenario.lights);
        auto t1=Clock::now();
        LumaWeaveClusters clusters;
        const auto stats=clusters.rebuild(sources,scenario.clusters);
        auto t2=Clock::now();
        const auto packed=packLumaSources(sources,clusters);
        auto t3=Clock::now();
        LumaStressProfile out;
        out.sourceCount=count;out.frame=frame;
        out.clusterCells=stats.clusters;
        out.clusterReferences=stats.references;
        out.clusterOverflows=stats.overflows;
        out.gpuPacketBytes=packed.totalBytes();
        out.registryCpuMs=std::chrono::duration<double,std::milli>(t1-t0).count();
        out.clustersCpuMs=std::chrono::duration<double,std::milli>(t2-t1).count();
        out.packetCpuMs=std::chrono::duration<double,std::milli>(t3-t2).count();
        out.selected=LumaWeaveQualityPolicy::choose(
            quality,viewWidth,viewHeight,count,
            LumaWeaveQualityPolicy::settingsFor(quality));
        // Fixed 9x9 world-space diagnostic grid, including a few receivers
        // outside the dense source area to exercise full registry fallback.
        for(int z=-4;z<=4;++z)for(int x=-4;x<=4;++x) {
            const float3 position{float(x*30),0,float(z*30)};
            const auto candidate=clusters.query(position,sources);
            if(candidate.requiresFullScan) {
                ++out.slowReceiverQueries;
                out.candidateVisits+=sources.lights().size();
            } else {
                ++out.fastReceiverQueries;
                out.candidateVisits+=candidate.fastIndices.size();
            }
        }
        return out;
    }
};

} // namespace aiverse::render
