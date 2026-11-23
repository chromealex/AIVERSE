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
        if(action=="playback") {
            // Preview-only playback settings: no graph file or gameplay
            // simulation writes. Apply even before the first graph is loaded.
            if(!input["speed"].isNumber()||
               input["loop"].type()!=Json::Type::Bool)
                return errorStatus("Playback needs numeric speed and boolean loop");
            const double speed=input["speed"].toDouble();
            if(!std::isfinite(speed) || (speed!=0.25 && speed!=0.5 &&
                speed!=1.0 && speed!=2.0 && speed!=4.0))
                return errorStatus("Playback speed must be 0.25, 0.5, 1, 2 or 4");
            speed_=float(speed);
            loop_=input.getBool("loop");
            phase_=0;
            error_.clear();
            return status();
        }
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
            phase_=0;
            pendingClear_=false;
            burstsPending_=0;
            triggeredBursts_=0;
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
        if(action=="replayPrepare") {
            // Validate every input before changing live preview state.
            if(!input["tick"].isNumber() ||
               input["tick"].numberLiteral().find_first_of(".eE-")!=std::string::npos ||
               input["tick"].integer(-1)<0 || input["tick"].integer(-1)>kMaxTicks)
                return errorStatus("Replay tick must be an integer in 0..3600");
            if(!input["parameters"].isObject() || input["parameters"].size()>8)
                return errorStatus("Replay parameters must be an object with at most 8 scalars");
            std::map<std::string,float> validated;
            for(const auto& [name,value]:input["parameters"].members()) {
                const auto it=runtime_.instanceParameters().find(name);
                if(it==runtime_.instanceParameters().end() || !value.isNumber())
                    return errorStatus("Unknown replay parameter: "+name);
                const double n=value.toDouble();
                if(!std::isfinite(n)||n<it->second.minimum||n>it->second.maximum)
                    return errorStatus("Replay parameter outside authored range: "+name);
                validated[name]=float(n);
            }
            playing_=false;phase_=0;pendingRestart_=true;
            pendingClear_=false;burstsPending_=0;triggeredBursts_=0;
            stepsPending_=0;timeTicks_=0;
            targetTicks_=uint32_t(input["tick"].integer());
            requestedParameters_=std::move(validated);
            error_.clear();
            return status();
        }
        if(action=="burst") {
            const auto& raw=input["count"];
            if(!raw.isNull() &&
               (!raw.isNumber() || raw.numberLiteral().find_first_of(".eE-")!=std::string::npos ||
                raw.integer(0)<1 || raw.integer(0)>8))
                return errorStatus("Burst count must be an integer from 1 to 8");
            const auto requested=uint32_t(raw.isNull()?1:raw.integer());
            if(burstsPending_+requested>32)
                return errorStatus("SpectraFX preview burst queue is full");
            burstsPending_+=requested;
            error_.clear();
            return status();
        }
        if(action=="clear") {
            // GPU pool reset happens next frame after beginFrame, not in an
            // arbitrary Studio IPC callback or an open graphics encoder.
            playing_=false;phase_=0;
            timeTicks_=targetTicks_=stepsPending_=0;
            burstsPending_=0;triggeredBursts_=0;
            pendingRestart_=false;
            pendingClear_=true;
            error_.clear();
            return status();
        }
        if(action=="play") {error_.clear();playing_=true;return status();}
        if(action=="pause") {
            playing_=false;targetTicks_=timeTicks_;stepsPending_=0;
            phase_=0;return status();
        }
        if(action=="restart") {
            error_.clear();
            playing_=input.getBool("play",false);
            pendingRestart_=true;timeTicks_=0;targetTicks_=0;stepsPending_=0;
            phase_=0;
            pendingClear_=false;
            burstsPending_=0;triggeredBursts_=0;
            return status();
        }
        if(action=="step") {
            playing_=false;phase_=0;
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
            phase_=0;
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
        if(pendingClear_) {
            runtime_.clear();
            pendingClear_=false;
        }
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
            ++triggeredBursts_;
        }
        for(const auto& [name,value]:requestedParameters_) {
            if(!runtime_.setNamedProgramParameter(name,value,true)) {
                playing_=false;error_=runtime_.reason();break;
            }
        }
        requestedParameters_.clear();
        if(!error_.empty())return;
        // Repeated one-shot triggers use the same validated emitter graph,
        // but create additional real particle bursts without recompilation.
        // The queue is bounded and consumed only inside the device frame.
        const uint32_t count=std::exchange(burstsPending_,0u);
        for(uint32_t i=0;i<count;++i) {
            if(!runtime_.burstAt({origin.x,origin.y+1.0f,origin.z})) {
                playing_=false;phase_=0;
                error_=runtime_.reason().empty()?
                    "SpectraFX one-shot burst rejected":runtime_.reason();
                return;
            }
            ++triggeredBursts_;
        }
        // Fixed 60 Hz physics regardless of UI speed: speed affects only
        // the number of entire simulation ticks executed per native frame.
        // A bounded accumulator supports 1/4 and 1/2 rate without dt drift.
        uint32_t playbackTicks=0;
        if(playing_) {
            phase_+=uint32_t(speed_*4.0f); // quarter-tick units
            playbackTicks=phase_/4;
            phase_%=4;
        }
        const uint32_t remaining=kMaxTicks-timeTicks_;
        const uint32_t requested=playing_?playbackTicks:
            targetTicks_>timeTicks_?
                std::min(kSeekTicksPerFrame,targetTicks_-timeTicks_):
                stepsPending_;
        const uint32_t ticks=std::min(remaining,requested);
        stepsPending_=0;
        for(uint32_t i=0;i<ticks;++i) {
            if(!runtime_.step(kStep)) {
                playing_=false;phase_=0;
                ready_=runtime_.backend()==spectra::ComputeBackend::Cpu ||
                       runtime_.gpuActive();
                error_=runtime_.reason().empty()?
                    "SpectraFX preview simulation failed":runtime_.reason();
                break;
            }
            ++timeTicks_;
        }
        if(timeTicks_>=kMaxTicks) {
            if(playing_ && loop_ && error_.empty()) {
                // Reset at the next frame's safe simulation update point.
                // Never recursively call GPU clear() in an open pass.
                pendingRestart_=true;
                timeTicks_=targetTicks_=0;
                phase_=0;
            } else {
                playing_=false;phase_=0;
            }
        }
    }

    // Draw only inside the SAME still-open native Scene pass (before PRISMA
    // resolves that frame). No RHI work is ever submitted by the HTML UI.
    void draw() {
        if(ready_) runtime_.draw();
    }
    bool active() const {return ready_;}
    bool needsFrame() const {
        return ready_ && (playing_ || pendingRestart_ || pendingClear_ ||
                          burstsPending_>0 || targetTicks_>timeTicks_ || stepsPending_>0);
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
        result.set("speed",Json::number(double(speed_),2));
        result.set("loop",Json::fromBool(loop_));
        result.set("bursts",Json::number(int64_t(triggeredBursts_)));
        result.set("queuedBursts",Json::number(int64_t(burstsPending_)));
        result.set("clearing",Json::fromBool(pendingClear_));
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
        stepsPending_=timeTicks_=targetTicks_=phase_=0;
        pendingClear_=false;
        burstsPending_=triggeredBursts_=0;
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
    bool loop_=true;
    bool pendingClear_=false;
    float speed_=1.0f;
    uint32_t phase_=0,burstsPending_=0,triggeredBursts_=0;
    uint32_t timeTicks_=0,targetTicks_=0,stepsPending_=0;
    uint64_t generation_=1;
    std::string backend_,error_;
};

} // namespace aiverse::studio
