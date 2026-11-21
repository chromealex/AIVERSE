// NEREID stage 7C: CPU-only renderer bridge contract, no GPU device needed.
#include "engine/render/NereidWaterRenderer.h"
#include "engine/render/NereidSurfaceResident.h"
#include "engine/world/NereidWaterScene.h"
#include "engine/world/NereidWaterManifest.h"
#include "engine/world/NereidEcsPresentation.h"
#include "engine/world/NereidUnderwaterView.h"
#include "engine/render/Shaders.h"
#include "engine/render/PostFx.h"
#include "engine/render/PostFxShaders.h"
#include "engine/render/NereidWaterReflectionPolicy.h"
#include "tests/cloth/FakeDevice.h"
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
using namespace aiverse;
static void check(bool value,const char* message) {
    if(!value)throw std::runtime_error(message);
}
struct WaterTestDevice final:FakeDevice {
    bool waitForResourceIdle()override{return true;}
    bool gpuHealthy()const override{return true;}
};
int main() {
    lib::nereid::WaterSurfaceVertex shore;
    shore.shore=1.f;
    lib::nereid::WaterSurfaceVertex deep;
    const auto a=render::nereidWaterVertexColor(shore);
    const auto b=render::nereidWaterVertexColor(deep);
    check(a!=b,"shore foam must affect vertex tint");
    const lib::nereid::WaterSurfaceStyle style{};
    check(render::validNereidWaterStyle(style),"valid default water style");
    auto bad=style;bad.opacity=1.f;
    check(!render::validNereidWaterStyle(bad),
          "opaque water breaks translucent reflection path");
    check((render::kDrawWaterSurface & render::kDrawUnlit)==0 &&
          (render::kDrawWaterSurface & render::kDrawNoCastShadow)==0,
          "NEREID draw flag conflicts with existing flags");
    check(std::string(render::shaders::kLitFS).find("u_params.x < -0.5")
          !=std::string::npos,"GLSL water flag missing");
    check(std::string(render::shaders::kLitMSL).find("u.params.x < -0.5")
          !=std::string::npos,"Metal water flag missing");
    check(render::sameWaterReflectionPlane(12.f,12.01f),
          "same-level water must share primary planar reflection");
    check(!render::sameWaterReflectionPlane(12.f,9.f),
          "water at a distinct elevation must not reuse a wrong mirror");
    check(!render::sameWaterReflectionPlane(12.f,12.03f),
          "water beyond plane epsilon must not use primary reflection");
    check(std::string(render::shaders::kLitFS).find(
          "u_params.x < -1.5 ? 0.0 : u_params.y")!=std::string::npos,
          "GLSL secondary-plane reflection suppression missing");
    check(std::string(render::shaders::kLitMSL).find(
          "u.params.x < -1.5 ? 0.0 : u.params.y")!=std::string::npos,
          "Metal secondary-plane reflection suppression missing");
    // Stable patch reuses a resident ocean mesh until the camera moves into
    // another grid cell; small camera shifts are not GPU upload triggers.
    lib::nereid::WaterSurfaceMeshOptions opts;
    opts.oceanPatchRadius=128.f;
    opts.cameraX=10.f;opts.cameraZ=-10.f;
    auto first=render::makeNereidSurfaceKey(7,true,opts);
    opts.cameraX=120.f;opts.cameraZ=-120.f;
    auto shifted=render::makeNereidSurfaceKey(7,true,opts);
    check(render::sameNereidSurfaceKey(first,shifted),
          "camera motion within an ocean patch must not trigger upload");
    opts.cameraX=129.f;
    auto moved=render::makeNereidSurfaceKey(7,true,opts);
    check(!render::sameNereidSurfaceKey(first,moved),
          "crossing an ocean cell must trigger rebuild");
    auto revised=render::makeNereidSurfaceKey(8,true,opts);
    check(!render::sameNereidSurfaceKey(moved,revised),
          "geometry revision must invalidate resident surface");
    opts.cameraX=10.f;
    auto boxA=render::makeNereidSurfaceKey(7,false,opts);
    opts.cameraX=10000.f;
    auto boxB=render::makeNereidSurfaceKey(7,false,opts);
    check(render::sameNereidSurfaceKey(boxA,boxB),
          "finite water body must ignore camera motion");
    auto recolored=style;recolored.foamStrength=0.2f;
    check(!render::sameNereidSurfaceKey(
          boxB,render::makeNereidSurfaceKey(7,false,opts,recolored)),
          "foam tint change must trigger upload");
    check(render::nereidPatchAnchor(-1.f,128.f)==-128.f,
          "negative camera coordinates must floor to correct patch");
    check(!std::isfinite(render::nereidPatchAnchor(0.f,0.f)),
          "invalid ocean radius must be rejected");
    world::NereidWaterScene sceneWater;
    std::vector<world::NereidSceneSurface> waterSources(2);
    sceneWater.setSurfaces(waterSources);
    check(sceneWater.visibleSurfaceCount()==0,
          "unuploaded water must not be counted as resident");
    bool rejected=false;
    try {
        sceneWater.setSurfaces(std::vector<world::NereidSceneSurface>(
            world::NereidWaterScene::kMaxSurfaces+1));
    }catch(const std::invalid_argument&) {rejected=true;}
    check(rejected,"water scene must reject unbounded surface lists");
    check(sceneWater.visibleSurfaceCount()==0,
          "rejected configuration must leave scene state unchanged");
    // WorldScene manifest is opt-in; legacy manifests instantiate no water.
    const auto emptyManifest=world::parseNereidWaterManifest(Json::parse("{}"));
    check(emptyManifest.surfaces.empty() && emptyManifest.maxUploadsPerFrame==2,
          "legacy world manifest should not opt in to NEREID");
    const auto authored=world::parseNereidWaterManifest(Json::parse(R"json(
    {"nereidWater":{"maxUploadsPerFrame":3,"surfaces":[
      {"kind":"ocean","shape":"infinite","surfaceY":2,"bottomY":-100,
       "oceanPatchRadius":300,"lod":"far"},
      {"kind":"lake","shape":"polygon","surfaceY":4,"bottomY":-5,
       "points":[[0,0],[40,0],[40,40],[0,40]],"style":{"opacity":0.7}},
      {"kind":"river","shape":"river","surfaceY":3,"bottomY":0,
       "points":[[0,0,4],[100,0,8]],"lod":"medium"}
    ]}})json"));
    check(authored.maxUploadsPerFrame==3 && authored.surfaces.size()==3,
          "manifest must parse water bodies and upload quota");
    check(authored.surfaces[0].body.footprint==lib::nereid::WaterFootprint::Infinite &&
          authored.surfaces[0].mesh.oceanPatchRadius==300.f,
          "manifest ocean patch configuration missing");
    check(authored.surfaces[1].useGeometry &&
          authored.surfaces[1].geometry.points.size()==4,
          "manifest polygon points missing");
    check(authored.surfaces[2].geometry.kind==lib::nereid::WaterGeometryKind::RiverSpline &&
          authored.surfaces[2].geometry.points[1].halfWidth==8_fx,
          "manifest river node widths missing");
    auto rejectedManifest=[](const char* json){
        try {
            world::parseNereidWaterManifest(Json::parse(json));
        }catch(const JsonError&){return true;}
        return false;
    };
    check(rejectedManifest(R"({"nereidWater":{"maxUploadsPerFrame":0,"surfaces":[]}})"),
          "manifest must reject zero per-frame GPU budget");
    check(rejectedManifest(R"({"nereidWater":{"surfaces":[
      {"shape":"river","surfaceY":2,"bottomY":1,"points":[[0,0,-1],[2,3,1]]}
    ]}})"),"manifest must reject negative river width");
    check(rejectedManifest(R"({"nereidWater":{"surfaces":[
      {"shape":"box","surfaceY":2,"bottomY":1,"halfExtents":[1,-3]}
    ]}})"),"manifest must reject negative box footprint");
    check(rejectedManifest(R"({"nereidWater":{"surfaces":[
      {"shape":"disc","surfaceY":2,"bottomY":1,"radius":0}
    ]}})"),"manifest must reject empty circle");
    sceneWater.setMaxUploadsPerFrame(1);
    check(sceneWater.maxUploadsPerFrame()==1,
          "scene water scheduler must expose configured budget");
    bool rejectedQuota=false;
    try {sceneWater.setMaxUploadsPerFrame(17);}
    catch(const std::invalid_argument&){rejectedQuota=true;}
    check(rejectedQuota && sceneWater.maxUploadsPerFrame()==1,
          "out-of-range upload budget must preserve last valid value");
    // Stable identity retains only changed per-body revisions.
    world::NereidWaterScene identity;
    world::NereidSceneSurface lakeA,lakeB;
    lakeA.id=10;lakeA.body.center.x=4_fx;
    lakeB.id=20;lakeB.body.center.x=30_fx;
    identity.setSurfaces({lakeA,lakeB});
    const auto initialA=identity.surfaceRevision(10);
    const auto initialB=identity.surfaceRevision(20);
    check(initialA!=0 && initialB!=0,"surface revisions must initialize");
    identity.setSurfaces({lakeB,lakeA});
    check(identity.surfaceRevision(10)==initialA &&
          identity.surfaceRevision(20)==initialB,
          "reordering surfaces must not invalidate stable GPU owners");
    lakeA.body.center.x=8_fx;
    identity.setSurfaces({lakeA,lakeB});
    check(identity.surfaceRevision(10)!=initialA &&
          identity.surfaceRevision(20)==initialB,
          "a changed water body must not invalidate unrelated surfaces");
    bool duplicate=false;
    try {identity.setSurfaces({lakeA,lakeA});}
    catch(const std::invalid_argument&){duplicate=true;}
    check(duplicate && identity.surfaceCount()==2,
          "duplicate id must reject snapshot transactionally");
    identity.setMaxEstimatedUploadBytes(4096);
    check(identity.maxEstimatedUploadBytes()==4096,
          "estimated byte quota must be configurable");
    check(identity.estimatedUploadBytes(lakeA)>0,
          "estimated surface upload must account for geometry budget");
    identity.setVisibilityRange(20.f);
    Vec3 camera{0_fx,0_fx,0_fx};
    check(world::NereidWaterScene::visibleFrom(lakeA,&camera,20.f),
          "near water footprint should be visible");
    lakeB.body.center.x=1000_fx;
    check(!world::NereidWaterScene::visibleFrom(lakeB,&camera,20.f),
          "far water must be distance culled");
    lakeB.body.footprint=lib::nereid::WaterFootprint::Infinite;
    check(world::NereidWaterScene::visibleFrom(lakeB,&camera,20.f),
          "infinite ocean must remain visible");
    check(rejectedManifest(R"({"nereidWater":{"surfaces":[
      {"id":1,"surfaceY":2,"bottomY":1,"shape":"box","halfExtents":[2,2]},
      {"id":1,"surfaceY":3,"bottomY":1,"shape":"box","halfExtents":[2,2]}
    ]}})"),"manifest must reject duplicate explicit water ids");
    const auto budgets=world::parseNereidWaterManifest(Json::parse(R"({
      "nereidWater":{"maxEstimatedUploadBytes":8192,"visibilityRange":200,
        "surfaces":[{"id":17,"surfaceY":2,"bottomY":1,
                     "shape":"box","halfExtents":[10,20]}]}
    })"));
    check(budgets.surfaces[0].id==17 &&
          budgets.maxEstimatedUploadBytes==8192 &&
          budgets.visibilityRange==200.f,
          "manifest stable ID and culling/upload quota not applied");
    // ECS snapshots use generation-safe Entity IDs and copy geometry.
    World ecsWorld;
    const Entity bodyEntity=ecsWorld.create();
    lib::nereid::WaterBodyComponent ecsBody;
    ecsBody.center.x=12_fx;
    ecsWorld.add<lib::nereid::WaterBodyComponent>(bodyEntity,ecsBody);
    lib::nereid::WaterGeometryComponent ecsGeometry;
    ecsGeometry.points={
        {0_fx,0_fx,0_fx},{20_fx,0_fx,0_fx},{20_fx,10_fx,0_fx}};
    ecsWorld.add<lib::nereid::WaterGeometryComponent>(bodyEntity,ecsGeometry);
    const auto ecsSnapshot=world::snapshotNereidEcsWater(ecsWorld);
    check(ecsSnapshot.size()==1 &&
          ecsSnapshot[0].id==uint64_t(bodyEntity.packed())+1 &&
          ecsSnapshot[0].useGeometry &&
          ecsSnapshot[0].geometry.points.size()==3,
          "ECS snapshot must copy water and geometry with stable entity ID");
    // Stage 13: prepared CPU geometry has an exact RHI payload byte count.
    lib::nereid::WaterSurfaceMeshOptions preparedOpts;
    preparedOpts.lod=lib::nereid::WaterSurfaceLod::Far;
    const auto prepared=render::NereidSurfaceResident::prepare(
        lakeA.body,nullptr,123,preparedOpts);
    check(prepared.has_value(),"valid box must prepare its CPU surface");
    check(prepared->bufferBytes==
          prepared->cpu.vertices.size()*sizeof(render::MeshVertex)+
          prepared->cpu.indices.size()*sizeof(uint32_t),
          "prepared water upload budget must equal actual VB/IB payload");
    check(prepared->bufferBytes==render::nereidMeshBufferBytes(prepared->cpu),
          "exact byte accounting must match standalone policy");
    auto invalidStyle=style;invalidStyle.opacity=1.f;
    check(!render::NereidSurfaceResident::prepare(
        lakeA.body,nullptr,123,preparedOpts,invalidStyle).has_value(),
        "invalid water material must not stage GPU geometry");
    // Automatic LOD has no effect unless explicitly enabled.
    world::NereidSceneSurface lodSurface=lakeA;
    lodSurface.mesh.lod=lib::nereid::WaterSurfaceLod::Medium;
    lodSurface.autoLod=true;
    lodSurface.mediumDistance=30.f;
    lodSurface.farDistance=100.f;
    Vec3 nearCam{8_fx,0_fx,0_fx},midCam{60_fx,0_fx,0_fx},
         farCam{400_fx,0_fx,0_fx};
    check(world::NereidWaterScene::selectedLod(lodSurface,&nearCam)==
          lib::nereid::WaterSurfaceLod::Near,
          "near water should select near LOD");
    check(world::NereidWaterScene::selectedLod(lodSurface,&midCam)==
          lib::nereid::WaterSurfaceLod::Medium,
          "mid-distance water should select medium LOD");
    check(world::NereidWaterScene::selectedLod(lodSurface,&farCam)==
          lib::nereid::WaterSurfaceLod::Far,
          "far-distance water should select far LOD");
    Vec3 overhead{lodSurface.body.center.x,lodSurface.body.surfaceY+200_fx,lodSurface.body.center.z};
    check(world::NereidWaterScene::selectedLod(lodSurface,&overhead)==
          lib::nereid::WaterSurfaceLod::Far,
          "camera altitude over water must select far LOD instead of staying near");
    Vec3 boundaryCam{31_fx,0_fx,0_fx};
    check(world::NereidWaterScene::selectedLod(
              lodSurface,&boundaryCam,lib::nereid::WaterSurfaceLod::Near)==
          lib::nereid::WaterSurfaceLod::Near,
          "LOD hysteresis must retain near detail at transition boundary");
    Vec3 crossCam{56_fx,0_fx,0_fx};
    check(world::NereidWaterScene::selectedLod(
              lodSurface,&crossCam,lib::nereid::WaterSurfaceLod::Near)==
          lib::nereid::WaterSurfaceLod::Medium,
          "LOD hysteresis must step down after positive threshold");
    Vec3 backCam{38_fx,0_fx,0_fx};
    check(world::NereidWaterScene::selectedLod(
              lodSurface,&backCam,lib::nereid::WaterSurfaceLod::Medium)==
          lib::nereid::WaterSurfaceLod::Medium,
          "LOD hysteresis must resist oscillation when moving back");
    lodSurface.autoLod=false;
    check(world::NereidWaterScene::selectedLod(lodSurface,&farCam)==
          lib::nereid::WaterSurfaceLod::Medium,
          "manual water LOD must remain unchanged");
    const auto lodConfig=world::parseNereidWaterManifest(Json::parse(R"({
      "nereidWater":{"surfaces":[{"id":22,"shape":"box",
        "surfaceY":2,"bottomY":-2,"halfExtents":[8,8],
        "autoLod":true,"mediumDistance":80,"farDistance":200}]}}
    )"));
    check(lodConfig.surfaces.size()==1 && lodConfig.surfaces[0].autoLod &&
          lodConfig.surfaces[0].mediumDistance==80.f &&
          lodConfig.surfaces[0].farDistance==200.f,
          "JSON camera LOD thresholds must deserialize");
    check(rejectedManifest(R"({"nereidWater":{"surfaces":[{
      "surfaceY":2,"bottomY":1,"shape":"box","halfExtents":[1,1],
      "autoLod":true,"mediumDistance":500,"farDistance":100
    }]}})"),"reversed LOD thresholds must be rejected");
    check(rejectedManifest(R"({"nereidWater":{"surfaces":[{
      "surfaceY":2,"bottomY":1,"shape":"box","halfExtents":[1,1],
      "autoLod":true,"mediumDistance":30,"farDistance":40,
      "lodHysteresis":20
    }]}})"),"hysteresis larger than transition band must fail");
    // Stage 14: worker configuration and staging memory validation.
    const auto asyncCfg=world::parseNereidWaterManifest(Json::parse(R"({
        "nereidWater":{"asyncPrepare":true,"maxStagingBytes":4194304,
                       "surfaces":[]}
    })"));
    check(asyncCfg.asyncPrepare && asyncCfg.maxStagingBytes==4194304,
          "async water preparation manifest must parse");
    check(rejectedManifest(R"({"nereidWater":{"asyncPrepare":5,"surfaces":[]}})"),
          "async water preparation must be a boolean");
    check(rejectedManifest(R"({"nereidWater":{"maxStagingBytes":1,"surfaces":[]}})"),
          "too-small staging memory budget must be rejected");
    world::NereidWaterScene asyncWater;
    check(!asyncWater.asyncPreparation(),
          "worker mode must be opt-in for old scenes");
    asyncWater.setAsyncPreparation(true);
    asyncWater.setMaxWaterStagingBytes(1024*1024);
    check(asyncWater.asyncPreparation() &&
          asyncWater.maxWaterStagingBytes()==1024*1024,
          "async scene controls must retain explicit configuration");
    bool rejectedStaging=false;
    try{asyncWater.setMaxWaterStagingBytes(512);}
    catch(const std::invalid_argument&){rejectedStaging=true;}
    check(rejectedStaging && asyncWater.maxWaterStagingBytes()==1024*1024,
          "staging budget failure must preserve previous value");
    // Stage 15: no float rounding of huge absolute XY coordinates before
    // converting each water vertex to its near-body local basis.
    lib::nereid::WaterBodyComponent hugeBody;
    hugeBody.footprint=lib::nereid::WaterFootprint::Box;
    hugeBody.center.x=Fixed::fromInt(100000000);
    hugeBody.center.z=Fixed::fromInt(-100000000);
    hugeBody.halfExtents={2_fx,1_fx};
    hugeBody.surfaceY=5_fx;
    hugeBody.bottomY=0_fx;
    lib::nereid::WaterSurfaceMeshOptions hugeOpts;
    hugeOpts.originX=hugeBody.center.x.toDouble();
    hugeOpts.originZ=hugeBody.center.z.toDouble();
    hugeOpts.lod=lib::nereid::WaterSurfaceLod::Far;
    auto local=lib::nereid::buildWaterSurfaceMesh(hugeBody,nullptr,hugeOpts);
    check(local.vertices.size()==4 &&
          local.vertices[0].x==-2.f &&
          local.vertices[0].z==-1.f &&
          local.vertices[2].x==2.f &&
          local.vertices[2].z==1.f,
          "large-world box vertices must remain precise in local coordinates");
    const auto localPrepared=render::NereidSurfaceResident::prepare(
        hugeBody,nullptr,101,hugeOpts);
    check(localPrepared && localPrepared->key.originX==hugeOpts.originX &&
          localPrepared->key.originZ==hugeOpts.originZ,
          "prepared GPU resource must retain exact mesh coordinate basis");
    auto changedBasis=hugeOpts;
    changedBasis.originX+=256.0;
    check(!render::sameNereidSurfaceKey(
        render::makeNereidSurfaceKey(101,false,hugeOpts),
        render::makeNereidSurfaceKey(101,false,changedBasis)),
        "changing local basis must invalidate prior GPU mesh");
    world::NereidSceneSurface hugeSurface;
    hugeSurface.body=hugeBody;
    world::NereidWaterScene::applyWaterMeshBasis(hugeOpts,hugeSurface);
    check(hugeOpts.originX==100000000.0 && hugeOpts.originZ==-100000000.0,
          "scene must anchor finite water at its precise fixed-point center");
    lib::nereid::WaterGeometryComponent geoLarge;
    geoLarge.kind=lib::nereid::WaterGeometryKind::Polygon;
    geoLarge.points={
        {Fixed::fromInt(100000000),Fixed::fromInt(-100000000),{}},
        {Fixed::fromInt(100000004),Fixed::fromInt(-100000000),{}},
        {Fixed::fromInt(100000004),Fixed::fromInt(-99999996),{}},
        {Fixed::fromInt(100000000),Fixed::fromInt(-99999996),{}}};
    hugeSurface.useGeometry=true;
    hugeSurface.geometry=geoLarge;
    world::NereidWaterScene::applyWaterMeshBasis(hugeOpts,hugeSurface);
    const auto polygonLocal=lib::nereid::buildWaterSurfaceMesh(
        hugeBody,&geoLarge,hugeOpts);
    check(!polygonLocal.empty() && polygonLocal.vertices[0].x==0.f &&
          polygonLocal.vertices[0].z==0.f &&
          polygonLocal.vertices[2].x==4.f,
          "large-world polygon must retain four-metre vertex separation");
    // Render-origin rebasing subtracts in double precision before float.
    const double renderOriginX=99999744.0;
    const float relativeX=float(hugeOpts.originX-renderOriginX);
    check(relativeX==256.f,"render-origin translation must remain precise");
    // Precise patch cell selection cannot first round camera X/Z to float.
    lib::nereid::WaterSurfaceMeshOptions seaOptions;
    seaOptions.preciseCamera=true;
    seaOptions.cameraX64=100000000.25;
    seaOptions.cameraZ64=-100000000.25;
    seaOptions.oceanPatchRadius=1.f;
    // This fixture expects a single unsubdivided quad (four vertices).
    // WaterSurfaceMeshOptions defaults to Medium, which subdivides it.
    seaOptions.lod=lib::nereid::WaterSurfaceLod::Far;
    seaOptions.originX=100000000.0;
    seaOptions.originZ=-100000001.0;
    const auto preciseKey=render::makeNereidSurfaceKey(55,true,seaOptions);
    check(preciseKey.patchX==100000000.0 &&
          preciseKey.patchZ==-100000001.0,
          "ocean patch snapping must use double-precision camera coordinates");
    // Infinite footprint is required to exercise ocean-specific preparation.
    auto infiniteBody=hugeBody;
    infiniteBody.footprint=lib::nereid::WaterFootprint::Infinite;
    const auto oceanPrepared=render::NereidSurfaceResident::prepare(
        infiniteBody,nullptr,55,seaOptions);
    check(oceanPrepared && oceanPrepared->key.patchZ==-100000001.0 &&
          oceanPrepared->cpu.vertices.size()==4 &&
          oceanPrepared->cpu.vertices[0].x==-1.f &&
          oceanPrepared->cpu.vertices[0].z==-1.f,
          "large-world ocean patch vertices must be local and stable");
    // Medium LOD must retain the same local basis even though it creates
    // more vertices. Do not weaken the real generator's LOD policy.
    auto mediumSeaOptions=seaOptions;
    mediumSeaOptions.lod=lib::nereid::WaterSurfaceLod::Medium;
    const auto mediumOceanPrepared=render::NereidSurfaceResident::prepare(
        infiniteBody,nullptr,55,mediumSeaOptions);
    check(mediumOceanPrepared &&
          mediumOceanPrepared->cpu.vertices.size()>4 &&
          mediumOceanPrepared->cpu.vertices[0].x==-1.f &&
          mediumOceanPrepared->cpu.vertices[0].z==-1.f &&
          mediumOceanPrepared->key.patchX==100000000.0 &&
          mediumOceanPrepared->key.patchZ==-100000001.0,
          "medium ocean LOD must preserve precise local patch coordinates");
    seaOptions.cameraX64=100000001.25;
    const auto nextPatch=render::makeNereidSurfaceKey(55,true,seaOptions);
    check(nextPatch.patchX==100000001.0 &&
          !render::sameNereidSurfaceKey(preciseKey,nextPatch),
          "crossing a one-metre distant-ocean cell must invalidate its GPU key");
    // Stage 16: high-altitude water uses a Y-local mesh basis.
    auto highBody=hugeBody;
    highBody.surfaceY=Fixed::fromInt(100000000);
    highBody.bottomY=Fixed::fromInt(99999990);
    lib::nereid::WaterSurfaceMeshOptions highOpts;
    highOpts.originX=highBody.center.x.toDouble();
    highOpts.originZ=highBody.center.z.toDouble();
    highOpts.originY=highBody.surfaceY.toDouble();
    highOpts.lod=lib::nereid::WaterSurfaceLod::Far;
    const auto highMesh=lib::nereid::buildWaterSurfaceMesh(
        highBody,nullptr,highOpts);
    check(highMesh.vertices.size()==4 && highMesh.vertices[0].y==0.f,
          "high-altitude water must emit Y-local vertex coordinates");
    auto otherY=highOpts;
    otherY.originY-=256.0;
    check(!render::sameNereidSurfaceKey(
              render::makeNereidSurfaceKey(22,false,highOpts),
              render::makeNereidSurfaceKey(22,false,otherY)),
          "changing Y origin must invalidate the GPU water cache");
    world::NereidSceneSurface highSource;
    highSource.body=highBody;
    world::NereidWaterScene::applyWaterMeshBasis(highOpts,highSource);
    check(highOpts.originY==100000000.0,
          "water scene must select the precise surface-Y basis");
    auto refractive=style;
    refractive.refractionStrength=0.65f;
    check(render::validNereidWaterStyle(refractive),
          "valid refractive water style must be accepted");
    check(!render::sameNereidSurfaceKey(
          render::makeNereidSurfaceKey(22,false,highOpts,style),
          render::makeNereidSurfaceKey(22,false,highOpts,refractive)),
          "changing refraction must invalidate water material residency");
    refractive.refractionStrength=1.1f;
    check(!render::validNereidWaterStyle(refractive),
          "refraction strength must be clamped to the material contract");
    const auto refractConfig=world::parseNereidWaterManifest(Json::parse(R"({
      "nereidWater":{"surfaces":[{
        "kind":"lake","shape":"box","surfaceY":2,"bottomY":-5,
        "halfExtents":[10,10],"style":{"refractionStrength":0.5}
      }]}}
    )"));
    check(refractConfig.surfaces.size()==1 &&
          refractConfig.surfaces[0].style.refractionStrength==0.5f,
          "NEREID manifest must expose optional refraction");
    check(rejectedManifest(R"({"nereidWater":{"surfaces":[{
      "shape":"box","surfaceY":2,"bottomY":-5,"halfExtents":[10,10],
      "style":{"refractionStrength":2}
    }]}})"),"manifest must reject invalid refraction strength");
    check(std::string(render::shaders::kLitFS).find(
          "thickness = v_wpos.y - opaquePos.y")!=std::string::npos &&
          std::string(render::shaders::kLitFS).find(
          "u_params.x < -0.5 && u_trace2.w > 0.0")!=std::string::npos,
          "GLSL depth-gated NEREID optics missing");
    check(std::string(render::shaders::kLitMSL).find(
          "thickness = in.wpos.y - opaquePos.y")!=std::string::npos &&
          std::string(render::shaders::kLitMSL).find(
          "u.params.x < -0.5 && f.trace2.w > 0.0")!=std::string::npos,
          "Metal depth-gated NEREID optics missing");
    // Stage 17 optics controls are opt-in and must invalidate cached visuals.
    auto opticalStyle=style;
    opticalStyle.absorption=0.24f;
    opticalStyle.shoreFoamDepth=1.5f;
    opticalStyle.depthTint=0.6f;
    check(render::validNereidWaterStyle(opticalStyle),
          "valid depth optical water material");
    check(!render::sameNereidSurfaceKey(
          render::makeNereidSurfaceKey(30,false,highOpts,style),
          render::makeNereidSurfaceKey(30,false,highOpts,opticalStyle)),
          "optical material change must invalidate cached drawing");
    opticalStyle.absorption=5.f;
    check(!render::validNereidWaterStyle(opticalStyle),
          "optical absorption outside allowed range must fail");
    const auto opticalManifest=world::parseNereidWaterManifest(Json::parse(R"({
      "nereidWater":{"surfaces":[{"id":5,"shape":"box","surfaceY":8,
        "bottomY":-3,"halfExtents":[5,6],
        "style":{"absorption":0.3,"shoreFoamDepth":1.5,"depthTint":0.6}}]}}
    )"));
    const auto& optical=opticalManifest.surfaces[0].style;
    check(optical.absorption>0.29f && optical.absorption<0.31f &&
          optical.shoreFoamDepth==1.5f && optical.depthTint>0.59f,
          "optical configuration must parse in native scene manifest");
    check(rejectedManifest(R"({"nereidWater":{"surfaces":[{
      "shape":"box","surfaceY":2,"bottomY":1,"halfExtents":[1,1],
      "style":{"shoreFoamDepth":21}
    }]}})"),"reject invalid depth foam width");
    check(std::string(render::shaders::kLitFS).find(
          "float transmission = exp(-opticalPath * u_color.r * clamp(albedo.a,0.0,1.0))")!=std::string::npos &&
          std::string(render::shaders::kLitFS).find("float opticalPath = thickness + length(v_wpos - opaquePos)")!=std::string::npos &&
          std::string(render::shaders::kLitFS).find(
          "contactFoam=1.0-smoothstep(0.0,u_color.g,depth)")!=std::string::npos,
          "GLSL Beer-Lambert approximation or shore foam missing");
    check(std::string(render::shaders::kLitMSL).find(
          "float transmission = exp(-opticalPath * u.color.r * clamp(albedo.a,0.0,1.0))")!=std::string::npos &&
          std::string(render::shaders::kLitMSL).find(
          "contactFoam=1.0-smoothstep(0.0,u.color.g,depth)")!=std::string::npos,
          "Metal Beer-Lambert approximation or shore foam missing");
    // Underwater visual state is derived from ECS/manifest snapshot only.
    // It neither mutates ECS nor requires a graphics or postfx backend.
    world::NereidSceneSurface underwaterLake;
    underwaterLake.id=123;
    underwaterLake.body.footprint=lib::nereid::WaterFootprint::Box;
    underwaterLake.body.center={0_fx,0_fx,0_fx};
    underwaterLake.body.halfExtents={20_fx,20_fx};
    underwaterLake.body.surfaceY=10_fx;
    underwaterLake.body.bottomY=-50_fx;
    underwaterLake.style.absorption=0.5f;
    underwaterLake.style.deepR=0.1f;
    auto inclined=underwaterLake;inclined.mesh.useGrade=true;
    inclined.mesh.grade.risePerMetre={Fixed::fromRatio(1,10),Fixed{}};
    const auto belowGrade=world::sampleNereidUnderwater({inclined},{10_fx,10_fx,0_fx});
    check(belowGrade.submerged&&std::abs(belowGrade.waterSurfaceY-11.0)<.001,
          "underwater camera must use graded local water surface");
    check(!world::sampleNereidUnderwater({inclined},{-10_fx,10_fx,0_fx}).submerged,
          "above local grade must not inherit reference-plane submersion");
    const std::vector<world::NereidSceneSurface> opticalVolumes{underwaterLake};
    auto submerged=world::sampleNereidUnderwater(opticalVolumes,
                                                 {0_fx,8_fx,0_fx});
    world::NereidWaterScene sceneOptics;
    sceneOptics.setSurfaces(opticalVolumes);
    const auto sceneSubmerged=world::sampleNereidUnderwater(
        sceneOptics,{0_fx,8_fx,0_fx});
    check(sceneSubmerged.submerged && sceneSubmerged.waterId==123 &&
          sceneSubmerged.depthMetres==2.0,
          "scene-view underwater query must match immutable list sampling");
    check(submerged.submerged && submerged.waterId==123 &&
          submerged.depthMetres==2.0 &&
          submerged.transmission<0.38f && submerged.transmission>0.36f,
          "camera view must derive underwater extinction from depth");
    check(!world::sampleNereidUnderwater(opticalVolumes,
                                        {0_fx,11_fx,0_fx}).submerged &&
          !world::sampleNereidUnderwater(opticalVolumes,
                                        {30_fx,8_fx,0_fx}).submerged,
          "camera outside water must not inherit underwater visuals");
    auto higher=underwaterLake;
    higher.id=124;
    higher.body.priority=2;
    higher.body.surfaceY=12_fx;
    const auto chosen=world::sampleNereidUnderwater({underwaterLake,higher},
                                                    {0_fx,8_fx,0_fx});
    check(chosen.waterId==124 && chosen.depthMetres==4,
          "underwater view must honor shared priority selection");
    auto finiteInvalid=underwaterLake;
    finiteInvalid.body.enabled=false;
    check(!world::sampleNereidUnderwater({finiteInvalid},
                                         {0_fx,8_fx,0_fx}).submerged,
          "disabled water may not affect camera optics");
    // Foam and extinction happen *after* SSR to keep shore foam visible.
    const auto glShader=std::string(render::shaders::kLitFS);
    const auto metalShader=std::string(render::shaders::kLitMSL);
    const auto glFoam=glShader.find("if(contactFoam>0.0)");
    const auto metalFoam=metalShader.find("if(contactFoam>0.0)");
    check(glFoam!=std::string::npos && metalFoam!=std::string::npos &&
          glFoam>glShader.find("vec3 rc = ssrWater") &&
          metalFoam>metalShader.find("float3 rc = ssrWater"),
          "undistorted contact foam must composite after refraction and SSR");
    // Stage 18: Prisma's underwater shader is an opt-in composite variant.
    check(std::string(render::postfx::kComposite).find(
          "#ifdef PF_UNDERWATER")!=std::string::npos &&
          std::string(render::postfx::kComposite).find(
          "exp(-underwater1.x * opticalDistance)")!=std::string::npos,
          "Prisma must have a common GLSL/MSL underwater composite body");
    render::PostFxUnderwaterState fadeState{};
    render::PostFxUnderwater fadeTarget{};
    fadeTarget.submerged=true;
    fadeTarget.tintR=0.1f;fadeTarget.tintG=0.3f;fadeTarget.tintB=0.45f;
    fadeTarget.absorption=0.4f;
    fadeTarget.depthMetres=3.f;
    fadeState=render::advancePostFxUnderwater(fadeState,fadeTarget,0.1f);
    check(fadeState.blend>0.f && fadeState.blend<1.f &&
          fadeState.absorption>0 && fadeState.absorption<0.4f,
          "underwater entrance must smoothly approach target state");
    for(int i=0;i<20;++i)
        fadeState=render::advancePostFxUnderwater(fadeState,fadeTarget,0.1f);
    check(fadeState.blend>0.999f,
          "camera underwater transition must settle");
    fadeTarget.submerged=false;
    float beforeExit=fadeState.blend;
    fadeState=render::advancePostFxUnderwater(fadeState,fadeTarget,0.1f);
    check(fadeState.blend<beforeExit && fadeState.blend>0.f,
          "exit should fade instead of instantly popping");
    for(int i=0;i<60;++i)
        fadeState=render::advancePostFxUnderwater(fadeState,fadeTarget,0.1f);
    check(fadeState.blend==0.f,
          "underwater transition should fully clear after leaving");
    render::PostFxUnderwater invalidUnderwater{};
    invalidUnderwater.submerged=true;
    invalidUnderwater.absorption=999.f;
    invalidUnderwater.depthMetres=-100.f;
    auto sanitized=render::advancePostFxUnderwater({},invalidUnderwater,0.1f);
    check(sanitized.absorption<=4.f && sanitized.depthMetres==0.f,
          "untrusted volume optics must be clamped before compositor");
    // Stage 19: optional per-water underwater optical effects and transitions.
    world::NereidSceneSurface opticalCamera=underwaterLake;
    opticalCamera.style.underwaterDistortion=0.7f;
    opticalCamera.style.underwaterScattering=0.5f;
    auto cameraEffects=world::sampleNereidUnderwater({opticalCamera},
                                                      {0_fx,8_fx,0_fx});
    check(cameraEffects.submerged && cameraEffects.distortion==0.7f &&
          cameraEffects.scattering==0.5f,
          "camera optical snapshot should preserve per-water effect settings");
    auto withDistortion=style;
    withDistortion.underwaterDistortion=1.f;
    withDistortion.underwaterScattering=0.4f;
    check(render::validNereidWaterStyle(withDistortion),
          "NEREID must accept bounded distortion/scattering");
    check(!render::sameNereidSurfaceKey(
          render::makeNereidSurfaceKey(30,false,highOpts,style),
          render::makeNereidSurfaceKey(30,false,highOpts,withDistortion)),
          "water presentation cache must notice underwater optical changes");
    withDistortion.underwaterScattering=2.f;
    check(!render::validNereidWaterStyle(withDistortion),
          "NEREID must reject unbounded underwater scattering");
    const auto opticsManifest=world::parseNereidWaterManifest(Json::parse(R"({
      "nereidWater":{"surfaces":[{"kind":"lake","shape":"box",
        "surfaceY":4,"bottomY":-10,"halfExtents":[5,5],
        "style":{"underwaterDistortion":0.7,"underwaterScattering":0.5}}]}}
    )"));
    check(opticsManifest.surfaces[0].style.underwaterDistortion>0.69f &&
          opticsManifest.surfaces[0].style.underwaterScattering==0.5f,
          "manifest must expose underwater settings per surface");
    // Turbidity: one authored value, same resolved optics above and below.
    const auto murkyManifest=world::parseNereidWaterManifest(Json::parse(R"({
      "nereidWater":{"surfaces":[{"kind":"lake","shape":"box",
        "surfaceY":4,"bottomY":-10,"halfExtents":[5,5],
        "style":{"absorption":0.3,"turbidity":0.5,"siltColor":[0.5,0.45,0.3]}}]}}
    )"));
    const auto& murkyStyle=murkyManifest.surfaces[0].style;
    check(murkyStyle.turbidity==0.5f && murkyStyle.siltR==0.5f,
          "manifest must expose turbidity and silt colour");
    const auto clearResolved=lib::nereid::resolvedWaterOptics(style);
    check(clearResolved.absorption==style.absorption && clearResolved.deepR==style.deepR,
          "zero turbidity must keep legacy optics");
    const auto murkyResolved=lib::nereid::resolvedWaterOptics(murkyStyle);
    check(murkyResolved.absorption>murkyStyle.absorption+0.7f &&
          murkyResolved.depthTint>murkyStyle.depthTint &&
          murkyResolved.deepR>murkyStyle.deepR && murkyResolved.absorption<=4.f,
          "turbidity must raise extinction, in-scatter and milkiness");
    check(render::validNereidWaterStyle(murkyStyle),"bounded turbidity must be valid");
    auto badSilt=murkyStyle;badSilt.turbidity=1.5f;
    check(!render::validNereidWaterStyle(badSilt),"unbounded turbidity must be rejected");
    check(!render::sameNereidSurfaceKey(
          render::makeNereidSurfaceKey(30,false,highOpts,style),
          render::makeNereidSurfaceKey(30,false,highOpts,murkyStyle)),
          "water presentation cache must notice turbidity changes");
    world::NereidSceneSurface clearCamera=underwaterLake,murkyCamera=underwaterLake;
    clearCamera.style.absorption=0.3f;
    murkyCamera.style=murkyStyle;
    const auto clearView=world::sampleNereidUnderwater({clearCamera},{0_fx,3_fx,0_fx});
    const auto murkyView=world::sampleNereidUnderwater({murkyCamera},{0_fx,3_fx,0_fx});
    check(murkyView.absorption>clearView.absorption+0.7f &&
          murkyView.scattering>clearView.scattering && murkyView.silt==0.5f &&
          murkyView.transmission<clearView.transmission,
          "diving into turbid water must shorten visibility");
    render::PostFxUnderwater cameraTarget{};
    cameraTarget.submerged=true;
    cameraTarget.distortion=0.8f;
    cameraTarget.scattering=0.6f;
    auto cameraFade=render::advancePostFxUnderwater({},cameraTarget,0.1f);
    check(cameraFade.distortion>0.f && cameraFade.distortion<0.8f &&
          cameraFade.scattering>0.f && cameraFade.crossing>0.f,
          "entering water should start a bounded ripple and smooth optical settings");
    float oldPulse=cameraFade.crossing;
    cameraFade=render::advancePostFxUnderwater(cameraFade,cameraTarget,0.1f);
    check(cameraFade.crossing<oldPulse,
          "surface-crossing impulse should decay without a second crossing");
    cameraTarget.submerged=false;
    cameraFade=render::advancePostFxUnderwater(cameraFade,cameraTarget,0.1f);
    check(cameraFade.crossing>0.f && cameraFade.blend>0.f,
          "leaving water should generate ripple while tint fades out");
    const std::string compositeShader(render::postfx::kComposite);
    check(compositeShader.find("graphFrame.y")!=std::string::npos &&
          compositeShader.find("TEX0(warped)")!=std::string::npos &&
          compositeShader.find("scatterWeight")!=std::string::npos,
          "shared GLSL/Metal compositor must contain ripple and scattering");
    // Stage 20: explicit quality budgets and near-surface camera blend.
    const auto offBudget=render::postFxUnderwaterBudget(render::PostFxQuality::Off);
    const auto lowBudget=render::postFxUnderwaterBudget(render::PostFxQuality::Low);
    const auto medBudget=render::postFxUnderwaterBudget(render::PostFxQuality::Medium);
    const auto highBudget=render::postFxUnderwaterBudget(render::PostFxQuality::High);
    check(offBudget.samples==0 && offBudget.distortion==0.f &&
          lowBudget.samples==1 && medBudget.samples==2 &&
          highBudget.samples==3 &&
          lowBudget.scattering<medBudget.scattering &&
          medBudget.scattering<highBudget.scattering,
          "underwater mobile quality cost must increase monotonically");
    const auto topCamera=world::sampleNereidUnderwater(
        opticalVolumes,{0_fx,Fixed::fromRatio(101,10),0_fx});
    const auto belowCamera=world::sampleNereidUnderwater(
        opticalVolumes,{0_fx,Fixed::fromRatio(99,10),0_fx});
    check(!topCamera.submerged && topCamera.surfaceBlend>0.f &&
          topCamera.surfaceBlend<0.5f,
          "above-water camera must receive a small transition weight");
    check(belowCamera.submerged && belowCamera.surfaceBlend>0.5f &&
          belowCamera.surfaceBlend<1.f,
          "submerged near-surface camera must smoothly gain optical weight");
    check(!world::sampleNereidUnderwater(
        opticalVolumes,{0_fx,11_fx,0_fx}).submerged,
        "camera well above water should remain unsubmerged");
    render::PostFxUnderwater nearTarget{};
    nearTarget.submerged=false;
    nearTarget.surfaceBlend=topCamera.surfaceBlend;
    nearTarget.absorption=0.4f;
    const auto partialState=render::advancePostFxUnderwater({},nearTarget,0.1f);
    check(partialState.blend>0.f && partialState.blend<0.5f &&
          partialState.absorption>0.f,
          "surface transition may begin before canonical submersion");
    nearTarget.surfaceBlend=belowCamera.surfaceBlend;
    nearTarget.submerged=true;
    auto crossingState=render::advancePostFxUnderwater(
        partialState,nearTarget,0.1f);
    check(crossingState.blend>partialState.blend &&
          crossingState.crossing>0.f,
          "underwater boundary crossing must fade without a hard color jump");
    const std::string mobileShader(render::postfx::kComposite);
    check(mobileShader.find("underwater2.z > 1.5")!=std::string::npos &&
          mobileShader.find("underwater2.z > 2.5")!=std::string::npos &&
          mobileShader.find("TEX0(warped)")!=std::string::npos,
          "shared shader must expose bounded tier-dependent distortion");
    // Stage 21: shared compositor has an optional per-pixel depth path.
    // Backend selects the PF_UNDERWATER_DEPTH shader variant only with a
    // completed non-feedback scene depth texture. Low uses the old fallback.
    const std::string depthComposite(render::postfx::kComposite);
    check(depthComposite.find("#ifdef PF_UNDERWATER_DEPTH")!=std::string::npos &&
          depthComposite.find("PF_DEPTH_SAMPLE(TUV(s))")!=std::string::npos &&
          depthComposite.find("inverseViewProj0 * clip.x")!=std::string::npos &&
          depthComposite.find("inverseViewProj3 * clip.w")!=std::string::npos,
          "underwater shader must reconstruct per-pixel world position from depth");
    check(depthComposite.find("underwaterCamera.w > 0.5")!=std::string::npos &&
          depthComposite.find("rawDepth * 2.0 - 1.0")!=std::string::npos &&
          depthComposite.find("rawDepth < 0.9999")!=std::string::npos,
          "underwater shader must handle GL/Metal clip ranges and sky fallback");
    check(depthComposite.find("length(hitPosition - underwaterCamera.xyz)")!=
          std::string::npos &&
          depthComposite.find("float opticalDistance = min(underwater1.y + 5.0, 120.0)")!=
          std::string::npos,
          "underwater view distance must preserve a depth-free fallback");
    check(render::postFxUnderwaterBudget(render::PostFxQuality::Low).samples==1 &&
          render::postFxUnderwaterBudget(render::PostFxQuality::High).samples==3,
          "depth capability must not change established water quality budgets");
    // Depth-aware underwater compositing must integrate only the portion
    // of a view ray inside the volume, including above-to-below crossings.
    check(render::postFxUnderwaterRayFraction(5.f,10.f)==1.f,
          "fully submerged rays should attenuate over their entire distance");
    check(render::postFxUnderwaterRayFraction(-5.f,-2.f)==0.f,
          "entirely airborne rays must not receive underwater extinction");
    check(render::postFxUnderwaterRayFraction(-3.f,7.f)>0.69f &&
          render::postFxUnderwaterRayFraction(-3.f,7.f)<0.71f,
          "entry ray should only attenuate over the submerged portion");
    check(render::postFxUnderwaterRayFraction(2.f,-6.f)==0.25f,
          "exiting water should attenuate only the initial immersed segment");
    check(render::postFxUnderwaterRayFraction(
          std::numeric_limits<float>::quiet_NaN(),1.f)==0.f,
          "invalid depth values must not propagate into optical absorption");
    render::PostFxUnderwater rayTarget{};
    rayTarget.submerged=true;
    rayTarget.surfaceOffset=2.5f;
    const auto rayState=render::advancePostFxUnderwater({},rayTarget,0.1f);
    check(rayState.surfaceOffset==2.5f,
          "Prisma should retain signed water-plane offset from NEREID");
    check(depthComposite.find("float startDepth = underwater2.w")!=
          std::string::npos &&
          depthComposite.find("float endDepth = startDepth +")!=
          std::string::npos &&
          depthComposite.find("immersedFraction")!=std::string::npos,
          "shared GL/Metal shader must clip underwater distance to water plane");
    // Finite flow texture depends on canonical water data, never observer/mesh LOD.
    WaterTestDevice flowDevice;
    world::NereidWaterScene localWater;
    world::NereidSceneSurface localSource;
    localSource.id=42;localSource.body.halfExtents={8_fx,8_fx};
    localSource.body.flow={1_fx,0_fx,-2_fx};
    localSource.autoLod=true;localSource.mediumDistance=20;localSource.farDistance=40;
    localWater.setSurfaces({localSource});
    Vec3 nearObserver{0_fx,5_fx,0_fx},farObserver{100_fx,5_fx,100_fx};
    localWater.update(flowDevice,&nearObserver);
    check(localWater.stats().flowUploads==1,"first resident water needs local current texture");
    localWater.update(flowDevice,&farObserver);
    check(localWater.stats().flowUploads==0&&localWater.stats().flowBuilds==0,
          "camera and LOD must not rebuild finite flow field");
    const auto meshRevision=localWater.surfaceRevision(42);
    localSource.body.flow.x=-1_fx;localWater.setSurfaces({localSource});
    check(localWater.surfaceRevision(42)==meshRevision,"current change must not remesh water");
    localWater.update(flowDevice,&nearObserver);
    check(localWater.stats().flowUploads==1,"manual current edit must update local texture");
    auto transfer=[](uint64_t id,Vec3,Vec3 flow){check(id==42,"resolver stable water identity");flow.z=3_fx;return flow;};
    localWater.update(flowDevice,&nearObserver,transfer);
    check(localWater.stats().flowUploads==1,"host confluence current must reach local texture");
    localWater.update(flowDevice,&nearObserver,transfer);
    check(localWater.stats().flowUploads==0&&localWater.stats().flowBuilds==1,
          "unversioned resolver must revalidate but reuse exact unchanged GPU payload");
    localWater.shutdown(flowDevice);
    std::cout<<"PASS NEREID renderer bridge + multi-height planar fallback contract\n";
}
