// Authoring host for native Arbor/Grass APIs. No scene-specific morphology.
#include "engine/render/TerraWeaveModelAdapter.h"
#include "engine/render/TerraWeaveGrassRender.h"
#include "engine/world/TerraWeaveTreePresets.h"
#include "engine/world/TerraWeaveBotanicalAppearance.h"
#include <set>
#include "tools/terrain/ExportModel.h"
#include <filesystem>
#include <fstream>
#include <iostream>
namespace fs=std::filesystem;
using aiverse::Json;
using namespace aiverse::terraweave;
using namespace aiverse::terrain_export;
int main(int argc,char** argv){try{
    fs::path out,materials,groundFile,placementFile;int64_t placementSpacing=550;uint64_t seed=42;std::string catalogKind="conifers",season="summer";uint32_t variants=4;PlantEnvironment environment;environment.ageTicks=100;environment.matureTicks=100;
    for(int i=1;i<argc;++i){const std::string k=argv[i];if(i+1==argc)throw std::runtime_error("missing value");const std::string v=argv[++i];if(k=="--out")out=v;else if(k=="--materials")materials=v;else if(k=="--ground")groundFile=v;else if(k=="--placements")placementFile=v;else if(k=="--placement-spacing-cm")placementSpacing=std::stoll(v);else if(k=="--seed")seed=std::stoull(v);else if(k=="--catalog")catalogKind=v;else if(k=="--season")season=v;else if(k=="--variants"){const auto n=std::stoull(v);if(n<1||n>8)throw std::runtime_error("variants1..8");variants=uint32_t(n);}else if(k=="--age-ticks")environment.ageTicks=std::stoull(v);else if(k=="--mature-ticks")environment.matureTicks=std::stoull(v);else if(k=="--lifespan-ticks")environment.lifespanTicks=std::stoull(v);else throw std::runtime_error("unknown option: "+k);}
    if(out.empty() || materials.empty())throw std::runtime_error("usage: aiverse_terraweave_vegetation --out NEW_DIR --materials VEGETATION_TEXTURES [--ground HEIGHT_GRID_JSON --seed N]");
    if(fs::exists(out))throw std::runtime_error("output exists; refusing overwrite");
    if((catalogKind!="conifers"&&catalogKind!="botanical")||(season!="summer"&&season!="winter")||variants<1||variants>8)throw std::runtime_error("catalog conifers|botanical; season summer|winter; variants1..8");
    (void)plantAppearance(environment);
    const bool botanical=catalogKind=="botanical";
    std::set<std::string> stems={"spruce_bark","spruce_leaves","pine_bark","pine_leaves"};
    if(botanical)for(const auto& p:botanicalTreePresets()){stems.insert(std::string(p.barkMaterial));stems.insert(std::string(p.leafMaterial));}
    for(const auto& name:stems)for(auto suffix:{"basecolor","normal","orm","ao"})if(!fs::is_regular_file(materials/(name+"_"+suffix+".png")))throw std::runtime_error("missing PBR map: "+name);
    Json placementSource;
    if(!placementFile.empty()) {
        if(!botanical||placementSpacing<1||placementSpacing>1000000)throw std::runtime_error("placements require botanical catalog and valid cell spacing");
        placementSource=Json::parseFile(placementFile.string());
        if(!placementSource["trees"].isArray()||placementSource["trees"].size()>100000)throw std::runtime_error("bounded landscape trees array required");
        for(const auto& entry:placementSource["trees"].items())
            for(const auto* key:{"xCm","yCm","zCm","yaw","scale"}) {
                if(!entry[key].isNumber())throw std::runtime_error("invalid placement transform");
                const auto value=entry[key].integer();
                if(value< -kWorldLimitCm||value>kWorldLimitCm)throw std::runtime_error("placement transform range");
            }
    }
    fs::create_directories(out/"textures");
    for(const auto& name:stems)for(auto suffix:{"basecolor","normal","orm","ao"}){std::string f=name+"_"+suffix+".png";fs::copy_file(materials/f,out/"textures"/f);}
    auto catalog=Json::object(),trees=Json::array(),patches=Json::array();
    const uint32_t count=botanical?uint32_t(botanicalTreePresets().size())*variants:12;
    for(uint32_t i=0;i<count;++i){
        const bool pine=i%3==0;TreeProfile p;TreePreset preset;
        std::string kind,bark,leaves;
        if(botanical) {
            preset=botanicalTreePresets()[i/variants];p=treeProfileAtAge(preset,environment);
            kind=std::string(preset.name);bark=std::string(preset.barkMaterial);leaves=std::string(preset.leafMaterial);
        } else {
            p.speciesId=pine?2:1;p.shape=CrownShape::Conifer;
            p.minHeightCm=pine?1200:900;p.maxHeightCm=pine?1800:1600;p.trunkRadiusCm=pine?28:24;
            p.crownStartPermille=pine?490:100;p.primaryBranches=pine?60:95;p.whorlSize=pine?3:5;p.roundedConiferCrown=pine;
            p.branchLengthPermille=pine?620:440;p.branchRisePermille=pine?90:35;p.leafRadiusCm=45;
            kind=pine?"pine":"spruce";bark=kind+"_bark";leaves=kind+"_leaves";
        }
        ArborMeshSettings mesh;mesh.radialSegments=8;mesh.doubleSidedLeaves=false;mesh.organicRootFlare=botanical;mesh.organicBranches=botanical;
        mesh.naturalConifer=p.shape==CrownShape::Conifer;mesh.naturalBroadleaf=!mesh.naturalConifer;
        mesh.compactConifer=mesh.naturalConifer;mesh.sprayDroop=mesh.naturalBroadleaf?(kind=="willow"?.85f:.12f):(kind=="pine"?.08f:kind=="larch"?.55f:.35f);mesh.sprayPairs=4;
        if(botanical)mesh=botanicalArborMeshSettings(preset);
        CanopyPhenology phenology;if(season=="winter")phenology.broadleafCoverPermille=0;
        mesh.leafCoveragePermille=botanical?treeCanopyAtAge(preset,environment,phenology):1000;
        const uint64_t instanceSeed=detail::mix64(seed^i);
        auto model=makeArborModel(p,instanceSeed,mesh);
        model->materials[0].baseColor={.68f,.62f,.53f,1};model->materials[0].roughness=1;
        model->materials[1].baseColor={.65f,.80f,.62f,1};model->materials[1].roughness=1;model->materials[1].alpha=aiverse::render::AlphaMode::Mask;model->materials[1].alphaCutoff=.38f;
        const std::string name="tree-"+std::to_string(i);save(out,name,*model,{bark,leaves});
        auto e=Json::object(),tri=Json::array();e.set("file",Json::string(name+".gltf"));e.set("kind",Json::string(kind));
        e.set("speciesId",Json::number(int64_t(p.speciesId)));e.set("seed",Json::string(std::to_string(instanceSeed)));e.set("season",Json::string(season));e.set("ageTicks",Json::number(int64_t(environment.ageTicks)));e.set("matureTicks",Json::number(int64_t(environment.matureTicks)));e.set("lifespanTicks",Json::number(int64_t(environment.lifespanTicks)));e.set("foliagePermille",Json::number(int64_t(mesh.leafCoveragePermille)));
        for(const auto& m:model->meshes){uint32_t n=0;for(const auto& pr:m.primitives)n+=pr.indexCount/3;tri.push(Json::number(int64_t(n)));}e.set("triangles",tri);trees.push(e);
    }
    if(!groundFile.empty()){
        const auto doc=Json::parseFile(groundFile.string());const int64_t ox=doc.getInt("originXcm"),oz=doc.getInt("originZcm"),step=doc.getInt("stepCm");const int64_t nx=doc.getInt("columns"),nz=doc.getInt("rows");
        if(nx<2||nz<2||nx>2048||nz>2048||step<1||step>1000||doc["heightCm"].size()!=size_t(nx*nz)||doc["coverPermille"].size()!=size_t(nx*nz)||doc["patches"].size()>256)throw std::runtime_error("invalid bounded height/cover grid");
        if(ox< -kWorldLimitCm||ox>kWorldLimitCm||oz< -kWorldLimitCm||oz>kWorldLimitCm||ox+nx*step>kWorldLimitCm||oz+nz*step>kWorldLimitCm)throw std::runtime_error("grid extent");
        if(doc.has("waterHeightCm")&&doc["waterHeightCm"].size()!=size_t(nx*nz))throw std::runtime_error("water height grid size");
        const bool nativeShore=doc.getStr("grassMaskSource")=="terra-nereid-shore-v1";
        const auto sandLimit=doc.getInt("grassSandLimitPermille",50);
        if(nativeShore){
            if(sandLimit<0||sandLimit>1000||doc["sandPermille"].size()!=size_t(nx*nz)||doc["wetPermille"].size()!=size_t(nx*nz))throw std::runtime_error("native shore mask schema");
            for(const auto* field:{"sandPermille","wetPermille"})for(const auto& v:doc[field].items())if(!v.isNumber()||v.integer()<0||v.integer()>1000)throw std::runtime_error("native shore mask range");
        }
        for(const auto& v:doc["heightCm"].items())if(!v.isNumber()||v.integer()< -1000000||v.integer()>1000000)throw std::runtime_error("height range");
        for(const auto& v:doc["coverPermille"].items())if(!v.isNumber()||v.integer()<0||v.integer()>1000)throw std::runtime_error("cover range");
        auto sample=[&](const char* field,int64_t x,int64_t z){const double gx=double(x-ox)/step,gz=double(z-oz)/step;const auto ix=int64_t(std::floor(gx)),iz=int64_t(std::floor(gz));if(ix<0||iz<0||ix>=nx-1||iz>=nz-1)throw std::runtime_error("patch outside grid");const double tx=gx-ix,tz=gz-iz;const auto& a=doc[field];return int32_t(std::lround((a[size_t(iz*nx+ix)].integer()*(1-tx)+a[size_t(iz*nx+ix+1)].integer()*tx)*(1-tz)+(a[size_t((iz+1)*nx+ix)].integer()*(1-tx)+a[size_t((iz+1)*nx+ix+1)].integer()*tx)*tz));};
        uint64_t key=1;
        for(const auto& entry:doc["patches"].items()){
            GrassPatch p;p.key=key++;p.seed=seed;p.bounds={entry.getInt("minX"),entry.getInt("minZ"),entry.getInt("maxX"),entry.getInt("maxZ")};p.maximumBlades=uint32_t(std::clamp<int64_t>(entry.getInt("blades",2048),1,8192));
            GrassRenderQuality q;q.maxBladesPerPatch=p.maximumBlades;q.crossedBlades=false;q.bladeSegments=3;q.bladeHeightCm=uint32_t(entry.getInt("bladeHeightCm",65));q.naturalBlades=true;q.bladeHalfWidthCm=uint32_t(entry.getInt("bladeHalfWidthCm",1));q.bladesPerTuft=uint32_t(entry.getInt("bladesPerTuft",16));q.tuftRadiusCm=uint32_t(entry.getInt("tuftRadiusCm",24));q.heightVariationPermille=uint32_t(entry.getInt("heightVariationPermille",450));validateGrassRenderQuality(q);
            const auto ground=[&](int64_t x,int64_t z){return sample("heightCm",x,z);};
            const auto allows=[&](int64_t x,int64_t z){if(nativeShore){if(sample("sandPermille",x,z)>sandLimit)return false;}else if(doc.has("waterHeightCm")&&sample("heightCm",x,z)-sample("waterHeightCm",x,z)<270)return false;return int64_t(detail::mix64(uint64_t(x)^detail::mix64(uint64_t(z)))%1000)<sample("coverPermille",x,z);};
            auto model=makeGrassPatchLodModel(p,ground,q,allows);
            if(!model)continue;
            model->materials[0].baseColor={.34f,.43f,.13f,1};const std::string name="grass-"+std::to_string(p.key);save(out,name,*model);
            auto e=Json::object();e.set("file",Json::string(name+".gltf"));e.set("xCm",Json::number(p.bounds.minX));e.set("zCm",Json::number(p.bounds.minZ));patches.push(e);
        }
    }
    if(!placementFile.empty()) {
        auto placements=Json::array();
        for(const auto& source:placementSource["trees"].items()) {
            auto entry=source;
            const auto index=treePrototypeVariant(detail::floorDiv(source.getInt("xCm"),placementSpacing),
                detail::floorDiv(source.getInt("zCm"),placementSpacing),seed,uint32_t(botanicalTreePresets().size()),variants);
            entry.set("variant",Json::number(int64_t(index)));
            entry.set("kind",trees[index]["kind"]);entry.set("prototypeSeed",trees[index]["seed"]);
            placements.push(entry);
        }
        catalog.set("placements",placements);
        catalog.set("placementSpacingCm",Json::number(placementSpacing));
        catalog.set("placementPolicy",Json::string("global-four-color-species-palette"));
    }
    catalog.set("version",Json::number(int64_t(1)));catalog.set("trees",trees);catalog.set("grass",patches);write(out/"catalog.json",catalog.dump());std::cout<<"Native Arbor prototypes: "<<trees.size()<<"; native grass patches: "<<patches.size()<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
