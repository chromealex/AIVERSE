#pragma once
// Render-owner-only Studio adapter. Reuses GameClient's existing bounded
// Caverns streaming, STRATA/GeometryScene and fence retirement pipeline.
// Never mutates MapDoc, gameplay ECS, collision or navigation.
#include "apps/editor/core/MapDoc.h"
#include "engine/world/TerraWeaveCavernsArchive.h"
#include "engine/world/TerraWeaveCavernsClientVisualHost.h"
#include <algorithm>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <string>

namespace aiverse::studio {
class StudioCavernsPreview {
    using Host=terraweave::caverns::CaveClientVisualHost;
    using Point=terraweave::caverns::PointCm;
public:
    // Explicit, session-only controls. Changing the paging policy is a
    // deliberate preview restart, not a GPU-idle wait on each brush dab.
    bool configure(gfx::Device& device,const Json& input) {
        const bool enabled=input.getBool("enabled",enabled_);
        const bool isolate=input.getBool("isolate",isolate_);
        const bool paging=input.getBool("paging",paging_);
        if((!enabled||paging!=paging_)&&owner_&&!close(device))return false;
        enabled_=enabled;isolate_=isolate;paging_=paging;
        if(!enabled_)error_.clear();
        return true;
    }
    // Before beginFrame: map transitions may need an explicit idle barrier.
    void syncDocument(gfx::Device& device,const editor::MapDoc& doc,
                      const std::string& project) {
        if(!enabled_)return;
        // Snapshot identity includes actual document bytes on every new
        // immutable MapDoc capture (also catches same-revision Undo/Redo).
        const bool contextChanged=project!=project_||doc.path!=map_;
        if(contextChanged){
            if(!close(device)){error_="Previous Caverns preview retirement refused";return;}
            project_=project;map_=doc.path;
        }
        if(doc.data.planet()||doc.data.extra["terraweaveCaverns"].isNull()){
            if(owner_&&!close(device))error_="Caverns preview retirement refused";
            return;
        }
        try{
            const auto& authored=doc.data.extra["terraweaveCaverns"];
            if(seenDoc_!=&doc||seenRevision_!=doc.revision||!owner_){
                const auto bytes=authored.dump(-1);
                if(bytes!=document_||!owner_){
                    auto source=std::make_shared<const terraweave::caverns::CaveVolumeSnapshot>(
                        terraweave::caverns::caveSnapshotFromJson(authored));
                    if(!owner_){
                        terraweave::caverns::StreamSettings stream;
                        stream.radiusXZ=1;stream.radiusY=1;stream.maxResident=27;
                        stream.maxBuildsPerPump=2;stream.maxRetiresPerPump=4;
                        owner_=std::make_unique<Host>(stream);
                        terraweave::caverns::CaveBatchSpec spec;
                        spec.enableStrata=paging_;
                        if(paging_)spec.paging={4u*1024*1024,24u*1024*1024,128u*1024*1024,
                                                512u*1024,2u*1024*1024,8u*1024*1024,true};
                        owner_->setGraphicsOptions(spec);
                        owner_->open(source);
                    }else if(owner_->opened())owner_->replaceSnapshot(source);
                    else owner_->open(source);
                    document_=bytes;
                    failed_=false;error_.clear();
                }
                seenDoc_=&doc;seenRevision_=doc.revision;
            }
        }catch(const std::exception& e){failed_=true;error_=e.what();}
    }
    // Inside frame but before any pass, where the RHI exposes real fences.
    void beforeRender(gfx::Device& device,render::float3 focus) {
        if(!visible())return;
        try{
            if(!std::isfinite(focus.x)||!std::isfinite(focus.y)||!std::isfinite(focus.z))
                throw std::invalid_argument("Caverns preview focus");
            const auto cm=[](float x){return int64_t(std::llround(std::clamp(double(x),-9990000.,9990000.)*100.));};
            Point p{cm(focus.x),cm(focus.y),cm(focus.z)};
            owner_->beforeRender(device,p);
            error_.clear();
        }catch(const std::exception& e){failed_=true;error_=e.what();}
    }
    bool visible()const noexcept{return enabled_&&owner_&&owner_->opened()&&!failed_&&error_.empty();}
    bool isolate()const noexcept{return visible()&&isolate_;}
    bool needsFrame()const noexcept{
        if(!visible())return false;
        const auto s=owner_->streaming();
        return owner_->pendingCpu()||s.buildsInFlight||s.retirePending||s.ready!=s.tracked;
    }
    void draw(render::Renderer3D& renderer){if(visible())owner_->draw(renderer);}
    bool close(gfx::Device& device) {
        if(owner_&&!owner_->closeAfterGpuIdle(device))return false;
        owner_.reset();seenDoc_=nullptr;seenRevision_=0;document_.clear();error_.clear();failed_=false;
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
        boolean("visible",visible());boolean("visualOnly",true);
        out.set("error",Json::string(error_));
        if(owner_){
            const auto r=owner_->streaming();const auto g=owner_->gpuStats();const auto m=owner_->gpuMemoryStats();
            count("sourceRevision",owner_->sourceRevision());count("tracked",r.tracked);count("ready",r.ready);
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
    std::unique_ptr<Host> owner_;
    const editor::MapDoc* seenDoc_=nullptr; // identity only, never dereferenced
    uint64_t seenRevision_=0;
    std::string project_,map_,document_,error_;
    bool enabled_=true,isolate_=false,paging_=false,failed_=false;
};
} // namespace aiverse::studio
