// CPU-only camera layout/stack authoring contract. GPU shader test deferred.
#include "engine/render/CameraCompositor.h"
#include "engine/render/CameraRenderScheduler.h"
#include <stdexcept>
using namespace aiverse;
static void check(bool b){if(!b)throw std::runtime_error("camera compositor layout");}
int main(){
 CameraViewportComponent p{};
 auto full=render::cameraViewportRect(p,1000,800);
 check(full.w==1000&&full.h==800);
 p.enabled=true;p.x=Fixed::fromRatio(1,2);p.y=0_fx;
 p.width=Fixed::fromRatio(1,2);p.height=1_fx;
 auto right=render::cameraViewportRect(p,1000,800);
 check(right.x==500&&right.w==500&&right.h==800);
 p.x=Fixed::fromRatio(3,4);p.width=Fixed::fromRatio(1,2);
 check(render::cameraViewportRect(p,1000,800).w==0);
 World world;
 auto one=world.create(),two=world.create();
 for(auto e:{one,two}){
   world.add<WorldTransform>(e,hierarchy_detail::root({}));
   world.add<CameraComponent>(e,CameraComponent{});
 }
 world.add<CameraViewportComponent>(two,p);
 p.x=Fixed::fromRatio(1,2);p.width=Fixed::fromRatio(1,2);
 world.add<CameraViewportComponent>(two,p);
 CameraStackComponent stack;stack.order=1;stack.overlay=true;
 world.add<CameraStackComponent>(two,stack);
 render::CameraRenderScheduler schedule;
 auto plan=schedule.buildPlan(world,1,0);
 check(plan.jobs.size()==2);
 check(plan.diagnostics.empty());
}
