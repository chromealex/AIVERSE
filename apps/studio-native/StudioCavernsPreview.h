#pragma once
// Render-owner-only Studio adapter. Reuses GameClient's existing bounded
// Caverns streaming, STRATA/GeometryScene and fence retirement pipeline.
// Never mutates MapDoc, gameplay ECS, collision or navigation.
#include "apps/editor/core/MapDoc.h"
#include "apps/editor/core/TerraWeavePlanetCavernsDoc.h"
#include "engine/world/TerraWeaveCavernsArchive.h"
#include "engine/world/TerraWeaveCavernsClientVisualHost.h"
#include "apps/studio-native/StudioPlanetCavernsRim.h"
#if defined(AIVERSE_STRATA_STAGING_ENABLED)
#include "engine/render/StrataStreamingCoordinator.h"
#endif
#include <algorithm>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <string>

namespace aiverse::studio {
class StudioCavernsPreview {
    using Host=terraweave::caverns::CaveClientVisualHost;
    using Point=terraweave::caverns::PointCm;
    using Group=terraweave::caverns::CaveSurfaceGroupPlan;
public:
    // Explicit, session-only controls. Changing the paging policy is a
    // deliberate preview restart, not a GPU-idle wait on each brush dab.
    bool configure(gfx::Device& device,const Json& input) {
        const bool enabled=input.getBool("enabled",enabled_);
        const bool isolate=input.getBool("isolate",isolate_);
        const bool paging=input.getBool("paging",paging_);
        auto budget=budget_;
        if(input.has("budgets")){
            const auto& b=input["budgets"];
            if(!b.isObject())throw std::invalid_argument("Caverns budgets object");
            const auto bytes=[&](const char* key){
                const auto& v=b[key];const auto n=v.toDouble();
                if(!v.isNumber()||!std::isfinite(n)||std::trunc(n)!=n||n<4||n>2147483648.||int64_t(n)%4)
                    throw std::invalid_argument(std::string("Caverns invalid byte budget: ")+key);
                return size_t(n);
            };
            budget={bytes("perSourceBytes"),bytes("perShardBytes"),bytes("deviceBytes"),
                    bytes("perSourceUploadBytes"),bytes("perShardUploadBytes"),bytes("deviceUploadBytes"),true};
            if(budget.perSourceBytes>budget.perShardBytes||budget.perShardBytes>budget.deviceBytes||
               budget.perSourceUploadBytes>budget.perShardUploadBytes||budget.perShardUploadBytes>budget.deviceUploadBytes)
                throw std::invalid_argument("Caverns budgets must ascend source/shard/device");
        }
        const bool changed=budget.perSourceBytes!=budget_.perSourceBytes||budget.perShardBytes!=budget_.perShardBytes||
            budget.deviceBytes!=budget_.deviceBytes||budget.perSourceUploadBytes!=budget_.perSourceUploadBytes||
            budget.perShardUploadBytes!=budget_.perShardUploadBytes||budget.deviceUploadBytes!=budget_.deviceUploadBytes;
        if((!enabled||paging!=paging_||changed)&&owner_&&!close(device))return false;
#if defined(AIVERSE_STRATA_STAGING_ENABLED)
        if(enabled&&paging){
            const auto shared=render::strata::deviceIndexBudgetStats(device);
            if(shared.activeLeases&&((shared.limitBytes&&shared.limitBytes!=budget.deviceBytes)||
                (shared.frameUploadLimitBytes&&shared.frameUploadLimitBytes!=budget.deviceUploadBytes)))
                throw std::invalid_argument("Caverns paging conflicts with live device STRATA policy");
        }
#endif
        budget_=budget;enabled_=enabled;isolate_=isolate;paging_=paging;
        if(!enabled_)error_.clear();
        return true;
    }
    // Before beginFrame: map transitions may need an explicit idle barrier.
    void syncDocument(gfx::Device& device,const editor::MapDoc& doc,
                      const std::string& project) {
        if(!enabled_)return;
        // Snapshot identity includes actual document bytes on every new
        // immutable MapDoc capture (also catches same-revision Undo/Redo).
        const bool contextChanged=project!=project_||doc.path!=map_||planetary_!=doc.data.planet();
        if(contextChanged){
            if(!close(device)){error_="Previous Caverns preview retirement refused";return;}
            project_=project;map_=doc.path;planetary_=doc.data.planet();
        }
        try{
        if(planetary_?editor::planet_caves::activeRegionId(doc.data).empty():
           doc.data.extra["terraweaveCaverns"].isNull()){
            if(owner_&&!close(device))error_="Caverns preview retirement refused";
            return;
        }
            if(seenDoc_!=&doc||seenRevision_!=doc.revision||!owner_){
                const auto& authored=planetary_?editor::planet_caves::selectedRegionRecord(doc.data):
                    doc.data.extra["terraweaveCaverns"];
                const auto bytes=(planetary_?editor::planet_caves::activeRegionId(doc.data):std::string{})+":"+authored.dump(-1);
                if(bytes!=document_||!owner_){
                    namespace cave=terraweave::caverns;
                    auto region=planetary_?std::optional{editor::planet_caves::read(doc.data)}:std::nullopt;
                    const auto anchorBytes=region?editor::planet_caves::activeRegionId(doc.data)+":"+cave::cavePlanetAnchorToJson(region->anchor).dump(-1):std::string{};
                    if(owner_&&anchorBytes!=chartIdentity_){
                        if(!close(device))throw std::runtime_error("Caverns chart retirement refused");
                    }
                    chartIdentity_=anchorBytes;
                    auto source=std::make_shared<const cave::CaveVolumeSnapshot>(
                        region?region->volume:cave::caveSnapshotFromJson(authored));
                    if(!owner_){
                        terraweave::caverns::StreamSettings stream;
                        stream.radiusXZ=1;stream.radiusY=1;stream.maxResident=64; // 27 camera slots or up to 64 exact rim slots
                        stream.maxBuildsPerPump=2;stream.maxRetiresPerPump=4;
                        owner_=std::make_unique<Host>(stream);
                        terraweave::caverns::CaveBatchSpec spec;
                        spec.enableStrata=paging_;
                        if(paging_)spec.paging=budget_;
                        owner_->setGraphicsOptions(spec);
                        owner_->open(source);
                        if(region){
                            auto lattice=cave::caveChartPreviewLattice(region->anchor.halfExtentCm);
                            resolution_=lattice.resolution;spacingCm_=lattice.spacingCm;
                            owner_->setChartPreviewDemand(std::move(lattice.keys),lattice.halfExtentCm);
                        }
                    }else if(owner_->opened())owner_->replaceSnapshot(source);
                    else owner_->open(source);
                    authoredSnapshot_=source;rimPlan_.reset();rejectedRim_.reset();rimError_.clear();
                    document_=bytes;
                    failed_=false;error_.clear();
                }
                // Terrain edits can change the surface while authored cave bytes
                // stay identical. Recompute on the immutable document identity too.
                planetPlan_.reset();planetPlanError_.clear();
                if(planetary_&&owner_&&authoredSnapshot_&&!authoredSnapshot_->entrances.empty()){
                    try{
                        namespace pc=editor::planet_caves;
                        const auto region=pc::read(doc.data);
                        const auto frame=pc::frameForDocument(doc.data,region.anchor);
                        const auto patch=pc::captureLocalSurface(doc.data,region.anchor,doc.revision);
                        const auto cut=pc::planLocalSurfaceCut(frame,patch,region.volume,doc.revision?doc.revision:1);
                        if(cut.rims.empty())throw std::invalid_argument("No certified chart mouths");
                        if(cut.authorityRevision!=authoredSnapshot_->sourceRevision)
                            throw std::invalid_argument("Caverns chart authored revision mismatch");
                        planetPlan_=std::make_shared<const Group>(refinePlanetChartRim(cut,patch,
                            owner_->sourceRevision(),resolution_,spacingCm_));
                    }catch(const std::exception& e){planetPlanError_=e.what();}
                }
                seenDoc_=&doc;seenRevision_=doc.revision;
            }
        }catch(const std::exception& e){failed_=true;error_=e.what();}
    }
    bool enabled()const noexcept{return enabled_;}
    // The sparse terrain owner passes its exact plan. Automatically choose
    // a conservative lower plane between the authored cave end and actual
    // lowest mouth vertex. Unsupported shaft topology falls back to solid.
    void syncRimGroup(std::shared_ptr<const Group> plan){
        if(planetary_&&plan&&plan!=planetPlan_)return;
        if(plan&&plan->rims.empty())plan.reset();
        if(!owner_||!authoredSnapshot_)return;
        if(plan==rimPlan_|| (plan&&plan==rejectedRim_))return;
        try{
            std::map<uint64_t,Fixed> planes;
            if(plan){
                if(plan->authorityRevision!=(planetary_?owner_->sourceRevision():authoredSnapshot_->sourceRevision))
                    throw std::invalid_argument("Caverns preview terrain/volume revision mismatch");
                for(const auto& rim:plan->rims){
                    const auto it=std::find_if(authoredSnapshot_->entrances.begin(),authoredSnapshot_->entrances.end(),
                        [&](const auto& e){return e.id==rim.entranceId;});
                    if(it==authoredSnapshot_->entrances.end()||rim.ring.empty())
                        throw std::invalid_argument("Caverns preview missing rim entrance");
                    int64_t high=Fixed::fromRatio(it->surfaceYcm,100).raw;
                    for(const auto& p:rim.ring)high=std::min(high,p[1]);
                    const auto low=Fixed::fromRatio(it->caveYcm,100).raw;
                    if(high-low<Fixed::kOne/2)throw std::invalid_argument("Caverns preview shaft too shallow for collar");
                    planes.emplace(rim.entranceId,Fixed::fromRaw(low+(high-low)/2));
                }
            }
            owner_->setPreviewRimGroup(plan,planes,resolution_,spacingCm_);
            rimPlan_=std::move(plan);rejectedRim_.reset();rimError_.clear();
        }catch(const std::exception& e){
            rejectedRim_=plan;rimError_=e.what();rimPlan_.reset();
            try{owner_->setPreviewRimGroup({},{});}catch(...){failed_=true;error_="Caverns preview demand recovery refused";}
        }
    }
    std::shared_ptr<const Group> planetRimPlan()const noexcept{return visible()?planetPlan_:nullptr;}
    const std::string& planetRimDiagnostic()const noexcept{return planetPlanError_;}
    bool rimReady(const std::shared_ptr<const Group>& plan)const noexcept{
        return !plan||plan->rims.empty()||(visible()&&owner_->previewRimReady(*plan));
    }
    bool rimRejected(const std::shared_ptr<const Group>& plan)const noexcept{
        return plan&&(plan==rejectedRim_||!visible());
    }
    const std::string& rimError()const noexcept{return rimError_;}
    // Inside frame but before any pass, where the RHI exposes real fences.
    void beforeRender(gfx::Device& device,render::float3 focus) {
        if(!visible())return;
        try{
            if(!std::isfinite(focus.x)||!std::isfinite(focus.y)||!std::isfinite(focus.z))
                throw std::invalid_argument("Caverns preview focus");
            const auto cm=[](float x){return int64_t(std::llround(std::clamp(double(x),-9990000.,9990000.)*100.));};
            Point p{cm(focus.x),cm(focus.y),cm(focus.z)};
            owner_->beforeRender(device,p,resolution_,spacingCm_);
            error_.clear();
        }catch(const std::exception& e){
            if(rimPlan_){rejectedRim_=rimPlan_;rimError_=e.what();rimPlan_.reset();
                try{owner_->setPreviewRimGroup({},{});}catch(...){failed_=true;error_=e.what();}
            }else {failed_=true;error_=e.what();}
        }
    }
    bool visible()const noexcept{return enabled_&&owner_&&owner_->opened()&&!failed_&&error_.empty();}
    bool isolate()const noexcept{return visible()&&isolate_;}
    bool needsFrame()const noexcept{
        if(!visible())return false;
        const auto s=owner_->streaming();
        return owner_->previewDemandChanging()||owner_->pendingCpu()||s.buildsInFlight||s.retirePending||s.ready!=s.tracked;
    }
    void draw(render::Renderer3D& renderer){if(visible())owner_->draw(renderer);}
    bool close(gfx::Device& device) {
        if(owner_&&!owner_->closeAfterGpuIdle(device))return false;
        owner_.reset();authoredSnapshot_.reset();rimPlan_.reset();rejectedRim_.reset();rimError_.clear();planetPlan_.reset();planetPlanError_.clear();seenDoc_=nullptr;seenRevision_=0;document_.clear();chartIdentity_.clear();resolution_=16;spacingCm_=100;error_.clear();failed_=false;
        return true;
    }
    // Last resort on a failed GPU-idle proof during device destruction:
    // preserve the native owner rather than destroying possibly live Models.
    void quarantineOnFailedTeardown()noexcept{(void)owner_.release();}
    Json stats()const {
        Json out=Json::object();
        const auto boolean=[&](const char* key,bool value){out.set(key,Json::fromBool(value));};
        const auto count=[&](const char* key,uint64_t n){out.set(key,Json::string(std::to_string(n)));};
        boolean("enabled",enabled_);boolean("isolate",isolate_);boolean("paging",paging_);
        boolean("visible",visible());boolean("visualOnly",true);boolean("planetaryChart",planetary_);
        count("previewResolution",resolution_);count("previewSpacingCm",spacingCm_);
        out.set("error",Json::string(error_));
        boolean("rimGroupReady",rimPlan_&&rimReady(rimPlan_));
        boolean("rimGroupPending",rimPlan_&&!rimReady(rimPlan_));
        out.set("rimDiagnostic",Json::string(rimError_));
        Json budgets=Json::object();
        const auto bytes=[&](const char* k,size_t n){budgets.set(k,Json::number(int64_t(n)));};
        bytes("perSourceBytes",budget_.perSourceBytes);bytes("perShardBytes",budget_.perShardBytes);bytes("deviceBytes",budget_.deviceBytes);
        bytes("perSourceUploadBytes",budget_.perSourceUploadBytes);bytes("perShardUploadBytes",budget_.perShardUploadBytes);bytes("deviceUploadBytes",budget_.deviceUploadBytes);
        out.set("budgets",std::move(budgets));
        if(owner_){
            const auto r=owner_->streaming();const auto g=owner_->gpuStats();const auto m=owner_->gpuMemoryStats();
            count("sourceRevision",owner_->sourceRevision());count("tracked",r.tracked);count("ready",r.ready);
            count("chartSlots",owner_->chartSlots());count("exactChartSlots",owner_->exactChartSlots());
            boolean("chartLodFallback",owner_->chartLodFallback());
            count("pendingCpu",owner_->pendingCpu());count("retirePending",r.retirePending);
            count("activeShards",g.activeShards);count("stagedShards",g.pendingShards);count("retiredScenes",g.retiredScenes);
            count("activeSourceBytes",m.activeSourceBytes);count("stagedSourceBytes",m.stagedSourceBytes);
            count("retiredSourceBytes",m.retiredSourceBytes);count("activeStrataArenaBytes",m.activeStrataArenaBytes);
            count("stagedStrataArenaBytes",m.stagedStrataArenaBytes);count("retiredStrataArenaBytes",m.retiredStrataArenaBytes);
            count("pageTransfers",m.activePageTransfers);count("pageDeferrals",m.activePageDeferrals);
        }
        return out;
    }
private:
    terraweave::caverns::CaveStrataPaging budget_{4u*1024*1024,24u*1024*1024,128u*1024*1024,
                                                512u*1024,2u*1024*1024,8u*1024*1024,true};
    std::unique_ptr<Host> owner_;
    std::shared_ptr<const terraweave::caverns::CaveVolumeSnapshot> authoredSnapshot_;
    std::shared_ptr<const Group> rimPlan_,rejectedRim_,planetPlan_;
    std::string planetPlanError_;
    std::string rimError_;
    const editor::MapDoc* seenDoc_=nullptr; // identity only, never dereferenced
    uint64_t seenRevision_=0;
    std::string project_,map_,document_,error_;
    std::string chartIdentity_;
    uint32_t resolution_=16;int32_t spacingCm_=100;
    bool planetary_=false;
    bool enabled_=true,isolate_=false,paging_=false,failed_=false;
};
} // namespace aiverse::studio
