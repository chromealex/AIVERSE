#pragma once
#include "engine/world/TerraWeaveCavernsPlanetFrame.h"
#include "engine/world/TerraWeaveCavernsEcsAuthority.h"

namespace aiverse::terraweave::caverns {
// Opt-in additional resource; flat world save layout remains unchanged.
// The immutable chart is a recipe, not a transient camera transform.
inline void io(Archive& a,CavePlanetRegionResource& r){
    a(r.version);
    if(r.version!=1){a.fail();return;}
    auto& s=r.anchor.bodyId;
    if(a.writing()&&(s.empty()||s.size()>128)){a.fail();return;}
    const auto count=a.count(s.size());
    if(!a.ok()||!count||count>128||(!a.writing()&&count>a.remaining())){a.fail();return;}
    if(!a.writing())s.resize(count);
    a.block(s.data(),count);
    a(r.anchor.face,r.anchor.a,r.anchor.b,r.anchor.halfExtentCm);
}
inline bool sameCavePlanetAnchor(const CavePlanetAnchor& a,const CavePlanetAnchor& b){
    return a.bodyId==b.bodyId&&a.face==b.face&&a.a==b.a&&a.b==b.b&&a.halfExtentCm==b.halfExtentCm;
}
inline void validateCavePlanetVolume(const CavePlanetFrame& frame,const CaveVolumeSnapshot& snapshot){
    validateCavePlanetBounds(frame.anchor(),snapshot);
}
inline Json cavePlanetAnchorToJson(const CavePlanetAnchor& anchor){
    Json out=Json::object();
    out.set("version",Json::number(int64_t(1)));
    out.set("body",Json::string(anchor.bodyId));
    out.set("face",Json::number(int64_t(anchor.face)));
    out.set("aQ16",Json::number(anchor.a.raw));out.set("bQ16",Json::number(anchor.b.raw));
    out.set("halfExtentCm",Json::number(int64_t(anchor.halfExtentCm)));
    return out;
}
inline CavePlanetAnchor cavePlanetAnchorFromJson(const Json& doc){
    if(!doc.isObject()||doc.getInt("version",0)!=1||!doc["body"].isString())
        throw std::invalid_argument("Caverns planetary chart JSON");
    const auto f=doc.getInt("face",-1),a=doc.getInt("aQ16",-1),b=doc.getInt("bQ16",-1),extent=doc.getInt("halfExtentCm",0);
    if(f<0||f>5||a<0||b<0||a>INT32_MAX||b>INT32_MAX||extent<1||extent>100000)
        throw std::invalid_argument("Caverns planetary chart JSON extent");
    auto body=doc.getStr("body");
    if(body.empty()||body.size()>128)throw std::invalid_argument("Caverns planetary chart body");
    return {std::move(body),int32_t(f),Fixed::fromRaw(a),Fixed::fromRaw(b),int32_t(extent)};
}
// Register AFTER the ordinary cave authority, before registry seal.
// The supplied Surface is the actual immutable simulation recipe surface.
inline void registerCavePlanetRegion(Simulation& sim,std::shared_ptr<const game::Surface> surface,
        const CavePlanetAnchor& anchor,const CaveAuthorityConfig& cfg){
    const auto frame=CavePlanetFrame::make(surface,anchor);
    validateCavePlanetVolume(frame,*freezeCaveAuthority(sim.world(),cfg));
    sim.registerPersistentResource<CavePlanetRegionResource>("terraweave.caverns.planet_region",1);
    sim.addPersistentLoadValidator("terraweave.caverns.planet_region.recipe",
        [surface,anchor,cfg](const World& staged,std::string& error){
            try{
                const auto* r=staged.resourceConst<CavePlanetRegionResource>();
                if(!r||r->version!=1||!sameCavePlanetAnchor(r->anchor,anchor))
                    throw std::invalid_argument("Caverns restored planetary chart differs from recipe");
                validateCavePlanetVolume(CavePlanetFrame::make(surface,anchor),*freezeCaveAuthority(staged,cfg));
                return true;
            }catch(const std::exception& e){error=e.what();return false;}
        });
    sim.world().setResource<CavePlanetRegionResource>({1,anchor});
}
} // namespace aiverse::terraweave::caverns
