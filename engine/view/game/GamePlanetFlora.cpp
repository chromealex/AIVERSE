// TerraWeave trees and rocks on cube-sphere bodies (GamePlanetFlora.h).
#include "GamePlanetFlora.h"
#include "../../game/MapGrid.h"
#include "engine/render/TerraWeaveModelAdapter.h"
#include "engine/render/TerraWeaveRockModel.h"
#include "engine/world/TerraWeaveSphere.h"
#include "engine/world/TerraWeaveTreePresets.h"
#include "engine/world/TerraWeaveForestSpeciesManifest.h"
#include "engine/world/TerraWeaveDeadwoodPresentation.h"
#include "aiverselib/world/EvolvePlanetDeadwood.h"
#include "aiverselib/world/PlanetConsequenceOwner.h"
#include "engine/world/TerraWeaveForestHarvestEcs.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>
#include <tuple>

namespace aiverse::game {
using render::float3;
namespace tw = aiverse::terraweave;

namespace {
// Prototype order = species order below. Trees use the botanical presets, rocks the rock generator.
constexpr int kTreeProtos = 5;
const uint32_t kTreePreset[kTreeProtos] = {0 /*spruce*/, 1 /*pine*/, 4 /*oak*/, 5 /*birch*/, 8 /*willow*/};
constexpr float kRockBoost = 2.0f;   // rocks read better a bit larger than life at planet scale

std::vector<tw::FoliageSpecies> floraSpecies() {
    using B = tw::Biome;
    auto sp = [](uint32_t id, uint32_t mask, uint32_t weight, int32_t slope, uint32_t lo, uint32_t hi) {
        tw::FoliageSpecies s;
        s.id = id; s.biomeMask = mask; s.weight = weight; s.maxSlopePermille = slope;
        s.scaleMinPermille = lo; s.scaleMaxPermille = hi;
        return s;
    };
    const uint32_t tundra = tw::biomeBit(B::Tundra), forest = tw::biomeBit(B::Forest), grass = tw::biomeBit(B::Grassland),
                   wet = tw::biomeBit(B::Wetland), desert = tw::biomeBit(B::Desert), alpine = tw::biomeBit(B::Alpine);
    return {sp(1, tundra | forest, 3, 700, 800, 1200),           // spruce
            sp(2, tundra | grass | forest, 2, 700, 800, 1200),   // pine
            sp(3, forest | grass, 3, 600, 800, 1250),            // oak
            sp(4, forest | wet | grass, 2, 600, 800, 1200),      // birch
            sp(5, wet, 3, 500, 850, 1200),                       // willow
            sp(6, desert | grass | tundra | alpine, 1, 3000, 600, 1500),   // boulder
            sp(7, desert | alpine, 2, 3000, 600, 1400)};                   // slab
}
} // namespace

bool collectPlanetForestVisuals(const World& world,const Surface& surface,
                              PlanetForestVisualSnapshot& output,
                              const std::vector<uint8_t>* visibility){
    namespace ev=aiverse::evolve;
    namespace wp=aiverse::lib::worldprocess;
    const auto* roots=world.resourceConst<tw::TerraWeaveAuthorityResource>();
    const auto* delivery=world.resourceConst<ev::PlanetRootDeliveryResource>();
    PlanetForestVisualSnapshot next;
    if(!roots||!delivery){output=std::move(next);return true;}
    if(!surface.tab||roots->rooted.size()!=delivery->bindings.size()||
       roots->rooted.size()>ev::kMaxPlanetRootBindings)return false;
    // In restricted views, the opt-in owner suppresses cosmetic tree scatter
    // on every body, even with zero roots. Dynamic binding counts must not
    // toggle static scenery on a visible part of an otherwise unseen body.
    next.managedBodies.resize(surface.bodies.size(),visibility?1:0);
    const bool validVision=!visibility||(surface.W>0&&surface.H>0&&
        visibility->size()==size_t(surface.W)*size_t(surface.H));
    if(!validVision){output=std::move(next);return true;}
    const auto visible=[&](Vec3 p){
        if(!visibility)return true;
        if(!Surface::onSphere(p))return false;
        const int cell=surface.cellAt(p);
        return cell>=0&&size_t(cell)<visibility->size()&&(*visibility)[size_t(cell)]==2;
    };
    std::map<tw::AnchoredPlantId,const ev::PlanetRootBinding*> bindings;
    for(const auto& b:delivery->bindings){
        const auto frame=wp::surfaceFrame(&surface,b.plantedPosition);
        if(!frame||surface.bodies[size_t(frame->body)].id!=b.bodyId||
           !bindings.emplace(b.id,&b).second)return false;
        next.managedBodies[size_t(frame->body)]=1;
    }
    std::set<tw::AnchoredPlantId> dead;
    if(const auto* ecology=world.resourceConst<tw::ForestEcologyResource>())
        for(const auto& item:ecology->plants)if(item.dead)dead.insert(item.id);
    const auto* wood=world.resourceConst<ev::PlanetDeadwoodResource>();
    const auto* harvest=world.resourceConst<tw::ForestHarvestResource>();
    std::map<tw::AnchoredPlantId,const ev::PlanetPlantFallRecord*> falls;
    if(delivery->events.size()>ev::kMaxPlanetPlantFallEvents)return false;
    for(const auto& event:delivery->events){
        const auto binding=bindings.find(event.fall.event.id);
        if(binding==bindings.end()||event.fall.bodyId!=binding->second->bodyId||
           event.fall.plantedPosition!=binding->second->plantedPosition)continue;
        auto [it,inserted]=falls.emplace(event.fall.event.id,&event);
        if(!inserted&&ev::planetFallKey(it->second->fall)<ev::planetFallKey(event.fall))it->second=&event;
    }
    std::map<wp::ConsequenceKey,const wp::ConsequenceBody*> physical;
    if(const auto* consequences=world.resourceConst<wp::PlanetConsequenceResource>())
        for(const auto& item:consequences->bodies)
            if(item.key.kind==wp::ConsequenceKind::Plant)
                if(!physical.emplace(item.key,&item).second)return false;
    const auto asFloat=[](Vec3 p){return float3{float(p.x.toDouble()),float(p.y.toDouble()),float(p.z.toDouble())};};
    std::set<tw::AnchoredPlantId> seen;
    for(const auto& plant:roots->rooted){
        const tw::AnchoredPlantId id{plant.anchor.speciesId,plant.anchor.plantKey};
        if(!id.speciesId||!id.key||!seen.insert(id).second||!bindings.contains(id))return false;
        if(harvest&&tw::isForestHarvested(*harvest,id))continue;
        const auto& binding=*bindings.at(id);
        const auto frame=wp::surfaceFrame(&surface,binding.plantedPosition);
        if(!frame)return false;
        const auto body=frame->body;
        const auto anchor=Surface::anchor(body);
        const auto appearance=tw::plantAppearance(plant.environment);
        Vec3 visibilityPosition=binding.plantedPosition;
        PlanetForestVisual visual;
        visual.species=id.speciesId;visual.key=id.key;visual.body=body;
        // Explicit built-in fallback palette, not species asset authoring.
        visual.proto=int((id.speciesId-1)%kTreeProtos);
        visual.size=float(appearance.sizePermille)*.001f;
        if(plant.anchor.roots.habit==tw::PlantHabit::Shrub)visual.size*=.25f;
        visual.foliage=float(appearance.foliagePermille)*.001f;
        visual.local=asFloat(binding.plantedPosition-anchor);
        visual.up=asFloat(frame->up);visual.forward=asFloat(frame->south);
        visual.fallen=plant.fallen;
        if(plant.fallen||dead.contains(id)){
            visual.foliage=0;
            if(wood){
                const auto state=tw::deadwoodVisualState(wood->ledger,id);
                if(state.tier==tw::DeadwoodVisualTier::Gone)continue;
                visual.tint={state.tintRPermille*.001f,state.tintGPermille*.001f,state.tintBPermille*.001f};
                // At constant length, transverse area tracks remaining organic mass.
                visual.thickness=std::sqrt(float(state.remainingPermille)*.001f);
            }
        }
        if(plant.fallen){
            const auto receipt=falls.find(id);
            const auto motion=receipt!=falls.end()&&receipt->second->acknowledged
                ?physical.find(wp::consequenceKey(receipt->second->fall)):physical.end();
            if(motion!=physical.end()){
                const auto& record=*motion->second;
                if(record.body!=binding.bodyId)return false;
                const auto basis=fxQuaternionBasis(record.motion.rotation);
                if(!basis)return false;
                visibilityPosition=record.motion.position;
                visual.local=asFloat(record.motion.position-anchor);
                visual.up=asFloat(basis->up);visual.forward=asFloat(basis->forward);
                visual.physical=true;
            }else{
                // Acknowledged and retired physical source stays hidden.
                if(receipt!=falls.end()&&receipt->second->acknowledged)continue;
                const auto direction=receipt==falls.end()?frame->east:receipt->second->fall.tangentDirection;
                visual.up=asFloat(direction.lengthSq().raw>0?fxNormalize(direction):frame->east);
                visual.forward=asFloat(frame->up);
                visual.local=asFloat(surface.project(binding.plantedPosition)-anchor);
            }
        }
        if(visible(visibilityPosition))next.plants.push_back(visual);
    }
    std::sort(next.plants.begin(),next.plants.end(),[](const auto& a,const auto& b){
        return std::tuple{a.body,a.species,a.key}<std::tuple{b.body,b.species,b.key};
    });
    output=std::move(next);return true;
}

PlanetForestModels buildPlanetForestSpecies(const Json& manifest,const render::FileReader& reader){
    PlanetForestModels models;
    if(manifest.type()==Json::Type::Null)return models;
    if(!manifest.isObject()||!manifest["species"].isArray()||
       manifest["species"].size()==0||manifest["species"].size()>64)
        throw JsonError("world.terraweaveForest.species: expected 1..64 entries");
    if(manifest.has("botanicalTextures")&&!manifest["botanicalTextures"].isString())
        throw JsonError("world.terraweaveForest.botanicalTextures: expected string");
    const auto dir=manifest.getStr("botanicalTextures","");
    if(!dir.empty()&&(!reader||!world::safeAssetPath(dir)))
        throw JsonError("world.terraweaveForest.botanicalTextures: invalid content directory or reader");
    for(const auto& def:manifest["species"].items()){
        const auto recipe=world::parseForestSpeciesRecipe(def);
        const auto id=recipe.profile.speciesId;
        if(models.contains(id))throw JsonError("world.terraweaveForest.species: duplicate id");
        auto model=tw::makeArborModel(recipe.profile,recipe.seed,recipe.mesh);
        if(recipe.preset&&!dir.empty()){
            tw::applyBotanicalTexture(*model,0,dir,std::string(recipe.preset->barkMaterial),reader);
            tw::applyBotanicalTexture(*model,1,dir,std::string(recipe.preset->leafMaterial),reader);
        }
        models.emplace(id,std::move(model));
    }
    return models;
}

namespace {
void releasePlanetForestModels(gfx::Device& dev,PlanetForestModels& models){
    for(auto& [id,model]:models){(void)id;render::releaseModel(dev,*model);}
    models.clear();
}
}
void PlanetFlora::setAuthoredSpecies(gfx::Device& dev,const Json& manifest,const render::FileReader& reader){
    auto next=buildPlanetForestSpecies(manifest,reader);
    if(next.empty()&&authored_.empty()&&pendingAuthored_.empty())return;
    // Map switch/reload only. Never retire a model used by an earlier frame
    // until the owner has proved device idle.
    if(!dev.waitForResourceIdle())throw std::runtime_error("planet forest: GPU idle barrier failed");
    releasePlanetForestModels(dev,pendingAuthored_);
    pendingAuthored_=std::move(next);
    try{
        for(auto& [id,model]:pendingAuthored_){
            (void)id;render::prepareModelUpload(*model);
            if(!render::uploadModel(dev,*model,false,false))
                throw std::runtime_error("planet forest: authored model upload failed");
        }
    }catch(...){
        if(dev.waitForResourceIdle())releasePlanetForestModels(dev,pendingAuthored_);
        throw;
    }
    releasePlanetForestModels(dev,authored_);
    authored_.swap(pendingAuthored_);
}

bool PlanetFlora::makePrototypes(gfx::Device& dev) {
    if (!protos_.empty()) return true;
    try {
        const auto& presets = tw::botanicalTreePresets();
        for (int i = 0; i < kTreeProtos; ++i) {
            auto m = tw::makeArborModel(presets[kTreePreset[i]].profile, 0x9e37u + uint64_t(i) * 7919u);
            protos_.push_back(std::move(m));
        }
        tw::RockProfile boulder;
        protos_.push_back(tw::makeRockModel(boulder, 0x51u));
        tw::RockProfile slab;
        slab.shape = tw::RockShape::Slab; slab.widthCm = 320; slab.heightCm = 110; slab.depthCm = 260;
        protos_.push_back(tw::makeRockModel(slab, 0x77u));
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[planet] flora prototypes: %s\n", e.what());
        protos_.clear();
        return false;
    }
    for (auto& m : protos_)
        if (!render::uploadModel(dev, *m)) {
            std::fprintf(stderr, "[planet] flora: model upload failed\n");
            for (auto& q : protos_) render::releaseModel(dev, *q);
            protos_.clear();
            return false;
        }
    return true;
}

void PlanetFlora::shutdown(gfx::Device& dev) {
    for (auto& m : protos_) render::releaseModel(dev, *m);
    protos_.clear();
    releasePlanetForestModels(dev,authored_);
    releasePlanetForestModels(dev,pendingAuthored_);
    instances_.clear();
}

void PlanetFlora::setWorld(gfx::Device& dev, const PlanetWorld& w, const std::vector<const PlanetSurface*>& surfaces) {
    instances_.clear();
    bool any = false;
    for (size_t bi = 0; bi < w.bodies.size(); ++bi)
        if (w.bodies[bi].surface() && w.bodies[bi].floraDensity > 0 && bi < surfaces.size() && surfaces[bi]) any = true;
    if (!any || !makePrototypes(dev)) return;
    const std::vector<tw::FoliageSpecies> species = floraSpecies();
    for (size_t bi = 0; bi < w.bodies.size(); ++bi) {
        const PlanetBody& pb = w.bodies[bi];
        const PlanetSurface* sf = bi < surfaces.size() ? surfaces[bi] : nullptr;
        if (!pb.surface() || pb.floraDensity <= 0 || !sf) continue;
        const int N = sf->faceSize();
        const float R = sf->radius();
        const float cell = 6.2831853f * R / float(4 * N);   // world units across one cell
        const float sea = pb.seaLevel > -1e8f ? pb.seaLevel : -1e9f;
        // the climate fields on a sphere of the body's size (1 world unit = 1 m), seeded by the body id
        uint64_t seed = 0xcbf29ce484222325ULL;
        for (char ch : pb.id) seed = (seed ^ uint8_t(ch)) * 0x100000001b3ULL;
        tw::SphereSettings sph;
        sph.radiusCm = std::clamp<int64_t>(int64_t(R * 100.0f), 100, tw::kWorldLimitCm / 4);
        tw::BiomeSettings bs;
        bs.seed = seed;
        bs.seaLevelCm = 0;
        bs.climatePeriodCm = int32_t(std::clamp<int64_t>(sph.radiusCm / 2, 100, 1 << 30));
        const uint32_t density = uint32_t(std::clamp(pb.floraDensity, 0.0f, 1.0f) * 1000.0f + 0.5f);
        for (int f = 0; f < 6; ++f)
            for (int y = 0; y < N; ++y)
                for (int x = 0; x < N; ++x) {
                    const uint8_t fl = sf->cellFlags(f, x, y);
                    if ((fl & kCellWater) || !(fl & kCellGround)) continue;
                    const float h00 = sf->cornerHeight(f, x, y), h10 = sf->cornerHeight(f, x + 1, y),
                                h01 = sf->cornerHeight(f, x, y + 1), h11 = sf->cornerHeight(f, x + 1, y + 1);
                    const float hAvg = (h00 + h10 + h01 + h11) * 0.25f;
                    if (hAvg < sea + 0.15f) continue;   // the coast stays clear
                    const float rise = std::max({h00, h10, h01, h11}) - std::min({h00, h10, h01, h11});
                    const int32_t slope = int32_t(std::min(rise / cell, 20.0f) * 1000.0f);
                    const float3 cd = sf->dirFromFace(f, float(x) + 0.5f, float(y) + 0.5f);
                    const tw::SpherePoint p{int64_t(cd.x * float(sph.radiusCm)), int64_t(cd.y * float(sph.radiusCm)),
                                            int64_t(cd.z * float(sph.radiusCm))};
                    const uint64_t key = (seed ^ (uint64_t(bi) << 48)) + uint64_t((f * N + y) * N + x) * 0x9e3779b97f4a7c15ULL;
                    const int32_t hCm = int32_t((hAvg - (sea > -1e8f ? sea : 0.0f)) * 100.0f);
                    const tw::SpherePlacement pl = tw::placeOnSphere(sph, bs, p, hCm, slope, key, density, species);
                    if (pl.species < 0) continue;
                    const float3 dir = sf->dirFromFace(f, float(x) + float(pl.offsetU) * 0.001f, float(y) + float(pl.offsetV) * 0.001f);
                    Instance in;
                    in.body = int(bi);
                    in.proto = pl.species;
                    in.up = dir;
                    in.local = dir * (R + sf->heightAt(dir));
                    in.yaw = float(pl.yawMilliDegrees) * 0.00001745329252f;
                    in.scale = pb.floraScale * float(pl.scalePermille) * 0.001f * (pl.species >= kTreeProtos ? kRockBoost : 1.0f);
                    instances_.push_back(in);
                }
    }
}

} // namespace aiverse::game
