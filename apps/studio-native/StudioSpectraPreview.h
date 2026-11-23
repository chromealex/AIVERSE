// SpectraFX preview owned solely by the Studio Scene render thread.
// Native gfx::Device & SpectraGraphRuntime perform the actual simulation and
// depth-tested draw. Browser only sends bounded authoring/control requests.
// No ImGui, no game/ECS mutation and no second GPU device.
#pragma once
#include "engine/fx/SpectraGraphRuntime.h"
#include "engine/fx/SpectraFX.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <utility>
#include <string>
#include <vector>

namespace aiverse::studio {

class StudioSpectraPreview final {
public:
    static constexpr uint32_t kMaxTicks=3600;
    static constexpr uint32_t kSeekTicksPerFrame=8;
    static constexpr float kStep=1.f/60.f;

    // Render-thread command. Graph loads happen BETWEEN frames so retiring
    // GPU resources can settle before the new backend allocates. Commands
    // never run inside an active render/compute pass.
    Json command(gfx::Device& device,const Json& input) {
        const std::string action=input.getStr("action","status");
        if(action=="status")return status();
        if(action=="stop") {
            stop(device);
            return status();
        }
        if(action=="load") {
            if(!input["graph"].isObject() ||
               input["graph"].getInt("version")!=2)
                return errorStatus("Preview requires a version-2 SpectraFX graph");
            const Json graph=input["graph"];
            const std::string authored=graph.dump(-1);
            if(authored.size()>512u*1024u)
                return errorStatus("Preview graph exceeds 512 KiB");
            fx::EffectDef checked;
            std::vector<std::string> errors;
            if(!fx::compileSpectraGraph(graph,Json::object(),checked,errors))
                return errorStatus(errors.empty()?"SpectraFX graph validation failed":errors.front());
            // Validation completed before destroying any last-known-good
            // preview. GPU resources are retired while NO pass is open.
            device.waitForResourceIdle();
            runtime_.shutdown();
            spectra::GraphRuntimeOptions opts;
            opts.capacity=1024;
            opts.gpuProgram=true; // strict GPU VM eligibility, CPU twin otherwise
            opts.preference=spectra::ComputePreference::Automatic;
            if(!runtime_.initGraph(&device,graph,Json::object(),opts)) {
                ready_=false;playing_=false;pendingRestart_=false;
                return errorStatus(runtime_.reason().empty()?
                    "SpectraFX preview backend unavailable":runtime_.reason());
            }
            ready_=true;
            playing_=false;
            pendingRestart_=true;
            timeTicks_=0;
            targetTicks_=0;
            stepsPending_=0;
            requestedParameters_.clear();
            backend_=runtime_.gpuActive()?"Metal GPU":"CPU (full effect)";
            error_.clear();
            ++generation_;
            return status();
        }
        if(!ready_)return errorStatus("Load a valid graph before preview playback");
        if(action=="play") {error_.clear();playing_=true;return status();}
        if(action=="pause") {playing_=false;targetTicks_=timeTicks_;stepsPending_=0;return status();}
        if(action=="restart") {
            error_.clear();
            playing_=input.getBool("play",false);
            pendingRestart_=true;timeTicks_=0;targetTicks_=0;stepsPending_=0;
            return status();
        }
        if(action=="step") {
            playing_=false;
            if(timeTicks_>=kMaxTicks)return errorStatus("Preview reached 60-second limit");
            // An explicit Step supersedes any earlier unfinished seek.
            error_.clear();
            targetTicks_=timeTicks_;
            stepsPending_=1;
            return status();
        }
        if(action=="seek") {
            if(!input["tick"].isNumber() ||
               input["tick"].numberLiteral().find_first_of(".eE-")!=std::string::npos ||
               input["tick"].integer(-1)<0 || input["tick"].integer(-1)>kMaxTicks)
                return errorStatus("Preview seek expects a tick in 0..3600");
            playing_=false;
            error_.clear();
            targetTicks_=uint32_t(input["tick"].integer());
            stepsPending_=0;
            if(targetTicks_<timeTicks_) {
                pendingRestart_=true;
                timeTicks_=0;
            }
            return status();
        }
        if(action=="parameter") {
            const auto name=input.getStr("name");
            const auto it=runtime_.instanceParameters().find(name);
            if(it==runtime_.instanceParameters().end() || !input["value"].isNumber())
                return errorStatus("Unknown preview parameter or missing numeric value");
            const double n=input["value"].toDouble();
            if(!std::isfinite(n) || n<it->second.minimum || n>it->second.maximum)
                return errorStatus("Preview parameter value outside authored range");
            requestedParameters_[name]=float(n);
            error_.clear();
            return status();
        }
        return errorStatus("Unknown SpectraFX preview action");
    }

    // Called once INSIDE the native Scene frame, after gfx::beginFrame and
    // before Renderer3D begins scene passes. Even paused views update their
    // camera-dependent draw uniforms, but never advance particle age.
    void prepare(const fx::SpectraFxView& view,render::float3 origin) {
        if(!ready_ || !view.valid())return;
        runtime_.setView(view);
        if(pendingRestart_) {
            runtime_.clear();
            timeTicks_=0;
            pendingRestart_=false;
            // Native burst, NOT an HTML animation or placeholder quad.
            if(!runtime_.burstAt({origin.x,origin.y+1.0f,origin.z})) {
                playing_=false;
                error_=runtime_.reason().empty()?
                    "SpectraFX preview burst rejected":runtime_.reason();
                return;
            }
        }
        for(const auto& [name,value]:requestedParameters_) {
            if(!runtime_.setNamedProgramParameter(name,value,true)) {
                playing_=false;error_=runtime_.reason();break;
            }
        }
        requestedParameters_.clear();
        if(!error_.empty())return;
        const uint32_t remaining=kMaxTicks-timeTicks_;
        const uint32_t ticks=std::min(remaining,playing_?1u:
            targetTicks_>timeTicks_?
                std::min(kSeekTicksPerFrame,targetTicks_-timeTicks_):
                stepsPending_);
        stepsPending_=0;
        for(uint32_t i=0;i<ticks;++i) {
            if(!runtime_.step(kStep)) {
                playing_=false;
                ready_=runtime_.backend()==spectra::ComputeBackend::Cpu ||
                       runtime_.gpuActive();
                error_=runtime_.reason().empty()?
                    "SpectraFX preview simulation failed":runtime_.reason();
                break;
            }
            ++timeTicks_;
        }
        if(timeTicks_>=kMaxTicks)playing_=false;
    }

    // Draw only inside the SAME still-open native Scene pass (before PRISMA
    // resolves that frame). No RHI work is ever submitted by the HTML UI.
    void draw() {
        if(ready_) runtime_.draw();
    }
    bool active() const {return ready_;}
    bool needsFrame() const {
        return ready_ && (playing_ || pendingRestart_ ||
                          targetTicks_>timeTicks_ || stepsPending_>0);
    }
    uint32_t timeTicks() const {return timeTicks_;}
    Json status() const {
        Json result=Json::object();
        result.set("ready",Json::fromBool(ready_));
        result.set("playing",Json::fromBool(playing_));
        result.set("backend",Json::string(backend_));
        result.set("tick",Json::number(int64_t(timeTicks_)));
        result.set("targetTick",Json::number(int64_t(targetTicks_)));
        result.set("timeSeconds",Json::number(double(timeTicks_)/60.0,3));
        result.set("generation",Json::number(int64_t(generation_)));
        result.set("error",Json::string(error_));
        result.set("gpu",Json::fromBool(ready_&&runtime_.gpuActive()));
        result.set("alive",Json::number(int64_t(ready_?runtime_.gpuStats().alive:0)));
        return result;
    }
    void stop(gfx::Device& device) {
        device.waitForResourceIdle();
        runtime_.shutdown();
        ready_=false;playing_=false;pendingRestart_=false;
        stepsPending_=timeTicks_=targetTicks_=0;
        requestedParameters_.clear();
        backend_.clear();error_.clear();
        ++generation_;
    }
private:
    Json errorStatus(std::string reason) {
        error_=std::move(reason);
        return status();
    }
    spectra::GraphRuntime runtime_;
    std::map<std::string,float> requestedParameters_;
    bool ready_=false,playing_=false,pendingRestart_=false;
    uint32_t timeTicks_=0,targetTicks_=0,stepsPending_=0;
    uint64_t generation_=1;
    std::string backend_,error_;
};

} // namespace aiverse::studio
