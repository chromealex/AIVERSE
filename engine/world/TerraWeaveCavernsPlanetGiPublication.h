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
using CavePlanetGiProject=std::function<render::float3(const std::string&,Vec3)>;
// Maps other producers' existing GI vertices into the new GI space.
// Identity is explicit for chunks whose coordinate frame did not change.
using CavePlanetGiOtherProject=std::function<render::float3(uint64_t,render::float3)>;
struct CavePlanetGiBindings {
    // Return the EXISTING baseline GI chunk ID for a physical planet leaf.
    // The host must exclude other writers to the leased leaves until retire.
    std::function<std::optional<uint64_t>(const std::string&,const CavePlanetSurfaceSlotKey&)> surfaceId;
    // Globally reserved ID for this body/region/volume slot (no hash alias).
    std::function<std::optional<uint64_t>(const std::string&,uint64_t,ChunkKey3)> volumeId;
    // The SAME body-relative -> GI-space transform used by the baseline
    // planet owner. Capture by value; change only via reproject().
    CavePlanetGiProject project;
    // Select actual diffuse material for each new cut/volume triangle.
    // This bridge must not fabricate a new material system.
    std::function<render::float3(const std::string&,const CavePlanetSlotId&,const world::Triangle&)> albedo;
    // Host proves existing GI target can mutate (resource fences, no pass).
    std::function<bool()> ready;
    // Optional real terrain baseline producer. Both callbacks must be
    // supplied together. Proof covers ID reservation AND absence of any
    // overlapping GI geometry (including coarse parent terrain chunks).
    std::function<bool(const std::string&,const CavePlanetSurfaceSlotKey&,uint64_t)> baselineUnowned;
    std::function<render::float3(const std::string&,const CavePlanetSurfaceSlotKey&,const world::Triangle&)> baselineAlbedo;
    bool providesBaseline()const noexcept{return baselineUnowned&&baselineAlbedo;}
    bool valid()const noexcept{return surfaceId&&volumeId&&project&&albedo&&ready&&
        bool(baselineUnowned)==bool(baselineAlbedo);}
};

template<class Target>
class CavePlanetGiPublication {
    using Geometry=render::StaticShadowGeometry;
    using GeometryPtr=std::shared_ptr<const Geometry>;
    using Snapshot=render::GiChunkedGeometrySnapshot;
    using Triangle=Geometry::Triangle;
    using Ids=std::map<uint64_t,GeometryPtr>;
    struct BodyTriangle {world::Triangle triangle;uint32_t surface=0;};
    struct BodyGeometry {
        std::vector<BodyTriangle> triangles;
        std::vector<render::float3> albedos;
    };
    using BodySources=std::map<uint64_t,BodyGeometry>;
    struct Cohort {
        uint64_t revision=0;
        BodySources originals; // exact uncut body geometry and material indices
        BodySources sources;   // exact committed cut/volume body geometry
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

    // Other scene writers may publish unrelated chunks but cannot replace
    // leased identities. Presentation must not bind a violated lease.
    bool current()const{
        if(!scene_.current()||scene_.hasPendingChanges())return false;
        for(const auto& [body,c]:active_){(void)body;if(!matches(c,*scene_.current()))return false;}
        return true;
    }

    // The owner supplies frozen, value-captured projections and calls this
    // under the same exclusive frame barrier used for normal publication.
    // No physics, SDF, navigation or authoritative simulation changes.
    // All unleased chunks are mapped by their actual host; no extra GI owner.
    bool reproject(CavePlanetGiProject nextProject,const CavePlanetGiOtherProject& other){
        if(!nextProject||!other||pending_||!canStage()||!current()||scene_.revision()==UINT64_MAX)return false;
        try{
            const auto base=scene_.currentShared();
            auto prospective=scene_;
            auto next=active_;
            std::set<uint64_t> leased;
            for(auto& [body,c]:next){
                for(const auto& [id,source]:c.sources){
                    if(!leased.insert(id).second||!c.expected.contains(id))return false;
                    replaceBody(prospective,id,body,source,nextProject);
                }
                if(c.sources.size()!=c.expected.size())return false;
                // Validate hidden baseline now, not during irreversible retire.
                for(const auto& [id,source]:c.originals){(void)id;(void)projectBody(body,source,nextProject);}
            }
            base->forEachChunkShared([&](uint64_t id,auto,const auto& geometry){
                if(leased.contains(id))return;
                auto triangles=geometry->triangles;
                bool changed=false;
                for(auto& t:triangles){
                    const auto before=key(t);
                    t.a=other(id,t.a);t.b=other(id,t.b);t.c=other(id,t.c);
                    validateTriangle(t);changed|=before!=key(t);
                }
                if(changed)prospective.replaceChunk(id,std::move(triangles),geometry->albedos);
            });
            if(!canStage()||scene_.currentShared()!=base||!current())return false;
            // A changed coordinate frame invalidates the complete GI probe
            // history, even when the union of dirty bounds is small.
            prospective.requestFullPublication();
            if(!prospective.publish(target_).published)return false;
            for(auto& [body,c]:next){
                (void)body;
                for(auto& [id,expected]:c.expected){(void)id;expected.reset();}
                prospective.current()->forEachChunkShared([&](uint64_t id,auto,const auto& value){
                    const auto i=c.expected.find(id);if(i!=c.expected.end())i->second=value;
                });
            }
            static_assert(std::is_nothrow_move_assignable_v<render::GiGeometryScene>);
            scene_=std::move(prospective);
            active_.swap(next);bindings_.project.swap(nextProject);
            return true;
        }catch(const std::exception&){return false;}
    }

    bool providesBaseline()const noexcept{return bindings_.providesBaseline();}
    // Publish actual uncut terrain leaves before native physics replacement.
    // These are ordinary host terrain chunks, not cave-owned leases; after
    // retirement the host remains responsible for their residency/eviction.
    // Existing chunks must match exactly; never overwrite another producer.
    bool ensureBaseline(const game::PlanetRenderer& planet,const CavePlanetBodyCutPlan& plan){
        if(!providesBaseline()||pending_||scene_.hasPendingChanges()||!bindings_.ready()||
           scene_.revision()==UINT64_MAX||planet.caveSurfaceRevision()!=plan.surfaceRevision)return false;
        try{
            const auto body=planet.world().find(plan.bodyId);
            if(body<0)return false;
            const auto slots=collectPlanetBodyCutSlots(plan);
            // An already active cohort has retained the exact original.
            // prepare() validates its geometry and lease before replacement.
            if(active_.contains(plan.bodyId))return current();
            const auto base=scene_.currentShared();
            std::vector<CavePlanetSurfaceSlotKey> keys;
            for(const auto& [key,ref]:slots){(void)ref;keys.push_back(key);}
            const auto captured=planet.captureCaveSurfaceSlots(body,keys);
            if(captured.size()!=keys.size())return false;
            auto prospective=scene_;
            std::set<uint64_t> used;
            for(const auto& leaf:captured){
                const auto found=slots.find(leaf.key);
                if(found==slots.end())return false;
                BodyGeometry source;
                source.triangles.reserve(leaf.bodyTriangles.size());
                for(const auto& triangle:leaf.bodyTriangles)source.triangles.push_back({triangle,0});
                if(!matchesOriginal(plan.bodyId,found->second.slot->originalBody,source))return false;
                const auto id=bindings_.surfaceId(plan.bodyId,leaf.key);
                if(!id||!used.insert(*id).second||ownedElsewhere(plan.bodyId,*id))return false;
                if(base){
                    const auto original=chunk(*base,*id);
                    if(original){
                        if(!captureOriginal(plan.bodyId,leaf.bodyTriangles,*original))return false;
                        continue;
                    }
                }
                if(!bindings_.baselineUnowned(plan.bodyId,leaf.key,*id))return false;
                source.albedos.reserve(source.triangles.size());
                for(auto& item:source.triangles){
                    item.surface=uint32_t(source.albedos.size());
                    source.albedos.push_back(bindings_.baselineAlbedo(plan.bodyId,leaf.key,item.triangle));
                }
                replaceBody(prospective,*id,plan.bodyId,source,bindings_.project);
            }
            if(!bindings_.ready()||scene_.hasPendingChanges()||scene_.currentShared()!=base||
               planet.caveSurfaceRevision()!=plan.surfaceRevision)return false;
            if(!prospective.hasPendingChanges())return bool(base);
            if(!prospective.publish(target_).published)return false;
            static_assert(std::is_nothrow_move_assignable_v<render::GiGeometryScene>);
            scene_=std::move(prospective);
            return true;
        }catch(const std::exception&){return false;}
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
            cohort.expected.clear();cohort.volumes.clear();cohort.originals.clear();cohort.sources.clear();
            auto prospective=scene_; // immutable existing BVH pointers are shared
            std::set<uint64_t> used;
            for(const auto& [key,ref]:slots){
                const auto id=bindings_.surfaceId(body,key);
                if(!id||!used.insert(*id).second||ownedElsewhere(body,*id)||previousVolumes.contains(*id))return false;
                const auto old=previousOriginals.find(*id);
                if(old==previousOriginals.end()){
                    const auto original=chunk(*baseline,*id);
                    if(!original)return false;
                    auto source=captureOriginal(body,ref.slot->originalBody,*original);
                    if(!source)return false;
                    cohort.originals.emplace(*id,std::move(*source));
                }else{
                    if(!matchesOriginal(body,ref.slot->originalBody,old->second))return false;
                    cohort.originals.emplace(*id,old->second);
                }
                cohort.expected.emplace(*id,GeometryPtr{});
                CavePlanetSlotId slot;slot.kind=CavePlanetSlotKind::Surface;slot.surface=key;slot.region=ref.region;
                auto source=captureCut(body,slot,ref.slot->bodyTriangles,{});
                replaceBody(prospective,*id,body,source,bindings_.project);
                cohort.sources.emplace(*id,std::move(source));
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
                auto source=captureCut(body,volume.id,volume.payload.collision.triangles,
                    game::Surface::anchor(volume.payload.body));
                replaceBody(prospective,*id,body,source,bindings_.project);
                cohort.sources.emplace(*id,std::move(source));
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
                replaceBody(prospective,id,body,original,bindings_.project);
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
    static void validateTriangle(const Triangle& out){
        for(float v:key(out))if(!std::isfinite(v))throw std::invalid_argument("Caverns GI projection");
        const auto n=render::cross(out.b-out.a,out.c-out.a);
        const auto area=render::dot(n,n);
        if(!std::isfinite(area)||area<=0)throw std::invalid_argument("Caverns GI projection lost triangle");
    }
    static Triangle convert(const std::string& body,const BodyTriangle& t,const CavePlanetGiProject& project){
        Triangle out{project(body,t.triangle.a),project(body,t.triangle.b),project(body,t.triangle.c),t.surface};
        validateTriangle(out);return out;
    }
    static std::vector<Triangle> projectBody(const std::string& body,const BodyGeometry& source,
            const CavePlanetGiProject& project){
        std::vector<Triangle> triangles;triangles.reserve(source.triangles.size());
        for(const auto& t:source.triangles)triangles.push_back(convert(body,t,project));
        return triangles;
    }
    // Match BVH-reordered original triangles to exact body-space input and
    // retain each triangle's real surface/material index (including duplicates).
    std::optional<BodyGeometry> captureOriginal(const std::string& body,
            const std::vector<world::Triangle>& source,const Geometry& current)const{
        if(source.size()!=current.triangles.size())return {};
        std::map<TriangleKey,std::vector<uint32_t>> materials;
        for(const auto& t:current.triangles)materials[key(t)].push_back(t.surface);
        BodyGeometry out;out.albedos=current.albedos;out.triangles.reserve(source.size());
        for(const auto& t:source){
            auto i=materials.find(key(convert(body,{t,0},bindings_.project)));
            if(i==materials.end()||i->second.empty())return {};
            out.triangles.push_back({t,i->second.back()});i->second.pop_back();
        }
        return out;
    }
    bool matchesOriginal(const std::string& body,const std::vector<world::Triangle>& source,
            const BodyGeometry& current)const{
        if(source.size()!=current.triangles.size())return false;
        // Compare exact fixed coordinates, not a potentially lossy projection.
        (void)body;
        using RawKey=std::array<int64_t,9>;
        const auto raw=[](const world::Triangle& t)->RawKey{
            return {t.a.x.raw,t.a.y.raw,t.a.z.raw,t.b.x.raw,t.b.y.raw,t.b.z.raw,
                t.c.x.raw,t.c.y.raw,t.c.z.raw};
        };
        std::vector<RawKey> a,b;a.reserve(source.size());b.reserve(source.size());
        for(const auto& t:source)a.push_back(raw(t));
        for(const auto& t:current.triangles)b.push_back(raw(t.triangle));
        std::sort(a.begin(),a.end());std::sort(b.begin(),b.end());return a==b;
    }
    BodyGeometry captureCut(const std::string& body,const CavePlanetSlotId& slot,
            const std::vector<world::Triangle>& source,Vec3 offset)const{
        BodyGeometry out;out.triangles.reserve(source.size());out.albedos.reserve(source.size());
        for(const auto& t:source){
            const world::Triangle local{t.a-offset,t.b-offset,t.c-offset};
            out.triangles.push_back({local,uint32_t(out.albedos.size())});
            out.albedos.push_back(bindings_.albedo(body,slot,local));
        }
        return out;
    }
    static void replaceBody(render::GiGeometryScene& dest,uint64_t id,const std::string& body,
            const BodyGeometry& source,const CavePlanetGiProject& project){
        dest.replaceChunk(id,projectBody(body,source,project),source.albedos);
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
