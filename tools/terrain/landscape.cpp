// Compose native Terra terrain/Earthworks and NEREID authoring definitions.
#include "engine/world/TerraWeaveRiverbed.h"
#include "engine/world/TerraWeaveAlpine.h"
#include "engine/world/TerraWeaveRocks.h"
#include "engine/world/TerraWeaveShoreCover.h"
#include "engine/world/nereid/NereidFootprintDistance.h"
#include "engine/render/TerraWeaveRockModel.h"
#include "engine/render/TerraWeaveTerrainPatchRender.h"
#include "engine/render/TerraWeaveTerrainLod.h"
#include "engine/world/NereidWaterManifest.h"
#include "tools/terrain/ExportModel.h"
#include <iostream>
using namespace aiverse;
using namespace aiverse::terraweave;
using namespace aiverse::terrain_export;
namespace fs=std::filesystem;
int main(int argc,char** argv){try{
    fs::path recipe,out,materials;
    for(int i=1;i<argc;++i){std::string k=argv[i];if(i+1==argc)throw std::runtime_error("missing value");std::string v=argv[++i];if(k=="--recipe")recipe=v;else if(k=="--out")out=v;else if(k=="--materials")materials=v;else throw std::runtime_error("unknown option");}
    if(recipe.empty()||out.empty()||materials.empty()||fs::exists(out))throw std::runtime_error("usage: aiverse_terraweave_landscape --recipe JSON --out NEW_DIR --materials TERRAIN_TEXTURES; no overwrite");
    auto doc=Json::parseFile(recipe.string());
    // New authored worlds opt into NEREID's existing camera-distance policy.
    // An explicit manual/fixed choice in the recipe remains authoritative.
    if(doc["nereidWater"].isObject()){
        auto water=doc["nereidWater"];auto surfaces=water["surfaces"];
        for(auto& surface:surfaces.itemsMut())if(!surface.has("autoLod"))surface.set("autoLod",Json::fromBool(true));
        water.set("surfaces",surfaces);doc.set("nereidWater",water);
    }
    const auto& t=doc["terrain"];
    TerrainSettings settings;settings.seed=uint64_t(t.getInt("seed",42));settings.baseHeightCm=int32_t(t.getInt("baseHeightCm",300));settings.reliefCm=int32_t(t.getInt("reliefCm",500));settings.basePeriodCm=int32_t(t.getInt("basePeriodCm",6400));settings.octaves=uint32_t(t.getInt("octaves",5));
    std::vector<AlpinePeak> peaks;
    for(const auto& p:doc["peaks"].items()) {
        const auto shape=p.getStr("morphology","legacy");
        if(shape!="legacy"&&shape!="ridged_massif"&&shape!="cragged_massif")throw std::invalid_argument("Alpine morphology legacy|ridged_massif|cragged_massif");
        auto peak=AlpinePeak{p.getInt("xCm"),p.getInt("zCm"),int32_t(p.getInt("radiusXcm",10000)),int32_t(p.getInt("radiusZcm",16000)),int32_t(p.getInt("heightCm",12000)),int32_t(p.getInt("shearPermille"))};
        peak.morphology=shape=="cragged_massif"?AlpineMorphology::CraggedMassif:shape=="ridged_massif"?AlpineMorphology::RidgedMassif:AlpineMorphology::LegacyCone;
        peaks.push_back(peak);
    }
    const auto water=world::parseNereidWaterManifest(doc);
    const auto bankWidthCm=doc.getInt("bankWidthCm",600),grassSandLimit=doc.getInt("grassSandLimitPermille",50),grassMaxElevation=doc.getInt("grassMaxElevationCm",2500);
    if(bankWidthCm<100||bankWidthCm>10000||grassSandLimit<0||grassSandLimit>1000||grassMaxElevation< -1000000||grassMaxElevation>1000000)throw std::runtime_error("invalid native shore/grass recipe");
    EarthworksLayer earth(50,[&](int64_t x,int64_t z){auto h=alpineHeightCm(settings,peaks,x,z,int32_t(doc.getInt("valleyHalfWidthCm",0)),int32_t(doc.getInt("valleyBlendCm",10000)));
        if(doc.getBool("alignRiverGrade",false)&&!water.surfaces.empty()&&water.surfaces[0].mesh.useGrade)h+=int32_t((lib::nereid::waterGradeOffset(water.surfaces[0].mesh.grade,{Fixed::fromRatio(x,100),{},Fixed::fromRatio(z,100)})*100_fx).floorInt());return h;});
    if(doc["brushes"].size()>64)throw std::runtime_error("too many terrain brushes");
    for(const auto& b:doc["brushes"].items())earth.sculpt({b.getInt("xCm"),b.getInt("zCm"),int32_t(b.getInt("radiusCm")),int32_t(b.getInt("depthCm")),b.getStr("mode","fill")=="dig"?EarthworkMode::Dig:EarthworkMode::Fill});
    const auto& bounds=doc["tiles"];const int64_t x0=bounds.getInt("minX"),z0=bounds.getInt("minZ"),x1=bounds.getInt("maxX"),z1=bounds.getInt("maxZ");
    if(x0< -128||x1>128||z0< -128||z1>128||x1<=x0||z1<=z0||(x1-x0)*(z1-z0)>128)throw std::runtime_error("tile budget");
    for(const auto& w:water.surfaces)if(w.useGeometry&&w.geometry.kind==lib::nereid::WaterGeometryKind::RiverSpline)
        for(auto z=z0;z<z1;++z)for(auto x=x0;x<x1;++x)carveNereidRiverbed(earth,w.body,w.geometry,{x*6400,z*6400,(x+1)*6400,(z+1)*6400},int32_t(bankWidthCm),w.mesh.useGrade?&w.mesh.grade:nullptr);
    fs::create_directories(out/"textures");const std::vector<std::string> mats={"grass","rock","snow","gravel_soil"};
    for(const auto& name:mats)for(auto suffix:{"basecolor","normal","orm","ao"}){const auto base=name+"_"+suffix;fs::copy_file(materials/(base+"_repeat.png"),out/"textures"/(base+".png"));}
    auto catalog=Json::object(),tiles=Json::array();
    for(auto z=z0;z<z1;++z)for(auto x=x0;x<x1;++x){
        const auto patch=bakeEarthworksTerrainPatch(earth,{int32_t(x),0,int32_t(z)},64,129);auto model=makeEarthworksTerrainModel(patch,nullptr,1.f/64.f);
        model->materials[0].name="terrain";model->materials[0].baseColor={1,1,1,1};model->materials[0].roughness=1;
        for(auto& v:model->vertices){
            const int64_t wx=x*6400+int64_t(std::llround(v.pos.x*100)),wz=z*6400+int64_t(std::llround(v.pos.z*100));
            const float dx=float(earth.sampleHeightCm(wx+50,wz)-earth.sampleHeightCm(wx-50,wz))*.01f;
            const float dz=float(earth.sampleHeightCm(wx,wz+50)-earth.sampleHeightCm(wx,wz-50))*.01f;
            v.nrm=render::normalize(render::float3{-dx,1,-dz});v.uv={v.pos.x/64,1-v.pos.z/64};
        }
        render::generateTangents(model->vertices,model->indices,0,uint32_t(model->indices.size()));
        const auto terrainLods=appendTerrainPatchLods(*model,patch);
        const std::string name="terrain-"+std::to_string(x)+"-"+std::to_string(z);save(out,name,*model,{name},true);auto e=Json::object();e.set("file",Json::string(name+".gltf"));e.set("xCm",Json::number(x*6400));e.set("zCm",Json::number(z*6400));auto levels=Json::array();for(const auto& level:terrainLods){auto entry=Json::object();entry.set("stride",Json::number(int64_t(level.stride)));entry.set("triangles",Json::number(int64_t(level.triangles)));entry.set("errorMetres",Json::number(double(level.errorMetres),6));entry.set("maxPixels",Json::number(double(level.maxPixels),6));levels.push(entry);}e.set("lods",levels);tiles.push(e);
    }
    // Same surface snapshot for vegetation and asset placement; no second noise.
    auto heights=Json::array(),waterHeights=Json::array(),cover=Json::array(),sand=Json::array(),wetness=Json::array(),ground=Json::object(),grass=Json::array();
    const int64_t minX=x0*6400,minZ=z0*6400,nx=(x1-x0)*64+1,nz=(z1-z0)*64+1;
    for(int64_t j=0;j<nz;++j)for(int64_t i=0;i<nx;++i){
        const int64_t x=minX+i*100,z=minZ+j*100;const auto h=earth.sampleHeightCm(x,z);
        const Vec2 point{Fixed::fromRatio(x,100),Fixed::fromRatio(z,100)};
        ShoreCover mask;int64_t legacyWater=0;bool first=true;
        for(const auto& w:water.surfaces) {
            const auto local=lib::nereid::gradedWaterBody(w.body,w.mesh.useGrade?&w.mesh.grade:nullptr,{point.x,{},point.y});
            if(first){legacyWater=(local.surfaceY*100_fx).floorInt();first=false;}
            const auto distance=lib::nereid::waterFootprintDistance(w.body,w.useGeometry?&w.geometry:nullptr,point);
            if(!distance)continue;
            const auto candidate=shoreCover(*distance,Fixed::fromRatio(h,100)-local.surfaceY,Fixed::fromRatio(bankWidthCm,100));
            mask.sandPermille=std::max(mask.sandPermille,candidate.sandPermille);
            mask.wetPermille=std::max(mask.wetPermille,candidate.wetPermille);
        }
        heights.push(Json::number(int64_t(h)));waterHeights.push(Json::number(legacyWater));
        sand.push(Json::number(int64_t(mask.sandPermille)));wetness.push(Json::number(int64_t(mask.wetPermille)));
        cover.push(Json::number(int64_t(mask.sandPermille>grassSandLimit||h>grassMaxElevation?0:950)));
    }
    ground.set("version",Json::number(int64_t(2)));ground.set("grassMaskSource",Json::string("terra-nereid-shore-v1"));ground.set("grassSandLimitPermille",Json::number(grassSandLimit));
    ground.set("sandPermille",sand);ground.set("wetPermille",wetness);
    ground.set("originXcm",Json::number(minX));ground.set("originZcm",Json::number(minZ));ground.set("stepCm",Json::number(int64_t(100)));ground.set("columns",Json::number(nx));ground.set("rows",Json::number(nz));ground.set("waterHeightCm",waterHeights);ground.set("heightCm",heights);ground.set("coverPermille",cover);
    for(const auto& p:doc["grassPatches"].items())grass.push(p);ground.set("patches",grass);write(out/"ground.json",ground.dump());
    auto placements=Json::array(),stones=Json::array();
    const auto& area=doc["vegetationBounds"];
    const RectCm region{area.getInt("minX"),area.getInt("minZ"),area.getInt("maxX"),area.getInt("maxZ")};
    FoliageSpecies species;species.id=1;species.biomeMask=0xffffffffu;species.scaleMinPermille=650;species.scaleMaxPermille=1250;
    ScatterSettings scatterSettings;scatterSettings.seed=settings.seed;scatterSettings.spacingCm=550;scatterSettings.densityPermille=760;
    const auto wetAt=[&](int64_t x,int64_t z,int32_t margin){
        for(const auto& w:water.surfaces){auto g=w.geometry;for(auto& p:g.points)p.halfWidth+=Fixed::fromRatio(margin,100);
            if(lib::nereid::coversWaterBody(w.body,w.useGeometry?&g:nullptr,{Fixed::fromRatio(x,100),{},Fixed::fromRatio(z,100)}))return true;}return false;
    };
    for(const auto& at:scatter(settings,{},scatterSettings,region,{species})){
        const auto h=earth.sampleHeightCm(at.xCm,at.zCm);
        if(wetAt(at.xCm,at.zCm,200)||h>4800||h<40)continue;
        auto e=Json::object();e.set("xCm",Json::number(at.xCm));e.set("zCm",Json::number(at.zCm));e.set("yCm",Json::number(int64_t(h-12)));e.set("yaw",Json::number(int64_t(at.yawMilliDegrees)));e.set("scale",Json::number(int64_t(at.scalePermille)));e.set("variant",Json::number(int64_t(rockVariant(terraweave::detail::floorDiv(at.xCm,550),terraweave::detail::floorDiv(at.zCm,550),settings.seed,12))));placements.push(e);
    }
    RockScatterSettings rs;rs.seed=settings.seed;rs.spacingCm=300;rs.radiusMinCm=35;rs.radiusMaxCm=140;rs.variants=12;rs.densityPermille=400;
    std::vector<std::unique_ptr<render::Model>> rockModels;
    for(uint32_t i=0;i<12;++i){RockProfile p;p.shape=RockShape(i%4);p.weatheringPermille=100+(i*173)%700;rockModels.push_back(makeRockModel(p,terraweave::detail::mix64(uint64_t(doc.getInt("rockSeed",42))^i)));}
    for(const auto& at:scatterRocks(region,rs,[&](int64_t x,int64_t z)->std::optional<int32_t>{return earth.sampleHeightCm(x,z);},[&](const auto& at){return wetAt(at.xCm,at.zCm,600);})){
        auto e=Json::object();const auto pose=rockPose(*rockModels[at.variant],at);
        auto pos=Json::array();pos.push(Json::number(double(pose.position.x)));pos.push(Json::number(double(pose.position.y)));pos.push(Json::number(double(pose.position.z)));e.set("position",pos);e.set("scale",Json::number(double(pose.scale),6));e.set("xCm",Json::number(at.xCm));e.set("zCm",Json::number(at.zCm));e.set("yCm",Json::number(int64_t(at.groundCm)));e.set("radiusCm",Json::number(int64_t(at.radiusCm)));e.set("yaw",Json::number(int64_t(at.yawMilliDegrees)));e.set("variant",Json::number(int64_t(at.variant)));stones.push(e);
    }
    catalog.set("trees",placements);catalog.set("rocks",stones);
    catalog.set("terrain",tiles);catalog.set("nereidWater",doc["nereidWater"]);write(out/"catalog.json",catalog.dump());write(out/"recipe.json",doc.dump());std::cout<<"Native Terra terrain tiles: "<<tiles.size()<<"; NEREID surfaces: "<<water.surfaces.size()<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
