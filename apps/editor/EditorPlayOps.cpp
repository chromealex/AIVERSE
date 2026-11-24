// Agent ops that act inside the play-test: list live objects and issue player commands, so an agent
// can test a mechanic by playing it (surrender, abilities, digging, roads, ...). Commands go through
// Simulation::submit like player input, so the play-test stays deterministic and replayable.
#include <algorithm>
#include <charconv>
#include <cmath>
#include <stdexcept>
#include "Editor.h"
#include "core/SimTools.h"

namespace aiverse::editor {

using namespace aiverse::game;

Json Editor::graphWatchSample(const std::string& domain,const std::string& entity) const {
    Json r=Json::object();r.set("available",Json::fromBool(false));
    if(!sim_||!graphPlayEpoch_||graphPlayEpoch_==~uint64_t{0})return r;
    if(domain=="motionweave"){
        uint64_t key=0;const auto parsed=std::from_chars(entity.data(),entity.data()+entity.size(),key);
        if(entity.empty()||parsed.ec!=std::errc{}||parsed.ptr!=entity.data()+entity.size()||!sim_->world().alive(Entity::fromPacked(key)))return r;
        return view_.sharedAnimationGraphDiagnostic(key,graphPlayEpoch_);
    }
    if(domain!="spectrafx"||entity!="preview"||fxPreview_.name.empty())return r;
    const auto sample=gameFx_.instanceTelemetry(fxPreview_.h);
    const auto seq=gameFx_.telemetryUpdateSerial();
    if(!sample.serial||!seq||seq==~uint64_t{0})return r;
    r.set("available",Json::fromBool(true));r.set("domain",Json::string(domain));
    r.set("worldEpoch",Json::string(std::to_string(graphPlayEpoch_)));
    r.set("instanceSerial",Json::string(std::to_string(sample.serial)));
    r.set("programIdentity",Json::string("instance:"+std::to_string(sample.serial)));
    r.set("sequence",Json::string(std::to_string(seq)));
    r.set("scope",Json::string("native-editor-cpu-preview-instance"));
    r.set("effect",Json::string(fxPreview_.name));r.set("particles",Json::number(int64_t(sample.particles)));
    r.set("emitting",Json::fromBool(sample.emitting));r.set("time",Json::number(double(sample.time),6));
    return r;
}
void Editor::captureGraphWatch() noexcept {
    if(!graphWatch_.running())return;
    try{
        if(!sim_){graphWatch_.stop("play-ended");return;}
        // Do not sample a hidden or non-published game viewport.
        if(!prismaGameRendered_)return;
        graphWatch_.capture(graphWatchSample(graphWatch_.domain(),graphWatch_.entity()));
    }catch(...){graphWatch_.stop("capture-failed");}
}

void Editor::registerPlayOps() {
    auto add = [this](const char* name, const std::string& desc, const char* schema, Workspace::OpFn fn) {
        ws_.registerOp({name, desc, Json::parse(schema), std::move(fn), true});
    };
    add("graph_live_owner_snapshot",
        "Read-only snapshot from the current native editor presentation owner; no gameplay mutation.",
        R"({"type":"object","properties":{"domain":{"type":"string","enum":["motionweave","spectrafx"]},"entity":{"type":"string","description":"Exact unsigned decimal packed entity ID"},"captureHistory":{"type":"boolean"}},"required":["domain"]})",
        [this](const Json& p){
            Json r=Json::object();
            const auto domain=p.getStr("domain");r.set("domain",Json::string(domain));
            r.set("scope",Json::string("native-editor-presentation"));
            r.set("gpuTimingAvailable",Json::fromBool(false));
            if(domain=="motionweave"){
                const auto id=p.getStr("entity");uint64_t key=0;
                const auto parsed=std::from_chars(id.data(),id.data()+id.size(),key);
                if(id.empty()||parsed.ec!=std::errc{}||parsed.ptr!=id.data()+id.size())
                    throw std::invalid_argument("entity must be an exact decimal packed ID string");
                if(!sim_||!sim_->world().alive(Entity::fromPacked(key))){
                    r.set("available",Json::fromBool(false));
                    r.set("reason",Json::string("No running play-test or entity generation is no longer alive"));
                    return r;
                }
                auto snapshot=view_.sharedAnimationGraphDiagnostic(key,graphPlayEpoch_==~uint64_t{0}?0:graphPlayEpoch_,p.getBool("captureHistory")?&graphPlayHistory_:nullptr);
                const bool shared=snapshot.getBool("available");
                if(!shared)snapshot=view_.animationGraphDiagnostic(key);
                snapshot.set("domain",Json::string(domain));if(!shared)snapshot.set("scope",Json::string("native-editor-play-view"));
                snapshot.set("gpuTimingAvailable",Json::fromBool(false));
                return snapshot;
            }
            if(domain!="spectrafx")throw std::invalid_argument("unsupported live domain");
            if(ws_.project.dir.empty()){
                r.set("available",Json::fromBool(false));r.set("reason",Json::string("No project open"));return r;
            }
            const auto& stats=gameFx_.stats();
            r.set("available",Json::fromBool(true));r.set("scope",Json::string("editor-shared-particle-system"));
            r.set("particles",Json::number(int64_t(stats.particles)));r.set("instances",Json::number(int64_t(stats.instances)));
            r.set("dropped",Json::number(int64_t(stats.dropped)));r.set("culled",Json::number(int64_t(stats.culled)));
            r.set("drawCalls",Json::number(int64_t(stats.drawCalls)));r.set("previewActive",Json::fromBool(!fxPreview_.name.empty()));
            r.set("previewFrozen",Json::fromBool(fxFrozen_));
            r.set("gpuPreviewActive",Json::fromBool(spectraGpuPreviewActive()));
            if(spectraGpuPreviewActive())r.set("gpuPreviewHistory",Json::parse(spectraGpuPreviewHistoryJson()));
            r.set("note",Json::string("Aggregate owner counters, not per-graph or per-entity; GPU preview history contains estimates, not GPU timings."));
            return r;
        });
    add("texture_graph_gpu_owner_preview","Queue, read or publish native texture GPU preview/bake/set; CPU fallback unless requireGpu.",
        R"({"properties":{"action":{"type":"string","enum":["start","read","publish","cancel"]},"graph":{"type":"object"},"overrides":{"type":"object"},"node":{"type":"string"},"output":{"type":"string"},"requireGpu":{"type":"boolean"},"resolution":{"type":"integer","minimum":8,"maximum":128},"bake":{"type":"boolean"},"bakeSet":{"type":"boolean"},"materialized":{"type":"boolean","description":"Opt-in experimental native materialized DAG execution"},"materializedDag":{"type":"boolean","description":"Compatibility alias for materialized"},"timeoutMs":{"type":"integer","minimum":1000,"maximum":300000},"request":{"type":"string"},"textureName":{"type":"string"}},"required":["action"]})",
        [this](const Json& p){
            if(!dev_)throw std::runtime_error("native graphics owner unavailable");
            textureGraphGpuPreview_.checkProject(ws_.project.dir);
            if(p.getStr("action")=="start")return textureGraphGpuPreview_.start(p,ws_.project.dir);
            if(p.getStr("action")=="read")return textureGraphGpuPreview_.status();
            if(p.getStr("action")=="cancel")return textureGraphGpuPreview_.cancel(p,ws_.project.dir);
            if(p.getStr("action")=="publish")return textureGraphGpuPreview_.publish(ws_,p);
            throw std::invalid_argument("unknown GPU preview action");
        });
    add("graph_gpu_owner_profile","Start, read or stop optional native RHI scope capture.",
        R"({"type":"object","properties":{"action":{"type":"string","enum":["start","read","stop"]}},"required":["action"]})",
        [this](const Json& p){
            const auto action=p.getStr("action");
            if(!dev_||ws_.project.dir.empty()){
                Json r=Json::object();r.set("available",Json::fromBool(false));r.set("reason",Json::string("Native editor graphics owner unavailable"));return r;
            }
            graphGpuProfile_.checkProject(ws_.project.dir);
            if(action=="start")graphGpuProfile_.start(dev_->info(),ws_.project.dir);
            else if(action=="stop")graphGpuProfile_.stop();
            else if(action!="read")throw std::invalid_argument("unknown GPU profile action");
            return graphGpuProfile_.report();
        });
    add("graph_live_owner_profile","Opt-in CPU profiling of one current common presentation instance; no gameplay controls.",
        R"({"type":"object","properties":{"action":{"type":"string","enum":["enable","read","reset","disable"]},"entity":{"type":"string"}},"required":["action","entity"]})",
        [this](const Json& p){
            const auto action=p.getStr("action"),id=p.getStr("entity");
            if(action!="enable"&&action!="read"&&action!="reset"&&action!="disable")throw std::invalid_argument("unknown profile action");
            uint64_t key=0;const auto parsed=std::from_chars(id.data(),id.data()+id.size(),key);
            if(id.empty()||parsed.ec!=std::errc{}||parsed.ptr!=id.data()+id.size())throw std::invalid_argument("exact packed entity ID required");
            if(!sim_||!graphPlayEpoch_||graphPlayEpoch_==~uint64_t{0}||!sim_->world().alive(Entity::fromPacked(key))){
                Json r=Json::object();r.set("available",Json::fromBool(false));r.set("reason",Json::string("Live entity unavailable"));return r;
            }
            return view_.sharedAnimationProfile(key,graphPlayEpoch_,action);
        });
    add("graph_live_owner_watch","Subscribe to one current published play-view instance. CPU diagnostics only.",
        R"({"type":"object","properties":{"action":{"type":"string","enum":["start","status","stop"]},"domain":{"type":"string","enum":["motionweave","spectrafx"]},"entity":{"type":"string"}},"required":["action"]})",
        [this](const Json& p){
            const auto action=p.getStr("action");
            if(action=="start"){
                const auto domain=p.getStr("domain"),entity=p.getStr("entity");
                graphWatch_.start(domain,entity,graphWatchSample(domain,entity));
            }else if(action=="stop")graphWatch_.stop();
            else if(action!="status")throw std::invalid_argument("unknown watch action");
            return graphWatch_.status();
        });
    add("play_units",
        "Live objects of the running play-test: entity id (for play_command), type, owner, position, hp, current order. "
        "Filter by owner / type; at most 'limit' (default 200).",
        R"({"properties": {"owner": {"type": "integer"}, "type": {"type": "string"}, "limit": {"type": "integer"}}})",
        [this](const Json& p) {
            if (!sim_) throw std::runtime_error("no play-test running (play_start)");
            const auto& R = rules();
            const int wantType = p.has("type") ? R.findType(p.getStr("type")) : -1;
            if (p.has("type") && wantType < 0) throw std::runtime_error("unknown type '" + p.getStr("type") + "'");
            const int64_t limit = std::clamp<int64_t>(p["limit"].integer(200), 1, 2000);
            Json arr = Json::array();
            int64_t total = 0;
            sim_->world().each<Unit, Owner>([&](Entity e, Unit& u, Owner& o) {
                if (p.has("owner") && o.player != p["owner"].integer()) return;
                if (wantType >= 0 && u.type != wantType) return;
                if (++total > limit) return;
                SimRefs refs{*sim_, *simModule(), playIds_};
                Json j = entityJson(refs, e);
                j.set("id", Json::number(int64_t(e.packed())));
                for (auto& [name, pe] : playIds_) if (pe == e) j.set("name", Json::string(name));
                arr.push(j);
            });
            Json r = Json::object();
            r.set("units", arr);
            r.set("total", Json::number(total));
            return r;
        });
    add("play_command",
        "Issue a player command in the running play-test, exactly like player input. command: " + commandNames() + ". " +
        kCommandHelp + " Map entity names ('id' in the map) work as references. player defaults to the human slot or 1.",
        (std::string(R"({"properties": {)") + commandSchemaProps() + R"(}, "required": ["command"]})").c_str(),
        [this](const Json& p) {
            if (!sim_) throw std::runtime_error("no play-test running (play_start)");
            SimRefs refs{*sim_, *simModule(), playIds_};
            Command c = commandFromJson(refs, p, uint8_t(human_ > 0 ? human_ : 1));
            sim_->submit(c);
            Json r = Json::object();
            r.set("ok", Json::fromBool(true));
            r.set("summary", Json::string("Команда " + p.getStr("command") + " от игрока " + std::to_string(c.player)));
            r.set("tick", Json::number(int64_t(sim_->tick())));
            r.set("units", Json::number(int64_t(c.units.size())));
            return r;
        });
}

} // namespace aiverse::editor
