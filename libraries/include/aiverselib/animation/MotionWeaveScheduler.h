// MotionWeave hybrid, cost-aware animation scheduler.
// Worker-safe graph entities use a persistent work-stealing thread pool.
// Lua/native operations, rig contacts and host callbacks run on caller thread.
#pragma once
#include "AnimationJobScheduler.h"
#include "AnimationWorkPolicy.h"
#include "MotionWeaveWorkers.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <unordered_map>
#include <utility>
#include <vector>

namespace aiverse::lib::animation {
struct MotionWeaveScheduleOptions {
    size_t minimumParallelActors=16;
    size_t minimumParallelCost=256;
    size_t chunksPerWorker=4;
    size_t minChunkActors=4;
};
struct MotionWeaveScheduleStats {
    size_t eligible=0,parallelEntities=0,serialEntities=0;
    size_t batches=0,workerThreads=0;
    size_t estimatedSafeCost=0,dispatchedEntities=0;
    size_t uniqueGraphs=0,offscreenEntities=0;
    uint64_t heaviestBatchCost=0;
    double workerPhaseMs=0,serialPhaseMs=0;
    bool usedWorkers=false;
};
class MotionWeaveScheduler final {
public:
    explicit MotionWeaveScheduler(
        size_t backgroundWorkers=MotionWeaveWorkerPool::recommendedWorkers(),
        MotionWeaveScheduleOptions options={})
        :workers_(backgroundWorkers),options_(options){}
    MotionWeaveScheduler(const MotionWeaveScheduler&)=delete;
    MotionWeaveScheduler& operator=(const MotionWeaveScheduler&)=delete;
    size_t workerCount() const noexcept {return workers_.workerCount();}
    const MotionWeaveScheduleStats& stats() const noexcept {return last_;}
    const MotionWeaveScheduleOptions& options() const noexcept {return options_;}
    // Apply host tuning between frames, not while update() is running.
    void setOptions(MotionWeaveScheduleOptions options) {options_=options;}

    MotionWeaveScheduleStats update(World& world,float dt) {
        last_={};
        last_.workerThreads=workerCount();
        if(!std::isfinite(dt)||dt<0.f)return last_;
        const auto prepared=prepareAnimationPresentation(world);
        if(!prepared.players || !prepared.poses)return last_;
        const World& read=world;
        // Read each optional component through the public non-creating
        // World API. poolIfExists() is private and must not be used by
        // presentation libraries.

        // Reuse allocation capacity across frames. Cache graph classification
        // only within the current frame: hot reload may reuse freed addresses.
        safe_.clear();unsafe_.clear();weight_.clear();graphPlans_.clear();
        safe_.reserve(prepared.players->size());
        unsafe_.reserve(prepared.players->size()/4+1);
        weight_.reserve(prepared.players->size());
        for(size_t i=0;i<prepared.players->size();++i) {
            const Entity e=prepared.players->entityAt(i);
            if(!world.alive(e)||!prepared.poses->has(e.index))continue;
            const auto* asset=read.tryGetConst<AnimationAssetComponent>(e);
            const auto* input=read.tryGetConst<AnimationInputsComponent>(e);
            const auto* hint=read.tryGetConst<AnimationWorkHintComponent>(e);
            const auto* face=read.tryGetConst<AnimationFacialControlComponent>(e);
            GraphPlan plan{};
            if(asset && asset->graph) {
                const auto* compiled=asset->graph.get();
                const auto [it,inserted]=graphPlans_.try_emplace(compiled);
                if(inserted) {
                    it->second={workerSafeGraph(*compiled),
                                estimateAnimationWork(*compiled)};
                }
                plan=it->second;
            }
            // Facial morph-only layers contain no callbacks and may run
            // parallel. An opt-in facial IK program can invoke native
            // AnimationRig contacts/host environment, so route it through
            // the same serial owner-thread barrier as other callback rigs.
            const bool facialWorkerSafe=!face || !face->enabled ||
                !face->rig ||
                (!face->rig->hasIk() && !face->controls.environment);
            const bool workerSafe=plan.safe && facialWorkerSafe &&
                (!input || !input->overrides.environment) &&
                (!hint || !hint->mainThread);
            if(!workerSafe){unsafe_.push_back(i);continue;}
            safe_.push_back(i);
            uint32_t cost=scaledAnimationCost(plan.cost,
                hint?hint->multiplier:1.f);
            const auto* visibility=read.tryGetConst<AnimationBudgetComponent>(e);
            const auto* pose=prepared.poses->tryGet(e.index);
            if(visibility && !visibility->visible && pose && pose->valid) {
                cost=std::max<uint32_t>(1,cost/4);
                ++last_.offscreenEntities;
            }
            weight_.push_back(cost);
            last_.estimatedSafeCost+=cost;
        }
        last_.eligible=safe_.size()+unsafe_.size();
        last_.parallelEntities=safe_.size();
        last_.serialEntities=unsafe_.size();
        last_.uniqueGraphs=graphPlans_.size();
        const bool eligible=workerCount()>0 &&
            safe_.size()>=std::max<size_t>(1,options_.minimumParallelActors) &&
            last_.estimatedSafeCost>=options_.minimumParallelCost;
        const auto now=[](){return std::chrono::steady_clock::now();};
        const auto millis=[](auto span) {
            return std::chrono::duration<double,std::milli>(span).count();
        };
        const auto start=now();
        if(eligible) {
            const size_t target=(workerCount()+1)*
                std::max<size_t>(1,options_.chunksPerWorker);
            const auto jobs=partitionAnimationWork(weight_,target,
                options_.minChunkActors);
            last_.batches=jobs.size();
            for(const auto& c:jobs)
                last_.heaviestBatchCost=std::max(last_.heaviestBatchCost,c.estimatedCost);
            if(jobs.size()>1) {
                workers_.parallelFor(jobs.size(),[&](size_t j) {
                    const auto& c=jobs[j];
                    evaluateAnimationPresentationRange(world,prepared,dt,
                        c.begin,c.end,&safe_);
                });
                last_.usedWorkers=true;
                last_.dispatchedEntities=safe_.size();
            } else {
                evaluateAnimationPresentationRange(world,prepared,dt,0,
                    safe_.size(),&safe_);
            }
        } else {
            evaluateAnimationPresentationRange(world,prepared,dt,0,
                safe_.size(),&safe_);
        }
        last_.workerPhaseMs=millis(now()-start);
        // Complete barrier: environment, Lua and host callbacks are now
        // guaranteed not to overlap any worker graph evaluation.
        const auto callbackStart=now();
        evaluateAnimationPresentationRange(world,prepared,dt,0,
            unsafe_.size(),&unsafe_);
        last_.serialPhaseMs=millis(now()-callbackStart);
        return last_;
    }
private:
    MotionWeaveWorkerPool workers_;
    MotionWeaveScheduleOptions options_;
    MotionWeaveScheduleStats last_;
    struct GraphPlan {bool safe=true;uint32_t cost=1;};
    std::unordered_map<const render::AnimationGraph*,GraphPlan> graphPlans_;
    std::vector<size_t> safe_,unsafe_;
    std::vector<uint32_t> weight_;
};
} // namespace aiverse::lib::animation
