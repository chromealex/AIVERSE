// Planetary PHYSIS authored field handles. No ECS mutation or new field owner.
#pragma once
#include "apps/studio-native/StudioPlanetProcessOverlays.h"

namespace aiverse::studio {
struct PlanetGravityHandle {
    std::string id,body;
    int bodyIndex=-1;
    render::float3 center{}; // body-local preview space, metres
    float radius=0;
};
inline bool validPlanetGravityCenter(render::float3 p){
    // The canonical simulation anchor spacing is 4096. Do not move the
    // field center into another body's coordinate bucket.
    constexpr float limit=float(game::Surface::kBodySpacing)/2.f;
    return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z)&&
        std::abs(p.x)<limit&&std::abs(p.y)<limit&&std::abs(p.z)<limit;
}
inline std::vector<PlanetGravityHandle> planetGravityHandles(
        const editor::MapDoc& doc,const StudioPlanetPreview& preview){
    std::vector<PlanetGravityHandle> out;
    const int bi=doc.data.findBody(preview.bodyId);
    if(bi<0||bi>=game::Surface::kMaxBodies)return out;
    const auto& config=editor::physis_authoring::read(doc);
    if(!config.isObject()||!config.getBool("enabled",true)||!config.getBool("enableGravityFields",false))return out;
    try{
        physis::InstallOptions options;options.enableGravityFields=true;
        options.enableStorms=config.getBool("enableStorms",false);
        options.enableWindZones=config.getBool("enableWindZones",false);
        const auto fields=physis::parseFieldAssets(config,options);
        const auto anchor=game::Surface::anchor(bi);
        for(const auto& asset:fields){
            if(out.size()>=64)break;
            if(asset.kind!=physis::AuthoredFieldKind::Gravity)continue;
            const auto& field=std::get<physis::GravityField>(asset.data);
            if(!field.enabled||field.radiusMeters.raw<=0)continue; // global uniform: numeric-only
            const auto center=StudioPlanetPreview::f3(field.center-anchor);
            if(!validPlanetGravityCenter(center))continue;
            out.push_back({asset.id,preview.bodyId,bi,center,float(field.radiusMeters.toDouble())});
        }
    }catch(const std::exception&){out.clear();}
    return out;
}
// Existing PHYSIS full-config validation and revision-guarded physis_set
// transaction remain authoritative. The Scene only prepares a new candidate.
inline std::optional<Json> planetGravityEditPayload(const editor::MapDoc& doc,
        const PlanetGravityHandle& original,render::float3 center,float radius,uint64_t revision){
    if(doc.revision!=revision||doc.data.findBody(original.body)!=original.bodyIndex||
       original.bodyIndex<0||original.bodyIndex>=game::Surface::kMaxBodies||
       !validPlanetGravityCenter(center)||!std::isfinite(radius)||radius<.001f||radius>100000.f)return {};
    Json config=editor::physis_authoring::read(doc);
    std::string collection;size_t index=0;Json entry;
    if(!editor::physis_authoring::findField(config,original.id,collection,index,&entry)||
       collection!="gravityFields"||!entry.getBool("enabled",true))return {};
    if(entry["radiusMeters"].toDouble()<=0)return {};
    const auto anchor=game::Surface::anchor(original.bodyIndex);
    if(center.x!=original.center.x||center.y!=original.center.y||center.z!=original.center.z)
        entry.set("center",editor::physis_authoring::point(
            anchor.x.toDouble()+center.x,anchor.y.toDouble()+center.y,anchor.z.toDouble()+center.z));
    if(radius!=original.radius)entry.set("radiusMeters",Json::number(double(radius),3));
    Json fields=config[collection];
    if(fields[index]==entry)return {};
    fields.at(index)=std::move(entry);config.set(collection,std::move(fields));
    if(!editor::physis_authoring::validate(config))return {};
    Json input=Json::object();input.set("config",std::move(config));
    input.set("expectedRevision",Json::number(int64_t(revision)));
    Json request=Json::object();request.set("op",Json::string("physis_set"));request.set("params",std::move(input));
    return request;
}
} // namespace aiverse::studio
