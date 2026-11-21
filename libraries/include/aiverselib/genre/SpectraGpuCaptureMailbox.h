// SpectraFX Stage 43 — UI-to-native-renderer capture request mailbox.
// No gfx::Device access, GPU handles, timers, threads or ECS mutations.
// All methods run on the SAME presentation/Editor owner thread. The editor
// may write request intent; only the native host may execute GPU commands.
#pragma once

#include "SpectraGpuCaptureHost.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

namespace aiverse::spectra {

enum class GpuCaptureMailboxPhase : uint8_t {
    Idle, Requested, Claimed, InFlight, Ready, Failed, ReleaseRequested
};

struct GpuCaptureMailboxStatus {
    GpuCaptureMailboxPhase phase=GpuCaptureMailboxPhase::Idle;
    uint64_t sequence=0;
    size_t groupIndex=0;
    uint64_t capturedRevision=0;
    size_t bytesAvailable=0;
    std::string reason;
};

class GpuCaptureRequestMailbox final {
public:
    GpuCaptureRequestMailbox()=default;
    GpuCaptureRequestMailbox(const GpuCaptureRequestMailbox&)=delete;
    GpuCaptureRequestMailbox& operator=(const GpuCaptureRequestMailbox&)=delete;

    bool request(size_t group) {
        if(status_.phase!=GpuCaptureMailboxPhase::Idle &&
           status_.phase!=GpuCaptureMailboxPhase::Failed)return false;
        status_={};
        status_.phase=GpuCaptureMailboxPhase::Requested;
        status_.sequence=++lastSequence_;
        status_.groupIndex=group;
        return true;
    }

    // Called ONLY by the host at its safe pre-pass, NEVER by ImGui itself.
    bool claimRequested(uint64_t& sequence,size_t& group) {
        if(status_.phase!=GpuCaptureMailboxPhase::Requested)return false;
        status_.phase=GpuCaptureMailboxPhase::Claimed;
        sequence=status_.sequence;
        group=status_.groupIndex;
        return true;
    }
    void publishFailure(uint64_t sequence,std::string reason) {
        if(status_.sequence!=sequence ||
           status_.phase==GpuCaptureMailboxPhase::ReleaseRequested)return;
        status_.phase=GpuCaptureMailboxPhase::Failed;
        status_.reason=std::move(reason);
        if(status_.reason.size()>256)status_.reason.resize(256);
    }
    void publishFromCapture(uint64_t sequence,const GpuCaptureStatus& capture) {
        if(status_.sequence!=sequence ||
           status_.phase==GpuCaptureMailboxPhase::ReleaseRequested ||
           status_.phase==GpuCaptureMailboxPhase::Idle)return;
        switch(capture.phase) {
        case GpuCapturePhase::Idle:
            return;
        case GpuCapturePhase::Queued:
        case GpuCapturePhase::InFlight:
            status_.phase=GpuCaptureMailboxPhase::InFlight;
            break;
        case GpuCapturePhase::Ready:
            status_.phase=GpuCaptureMailboxPhase::Ready;
            status_.capturedRevision=capture.capturedRevision;
            status_.bytesAvailable=capture.payloadBytes;
            status_.reason.clear();
            break;
        case GpuCapturePhase::Failed:
            publishFailure(sequence,capture.reason);
            break;
        }
    }

    // UI asks to discard or resume only; the native host owns the actual
    // capture controller and cancels it on the next safe owner-thread phase.
    bool requestRelease() {
        if(status_.phase!=GpuCaptureMailboxPhase::Ready &&
           status_.phase!=GpuCaptureMailboxPhase::Failed &&
           status_.phase!=GpuCaptureMailboxPhase::Requested &&
           status_.phase!=GpuCaptureMailboxPhase::InFlight)return false;
        status_.phase=GpuCaptureMailboxPhase::ReleaseRequested;
        return true;
    }
    bool consumeRelease() {
        if(status_.phase!=GpuCaptureMailboxPhase::ReleaseRequested)return false;
        status_={};
        return true;
    }
    void clearFromHost() {status_={};}
    const GpuCaptureMailboxStatus& status() const {return status_;}

private:
    GpuCaptureMailboxStatus status_{};
    uint64_t lastSequence_=0;
};

} // namespace aiverse::spectra
