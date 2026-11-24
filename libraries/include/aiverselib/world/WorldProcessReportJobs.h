// Optional native report jobs. Only immutable, owned value snapshots cross
// the worker barrier; the canonical World and owner queue never do.
#pragma once
#include "WorldProcessBudgetedQueries.h"
#include "EvolveRegionWorkBudget.h"
#include "engine/experiments/EcsExperiments.h"
#include <bit>
#include <functional>
#include <span>
#include <type_traits>
#include <vector>

namespace aiverse::lib::worldprocess {
struct ReportJobOptions {
    ecs_experiment::PersistentWorkers* workers=nullptr; // borrowed host pool
    ecs_experiment::ExecutionMode mode=ecs_experiment::ExecutionMode::Auto;
    size_t minimumItems=2048,minimumPerLane=256;
};
template<class Output> struct ReportJobResult {
    std::vector<Output> values;
    bool parallel=false; // dispatched to existing pool, not a speedup claim
};
// The owner supplies a pure const-callable transform. No World, renderer,
// mutable shared captures or owner state are allowed inside it. Each output
// is assigned once by its input index. Exceptions leave all staged results
// private; caller may retry using the unchanged queue and snapshot.
// Input/output must be self-contained values, not pointer-bearing aliases.
template<class Input,class Transform>
inline auto analyzeReportValues(std::span<const Input> inputs,
        const ReportJobOptions& options,const Transform& transform){
    using Output=std::remove_cvref_t<std::invoke_result_t<const Transform&,const Input&>>;
    static_assert(std::is_trivially_copyable_v<Input>);
    static_assert(std::is_trivially_copyable_v<Output> && !std::is_same_v<Output,bool>);
    static_assert(!std::is_pointer_v<Output> && !std::is_pointer_v<Input>);
    ReportJobResult<Output> out;
    out.values.resize(inputs.size());
    if(inputs.empty())return out;
    auto range=[&](size_t begin,size_t end){
        for(size_t i=begin;i<end;++i)out.values[i]=std::invoke(transform,inputs[i]);
    };
#if !defined(__EMSCRIPTEN__)
    const size_t lanes=options.workers?size_t(options.workers->workers())+1:1;
    const bool use=options.workers&&lanes>1&&inputs.size()>1&&
        (options.mode==ecs_experiment::ExecutionMode::Parallel||
        (options.mode==ecs_experiment::ExecutionMode::Auto&&
         inputs.size()>=options.minimumItems&&
         inputs.size()/lanes>=options.minimumPerLane));
    if(use){
        options.workers->parallelFor(inputs.size(),range); // blocking barrier
        out.parallel=true;
    }else
#endif
    {range(0,inputs.size());}
    return out;
}
// Built-in advisory summaries are integer-only and independent by item.
// Keep identity and both timestamps; don't feed these back into simulation.
struct CrowdReportAnalysis {
    Entity entity=kNullEntity;
    uint32_t requestedTick=0,sampleTick=0;
    int64_t x=0,z=0;
    bool hasSlot=false;
    bool operator==(const CrowdReportAnalysis&)const=default;
};
struct SocietyReportAnalysis {
    Entity entity=kNullEntity;
    uint32_t requestedTick=0,sampleTick=0,knownTopicCount=0;
    uint16_t credibility=0;
    uint32_t acceptance=0;
    bool operator==(const SocietyReportAnalysis&)const=default;
};
struct EvolveReportAnalysis {
    Entity entity=kNullEntity;
    uint32_t requestedTick=0,sampleTick=0;
    int64_t fertility=0,pollution=0,durability=0,succession=0;
    bool operator==(const EvolveReportAnalysis&)const=default;
};
inline ReportJobResult<CrowdReportAnalysis> analyzeCrowdReports(
        const EntityReportBatch<queries::CrowdSample>& batch,
        const ReportJobOptions& options={}){
    return analyzeReportValues(std::span<const TimedEntityReport<queries::CrowdSample>>(batch.samples),
        options,[](const auto& s){
            return CrowdReportAnalysis{s.entity,s.requestedTick,s.sampleTick,
                s.value.x,s.value.z,s.value.slot.valid()};
        });
}
inline ReportJobResult<SocietyReportAnalysis> analyzeSocietyReports(
        const EntityReportBatch<queries::SocietySample>& batch,
        const ReportJobOptions& options={}){
    return analyzeReportValues(std::span<const TimedEntityReport<queries::SocietySample>>(batch.samples),
        options,[](const auto& s){
            return SocietyReportAnalysis{s.entity,s.requestedTick,s.sampleTick,
                uint32_t(std::popcount(s.value.knowledge)),s.value.credibility,s.value.acceptance};
        });
}
inline ReportJobResult<EvolveReportAnalysis> analyzeEvolveReports(
        const evolve::RegionReportBatch& batch,const ReportJobOptions& options={}){
    return analyzeReportValues(std::span<const evolve::RegionReportSample>(batch.samples),
        options,[](const auto& s){
            return EvolveReportAnalysis{s.region,s.requestedTick,s.sampleTick,
                s.value.fertility,s.value.pollution,s.value.durability,s.value.succession};
        });
}
} // namespace aiverse::lib::worldprocess
