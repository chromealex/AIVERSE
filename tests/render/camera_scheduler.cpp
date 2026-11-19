// CPU-only scheduler contract; no GPU backend or genre dependencies.
#include "engine/render/CameraRenderScheduler.h"
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace aiverse;
static void check(bool result,const char* message) {
    if (!result) throw std::runtime_error(message);
}
static Entity camera(World& world, uint64_t output=0, int32_t priority=0) {
    Entity e=world.create();
    world.add<CameraComponent>(e,CameraComponent{});
    auto& c=world.get<CameraComponent>(e);c.priority=priority;
    world.add<WorldTransform>(e,hierarchy_detail::root({}));
    if (output) world.add<CameraOutputComponent>(e,
        CameraOutputComponent{CameraOutputKind::Texture,output,256,256});
    return e;
}
static bool hasIssue(const render::CameraRenderPlan& p,render::CameraPlanIssue c) {
    for(const auto& x:p.diagnostics)if(x.reason==c)return true;
    return false;
}
int main() {
    World world;
    const auto a=camera(world,100,1),b=camera(world,200,5),screen=camera(world,0,50);
    CameraDependenciesComponent dependencies{};dependencies.count=1;dependencies.reads[0]=200;
    world.add<CameraDependenciesComponent>(a,dependencies);
    render::CameraRenderScheduler scheduler;
    auto plan=scheduler.buildPlan(world,1,0);
    check(plan.jobs.size()==3,"all three cameras must render");
    check(plan.jobs[0].entity==b && plan.jobs[1].entity==a &&
          plan.jobs[2].entity==screen,"dependencies and screen order");
    for(const auto& job:plan.jobs)scheduler.acknowledge(job.entity,job.output,1,0,true);
    check(scheduler.buildPlan(world,1,0).jobs.empty(),"do not duplicate same frame");
    world.add<CameraUpdateComponent>(a,
        CameraUpdateComponent{CameraUpdateMode::Manual,1,30});
    world.add<CameraUpdateComponent>(b,
        CameraUpdateComponent{CameraUpdateMode::EveryNFrames,3,30});
    scheduler.acknowledge(a,*world.tryGetConst<CameraOutputComponent>(a),2,0.1,true);
    check(scheduler.buildPlan(world,3,0.1).jobs.size()==1,
          "manual and every-N should not render before request"); // screen only
    scheduler.requestCapture(a);
    plan=scheduler.buildPlan(world,3,0.1);
    check(plan.jobs.size()==2 && plan.jobs[0].entity==a,
          "manual request should be rendered");
    scheduler.acknowledge(a,*world.tryGetConst<CameraOutputComponent>(a),3,0.1,false);
    check(scheduler.buildPlan(world,3,0.1).jobs.size()==2,
          "failed render must retain manual request");
    scheduler.acknowledge(a,*world.tryGetConst<CameraOutputComponent>(a),3,0.1,true);
    check(scheduler.buildPlan(world,4,0.2).jobs.size()==2,
          "every-N due after 3 frames");
    auto alternate=camera(world,0,100);
    plan=scheduler.buildPlan(world,5,0.3);
    check(hasIssue(plan,render::CameraPlanIssue::ScreenSuppressed),
          "lower-priority screen camera must be suppressed");
    world.destroy(alternate);
    // Read-after-write feedback cycle between texture cameras is forbidden.
    CameraDependenciesComponent cycle{};cycle.count=1;cycle.reads[0]=100;
    world.add<CameraDependenciesComponent>(b,cycle);
    plan=scheduler.buildPlan(world,6,0.4);
    check(hasIssue(plan,render::CameraPlanIssue::DependencyCycle),"cycle detection");
    check(plan.jobs.size()==1 && plan.jobs[0].entity==screen,"blocked cycle views");
    world.remove<CameraDependenciesComponent>(b);
    auto duplicate=camera(world,200);
    plan=scheduler.buildPlan(world,7,0.5);
    check(hasIssue(plan,render::CameraPlanIssue::DuplicateOutput),"duplicate output rejection");
    world.destroy(duplicate);
    world.destroy(a);
    plan=scheduler.buildPlan(world,8,0.6);
    check(plan.jobs.size()>=1,"surviving camera still works");
    // Dirty policy: one update on creation, then only explicit invalidation.
    World dirtyWorld;
    auto d=camera(dirtyWorld,321);
    dirtyWorld.add<CameraUpdateComponent>(d,
        CameraUpdateComponent{CameraUpdateMode::OnDirty,1,30});
    scheduler.reset();
    auto first=scheduler.buildPlan(dirtyWorld,10,1.0);
    check(first.jobs.size()==1,"initial dirty capture");
    scheduler.acknowledge(d,first.jobs[0].output,10,1,true);
    check(scheduler.buildPlan(dirtyWorld,11,1.1).jobs.empty(),"clean capture skip");
    scheduler.markDirty(d);
    check(scheduler.buildPlan(dirtyWorld,11,1.1).jobs.size()==1,"dirty capture");
    std::cout<<"AIVERSE LENS scheduler CPU contract passed\\n";
}
