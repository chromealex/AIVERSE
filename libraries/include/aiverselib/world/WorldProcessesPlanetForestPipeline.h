// Opt-in native planetary forest orchestration. One staged World per due
// batch; reuse canonical TerraWeave, PHYSIS and NEREID authorities.
#pragma once
#include "WorldProcessesPlanetBankPipeline.h"
#include "EvolvePlanetForestEcology.h"
#include "EvolvePlanetNutrients.h"
#include "EvolvePlanetNaturalFalls.h"
#include "PlanetArborCollision.h"
#include "EvolvePlanetForestRetirement.h"
#include "PlanetForestPendingDelivery.h"

namespace aiverse::lib::worldprocess {
struct PlanetForestPipelineState {
    uint32_t lastAttempt=UINT32_MAX,lastSuccess=UINT32_MAX;
    uint64_t successes=0,rejections=0,births=0,deaths=0,decayed=0,absorbed=0,advected=0;
};
inline void io(Archive& a,PlanetForestPipelineState& s){
    a(s.lastAttempt,s.lastSuccess,s.successes,s.rejections,s.births,s.deaths,s.decayed,s.absorbed,s.advected);
    if(s.lastSuccess!=UINT32_MAX&&(s.lastAttempt==UINT32_MAX||s.lastSuccess>s.lastAttempt))a.fail();
}
struct PlanetForestPipelineConfig {
    std::vector<terraweave::ForestEcologySpecies> species;
    terraweave::ForestEcologySettings ecology{};
    terraweave::ForestDeadwoodDecaySettings decay{};
    terraweave::ForestNutrientCycleSettings nutrient{};
    std::vector<evolve::PlanetWeatherMapping> weather;
    uint32_t naturalFallPermille=0; // disabled for legacy maps
    std::map<uint32_t,PlanetForestCapsuleRecipe> naturalFallBodies;
    std::map<uint32_t,PlanetArborTrunk> arborTrunks;
    std::map<std::string,Fixed> floraScales;
};
inline bool runPlanetForestPipeline(World& world,const game::Surface& surface,
        uint32_t tick,const PlanetForestPipelineConfig& cfg){
    using namespace terraweave;
    const auto* status=world.resourceConst<PlanetForestPipelineState>();
    const auto* history=world.resourceConst<ForestEcologyResource>();
    const auto* wood=world.resourceConst<evolve::PlanetDeadwoodResource>();
    if(!status||!wood||tick==UINT32_MAX||(status->lastAttempt!=UINT32_MAX&&status->lastAttempt>=tick)||
       !cfg.ecology.intervalTicks||!cfg.decay.intervalTicks||!cfg.nutrient.intervalTicks)return false;
    const bool ecoDue=!history||!history->initialized||
        (tick>=history->lastStepTick&&tick-history->lastStepTick>=cfg.ecology.intervalTicks);
    const bool decayDue=!wood->ledger.initialized||
        (tick>=wood->ledger.lastStepTick&&tick-wood->ledger.lastStepTick>=cfg.decay.intervalTicks);
    const bool nutrientDue=!wood->ledger.nutrientInitialized||
        (tick>=wood->ledger.lastNutrientTick&&tick-wood->ledger.lastNutrientTick>=cfg.nutrient.intervalTicks);
    if(!ecoDue&&!decayDue&&!nutrientDue)return true;
    auto report=*status;report.lastAttempt=tick;
    const auto reject=[&](){if(report.rejections!=UINT64_MAX)++report.rejections;
        world.setResource<PlanetForestPipelineState>(report);return false;};
    const auto add=[](uint64_t& total,uint64_t delta){
        if(delta>UINT64_MAX-total)return false;
        total+=delta;return true;
    };
    World staged=world;
    try{
        const auto* met=staged.resourceConst<physis::State>();
        if(!cfg.weather.empty()&&!met)return reject();
        auto mappings=cfg.weather;
        struct Weather {evolve::PlanetWeatherMapping map;physis::Sample sample;};
        std::vector<Weather> samples;
        std::set<uint64_t> ids;
        for(auto& mapping:mappings){
            // The authored angular location remains fixed as ground changes.
            const auto frame=surfaceFrame(&surface,mapping.ground);
            if(!frame||surface.bodies[size_t(frame->body)].id!=mapping.bodyId||
               !ids.insert(mapping.cell.packed()).second)return reject();
            mapping.ground=surface.project(mapping.ground);
            physis::Sample sample;
            if(!physis::read(staged,mapping.cell,sample)||
               physis::cellAt(staged,sample.cell.x,sample.cell.z)!=mapping.cell||
               sample.atmosphere.rain<Fixed{}||sample.atmosphere.rain>1_fx||
               sample.surface.wetness<Fixed{}||sample.surface.wetness>1_fx)return reject();
            samples.push_back({mapping,sample});
        }
        size_t comparisons=0;
        const auto at=[&](std::string_view body,Vec3 pos)->const physis::Sample*{
            const Weather* best=nullptr;Fixed nearest{};
            for(const auto& entry:samples){
                if(entry.map.bodyId!=body)continue;
                if(++comparisons>evolve::kMaxPlanetWeatherComparisons)
                    throw std::length_error("planetForest weather budget");
                const auto d=surface.groundDist(pos,entry.map.ground);
                if(d>entry.map.coverage)continue;
                if(!best||d<nearest||(d==nearest&&entry.map.cell.packed()<best->map.cell.packed())){
                    best=&entry;nearest=d;
                }
            }
            return best?&best->sample:nullptr;
        };
        const auto permille=[](Fixed v){return uint32_t(std::clamp<int64_t>((v*1000).toInt(),0,1000));};
        if(ecoDue){
            const auto result=evolve::advancePlanetForestEcology(staged,surface,tick,cfg.species,
                cfg.ecology,mappings,met?met->tick:0);
            if(!result||!add(report.births,result->born.size())||!add(report.deaths,result->ecology.died.size()))return reject();
        }
        if(decayDue){
            const auto result=evolve::advancePlanetDeadwood(staged,surface,tick,cfg.decay,
                [&](std::string_view body,Vec3 ground){
                    const auto* sample=at(body,ground);
                    if(!sample)return ForestDeadwoodClimate{};
                    return ForestDeadwoodClimate{int32_t(std::clamp<int64_t>(
                        (sample->atmosphere.temperature*100).toInt(),-8000,7000)),permille(sample->surface.wetness)};
                });
            if(!result||!add(report.decayed,result->decayedUnits))return reject();
        }
        // Release fully spent plant records before staging new natural falls.
        // The native publisher reconciles removed handles on its next frame.
        if(!retireSpentPlanetForestBodies(staged,surface,tick))return reject();
        if(cfg.naturalFallPermille){
            if(!evolve::reconcilePlanetNaturalFalls(staged,surface,tick,cfg.naturalFallPermille))return reject();
        }
        // Drain all configured due receipts, including terrain-induced falls,
        // not just new natural falls. Wait for the canonical mass ledger.
        for(const auto& fall:evolve::pendingPlanetPlantFalls(staged)){
            if(fall.event.tick>tick||!currentPlanetForestFall(staged,surface,fall))continue;
            const auto* wood=staged.resourceConst<evolve::PlanetDeadwoodResource>();
            const auto log=std::lower_bound(wood->ledger.logs.begin(),wood->ledger.logs.end(),fall.event.id,
                [](const auto& r,auto id){return r.id<id;});
            if(log==wood->ledger.logs.end()||log->id!=fall.event.id||
               log->createdTick>tick||log->lastEvaluatedTick>tick)continue;
            if(!log->remainingUnits){
                if(!completeSpentPlanetFall(staged,surface,fall,tick))return reject();
                continue;
            }
            const auto recipe=cfg.naturalFallBodies.find(fall.event.id.speciesId);
            if(recipe==cfg.naturalFallBodies.end())continue;
            auto shape=recipe->second;
            if(const auto trunk=cfg.arborTrunks.find(fall.event.id.speciesId);trunk!=cfg.arborTrunks.end()){
                const auto scale=cfg.floraScales.find(fall.bodyId);
                if(scale==cfg.floraScales.end())return reject();
                const auto derived=resolvePlanetArborFall(staged,fall,trunk->second,scale->second,shape);
                if(!derived)return reject();
                shape=*derived;
            }
            if(!deliverPlanetForestCapsule(staged,surface,fall,tick,shape))return reject();
        }
        if(nutrientDue){
            const auto result=evolve::advancePlanetNutrients(staged,surface,tick,cfg.nutrient,
                [&](std::string_view body,Vec3 ground){
                    const auto* sample=at(body,ground);
                    if(!sample)return evolve::PlanetNutrientWeather{};
                    return evolve::PlanetNutrientWeather{permille(sample->atmosphere.rain),permille(sample->surface.wetness)};
                });
            if(!result||!add(report.absorbed,result->absorbedUnits)||!add(report.advected,result->advectedUnits))return reject();
        }
        if(!add(report.successes,1))return reject();
        report.lastSuccess=tick;staged.setResource<PlanetForestPipelineState>(report);
        world=std::move(staged);return true;
    }catch(const std::exception&){return reject();}
}
inline PlanetForestPipelineConfig installPlanetForestPipeline(game::GameCore& core,Simulation& sim,const Json& json){
    using namespace terraweave;
    using namespace evolve;
    auto& world=sim.world();
    const auto* gs=world.resourceConst<game::GameState>();
    if(!json.isObject()||!gs||!gs->surface||!gs->grid.hf||
       world.resourceConst<TerraWeaveAuthorityResource>()||
       world.resourceConst<ForestEcologyResource>()||world.resourceConst<ForestDeadwoodResource>()||
       !json["species"].isArray()||!json["species"].size()||json["species"].size()>64||
       !json["plants"].isArray()||json["plants"].size()>kMaxPlanetRootBindings)
        throw std::invalid_argument("planetForest requires heightfield and exclusive rooted authority");
    const auto surface=gs->surface;
    PlanetForestPipelineConfig cfg;
    const auto number=[](const Json& j,const std::string& key,int64_t def,int64_t lo,int64_t hi){
        return planetBankInteger(j,key,def,lo,hi);
    };
    for(const auto& [key,value]:json.members())
        if(key!="species"&&key!="plants"&&key!="weather"&&key!="growthInterval"&&key!="decayInterval"&&
           key!="nutrientInterval"&&key!="maxPlants"&&key!="baselineNutrients"&&key!="naturalFallPermille")
            throw std::invalid_argument("planetForest unknown key: "+key);
    cfg.naturalFallPermille=uint32_t(number(json,"naturalFallPermille",0,0,1000));
    cfg.ecology.intervalTicks=uint32_t(number(json,"growthInterval",32,1,100000));
    cfg.decay.intervalTicks=uint32_t(number(json,"decayInterval",64,1,100000));
    cfg.nutrient.intervalTicks=uint32_t(number(json,"nutrientInterval",256,1,100000));
    cfg.ecology.maxTrackedPlants=uint32_t(number(json,"maxPlants",4096,1,4096));
    const auto baseline=uint32_t(number(json,"baselineNutrients",500,0,1000));
    std::map<uint32_t,ForestEcologySpecies> policies;
    const auto& authoredForest=core.matchMap()["world"]["terraweaveForest"];
    std::map<uint32_t,aiverse::world::ForestSpeciesProfile> profiles;
    for(const auto& body:surface->bodies)
        cfg.floraScales.emplace(body.id,Fixed::fromRatio(6,100));
    for(const auto& body:core.matchMap()["world"]["bodies"].items()){
        const auto it=cfg.floraScales.find(body.getStr("id"));
        if(it!=cfg.floraScales.end()){
            const auto& scale=body["visual"]["flora"]["scale"];
            if(scale.isNumber())it->second=fxClamp(scale.fx(),Fixed::fromRatio(1,1000),10_fx);
        }
    }
    for(const auto& item:json["species"].items()){
        if(!item.isObject())throw std::invalid_argument("planetForest species object");
        ForestEcologySpecies s;
        s.speciesId=uint32_t(number(item,"id",0,1,1000000));
        s.matureTicks=uint64_t(number(item,"matureTicks",120000,1,1000000000));
        s.lifespanTicks=uint64_t(number(item,"lifespanTicks",2400000,2,1000000000));
        s.seedIntervalTicks=uint64_t(number(item,"seedIntervalTicks",24000,1,1000000000));
        s.seedSpreadCm=uint32_t(number(item,"seedSpreadCm",700,50,20000));
        s.shadeDeathTicks=uint64_t(number(item,"shadeDeathTicks",240000,1,1000000000));
        s.floodDeathTicks=uint64_t(number(item,"floodDeathTicks",80000,1,1000000000));
        s.droughtDeathTicks=uint64_t(number(item,"droughtDeathTicks",240000,1,1000000000));
        validateForestEcologySpecies(s);
        if(!policies.emplace(s.speciesId,s).second)throw std::invalid_argument("planetForest duplicate species");
        cfg.species.push_back(s);
        if(item.has("naturalFallBody")){
            const auto& shape=item["naturalFallBody"];
            if(!shape.isObject())
                throw std::invalid_argument("planetForest naturalFallBody requires object");
            for(const auto& [key,value]:shape.members())
                if(key!="radiusCm"&&key!="halfHeightCm"&&key!="density"&&key!="frictionPermille"&&key!="fromArbor")
                    throw std::invalid_argument("planetForest naturalFallBody unknown key: "+key);
            if(shape.has("fromArbor")&&shape["fromArbor"].type()!=Json::Type::Bool)
                throw std::invalid_argument("planetForest naturalFallBody.fromArbor requires Boolean");
            if(shape.getBool("fromArbor",false)){
                if(shape.has("radiusCm")||shape.has("halfHeightCm"))
                    throw std::invalid_argument("planetForest ARBOR and explicit dimensions are exclusive");
                if(profiles.empty()){
                    if(!authoredForest.isObject()||!authoredForest["species"].isArray()||
                       !authoredForest["species"].size()||authoredForest["species"].size()>64)
                        throw std::invalid_argument("planetForest ARBOR collision needs authored world.terraweaveForest species");
                    for(const auto& def:authoredForest["species"].items()){
                        const auto profile=aiverse::world::parseForestSpeciesProfile(def);
                        if(!profiles.emplace(profile.profile.speciesId,profile).second)
                            throw std::invalid_argument("planetForest duplicate ARBOR species");
                    }
                }
                const auto profile=profiles.find(s.speciesId);
                if(profile==profiles.end())throw std::invalid_argument("planetForest missing matching ARBOR species");
                cfg.arborTrunks.emplace(s.speciesId,planetArborTrunk(profile->second));
            }
            PlanetForestCapsuleRecipe recipe;
            recipe.radius=Fixed::fromRatio(number(shape,"radiusCm",15,2,100000),100);
            recipe.halfHeight=Fixed::fromRatio(number(shape,"halfHeightCm",200,2,100000),100);
            recipe.density=Fixed::fromInt(number(shape,"density",600,1,1000000));
            recipe.friction=Fixed::fromRatio(number(shape,"frictionPermille",500,0,10000),1000);
            if(!validPlanetForestCapsule(recipe))throw std::invalid_argument("planetForest naturalFallBody invalid");
            cfg.naturalFallBodies.emplace(s.speciesId,recipe);
        }
    }
    const auto& authored=core.matchMap()["entities"];
    if(!authored.isArray()||authored.size()!=core.mapPlaced().size())
        throw std::invalid_argument("planetForest map identity");
    std::map<std::string,Entity> entities;
    for(size_t i=0;i<authored.size();++i){
        const auto id=authored[i].getStr("id");
        if(!id.empty()&&!entities.emplace(id,core.mapPlaced()[i]).second)
            throw std::invalid_argument("planetForest duplicate entity id");
    }
    const auto point=[&](const Json& item){
        const auto id=item.getStr("entity");
        if(!entities.contains(id)||!world.alive(entities.at(id)))
            throw std::invalid_argument("planetForest unresolved marker");
        const auto* tr=world.tryGetConst<Transform>(entities.at(id));
        if(!tr)throw std::invalid_argument("planetForest marker transform");
        const auto frame=surfaceFrame(surface.get(),tr->pos);
        if(!frame)throw std::invalid_argument("planetForest marker body");
        return std::pair{surface->bodies[size_t(frame->body)].id,surface->project(tr->pos)};
    };
    TerraWeaveAuthorityResource authority;
    authority.immutableRecipeId="planet-forest-"+std::to_string(planetDeadwoodTopology(*surface));
    authority.earth.spacingCm=100;authority.earth.bedrockCm=-1000000;
    std::vector<PlanetRootBinding> bindings;
    std::set<AnchoredPlantId> plantIds;
    for(const auto& item:json["plants"].items()){
        if(!item.isObject()||authority.rooted.size()>=cfg.ecology.maxTrackedPlants)
            throw std::invalid_argument("planetForest plant budget");
        const auto [body,ground]=point(item);
        const auto species=uint32_t(number(item,"species",0,1,1000000));
        const auto key=uint64_t(number(item,"key",0,1,1000000000));
        if(!policies.contains(species)||!plantIds.insert({species,key}).second)
            throw std::invalid_argument("planetForest plant identity");
        AnchoredPlant p;p.anchor.speciesId=species;p.anchor.plantKey=key;
        const auto habit=item.getStr("habit","tree");
        if(habit!="tree"&&habit!="shrub")throw std::invalid_argument("planetForest plant habit");
        p.anchor.roots=defaultRootProfile(habit=="tree"?PlantHabit::Tree:PlantHabit::Shrub);
        const auto raw=surface->altitude(ground).raw*100;
        p.anchor.plantedHeightCm=int32_t(raw>=0?(raw+Fixed::kOne/2)/Fixed::kOne:-((-raw+Fixed::kOne/2)/Fixed::kOne));
        const auto& policy=policies.at(species);
        p.environment.ageTicks=uint64_t(number(item,"ageTicks",int64_t(policy.matureTicks),0,1000000000));
        p.environment.matureTicks=policy.matureTicks;p.environment.lifespanTicks=policy.lifespanTicks;
        p.environment.nutrientsPermille=baseline;
        PlanetRootBinding binding{{species,key},body,ground};
        const auto support=evaluatePlanetRootSupport(*surface,p,binding);
        if(!support||support->uprooted)throw std::invalid_argument("planetForest invalid initial roots");
        p.support=*support;
        authority.rooted.push_back(p);bindings.push_back(binding);
    }
    std::sort(authority.rooted.begin(),authority.rooted.end(),[](const auto& a,const auto& b){
        return AnchoredPlantId{a.anchor.speciesId,a.anchor.plantKey}<AnchoredPlantId{b.anchor.speciesId,b.anchor.plantKey};
    });
    if(json.has("weather")&&!json["weather"].isArray())throw std::invalid_argument("planetForest weather array");
    if(json["weather"].size()>kMaxPlanetWeatherMappings)throw std::invalid_argument("planetForest weather budget");
    std::set<uint64_t> weatherIds;
    for(const auto& item:json["weather"].items()){
        const auto [body,ground]=point(item);
        const auto cell=physis::cellAt(world,int32_t(number(item,"x",0,-1000000,1000000)),
            int32_t(number(item,"z",0,-1000000,1000000)));
        if(!world.alive(cell)||!weatherIds.insert(cell.packed()).second)
            throw std::invalid_argument("planetForest PHYSIS mapping");
        cfg.weather.push_back({cell,body,ground,Fixed::fromInt(number(item,"coverageMetres",32,1,4096))});
    }
    sim.registerPersistentResource<TerraWeaveAuthorityResource>("terraweave.authority",2);
    sim.registerPersistentResource<ForestEcologyResource>("terraweave.forest_ecology",1);
    if(!world.resourceConst<PlanetRootDeliveryResource>())registerPlanetRootDeliveryPersistence(sim);
    registerPlanetDeadwoodPersistence(sim);
    if(!cfg.naturalFallBodies.empty()&&!world.resourceConst<PlanetConsequenceResource>())
        registerPlanetConsequencePersistence(sim);
    sim.registerPersistentResource<PlanetForestPipelineState>("worldprocess.planet_forest_pipeline",1);
    world.setResource<TerraWeaveAuthorityResource>(std::move(authority));
    world.setResource<ForestEcologyResource>();
    world.setResource<PlanetForestPipelineState>();
    if(!setPlanetRootBindings(world,*surface,bindings)||!initializePlanetDeadwood(world,*surface,baseline))
        throw std::invalid_argument("planetForest authority setup");
    sim.addSystem("worldprocess.planet_forest_pipeline",[cfg](Simulation& current){
        const auto* state=current.world().resourceConst<game::GameState>();
        if(!state||!state->surface)return;
        const auto surface=state->surface;
        (void)runPlanetForestPipeline(current.world(),*surface,current.tick(),cfg);
    });
    return cfg;
}
} // namespace aiverse::lib::worldprocess
