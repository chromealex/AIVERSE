// SpectraFX preview owned solely by the Studio Scene render thread.
// Native gfx::Device & SpectraGraphRuntime perform the actual simulation and
// depth-tested draw. Browser only sends bounded authoring/control requests.
// No ImGui, no game/ECS mutation and no second GPU device.
#pragma once
#include "engine/fx/SpectraGraphRuntime.h"
#include "engine/fx/SpectraCheckpointTimelineFile.h"
#include "engine/fx/SpectraFX.h"
#include <algorithm>
#include <chrono>
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
        const auto now=std::chrono::steady_clock::now();
        if(importTransferId_&&!fileImportPending_&&now-importTouched_>std::chrono::seconds(30)){importTransferId_=0;fileImport_.clear();}
        if(exportTransferId_&&now-exportTouched_>std::chrono::seconds(30)){exportTransferId_=0;fileExport_.clear();}
        const auto transferId=input["transferId"].integer(-1);
        if(action=="checkpointFileCancel"){
            if(transferId>0&&uint64_t(transferId)==importTransferId_){fileImport_.clear();fileImportPending_=false;importTransferId_=0;}
            else if(transferId>0&&uint64_t(transferId)==exportTransferId_){fileExport_.clear();exportTransferId_=0;}
            else return errorStatus("Unknown transfer ID");
            error_.clear();return status();
        }
        if(action=="checkpointFileRead"){
            if(transferId<=0||uint64_t(transferId)!=exportTransferId_)return errorStatus("Stale export transfer ID");
            exportTouched_=now;
            const auto offset=input["offset"].integer(-1);
            if(offset<0||uint64_t(offset)>fileExport_.size())return errorStatus("Invalid checkpoint offset");
            static constexpr char hex[]="0123456789abcdef";
            std::string chunk;
            const size_t end=std::min(fileExport_.size(),size_t(offset)+size_t(32768));
            for(size_t i=size_t(offset);i<end;++i){chunk+=hex[fileExport_[i]>>4];chunk+=hex[fileExport_[i]&15];}
            Json out=status();out.set("fileChunk",Json::string(chunk));out.set("fileSize",Json::number(int64_t(fileExport_.size())));return out;
        }
        if(action=="checkpointFileBegin"){
            if(!ready_||playing_||checkpointRequest_||rollbackId_||fileImportPending_)return errorStatus("Checkpoint requires settled paused preview");
            if(importTransferId_)return errorStatus("Import transfer already active");
            fileImport_.clear();importTransferId_=nextTransferId_++;importTouched_=now;error_.clear();
            Json out=status();out.set("transferId",Json::number(int64_t(importTransferId_)));return out;
        }
        if(action=="checkpointFileAppend"){
            if(fileImportPending_||transferId<=0||uint64_t(transferId)!=importTransferId_)return errorStatus("Stale or pending import transfer");
            importTouched_=now;
            const auto chunk=input.getStr("chunk");
            if(chunk.empty()||chunk.size()>65536||chunk.size()%2||fileImport_.size()+chunk.size()/2>spectra::kRestoreCheckpointMaxBytes+sizeof(spectra::TimelineCheckpointHeader))return errorStatus("Invalid checkpoint chunk size");
            if(input["offset"].integer(-1)!=int64_t(fileImport_.size()))return errorStatus("Checkpoint chunk offset mismatch");
            auto digit=[](char c)->int{return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:-1;};
            std::vector<uint8_t> decoded;
            for(size_t i=0;i<chunk.size();i+=2){int a=digit(chunk[i]),b=digit(chunk[i+1]);if(a<0||b<0)return errorStatus("Invalid checkpoint hex");decoded.push_back(uint8_t(a*16+b));}
            fileImport_.insert(fileImport_.end(),decoded.begin(),decoded.end());return status();
        }
        if(action=="checkpointFileExport"){
            if(exportTransferId_)return errorStatus("Export transfer already active");
            if(importTransferId_)return errorStatus("Import transfer active");
            if(!exportTimelineCheckpoint(fileExport_))return errorStatus("Save a fresh paused checkpoint first");
            exportTransferId_=nextTransferId_++;exportTouched_=now;error_.clear();
            Json out=status();out.set("transferId",Json::number(int64_t(exportTransferId_)));out.set("fileSize",Json::number(int64_t(fileExport_.size())));return out;
        }
        if(action=="checkpointFileImport"){
            if(transferId<=0||uint64_t(transferId)!=importTransferId_)return errorStatus("Stale import transfer ID");
            if(fileImport_.empty()||fileImportPending_||checkpointRequest_||rollbackId_||playing_)return errorStatus("Import requires paused preview and uploaded bytes");
            fileImportPending_=true;error_.clear();return status();
        }
        if(action=="checkpointCancel"){
            checkpointRequest_=false;rollbackId_=0;checkpointWaitFrames_=0;fileImportPending_=false;fileImport_.clear();importTransferId_=0;
            error_.clear();return status();
        }
        if((checkpointRequest_||rollbackId_||fileImportPending_||importTransferId_)&&action!="stop")
            return errorStatus("Checkpoint operation pending; cancel or wait");

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
            checkpointTicks_.clear();checkpointRequest_=false;rollbackId_=0;fileImportPending_=false;fileImport_.clear();fileExport_.clear();importTransferId_=exportTransferId_=0;
            spectra::GraphRuntimeOptions opts;
            opts.capacity=1024;
            opts.gpuBirthRandom=input.getBool("gpuBirthRandom",false);
            opts.gpuMaterial=opts.gpuBirthRandom;
            opts.gpuProgram=true; // strict GPU VM eligibility, CPU twin otherwise
            opts.preference=spectra::ComputePreference::Automatic;
            if(!runtime_.initGraph(&device,graph,Json::object(),opts)) {
                ready_=false;playing_=false;pendingRestart_=false;restartBurstPending_=false;
        hasPendingGpuAllocationSeed_=false;
                return errorStatus(runtime_.reason().empty()?
                    "SpectraFX preview backend unavailable":runtime_.reason());
            }
            ready_=true;
            replayPrepared_=false;
            playing_=false;
            pendingRestart_=true;restartBurstPending_=true;
            phase_=0;
            pendingClear_=false;
            burstsPending_=0;
            triggeredBursts_=0;
            timeTicks_=0;
            targetTicks_=0;
            stepsPending_=0;
            requestedParameters_.clear();
            hasPendingGpuAllocationSeed_=false;
            gpuSeedFailed_=false;gpuSeedRetryCount_=0;
            backend_=runtime_.gpuActive()?"Metal GPU":"CPU (full effect)";
            error_.clear();
            ++generation_;
            return status();
        }
        if(!ready_)return errorStatus("Load a valid graph before preview playback");
        if(action=="checkpointSave"||action=="checkpointRestore") {
            if(!runtime_.gpuActive()||playing_||pendingRestart_||restartBurstPending_||
               pendingClear_||hasPendingGpuAllocationSeed_||burstsPending_||stepsPending_||
               targetTicks_!=timeTicks_||!requestedParameters_.empty()||checkpointRequest_||rollbackId_)
                return errorStatus("Checkpoint operation requires settled paused Metal preview");
            if(action=="checkpointSave"){checkpointRequest_=true;checkpointWaitFrames_=0;}
            else {
                const auto id=input["checkpointId"].integer(-1);
                if(id<=0||!checkpointTicks_.count(uint64_t(id)))return errorStatus("Unknown checkpoint ID");
                rollbackId_=uint64_t(id);
            }
            error_.clear();replayPrepared_=false;return status();
        }
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
            if(!input["gpuShaderSeed"].isNull() && (!input["seed"].isNull()||!input["gpuAllocationSeed"].isNull()))
                return errorStatus("GPU shader seed must be requested independently");
            if(!input["seed"].isNull() && !input["gpuAllocationSeed"].isNull())
                return errorStatus("CPU seed and GPU allocation seed cannot be combined");
            if(!input["seed"].isNull()) {
                const Json& seed=input["seed"];
                if(!seed.isNumber() || seed.numberLiteral().find_first_of(".eE-")!=std::string::npos ||
                   seed.integer(-1)<0 || seed.integer(-1)>2147483647)
                    return errorStatus("Replay seed must be integer 0..2147483647");
                if(runtime_.backend()!=spectra::ComputeBackend::Cpu)
                    return errorStatus("Seeded replay currently requires CPU backend");
                if(!runtime_.resetDeterministicSeed(uint32_t(seed.integer())))
                    return errorStatus(runtime_.reason());
            }
            if(!input["gpuShaderSeed"].isNull()) {
                const Json& seed=input["gpuShaderSeed"];
                if(!seed.isNumber()||seed.numberLiteral().find_first_of(".eE-")!=std::string::npos||seed.integer(-1)<0||seed.integer(-1)>4294967295LL)
                    return errorStatus("GPU shader seed must be uint32");
                if(!runtime_.gpuActive())return errorStatus("GPU shader seed requires Metal GPU backend");
                pendingGpuAllocationSeed_=uint32_t(seed.integer());
                pendingShaderSeedMode_=true;hasPendingGpuAllocationSeed_=true;
                gpuSeedRetryCount_=0;gpuSeedFailed_=false;
            }
            if(!input["gpuAllocationSeed"].isNull()) {
                pendingShaderSeedMode_=false;
                const Json& seed=input["gpuAllocationSeed"];
                if(!seed.isNumber() || seed.numberLiteral().find_first_of(".eE-")!=std::string::npos ||
                   seed.integer(-1)<0 || seed.integer(-1)>2147483647)
                    return errorStatus("GPU allocation seed must be integer 0..2147483647");
                if(!runtime_.gpuActive())
                    return errorStatus("GPU allocation seed requires Metal GPU backend");
                pendingGpuAllocationSeed_=uint32_t(seed.integer());
                gpuSeedRetryCount_=0;
                gpuSeedFailed_=false;
                hasPendingGpuAllocationSeed_=true;
            }
            playing_=false;phase_=0;pendingRestart_=true;restartBurstPending_=true;
            pendingClear_=false;burstsPending_=0;triggeredBursts_=0;
            stepsPending_=0;timeTicks_=0;
            targetTicks_=uint32_t(input["tick"].integer());
            requestedParameters_=std::move(validated);
            replayPrepared_=true;
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
            replayPrepared_=false;
            timeTicks_=targetTicks_=stepsPending_=0;
            burstsPending_=0;triggeredBursts_=0;
            pendingRestart_=false;restartBurstPending_=false;
            hasPendingGpuAllocationSeed_=false;
            gpuSeedFailed_=false;gpuSeedRetryCount_=0;
            pendingClear_=true;
            error_.clear();
            return status();
        }
        if(action=="play") {error_.clear();replayPrepared_=false;playing_=true;return status();}
        if(action=="pause") {
            playing_=false;targetTicks_=timeTicks_;stepsPending_=0;
            phase_=0;return status();
        }
        if(action=="restart") {
            replayPrepared_=false;
            error_.clear();
            playing_=input.getBool("play",false);
            pendingRestart_=true;restartBurstPending_=true;timeTicks_=0;targetTicks_=0;stepsPending_=0;
            hasPendingGpuAllocationSeed_=false;
            gpuSeedFailed_=false;gpuSeedRetryCount_=0;
            phase_=0;
            pendingClear_=false;
            burstsPending_=0;triggeredBursts_=0;
            return status();
        }
        if(action=="step") {
            replayPrepared_=false;
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
            replayPrepared_=false;
            phase_=0;
            error_.clear();
            targetTicks_=uint32_t(input["tick"].integer());
            stepsPending_=0;
            if(targetTicks_<timeTicks_) {
                pendingRestart_=true;restartBurstPending_=true;
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
        if(fileImportPending_){fileImportPending_=false;importTransferId_=0;auto bytes=std::move(fileImport_);fileImport_.clear();if(!importTimelineCheckpoint(bytes))error_="Checkpoint file restore rejected";return;}
        if(rollbackId_){
            const auto id=std::exchange(rollbackId_,uint64_t(0));
            if(!runtime_.restoreHistoricalGpuCheckpoint(id)){error_="GPU checkpoint restore rejected";return;}
            timeTicks_=targetTicks_=checkpointTicks_.at(id).first;
            triggeredBursts_=checkpointTicks_.at(id).second;
            phase_=0;error_.clear();
        }
        if(checkpointRequest_){
            if(++checkpointWaitFrames_>240){checkpointRequest_=false;error_="GPU checkpoint wait budget exceeded";return;}
            const auto state=runtime_.checkpointStatus();
            if(state.pending)return;
            if(state.restoreEligible&&runtime_.retainFreshGpuCheckpoint()){
                const auto ids=runtime_.historicalGpuCheckpointIds();
                checkpointTicks_[ids.back()]={timeTicks_,triggeredBursts_};
                for(auto it=checkpointTicks_.begin();it!=checkpointTicks_.end();){
                    if(std::find(ids.begin(),ids.end(),it->first)==ids.end())it=checkpointTicks_.erase(it);else ++it;
                }
                checkpointRequest_=false;error_.clear();
            }else if(!runtime_.requestGpuCheckpoint()){
                if(checkpointWaitFrames_>=240){checkpointRequest_=false;error_="GPU checkpoint request rejected after retry budget";}
            }
            return;
        }
        if(pendingClear_) {
            hasPendingGpuAllocationSeed_=false;
            runtime_.clear();
            pendingClear_=false;
        }
        if(hasPendingGpuAllocationSeed_) {
            // GPU buffer reset is legal only within the render-owner frame.
            // Keep the request pending after failure for explicit retry.
            if(!(pendingShaderSeedMode_?runtime_.resetGpuShaderSeed(pendingGpuAllocationSeed_):runtime_.resetGpuAllocationSeed(pendingGpuAllocationSeed_))) {
                playing_=false;
                ++gpuSeedRetryCount_;
                error_=runtime_.reason().empty()?
                    "SpectraFX GPU allocation seed reset failed":runtime_.reason();
                if(gpuSeedRetryCount_>=kMaxGpuSeedRetries) {
                    hasPendingGpuAllocationSeed_=false;
                    gpuSeedFailed_=true;
                }
                return;
            }
            hasPendingGpuAllocationSeed_=false;
            gpuSeedRetryCount_=0;
            gpuSeedFailed_=false;
            error_.clear();
            pendingRestart_=false; // seeded reset already cleared the GPU pool
            timeTicks_=0;
        }
        if(pendingRestart_) {
            runtime_.clear();
            timeTicks_=0;
            pendingRestart_=false;
        }
        // Apply replay uniforms BEFORE the initial native burst. Preserve
        // failed values for diagnosis/retry instead of silently dropping them.
        for(auto it=requestedParameters_.begin();it!=requestedParameters_.end();) {
            if(!runtime_.setNamedProgramParameter(it->first,it->second,true)) {
                playing_=false;
                error_=runtime_.reason().empty()?"SpectraFX replay parameter rejected":runtime_.reason();
                break;
            }
            it=requestedParameters_.erase(it);
        }
        if(!error_.empty())return;
        if(restartBurstPending_) {
            if(!runtime_.burstAt({origin.x,origin.y+1.0f,origin.z})) {
                playing_=false;
                error_=runtime_.reason().empty()?"SpectraFX preview burst rejected":runtime_.reason();
                return;
            }
            restartBurstPending_=false;
            ++triggeredBursts_;
        }
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
                pendingRestart_=true;restartBurstPending_=true;
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
        return ready_ && (fileImportPending_ || checkpointRequest_ || rollbackId_ || playing_ || pendingRestart_ || hasPendingGpuAllocationSeed_ ||
                          restartBurstPending_ || pendingClear_ ||
                          burstsPending_>0 || targetTicks_>timeTicks_ || stepsPending_>0);
    }
    bool exportTimelineCheckpoint(std::vector<uint8_t>& out) const {
        if(!ready_||playing_||checkpointRequest_||rollbackId_||pendingRestart_||restartBurstPending_||
           pendingClear_||hasPendingGpuAllocationSeed_||burstsPending_||stepsPending_||
           targetTicks_!=timeTicks_||!requestedParameters_.empty())return false;
        std::vector<uint8_t> payload;
        return runtime_.exportRestoreCheckpoint(payload)&&spectra::encodeTimelineCheckpoint(payload,timeTicks_,triggeredBursts_,out);
    }
    // Render-owner frame only; callers must not invoke this inside a pass.
    bool importTimelineCheckpoint(const std::vector<uint8_t>& bytes) {
        if(!ready_||playing_||checkpointRequest_||rollbackId_||pendingRestart_||restartBurstPending_||
           pendingClear_||hasPendingGpuAllocationSeed_||burstsPending_||stepsPending_||
           targetTicks_!=timeTicks_||!requestedParameters_.empty())return false;
        uint32_t tick=0,bursts=0;std::vector<uint8_t> payload;
        if(!spectra::decodeTimelineCheckpoint(bytes,tick,bursts,payload)||!runtime_.importRestoreCheckpoint(payload))return false;
        timeTicks_=targetTicks_=tick;triggeredBursts_=bursts;phase_=0;replayPrepared_=false;error_.clear();return true;
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
        result.set("gpuSeedPending",Json::fromBool(hasPendingGpuAllocationSeed_));
        result.set("gpuSeedFailed",Json::fromBool(gpuSeedFailed_));
        result.set("gpuSeedRetries",Json::number(int64_t(gpuSeedRetryCount_)));
        result.set("replayReady",Json::fromBool(replayPrepared_ && ready_ && error_.empty() &&
            !playing_ && !pendingRestart_ && !restartBurstPending_ &&
            !hasPendingGpuAllocationSeed_ && !pendingClear_ &&
            requestedParameters_.empty() && targetTicks_==timeTicks_));
        Json checkpoints=Json::array();
        for(const auto& [id,clock]:checkpointTicks_){Json item=Json::object();item.set("id",Json::number(int64_t(id)));item.set("tick",Json::number(int64_t(clock.first)));checkpoints.push(std::move(item));}
        result.set("checkpoints",std::move(checkpoints));
        result.set("checkpointWaitFrames",Json::number(int64_t(checkpointWaitFrames_)));
        result.set("checkpointPending",Json::fromBool(checkpointRequest_||rollbackId_||fileImportPending_));
        result.set("generation",Json::number(int64_t(generation_)));
        result.set("error",Json::string(error_));
        result.set("gpu",Json::fromBool(ready_&&runtime_.gpuActive()));
        result.set("alive",Json::number(int64_t(ready_?runtime_.gpuStats().alive:0)));
        return result;
    }
    void stop(gfx::Device& device) {
        device.waitForResourceIdle();
        runtime_.shutdown();
        checkpointTicks_.clear();checkpointRequest_=false;rollbackId_=0;
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
    bool ready_=false,playing_=false,pendingRestart_=false,restartBurstPending_=false;
    bool loop_=true;
    bool pendingClear_=false;
    float speed_=1.0f;
    uint32_t phase_=0,burstsPending_=0,triggeredBursts_=0;
    uint32_t timeTicks_=0,targetTicks_=0,stepsPending_=0;
    static constexpr uint32_t kMaxGpuSeedRetries=8;
    uint32_t pendingGpuAllocationSeed_=0,gpuSeedRetryCount_=0;
    bool gpuSeedFailed_=false,replayPrepared_=false;
    bool hasPendingGpuAllocationSeed_=false;
    bool pendingShaderSeedMode_=false;
    std::map<uint64_t,std::pair<uint32_t,uint32_t>> checkpointTicks_;
    std::vector<uint8_t> fileImport_,fileExport_;
    uint64_t nextTransferId_=1,importTransferId_=0,exportTransferId_=0;
    std::chrono::steady_clock::time_point importTouched_{},exportTouched_{};
    bool fileImportPending_=false;
    bool checkpointRequest_=false;
    uint32_t checkpointWaitFrames_=0;
    uint64_t rollbackId_=0;
    uint64_t generation_=1;
    std::string backend_,error_;
};

} // namespace aiverse::studio
