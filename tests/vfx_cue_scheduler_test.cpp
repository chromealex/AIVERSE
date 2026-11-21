// SpectraFX genre-neutral scheduler: queues, budgets, ECS attachments,
// tick deduplication, entity generation and rollback ownership.
#include <aiverselib/genre/SpectraFX.h>
#include "engine/fx/SpectraCueScheduler.h"
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace aiverse;
namespace sp = aiverse::spectra;
static void check(bool ok,const char* why) { if(!ok)throw std::runtime_error(why); }

static fx::EffectDef effect(bool looping,int burst) {
    fx::EffectDef def;
    fx::EmitterDef e;
    e.texture="builtin:soft";
    e.burst=burst;
    e.loop=looping;
    e.duration=looping?0.5f:0.f;
    e.rate={looping?12.f:0.f,looping?12.f:0.f};
    e.life={1.f,1.f};
    e.speed={0.f,0.f};
    e.size={0.3f,0.3f};
    e.spread=0;
    def.emitters.push_back(e);
    return def;
}
static void snapshot(const sp::BatchWorld& batch,std::vector<fx::FxParticleInfo>& parts) {
    batch.particles().snapshot(parts);
}

struct FleetShip {fx::float3 pos{2,3,4};};
struct ArenaPlayer {fx::float3 pos{5,6,7};};

template<class Kind>
static void ecsScenario() {
    sp::BatchWorld batch;
    check(batch.init(nullptr,fx::FxQuality::High),"headless BatchWorld init");
    sp::CueBudgets budgets;
    budgets.maxAttachments=1;
    sp::CueScheduler scheduler(batch,budgets);
    check(scheduler.registerEffect("engine_fire",effect(true,0)),"continuous registration");
    check(scheduler.registerEffect("impact",effect(false,3)),"one-shot registration");
    check(scheduler.bindCue(72,"impact"),"cue event binding");

    World world;
    Entity first=world.create();
    world.add<Kind>(first);
    sp::SourceComponent attachment;
    attachment.effect="engine_fire";
    attachment.killOnDetach=true;
    world.add<sp::SourceComponent>(first,attachment);
    const sp::SourcePoseResolver pose=[](const World& w,Entity entity,
                                         fx::float3& p,fx::float3& dir) {
        const Kind* kind=w.tryGetConst<Kind>(entity);
        if(!kind)return false;
        p=kind->pos;
        dir={0,1,0};
        return true;
    };
    sp::syncEcsCueSources(world,scheduler,pose);
    check(scheduler.stats().attached==1,"missing ECS attachment");
    batch.update(0.12f);
    std::vector<fx::FxParticleInfo> parts;
    snapshot(batch,parts);
    check(!parts.empty(),"continuous source emitted no CPU particles");

    // Stable source key: change of pose moves the existing effect, rather
    // than spawning a second one each render frame.
    const uint64_t restarts=scheduler.stats().restartedSources;
    world.get<Kind>(first).pos={9,4,2};
    sp::syncEcsCueSources(world,scheduler,pose);
    check(scheduler.stats().attached==1 &&
          scheduler.stats().restartedSources==restarts,
          "position-only change restarted an ECS source");

    // Event delivered on a committed simulation tick; duplicate render
    // frames must not play it again. The queue is independent of genre.
    SimEvent impact; impact.type=72; impact.pos={1_fx,2_fx,3_fx};
    check(sp::acceptSimCueTick(scheduler,44,std::span<const SimEvent>(&impact,1)),
          "committed tick rejected");
    check(!sp::acceptSimCueTick(scheduler,44,std::span<const SimEvent>(&impact,1)),
          "same committed tick replayed without rollback");
    check(scheduler.stats().queued==1 && scheduler.stats().duplicateTicks==1,
          "tick dedup failed");
    check(scheduler.flush()==1 && scheduler.stats().queued==0,"tick event not played");

    // Recycling the same ECS index must detach the old generation and
    // attach the new one. Retire old BEFORE applying attachment budgets.
    world.destroy(first);
    Entity next=world.create();
    check(next.index==first.index && next.gen!=first.gen,
          "ECS recycling fixture invalid");
    world.add<Kind>(next);
    world.add<sp::SourceComponent>(next,attachment);
    sp::syncEcsCueSources(world,scheduler,pose);
    check(scheduler.stats().attached==1 && scheduler.stats().stoppedSources>=1,
          "generation-recycled entity did not replace old attachment");

    // Rollback wipes *all* scheduler handles and pending events, but MUST
    // NOT kill a particle emitted by some other consumer of BatchWorld.
    const int externalId=batch.defineEffect(effect(false,2));
    check(externalId>=0,"external effect registration");
    const fx::FxHandle external=batch.play(externalId,{0,0,0});
    check(external.valid(),"external effect did not play");
    batch.update(0.02f); // bursts spawn in ParticleSystem::update()
    scheduler.resetForRollback();
    check(scheduler.stats().attached==0 && scheduler.stats().queued==0 &&
          !scheduler.haveTick(),"rollback retained scheduler state");
    snapshot(batch,parts);
    check(parts.size()==2,
          "rollback removed an unrelated shared-world effect or leaked cues");
    check(sp::acceptSimCueTick(scheduler,43,std::span<const SimEvent>(&impact,1)),
          "rollback must permit committed earlier ticks to be re-presented");
    batch.kill(external);
    scheduler.resetForRollback();
    batch.shutdown();
}

static void budgetsScenario() {
    sp::BatchWorld world;
    check(world.init(nullptr,fx::FxQuality::High),"batch init");
    sp::CueBudgets budgets;
    budgets.maxQueued=3;budgets.maxPerTick=2;
    budgets.maxPerFrame=1;budgets.maxTrackedOneShots=2;
    sp::CueScheduler scheduler(world,budgets);
    check(scheduler.registerEffect("blast",effect(false,3)),"effect registration");
    check(!scheduler.bindCue(9,"missing"),"unknown effect binding accepted");
    check(!scheduler.lastError().empty(),"missing binding error");
    check(scheduler.bindCue(9,"blast"),"valid bind");

    sp::CueEvent cue;cue.type=9;
    std::vector<sp::CueEvent> four(4,cue);
    check(scheduler.acceptTick(100,four),"tick 100 rejected");
    check(scheduler.stats().acceptedCues==2 &&
          scheduler.stats().droppedBudget==2,"per-tick budget not enforced");
    check(scheduler.flush()==1 && scheduler.stats().queued==1,
          "frame budget not enforced");
    world.update(0.02f);
    check(scheduler.flush()==1 && scheduler.stats().queued==0,
          "queued event not played in second frame");
    world.update(0.02f);

    // Queue persists across render frames; a third cue forces the
    // rollback-tracked handle ring to evict (kill) its oldest cue.
    check(scheduler.acceptTick(101,std::span<const sp::CueEvent>(&cue,1)),
          "next tick not accepted");
    check(scheduler.flush()==1 && scheduler.stats().trackedOneShots==2 &&
          scheduler.stats().evictedOneShots==1,
          "one-shot handle cap failed to kill oldest effect");
    world.update(0.02f);
    std::vector<fx::FxParticleInfo> parts;
    snapshot(world,parts);
    check(parts.size()==6,"tracked handle eviction left stale particles");
    scheduler.resetForRollback();
    snapshot(world,parts);
    check(parts.empty(),"rollback left tracked one-shot particles");
    check(scheduler.registerGraph("bad",Json::parse(R"({"version":2})"))==false,
          "invalid graph replaced registered state");
    check(!scheduler.lastError().empty(),"missing graph compiler diagnostics");

    // A bounded queue is never exhausted by a single malicious event tick.
    budgets.maxQueued=1;budgets.maxPerTick=5;budgets.maxPerFrame=0;
    sp::CueScheduler bounded(world,budgets);
    check(bounded.registerEffect("blast",effect(false,3)),"bounded registration");
    check(bounded.bindCue(9,"blast"),"bounded bind");
    check(bounded.acceptTick(1,four) && bounded.stats().queued==1,
          "queue bound not applied");
    check(bounded.stats().droppedBudget==3,"queued event overflow not recorded");
    bounded.resetForRollback();

    // Attachment budget: retire a vanished ECS source before admitting
    // the next one, even when the pool previously sat at its hard cap.
    sp::CueBudgets capped;
    capped.maxAttachments=1;
    sp::CueScheduler attachments(world,capped);
    check(attachments.registerEffect("continuous",effect(true,0)),
          "attachment budget registration");
    sp::AttachedCue first;first.key=11;first.effect="continuous";
    sp::AttachedCue second=first;second.key=22;
    const std::vector<sp::AttachedCue> both{first,second};
    attachments.syncAttachments(both);
    check(attachments.stats().attached==1 &&
          attachments.stats().droppedBudget==1,
          "attachment cap must admit no more than one source");
    const std::vector<sp::AttachedCue> replace{second};
    attachments.syncAttachments(replace);
    check(attachments.stats().attached==1 &&
          attachments.stats().stoppedSources==1,
          "retired source must free capacity for a replacement this frame");
    attachments.resetForRollback();
    world.shutdown();
}
int main() {
    ecsScenario<FleetShip>();
    ecsScenario<ArenaPlayer>();
    budgetsScenario();
    std::cout<<"PASS SpectraFX cue scheduler: two genres, bounded queues, "
                "ECS generation, tick dedup and rollback handles\n";
}
