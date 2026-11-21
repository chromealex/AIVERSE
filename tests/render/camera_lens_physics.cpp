// LENS physical lens: exposure stops, thin-lens depth of field, PRISMA pass
// values and the cameras.json round trip of the opt-in drive fields. CPU only.
#include "engine/render/CameraLensPhysics.h"
#include "engine/sim/CameraSceneAsset.h"
#include <cmath>
#include <cstdio>
#include <stdexcept>
using namespace aiverse;
using namespace aiverse::render;
static void check(bool value,const char* why){
    if(!value)throw std::runtime_error(why);
}
static bool near(double a,double b,double eps){return std::fabs(a-b)<=eps;}
int main() {
    // Exposure relative to the default lens (f/2.8, 1/60 s, ISO 100).
    const CameraLensValues base=cameraLensValues(CameraLensComponent{});
    check(cameraLensValuesValid(base),"default lens valid");
    check(near(cameraLensReferenceEV100(),std::log2(2.8*2.8*60.0),1e-3),"reference EV100");
    check(near(cameraLensExposureStops(base),0,1e-9),"default lens = 0 stops");
    CameraLensValues v=base;v.shutterSeconds=base.shutterSeconds*2;
    check(near(cameraLensExposureStops(v),1,1e-9),"twice the shutter = +1 stop");
    v=base;v.fNumber=base.fNumber*2;
    check(near(cameraLensExposureStops(v),-2,1e-9),"f/5.6 = -2 stops");
    v=base;v.iso=base.iso*4;
    check(near(cameraLensExposureStops(v,-.5),1.5,1e-9),"ISO 400 + compensation");
    v=base;v.shutterSeconds=0;
    check(cameraLensExposureStops(v)==0 && !cameraLensValuesValid(v),"invalid lens = no change");
    check(cameraLensExposureStops(base,NAN)==0,"non-finite compensation rejected");

    // Thin lens: 50 mm f/2.8 focused at 10 m, CoC 24/800 = 0.03 mm.
    check(near(cameraLensCocMm(base),0.03,1e-12),"automatic circle of confusion");
    const auto dof=cameraLensDepthOfField(base,1080);
    check(dof.valid && !dof.farInfinite,"finite depth of field");
    check(near(dof.hyperfocalMetres,29.8119,1e-3),"hyperfocal distance");
    check(near(dof.nearMetres,7.4944,1e-3),"near sharp limit");
    check(near(dof.farMetres,15.0222,1e-3),"far sharp limit");
    check(near(dof.blurRadiusAtInfinityPx,2.0190,1e-3),"background blur radius");
    const auto pass=cameraLensDofPass(dof);
    check(pass.valid && near(pass.focusDistance,(7.4944+15.0222)/2,1e-3) &&
          near(pass.focusRange,15.0222-7.4944,1e-3),"pass covers [near, far]");
    // Stopping down widens the sharp interval and reduces background blur.
    v=base;v.fNumber=8;
    const auto stopped=cameraLensDepthOfField(v,1080);
    check(stopped.nearMetres<dof.nearMetres && stopped.farMetres>dof.farMetres &&
          stopped.blurRadiusAtInfinityPx<dof.blurRadiusAtInfinityPx,"f/8 deeper focus");
    // Focusing past the hyperfocal distance: everything to infinity is sharp.
    v=base;v.focusMetres=40;
    const auto far=cameraLensDepthOfField(v,1080);
    check(far.valid && far.farInfinite,"focus past hyperfocal");
    const auto farPass=cameraLensDofPass(far,500.f);
    check(farPass.valid && near(farPass.focusRange,500,1e-3),"infinite far limit capped");
    // Explicit CoC overrides the automatic one.
    v=base;v.cocMm=0.06;
    check(cameraLensDepthOfField(v,1080).hyperfocalMetres<dof.hyperfocalMetres,"larger CoC");
    // Long telephoto blur is clamped to the pass limit.
    v=base;v.focalMm=300;v.fNumber=2;v.focusMetres=5;
    check(cameraLensDofPass(cameraLensDepthOfField(v,2160)).maxBlurPx==16.f,"blur clamp");
    // Focus inside the focal length, zero height: no plan.
    v=base;v.focusMetres=0.01;
    check(!cameraLensDepthOfField(v,1080).valid,"focus inside focal length");
    check(!cameraLensDepthOfField(base,0).valid,"zero image height");
    check(!cameraLensDofPass(CameraDepthOfFieldPlan{}).valid,"invalid plan pass");

    // cameras.json carries the opt-in drives; old files keep defaults.
    World source;SceneHierarchy sourceTree;
    const Entity camera=source.create();
    sourceTree.attach(source,camera,LocalTransform{});
    source.add<CameraComponent>(camera,CameraComponent{});
    source.add<CameraOutputComponent>(camera,CameraOutputComponent{});
    source.add<CameraUpdateComponent>(camera,CameraUpdateComponent{});
    source.add<CameraNameComponent>(camera,CameraNameComponent{7,"Lens"});
    CameraLensComponent lens;lens.driveExposure=true;lens.driveDepthOfField=true;
    lens.exposureCompensation=Fixed::fromRatio(-3,2);
    lens.circleOfConfusionMm=Fixed::fromRatio(2,100);
    source.add<CameraLensComponent>(camera,lens);
    CameraRailComponent rail;rail.count=3;rail.duration=7_fx;rail.loop=true;
    rail.points[1]={1_fx,2_fx,3_fx};rail.points[2]={-4_fx,0_fx,Fixed::fromRatio(1,2)};
    rail.interpolation=CameraRailInterpolation::CatmullRom;rail.constantSpeed=true;
    rail.ease=CameraRailEase::InOut;
    source.add<CameraRailComponent>(camera,rail);
    CameraShakeComponent shake;shake.positionAmplitude=Fixed::fromRatio(1,4);
    shake.rotationAmplitude=Fixed::fromRatio(1,50);shake.duration=2_fx;shake.startTime=1_fx;
    shake.style=CameraShakeStyle::Noise;shake.seed=4000000000u;shake.fadeExponent=3_fx;
    source.add<CameraShakeComponent>(camera,shake);
    sourceTree.update(source);
    const Json saved=cameraSceneToJson(source);
    World loaded;SceneHierarchy loadedTree;std::vector<Entity> created;std::string error;
    check(loadCameraScene(loaded,loadedTree,saved,&created,&error) && created.size()==1,"reload");
    const auto& back=loaded.get<CameraLensComponent>(created[0]);
    check(back.driveExposure && back.driveDepthOfField,"drive flags round trip");
    check(back.exposureCompensation==lens.exposureCompensation &&
          back.circleOfConfusionMm==lens.circleOfConfusionMm,"lens values round trip");
    check(loaded.get<CameraRailComponent>(created[0])==rail,"rail round trip");
    check(loaded.get<CameraShakeComponent>(created[0])==shake,"shake round trip");
    const auto rejects=[&](const char* field,Json value,const char* why){
        Json bad=Json::parse(saved.dump());
        Json item=bad["cameras"].items()[0];
        Json block=item[field];
        block.set(why,value);item.set(field,block);
        Json list=Json::array();list.push(item);bad.set("cameras",list);
        World w;SceneHierarchy h;std::vector<Entity> made;std::string err;
        check(!loadCameraScene(w,h,bad,&made,&err) && made.empty() && !err.empty(),
              "invalid rail/shake rejected before any entity");
    };
    rejects("rail",Json::string("bezier"),"interpolation");
    rejects("rail",Json::number(0_fx),"duration");
    rejects("shake",Json::string("perlin"),"style");
    rejects("shake",Json::number(int64_t(-1)),"seed");
    rejects("shake",Json::number(-1_fx),"positionAmplitude");
    Json old=saved;
    Json oldLens=Json::object();oldLens.set("fNumber",Json::number(4_fx));
    Json cams=Json::array();Json item=saved["cameras"].items()[0];
    item.set("lens",oldLens);cams.push(item);old.set("cameras",cams);
    World legacy;SceneHierarchy legacyTree;created.clear();
    check(loadCameraScene(legacy,legacyTree,old,&created,&error),"legacy lens block");
    const auto& legacyLens=legacy.get<CameraLensComponent>(created[0]);
    check(!legacyLens.driveExposure && !legacyLens.driveDepthOfField &&
          legacyLens.circleOfConfusionMm==0_fx,"legacy lens keeps defaults");
    check(legacy.get<CameraRailComponent>(created[0])==rail,"other blocks unaffected");
    std::puts("PASS camera_lens_physics");
}
