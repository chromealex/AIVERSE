// Read-only diagnostic geometry for authored planetary forest; no runtime ECS mutation.
#pragma once
#include "apps/studio-native/StudioPlanetProcessOverlays.h"
#include "apps/studio-native/StudioPlanetForestDocument.h"

namespace aiverse::studio {
struct PlanetForestOverlays {
    std::vector<PlanetProcessOverlayLine> lines;
    size_t plants=0,missingProfile=0,rejected=0;
    bool invalid=false,truncated=false;
};
inline Json studioForestOverlayRecipe(const editor::MapDoc& doc,const Json& rules){
    if(doc.data.worldExtra.has("planetForest"))return doc.data.worldExtra["planetForest"];
    const auto* module=planetForestModuleConfig(rules);
    return module?(*module)["planetForest"]:Json{};
}
inline PlanetForestOverlays buildPlanetForestOverlays(const editor::MapDoc& doc,
        const Json& rules,int bi,const StudioPlanetPreview& preview){
    namespace tw=terraweave;namespace wp=lib::worldprocess;
    PlanetForestOverlays out;
    if(bi<0||size_t(bi)>=doc.data.bodies.size())return out;
    const auto& body=doc.data.bodies[size_t(bi)];
    if(body.id!=preview.bodyId||body.faceSize!=preview.faceSize||preview.radius<=0)return out;
    const auto forest=studioForestOverlayRecipe(doc,rules);
    if(forest.isNull())return out;
    try{
        const auto cfg=wp::parsePlanetForestConfig(forest,doc.toJson());
        const auto scaleIt=cfg.floraScales.find(body.id);
        if(scaleIt==cfg.floraScales.end())throw std::invalid_argument("Missing flora scale");
        const auto& catalog=doc.data.worldExtra["terraweaveForest"];
        std::map<uint32_t,tw::TreeSkeleton> trees;
        if(!catalog.isNull()){
            if(!catalog["species"].isArray()||catalog["species"].size()>64)
                throw std::invalid_argument("Invalid forest catalog");
            for(const auto& def:catalog["species"].items()){
                const auto profile=world::parseForestSpeciesProfile(def);
                if(!trees.emplace(profile.profile.speciesId,tw::generateTree(profile.profile,profile.seed,tw::TreeLOD::High)).second)
                    throw std::invalid_argument("Duplicate forest profile");
            }
        }
        std::map<uint32_t,tw::ForestEcologySpecies> species;
        for(const auto& policy:cfg.species)species.emplace(policy.speciesId,policy);
        std::map<std::string,const editor::DocEntity*> markers;
        for(const auto& entity:doc.data.entities)if(!entity.id.empty())markers.emplace(entity.id,&entity);
        game::CubeSphere cube(body.faceSize);
        constexpr size_t lineBudget=8192;
        constexpr float tau=6.28318530718f;
        const auto add=[&](render::float3 a,render::float3 b,uint32_t color){
            if(out.lines.size()>=lineBudget){out.truncated=true;return;}
            out.lines.push_back({a,b,color,{}});
        };
        const auto ring=[&](render::float3 center,render::float3 a,render::float3 b,float radius,uint32_t color){
            auto p=center+a*radius;
            for(int i=1;i<=12;++i){
                const float t=tau*float(i)/12.f;
                const auto q=center+(a*std::cos(t)+b*std::sin(t))*radius;
                add(p,q,color);p=q;
            }
        };
        for(const auto& item:forest["plants"].items()){
            if(out.truncated)break;
            const Json* at=&item["at"];
            if(!item.has("at")){
                const auto marker=markers.find(item.getStr("entity"));
                if(marker==markers.end()){++out.rejected;continue;}
                at=&marker->second->at;
            }else if(item.has("entity")){++out.rejected;continue;}
            if(at->getStr("body")!=body.id)continue;
            const auto face=(*at)["face"].integer(-1);
            const auto x=(*at)["x"].toDouble(NAN),y=(*at)["y"].toDouble(NAN);
            if(!(*at)["face"].isNumber()||face<0||face>5||(*at)["face"].fx()!=Fixed::fromInt(face)||
               !std::isfinite(x)||!std::isfinite(y)||x<0||y<0||x>body.faceSize||y>body.faceSize){++out.rejected;continue;}
            const auto id=uint32_t(wp::planetBankInteger(item,"species",0,1,1000000));
            const auto policy=species.find(id);
            if(policy==species.end()){++out.rejected;continue;}
            const auto habit=item.getStr("habit","tree");
            if(habit!="tree"&&habit!="shrub"){++out.rejected;continue;}
            tw::AnchoredPlant plant;
            plant.anchor.speciesId=id;
            plant.anchor.roots=tw::defaultRootProfile(habit=="tree"?tw::PlantHabit::Tree:tw::PlantHabit::Shrub);
            plant.environment.ageTicks=uint64_t(wp::planetBankInteger(item,"ageTicks",int64_t(policy->second.matureTicks),0,1000000000));
            plant.environment.matureTicks=policy->second.matureTicks;
            plant.environment.lifespanTicks=policy->second.lifespanTicks;
            plant.environment.nutrientsPermille=uint32_t(wp::planetBankInteger(forest,"baselineNutrients",500,0,1000));
            const auto direction=cube.dirFromFace(int(face),(*at)["x"].fx(),(*at)["y"].fx());
            const auto up=render::normalize(StudioPlanetPreview::f3(direction));
            int canonical;Fixed ca,cb;cube.faceFromDir(direction,canonical,ca,cb);
            Vec3 east,south;game::CubeSphere::faceTangents(canonical,direction,east,south);
            const auto right=render::normalize(StudioPlanetPreview::f3(east));
            const auto forward=render::normalize(render::cross(right,up));
            const auto ground=planet_overlay_detail::surfacePoint(body,cube,direction,preview.radius,0);
            // Small lift only for the root ring, to keep its footprint visible.
            const auto lifted=ground+up*.025f;
            const float size=float(scaleIt->second.toDouble())*
                float(tw::plantAppearance(plant.environment).sizePermille)/1000.f*(habit=="shrub"?.25f:1.f);
            const auto tree=trees.find(id);
            const auto roots=plant.anchor.roots;
            const auto rootColor=render::rgba(225,156,82);
            // Project the root footprint through the same tangent chart used by
            // planetary support; sample authored terrain across cube seams.
            std::optional<render::float3> previous;
            const auto radius=StudioPlanetPreview::fixed(preview.radius);
            for(int j=0;j<=12;++j){
                const float t=tau*float(j)/12.f;
                const auto a=StudioPlanetPreview::fixed(float(roots.radiusCm)*.01f*std::cos(t));
                const auto b=StudioPlanetPreview::fixed(float(roots.radiusCm)*.01f*std::sin(t));
                const auto dir=planet_overlay_detail::regionDirection(direction,east,south,radius,a,b);
                if(!dir){previous.reset();continue;}
                const auto p=planet_overlay_detail::surfacePoint(body,cube,*dir,preview.radius,.025f);
                if(previous)add(*previous,p,rootColor);
                previous=p;
            }
            add(ground,ground-up*(float(roots.depthCm)*.01f),rootColor);
            if(tree!=trees.end()){
                const auto pos=[&](int32_t a,int32_t b,int32_t c){
                    return ground+(right*float(a)+up*float(b)+forward*float(c))*(size*.01f);
                };
                for(const auto& branch:tree->second.branches){
                    add(pos(branch.x0,branch.y0,branch.z0),pos(branch.x1,branch.y1,branch.z1),
                        branch.parent<0?render::rgba(180,125,76):render::rgba(114,211,123));
                    if(out.truncated)break;
                }
            }else{
                ++out.missingProfile;
                // An explicit missing-profile marker, not an invented tree.
                add(lifted-right*.15f,lifted+right*.15f,render::rgba(255,210,100));
                add(lifted-forward*.15f,lifted+forward*.15f,render::rgba(255,210,100));
            }
            if(const auto material=cfg.naturalFallBodies.find(id);material!=cfg.naturalFallBodies.end()){
                std::optional<wp::PlanetForestCapsuleRecipe> shape=material->second;
                if(const auto trunk=cfg.arborTrunks.find(id);trunk!=cfg.arborTrunks.end())
                    shape=wp::derivePlanetArborCapsule(trunk->second,plant,scaleIt->second,1_fx,*shape);
                if(shape){
                    // Upright recipe diagram only; not a live collider or fallen pose.
                    const float r=float(shape->radius.toDouble()),half=float(shape->halfHeight.toDouble());
                    const auto bottom=ground+up*r,top=bottom+up*(2.f*half);
                    const auto color=render::rgba(103,187,255);
                    ring(bottom,right,forward,r,color);ring(top,right,forward,r,color);
                    for(const auto axis:{right,forward}){
                        add(bottom+axis*r,top+axis*r,color);add(bottom-axis*r,top-axis*r,color);
                        for(int cap=0;cap<2;++cap){
                            const auto center=cap?top:bottom;
                            const auto vertical=up*(cap?1.f:-1.f);
                            auto prev=center+axis*r;
                            for(int j=1;j<=8;++j){
                                const float t=3.14159265359f*float(j)/8.f;
                                const auto next=center+(axis*std::cos(t)+vertical*std::sin(t))*r;
                                add(prev,next,color);prev=next;
                            }
                        }
                    }
                }else ++out.rejected;
            }
            ++out.plants;
        }
    }catch(const std::exception&){out.invalid=true;out.lines.clear();out.plants=0;}
    return out;
}
} // namespace aiverse::studio
