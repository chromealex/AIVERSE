// Bounded GPU collision feedback: visual-only, historical physical slot IDs.
#pragma once
#include "engine/fx/SpectraFxEmitterEvents.h"
#include <cmath>
#include <cstring>
#include <vector>
#include <functional>
namespace aiverse::spectra {
inline constexpr uint32_t kGpuCollisionEventCapacity=64;
inline constexpr size_t kGpuCollisionEventBytes=(2+8*kGpuCollisionEventCapacity)*sizeof(uint32_t);
enum class GpuCollisionKind:uint32_t { Sphere=1,Sdf=2,Depth=3 };
struct GpuCollisionEvent {
    render::float3 position{},normal{}; // corrected particle centre, not mesh hit point
    GpuCollisionKind kind=GpuCollisionKind::Sphere;
    uint32_t physicalSlot=0; // historical diagnostic, never a live particle/entity handle
};
struct GpuCollisionBatch {
    uint64_t frameSerial=0,revision=0;
    uint32_t dropped=0;
    std::vector<GpuCollisionEvent> events;
};
using GpuCollisionCallback=std::function<void(bool,GpuCollisionBatch)>;
inline bool decodeGpuCollisionBatch(const std::vector<uint8_t>& bytes,uint32_t capacity,
                                    GpuCollisionBatch& batch) {
    if(bytes.size()!=kGpuCollisionEventBytes)return false;
    const auto word=[&](size_t i){uint32_t u;std::memcpy(&u,bytes.data()+i*4,4);return u;};
    const auto scalar=[&](size_t i){float f;const auto u=word(i);std::memcpy(&f,&u,4);return f;};
    const uint32_t count=word(1);
    // At most one contact per slot per step.
    if(count>capacity)return false;
    const uint32_t n=std::min(count,kGpuCollisionEventCapacity);
    std::vector<GpuCollisionEvent> decoded;decoded.reserve(n);
    for(uint32_t i=0;i<n;++i){
        const size_t b=2+size_t(i)*8;
        GpuCollisionEvent e;
        e.position={scalar(b),scalar(b+1),scalar(b+2)};
        e.normal={scalar(b+3),scalar(b+4),scalar(b+5)};
        const uint32_t kind=word(b+6);e.physicalSlot=word(b+7);
        if(kind<1||kind>3||e.physicalSlot>=capacity||
           !std::isfinite(e.position.x)||!std::isfinite(e.position.y)||!std::isfinite(e.position.z)||
           std::abs(e.position.x)>1e7f||std::abs(e.position.y)>1e7f||std::abs(e.position.z)>1e7f||
           !std::isfinite(e.normal.x)||!std::isfinite(e.normal.y)||!std::isfinite(e.normal.z)||
           std::abs(render::length(e.normal)-1.f)>0.02f)return false;
        e.kind=GpuCollisionKind(kind);decoded.push_back(e);
    }
    batch.events=std::move(decoded);batch.dropped=count-n;return true;
}
// Use existing bounded presentation event router, not simulation events.
// Caller supplies a stable presentation key and drains at a safe CPU phase.
inline size_t publishGpuCollisionBatch(const GpuCollisionBatch& batch,uint64_t emitterKey,
                                        fx::SpectraFxEmitterEventRouter& router,size_t maxEvents=64) {
    size_t accepted=0;
    for(size_t i=0;i<std::min(maxEvents,batch.events.size());++i){
        const auto& e=batch.events[i];
        fx::SpectraFxEmitterEventRecord record;
        record.emitterKey=emitterKey;record.type=fx::SpectraFxEmitterEvent::Impact;
        record.marker=uint32_t(e.kind);record.position=e.position;record.direction=e.normal;
        if(router.publish(record))++accepted;
    }
    return accepted;
}
}
