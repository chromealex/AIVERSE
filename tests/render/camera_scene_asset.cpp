// AIVERSE camera authoring asset JSON round-trip (native C++/ECS).
#include "engine/sim/CameraSceneAsset.h"
#include "engine/render/EcsCameraRender.h"
#include <stdexcept>
using namespace aiverse;
static void check(bool b,const char* why){if(!b)throw std::runtime_error(why);}
int main(){
 World source;SceneHierarchy scene;
 auto a=source.create();
 LocalTransform local;local.position={1_fx,2_fx,3_fx};
 check(scene.attach(source,a,local),"attach");
 source.add<CameraComponent>(a,CameraComponent{});
 source.add<CameraNameComponent>(a,CameraNameComponent{5,"Overview"});
 source.add<CameraOutputComponent>(a,CameraOutputComponent{CameraOutputKind::Texture,42,256,256});
 CameraRigComponent rig;rig.mode=CameraRigMode::Orbit;rig.enabled=false;rig.distance=12_fx;
 source.add<CameraRigComponent>(a,rig);
 CameraLensComponent lens;lens.physicalFov=true;lens.focalLengthMm=85_fx;
 source.add<CameraLensComponent>(a,lens);
 CameraStackComponent stack;stack.overlay=true;
 source.add<CameraStackComponent>(a,stack);
 auto json=cameraSceneToJson(source);
 check(json["cameras"].size()==1,"serialize");
 World dest;SceneHierarchy target;std::vector<Entity> created;
 std::string error;
 check(loadCameraScene(dest,target,Json::parse(json.dump()),&created,&error),"parse");
 check(created.size()==1&&dest.get<CameraNameComponent>(created[0]).name=="Overview","name");
 render::EcsCameraView view;
 check(render::resolveEcsCamera(dest,created[0],view),"presentation");
 check(view.output.targetAssetKey==42 && int(view.camera.eye().x)==1,"render values");
 const auto& loadedRig=dest.get<CameraRigComponent>(created[0]);
 const auto& loadedLens=dest.get<CameraLensComponent>(created[0]);
 const auto& loadedStack=dest.get<CameraStackComponent>(created[0]);
 check(loadedRig.mode==CameraRigMode::Orbit&&!loadedRig.enabled&&loadedRig.distance==12_fx,
       "rig round-trip");
 check(loadedLens.physicalFov&&loadedLens.focalLengthMm==85_fx,"lens round-trip");
 check(loadedStack.overlay,"stack round-trip");
 auto invalid=json;
 invalid.find("cameras")->at(0).set("id",Json::number(int64_t(0)));
 World broken;SceneHierarchy hierarchy;
 check(!loadCameraScene(broken,hierarchy,invalid,nullptr,&error) &&
       broken.countExisting<CameraComponent>()==0,"reject before mutation");
 // Scheduler-rejected outputs are rejected by the loader too.
 auto twoCameras=[&](int64_t keyA,int64_t keyB,int64_t w,int64_t h){
  Json scene=json;
  Json second=scene["cameras"][0];
  second.set("id",Json::number(int64_t(6)));
  second.member("output").set("key",Json::number(keyB));
  second.member("output").set("width",Json::number(w));
  second.member("output").set("height",Json::number(h));
  scene.find("cameras")->at(0).member("output").set("key",Json::number(keyA));
  scene.member("cameras").push(second);
  return scene;
 };
 {World w;SceneHierarchy h;
  check(!loadCameraScene(w,h,twoCameras(42,42,256,256),nullptr,&error) &&
        w.countExisting<CameraComponent>()==0,"duplicate texture key rejected");}
 {World w;SceneHierarchy h;
  check(!loadCameraScene(w,h,twoCameras(42,43,256,0),nullptr,&error) &&
        w.countExisting<CameraComponent>()==0,"width xor height rejected");}
 {World w;SceneHierarchy h;
  check(loadCameraScene(w,h,twoCameras(42,43,0,0),nullptr,&error),"0x0 output allowed");}
 {World w;SceneHierarchy h;
  check(!loadCameraScene(w,h,twoCameras(42,43,256,256),nullptr,&error,256*256+1) &&
        w.countExisting<CameraComponent>()==0,"pixel budget rejected");
  check(loadCameraScene(w,h,twoCameras(42,43,256,256),nullptr,&error,2*256*256),
        "pixel budget boundary accepted");}
 // A hierarchy attach failure on the second camera leaves no partial World.
 {World w;SceneHierarchy h;
  const Entity stale=w.create(),clean=w.create();
  check(h.attach(w,stale),"stale attach");
  w.destroy(stale);   // hierarchy still holds this index: attach of a reuse fails
  w.destroy(clean);   // popped first by the next create()
  std::vector<Entity> out{Entity{}};
  check(!loadCameraScene(w,h,twoCameras(42,43,256,256),&out,&error),"attach failure reported");
  check(w.countExisting<CameraComponent>()==0 && w.countExisting<LocalTransform>()==0,
        "no partial camera entities");
  check(out.size()==1,"created list restored");
  check(!h.contains(Entity{clean.index,clean.gen+1}),"undone node detached");}
}
