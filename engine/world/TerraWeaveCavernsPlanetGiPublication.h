#pragma once
// Stage56: native planetary cave GI consumer. Uses the existing scene/BVH
// builder and the real CPU/GPU bridge; owns no probe, ray or ECS simulation.
// A shared GI scene may also contain terrain, buildings and other producers.
#include "engine/world/TerraWeaveCavernsPlanetRuntimeFrame.h"
#include "engine/render/GiGeometryScene.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <type_traits>
#include <utility>
#include <vector>

namespace aiverse::terraweave::caverns {
struct CavePlanetGiBindings {
    // Return the EXISTING baseline GI chunk ID for a physical planet leaf.
    // The host must exclude other writers to the leased leaves until retire.
    std::function<std::optional<uint64_t>(const std::string&,const CavePlanetSurfaceSlotKey&)> surfaceId;
    // Globally reserved ID for this body/region/volume slot (no hash alias).
    std::function<std::optional<uint64_t>(const std::string&,uint64_t,ChunkKey3)> volumeId;
    // The SAME body-relative -> GI-space transform used by the baseline
    // planet owner. It must remain fixed for a resident cohort.
    std::function<render::float3(const std::string&,Vec3)> project;
    // Select actual diffuse material for each new cut/volume triangle.
    // This bridge must not fabricate a new material system.
    std::function<render::float3(const std::string&,const CavePlanetSlotId&,const world::Triangle&)> albedo;
    // Host proves existing GI target can mutate (resource fences, no pass).
    std::function<bool()> ready;
    bool valid()const noexcept{return surfaceId&&volumeId&&project&&albedo&&ready;}
};

template<class Target>
class CavePlanetGiPublication {
    using Geometry=render::StaticShadowGeometry;
    using GeometryPtr=std::shared_ptr<const Geometry>;
    using Snapshot=render::GiChunkedGeometrySnapshot;
    using Triangle=Geometry::Triangle;
    using Ids=std::map<uint64_t,GeometryPtr>;
    struct Cohort {
        uint64_t revision=0;
        Ids originals; // retain real uncut leaves including material IDs
        Ids expected;  // committed chunk identity, null = absent
        std::set<uint64_t> volumes;
    };
    struct Pending {
        std::string body;
        uint64_t revision=0;
        bool retiring=false;
        std::shared_ptr<const Snapshot> base;
        render::GiGeometryScene scene;
        std::map<std::string,Cohort> next;
    };
public:
    CavePlanetGiPublication(render::GiGeometryScene& scene,Target& target,CavePlanetGiBindings bindings)
        :scene_(scene),target_(target),bindings_(std::move(bindings)){
        if(!bindings_.valid())throw std::invalid_argument("Caverns GI host bindings");
    }
    CavePlanetGiPublication(const CavePlanetGiPublication&)=delete;
    CavePlanetGiPublication& operator=(const CavePlanetGiPublication&)=delete;
    size_t activeBodies()const noexcept{return active_.size();}
    bool staged()const noexcept{return pending_.has_value();}
    uint64_t revision(const std::string& body)const{
        const auto i=active_.find(body);return i==active_.end()?0:i->second.revision;
    }

    bool prepare(const CavePlanetRuntimeCandidate& candidate){
        if(pending_||!candidate.cut||!candidate.nativeRevision||!canStage())return false;
        try{
            const auto& body=candidate.cut->bodyId;
            if(candidate.nativeRevision<=revision(body))return false;
            const auto slots=collectPlanetBodyCutSlots(*candidate.cut);
            const auto baseline=scene_.currentShared();
            auto next=active_;
            auto& cohort=next[body];
            if(!matches(cohort,*baseline))return false;
            const auto previousVolumes=cohort.volumes;
            const auto previousOriginals=cohort.originals;
            cohort.expected.clear();cohort.volumes.clear();cohort.originals.clear();
            auto prospective=scene_; // immutable existing BVH pointers are shared
            std::set<uint64_t> used;
            for(const auto& [key,ref]:slots){
                const auto id=bindings_.surfaceId(body,key);
                if(!id||!used.insert(*id).second||ownedElsewhere(body,*id)||previousVolumes.contains(*id))return false;
                const auto old=previousOriginals.find(*id);
                GeometryPtr original=old==previousOriginals.end()?chunk(*baseline,*id):old->second;
                if(!original||!matchesOriginal(body,ref.slot->originalBody,*original))return false;
                cohort.originals.emplace(*id,std::move(original));
                cohort.expected.emplace(*id,GeometryPtr{});
                CavePlanetSlotId slot;slot.kind=CavePlanetSlotKind::Surface;slot.surface=key;slot.region=ref.region;
                replaceTriangles(prospective,*id,body,slot,ref.slot->bodyTriangles,{});
            }
            // A changed surface leaf set must first retire through the native
            // baseline barrier. Never abandon a previously replaced leaf.
            if(!previousOriginals.empty()){
                if(previousOriginals.size()!=cohort.originals.size())return false;
                for(const auto& [id,old]:previousOriginals){(void)old;if(!cohort.originals.contains(id))return false;}
            }
            size_t count=0;
            for(const auto& volume:candidate.volume){
                if(volume.id.kind!=CavePlanetSlotKind::Volume||volume.payload.body<0||
                   volume.payload.anchor.bodyId!=body||
                   volume.payload.collision.revision!=candidate.nativeRevision||
                   !candidate.cut->regions.contains(volume.id.region)||
                   volume.payload.collision.triangles.size()>262144-count)return false;
                count+=volume.payload.collision.triangles.size();
                const auto id=bindings_.volumeId(body,volume.id.region,volume.id.volume);
                if(!id||!used.insert(*id).second||ownedElsewhere(body,*id)||
                   previousOriginals.contains(*id)||
                   (!previousVolumes.contains(*id)&&baseline->findChunk(*id)))return false;
                cohort.volumes.insert(*id);cohort.expected.emplace(*id,GeometryPtr{});
                // Collision payload is simulation-anchor space; the GI
                // projector expects body space (subtract the anchor once).
                replaceTriangles(prospective,*id,body,volume.id,volume.payload.collision.triangles,
                    game::Surface::anchor(volume.payload.body));
            }
            for(auto id:previousVolumes)if(!cohort.volumes.contains(id))prospective.removeChunk(id);
            cohort.revision=candidate.nativeRevision;
            pending_.emplace(Pending{body,candidate.nativeRevision,false,baseline,std::move(prospective),std::move(next)});
            return true;
        }catch(const std::exception&){return false;}
    }

    bool prepareRetire(const std::string& body,uint64_t rev){
        if(pending_)return false;
        const auto it=active_.find(body);
        if(it==active_.end())return rev==0;
        if(!canStage()||it->second.revision!=rev)return false;
        try{
            const auto base=scene_.currentShared();
            if(!matches(it->second,*base))return false;
            auto prospective=scene_;
            for(auto id:it->second.volumes)prospective.removeChunk(id);
            for(const auto& [id,original]:it->second.originals)
                prospective.replaceChunk(id,original->triangles,original->albedos);
            auto next=active_;next.erase(body);
            pending_.emplace(Pending{body,rev,true,base,std::move(prospective),std::move(next)});
            return true;
        }catch(const std::exception&){return false;}
    }
    bool commit(const std::string& body,uint64_t rev){
        return publish(body,rev,false);
    }
    bool retire(const std::string& body,uint64_t rev){
        if(!pending_&&!active_.contains(body))return rev==0;
        return publish(body,rev,true);
    }
    bool abort(const std::string& body,uint64_t rev){
        if(!pending_)return true;
        if(pending_->body!=body||pending_->revision!=rev)return false;
        pending_.reset();return true; // live scene never touched by prepare
    }
    // Use managed canonical navigation leases; external nav owners need
    // their own explicit composition. Never silently replace callbacks.
    bool bind(CavePlanetRuntimeHooks& hooks){
        if(!hooks.navigationInputs||hooks.prepareNavigationGi||hooks.commitNavigationGi||
           hooks.abortNavigationGi||hooks.retireNavigationGi||hooks.prepareRetireNavigationGi)return false;
        hooks.prepareNavigationGi=[this](const auto& c){return prepare(c);};
        hooks.commitNavigationGi=[this](const auto& b,uint64_t r){return commit(b,r);};
        hooks.abortNavigationGi=[this](const auto& b,uint64_t r){return abort(b,r);};
        hooks.retireNavigationGi=[this](const auto& b,uint64_t r){return retire(b,r);};
        hooks.prepareRetireNavigationGi=[this](const auto& b,uint64_t r){return prepareRetire(b,r);};
        return true;
    }
private:
    bool canStage()const{return scene_.current()&&!scene_.hasPendingChanges()&&bindings_.ready();}
    static GeometryPtr chunk(const Snapshot& source,uint64_t id){
        GeometryPtr out;
        source.forEachChunkShared([&](uint64_t key,auto,const auto& value){if(key==id)out=value;});
        return out;
    }
    static bool matches(const Cohort& c,const Snapshot& source){
        for(const auto& [id,expected]:c.expected)if(source.findChunk(id)!=expected.get())return false;
        return true;
    }
    bool ownedElsewhere(const std::string& body,uint64_t id)const{
        for(const auto& [key,c]:active_)if(key!=body&&c.expected.contains(id))return true;
        return false;
    }
    using TriangleKey=std::array<float,9>;
    static TriangleKey key(const Triangle& t){
        return {t.a.x,t.a.y,t.a.z,t.b.x,t.b.y,t.b.z,t.c.x,t.c.y,t.c.z};
    }
    Triangle convert(const std::string& body,const world::Triangle& t,Vec3 offset)const{
        Triangle out{bindings_.project(body,t.a-offset),bindings_.project(body,t.b-offset),bindings_.project(body,t.c-offset),0};
        for(float v:key(out))if(!std::isfinite(v))throw std::invalid_argument("Caverns GI projection");
        const auto n=render::cross(out.b-out.a,out.c-out.a);
        const auto area=render::dot(n,n);
        if(!std::isfinite(area)||area<=0)throw std::invalid_argument("Caverns GI projection lost triangle");
        return out;
    }
    // GI BVH construction reorders triangles. Compare sorted geometry,
    // not BVH traversal order; preserve original winding and duplicates.
    bool matchesOriginal(const std::string& body,const std::vector<world::Triangle>& source,const Geometry& current)const{
        if(source.size()!=current.triangles.size())return false;
        std::vector<TriangleKey> a,b;a.reserve(source.size());b.reserve(source.size());
        for(const auto& t:source)a.push_back(key(convert(body,t,{})));
        for(const auto& t:current.triangles)b.push_back(key(t));
        std::sort(a.begin(),a.end());std::sort(b.begin(),b.end());return a==b;
    }
    void replaceTriangles(render::GiGeometryScene& dest,uint64_t id,const std::string& body,
            const CavePlanetSlotId& slot,const std::vector<world::Triangle>& source,Vec3 offset)const{
        std::vector<Triangle> triangles;std::vector<render::float3> colors;
        triangles.reserve(source.size());colors.reserve(source.size());
        for(const auto& t:source){
            auto p=convert(body,t,offset);
            p.surface=uint32_t(colors.size());
            colors.push_back(bindings_.albedo(body,slot,{t.a-offset,t.b-offset,t.c-offset}));
            triangles.push_back(p);
        }
        dest.replaceChunk(id,std::move(triangles),std::move(colors));
    }
    bool publish(const std::string& body,uint64_t rev,bool retiring){
        if(!pending_||pending_->body!=body||pending_->revision!=rev||
           pending_->retiring!=retiring||!canStage()||scene_.currentShared()!=pending_->base)return false;
        auto& p=*pending_;
        if(scene_.revision()==UINT64_MAX)return false;
        // The real target may still refuse due to GPU capacity/fences.
        // Native owner quarantines if that occurs after irreversible physics.
        const auto stats=p.scene.publish(target_);
        if(!stats.published&&p.scene.hasPendingChanges())return false;
        if(!p.scene.current())return false;
        // No allocation after target publication. Capture exact immutable
        // identities in the already allocated map before swapping state.
        if(!retiring){
            auto& owned=p.next.at(body).expected;
            p.scene.current()->forEachChunkShared([&](uint64_t id,auto,const auto& value){
                const auto i=owned.find(id);if(i!=owned.end())i->second=value;
            });
        }
        static_assert(std::is_nothrow_move_assignable_v<render::GiGeometryScene>);
        scene_=std::move(p.scene);
        active_.swap(p.next);pending_.reset();return true;
    }
    render::GiGeometryScene& scene_;
    Target& target_;
    CavePlanetGiBindings bindings_;
    std::map<std::string,Cohort> active_;
    std::optional<Pending> pending_;
};
} // namespace aiverse::terraweave::caverns
