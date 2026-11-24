// Bounded read-only completed local ECS snapshot, never a physics owner.
#pragma once
#include "aiverselib/world/EvolvePlanetDeadwood.h"
#include "aiverselib/world/PlanetConsequenceOwner.h"
#include "engine/world/TerraWeaveForestHarvestEcs.h"
#include "apps/studio-native/StudioPlanetForestWeather.h"
#include "apps/studio-native/StudioForestCameraFocus.h"
namespace aiverse::studio {
inline constexpr size_t kStudioForestDiagnosticLimit=128;
inline constexpr size_t kStudioForestRenderLimit=2048;
inline Json sampleStudioPlanetForest(const World& ecs,const game::Surface& surface,uint32_t tick,size_t limit=128,
        const std::vector<evolve::PlanetWeatherMapping>& weather={},const std::string& focusBody={},
        const std::optional<StudioForestCameraFocus>& camera={},bool renderPopulation=false){
    limit=std::min(limit,renderPopulation?kStudioForestRenderLimit:kStudioForestDiagnosticLimit);
    namespace tw=terraweave;namespace ev=evolve;namespace wp=lib::worldprocess;
    Json out=Json::object(),rows=Json::array();
    const auto* roots=ecs.resourceConst<tw::TerraWeaveAuthorityResource>();
    const auto* bindings=ecs.resourceConst<ev::PlanetRootDeliveryResource>();
    const auto* history=ecs.resourceConst<tw::ForestEcologyResource>();
    const auto* wood=ecs.resourceConst<ev::PlanetDeadwoodResource>();
    const auto* physics=ecs.resourceConst<wp::PlanetConsequenceResource>();
    const auto* harvest=ecs.resourceConst<tw::ForestHarvestResource>();
    const bool active=tick!=UINT32_MAX&&roots&&bindings&&wood&&surface.tab&&roots->rooted.size()==bindings->bindings.size()&&
        roots->rooted.size()<=ev::kMaxPlanetRootBindings&&bindings->events.size()<=ev::kMaxPlanetPlantFallEvents&&
        (!physics||physics->bodies.size()<=wp::kMaxConsequences)&&ev::validPlanetDeadwood(*wood,surface);
    out.set("active",Json::fromBool(active));if(!active)return out;
    out.set("tick",Json::number(int64_t(tick)));
    out.set("focusBody",Json::string(focusBody));
    const auto xyz=[](Vec3 v){Json a=Json::array();for(auto c:{v.x,v.y,v.z})a.push(Json::number(c.toDouble(),4));return a;};
    std::map<tw::AnchoredPlantId,const ev::PlanetRootBinding*> mapped;
    for(const auto& b:bindings->bindings)mapped.emplace(b.id,&b);
    std::set<tw::AnchoredPlantId> dead;
    if(history)for(const auto& h:history->plants)if(h.dead)dead.insert(h.id);
    std::map<wp::ConsequenceKey,const wp::ConsequenceBody*> bodies;
    if(physics)for(const auto& body:physics->bodies)bodies.emplace(body.key,&body);
    std::map<tw::AnchoredPlantId,const ev::PlanetPlantFallRecord*> current;
    size_t pending=0;
    for(const auto& r:bindings->events){
        if(!r.acknowledged)++pending;
        const auto id=r.fall.event.id;const auto b=mapped.find(id);
        if(b==mapped.end()||r.fall.bodyId!=b->second->bodyId||r.fall.plantedPosition!=b->second->plantedPosition)continue;
        const auto old=current.find(id);
        if(old==current.end()||ev::planetFallKey(old->second->fall)<ev::planetFallKey(r.fall))current[id]=&r;
    }
    const bool localWindAllowed=ecs.countExisting<physis::WindZone>()<=1024&&ecs.countExisting<physis::WindShelter>()<=1024;
    out.set("windZonesCapped",Json::fromBool(!localWindAllowed));
    const bool stormsAllowed=ecs.countExisting<physis::StormEvent>()<=1024;
    out.set("stormsCapped",Json::fromBool(!stormsAllowed));
    const bool cameraRank=camera&&camera->valid()&&camera->body==focusBody&&!focusBody.empty();
    out.set("cameraPrioritized",Json::fromBool(cameraRank));
    // Rank cheap immutable candidates first; weather and JSON rows are sampled
    // only after the cap. Keep global summary counters independent of ranking.
    struct RankedPlant {
        const tw::AnchoredPlant* plant=nullptr;
        int group=99;
        float distance=0,targetDistance=0;
    };
    std::vector<RankedPlant> ranked;ranked.reserve(roots->rooted.size());
    size_t nearView=0;
    const auto cameraClip=cameraRank?camera->viewProjection():render::mat4{};
    for(const auto& plant:roots->rooted){
        RankedPlant entry;entry.plant=&plant;
        if(cameraRank){
            const tw::AnchoredPlantId id{plant.anchor.speciesId,plant.anchor.plantKey};
            const auto binding=mapped.find(id);
            if(binding!=mapped.end()&&binding->second->bodyId==focusBody){
                const auto frame=wp::surfaceFrame(&surface,binding->second->plantedPosition);
                if(frame&&surface.bodies[size_t(frame->body)].id==focusBody){
                    const auto anchor=game::Surface::anchor(frame->body);
                    Vec3 point=binding->second->plantedPosition;
                    const auto receipt=current.find(id);
                    const auto body=receipt!=current.end()&&receipt->second->acknowledged?
                        bodies.find(wp::consequenceKey(receipt->second->fall)):bodies.end();
                    bool physical=false;
                    if(body!=bodies.end()&&body->second->body==focusBody){
                        const auto& record=*body->second;
                        const auto motionFrame=wp::surfaceFrame(&surface,record.motion.position);
                        if(record.shape==wp::ConsequenceShape::Capsule&&wp::validConsequence(record)&&motionFrame&&
                           motionFrame->body==frame->body&&record.key.tick<=tick&&
                           (record.lastMotionTick==UINT32_MAX||record.lastMotionTick<=tick)){
                            point=record.motion.position;physical=true;
                        }
                    }
                    const auto local=point-anchor;
                    const render::float3 p{float(local.x.toDouble()),float(local.y.toDouble()),float(local.z.toDouble())};
                    const auto [group,distance,targetDistance]=camera->rank(p,float(surface.bodies[size_t(frame->body)].radius.toDouble()),cameraClip);
                    const auto log=std::lower_bound(wood->ledger.logs.begin(),wood->ledger.logs.end(),id,
                        [](const auto& r,auto key){return r.id<key;});
                    const bool retired=(log!=wood->ledger.logs.end()&&log->id==id&&!log->remainingUnits)||
                        (!physical&&(plant.fallen||(harvest&&tw::isForestHarvested(*harvest,id))));
                    entry.group=group+(retired?3:0);entry.distance=distance;entry.targetDistance=targetDistance;
                    if(group==0&&!retired)++nearView;
                }
            }
        }
        ranked.push_back(entry);
    }
    if(cameraRank)std::sort(ranked.begin(),ranked.end(),[](const RankedPlant& a,const RankedPlant& b){
        const auto& x=a.plant->anchor;const auto& y=b.plant->anchor;
        return std::tie(a.group,a.distance,a.targetDistance,x.speciesId,x.plantKey)<
               std::tie(b.group,b.distance,b.targetDistance,y.speciesId,y.plantKey);
    });
    if(cameraRank)out.set("viewCandidates",Json::number(int64_t(nearView)));
    size_t fallen=0,spent=0,physical=0,rejected=0,eligible=0;
    for(const auto& rankedPlant:ranked){
        const auto& plant=*rankedPlant.plant;
        const tw::AnchoredPlantId id{plant.anchor.speciesId,plant.anchor.plantKey};
        const auto b=mapped.find(id);if(b==mapped.end()){++rejected;continue;}
        const auto frame=wp::surfaceFrame(&surface,b->second->plantedPosition);
        if(!frame||surface.bodies[size_t(frame->body)].id!=b->second->bodyId){++rejected;continue;}
        const auto log=std::lower_bound(wood->ledger.logs.begin(),wood->ledger.logs.end(),id,
            [](const auto& r,auto key){return r.id<key;});
        const bool hasWood=log!=wood->ledger.logs.end()&&log->id==id;
        const bool isSpent=hasWood&&!log->remainingUnits;
        const auto receipt=current.find(id);
        const auto body=receipt!=current.end()&&receipt->second->acknowledged?
            bodies.find(wp::consequenceKey(receipt->second->fall)):bodies.end();
        const bool hasBody=body!=bodies.end()&&body->second->body==b->second->bodyId;
        fallen+=plant.fallen;spent+=isSpent;physical+=hasBody;
        if(!focusBody.empty()&&b->second->bodyId!=focusBody)continue;
        ++eligible;
        if(rows.size()>=limit)continue;
        Json row=Json::object();
        row.set("species",Json::number(int64_t(id.speciesId)));row.set("key",Json::string(std::to_string(id.key)));
        row.set("body",Json::string(b->second->bodyId));row.set("cell",Json::number(int64_t(frame->cell)));
        if(cameraRank)row.set("viewPriority",Json::number(int64_t(rankedPlant.group)));
        const auto anchor=game::Surface::anchor(frame->body);
        row.set("position",xyz(b->second->plantedPosition-anchor));
        row.set("up",xyz(frame->up));row.set("east",xyz(frame->east));
        try{studioForestWeatherSample(row,ecs,surface,*frame,b->second->plantedPosition,weather,localWindAllowed,stormsAllowed);}catch(const std::exception&){++rejected;}
        row.set("habit",Json::string(plant.anchor.roots.habit==tw::PlantHabit::Shrub?"shrub":plant.anchor.roots.habit==tw::PlantHabit::Tree?"tree":"grass"));
        try{
            const auto appearance=tw::plantAppearance(plant.environment);
            row.set("sizePermille",Json::number(int64_t(appearance.sizePermille)));
            row.set("foliagePermille",Json::number(int64_t(dead.contains(id)?0:appearance.foliagePermille)));
            row.set("exposedCm",Json::number(int64_t(plant.support.exposedCm)));
            row.set("visibleRoots",Json::fromBool(plant.support.hasVisibleRoots));
        }catch(const std::exception&){++rejected;}
        row.set("rootRadiusCm",Json::number(int64_t(plant.anchor.roots.radiusCm)));
        row.set("rootDepthCm",Json::number(int64_t(plant.anchor.roots.depthCm)));
        row.set("ageTicks",Json::string(std::to_string(plant.environment.ageTicks)));
        row.set("supportPermille",Json::number(int64_t(plant.support.supportPermille)));
        row.set("dead",Json::fromBool(dead.contains(id)));row.set("fallen",Json::fromBool(plant.fallen));
        row.set("harvested",Json::fromBool(harvest&&tw::isForestHarvested(*harvest,id)));
        row.set("spent",Json::fromBool(isSpent));row.set("physical",Json::fromBool(hasBody));
        row.set("remainingUnits",hasWood?Json::string(std::to_string(log->remainingUnits)):Json{});
        row.set("initialUnits",hasWood?Json::string(std::to_string(log->initialUnits)):Json{});
        if(receipt!=current.end())row.set("pending",Json::fromBool(!receipt->second->acknowledged));
        if(hasBody){
            row.set("motionTick",body->second->lastMotionTick==UINT32_MAX?Json{}:Json::number(int64_t(body->second->lastMotionTick)));
            row.set("awake",Json::fromBool(body->second->motion.awake));
            const auto& record=*body->second;
            const auto motionFrame=wp::surfaceFrame(&surface,record.motion.position);
            if(record.shape==wp::ConsequenceShape::Capsule&&wp::validConsequence(record)&&motionFrame&&
               motionFrame->body==frame->body&&record.key.tick<=tick&&
               (record.lastMotionTick==UINT32_MAX||record.lastMotionTick<=tick)){
                const auto& q=record.motion.rotation;
                // Rotate the native capsule local Y axis by its published quaternion.
                const Vec3 axis{(q.x*q.y-q.w*q.z)*2,1_fx-(q.x*q.x+q.z*q.z)*2,(q.y*q.z+q.w*q.x)*2};
                Json capsule=Json::object();capsule.set("center",xyz(record.motion.position-anchor));
                const Vec3 right{1_fx-(q.y*q.y+q.z*q.z)*2,(q.x*q.y+q.w*q.z)*2,(q.x*q.z-q.w*q.y)*2};
                capsule.set("right",xyz(right));capsule.set("axis",xyz(axis));capsule.set("radius",Json::number(record.dimensions.x.toDouble(),4));
                capsule.set("halfHeight",Json::number(record.dimensions.y.toDouble(),4));
                row.set("capsule",std::move(capsule));
            }
        }
        rows.push(std::move(row));
    }
    out.set("plantCount",Json::number(int64_t(roots->rooted.size())));
    out.set("deadCount",Json::number(int64_t(dead.size())));
    out.set("fallenCount",Json::number(int64_t(fallen)));out.set("spentCount",Json::number(int64_t(spent)));
    out.set("physicalCount",Json::number(int64_t(physical)));out.set("pendingCount",Json::number(int64_t(pending)));
    out.set("rejected",Json::number(int64_t(rejected)));
    out.set("focusPlantCount",Json::number(int64_t(eligible)));
    out.set("truncated",Json::fromBool(eligible>rows.size()));
    out.set("plants",std::move(rows));return out;
}
} // namespace aiverse::studio
