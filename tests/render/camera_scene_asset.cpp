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
}
