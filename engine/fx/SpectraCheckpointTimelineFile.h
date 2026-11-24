#pragma once
#include "SpectraCheckpointFile.h"
namespace aiverse::spectra {
struct TimelineCheckpointHeader {
    uint32_t magic=0x54465053u,version=1,tick=0,bursts=0;
    uint32_t payloadBytes=0,crc=0;
};
static_assert(sizeof(TimelineCheckpointHeader)==24);
inline bool encodeTimelineCheckpoint(const std::vector<uint8_t>& payload,uint32_t tick,uint32_t bursts,std::vector<uint8_t>& out){
    if(payload.empty()||payload.size()>kRestoreCheckpointMaxBytes||tick>3600)return false;
    TimelineCheckpointHeader h;
    h.tick=tick;h.bursts=bursts;h.payloadBytes=uint32_t(payload.size());
    std::vector<uint8_t> bytes(sizeof(h)+payload.size());
    std::memcpy(bytes.data(),&h,sizeof(h));
    std::memcpy(bytes.data()+sizeof(h),payload.data(),payload.size());
    h.crc=gpuCheckpointPayloadCrc32(bytes.data(),bytes.size());
    std::memcpy(bytes.data(),&h,sizeof(h));out.swap(bytes);return true;
}
inline bool decodeTimelineCheckpoint(const std::vector<uint8_t>& bytes,uint32_t& tick,uint32_t& bursts,std::vector<uint8_t>& payload){
    if(bytes.size()<sizeof(TimelineCheckpointHeader)||bytes.size()>sizeof(TimelineCheckpointHeader)+kRestoreCheckpointMaxBytes)return false;
    TimelineCheckpointHeader h{};std::memcpy(&h,bytes.data(),sizeof(h));
    if(h.magic!=0x54465053u||h.version!=1||h.tick>3600||!h.payloadBytes||bytes.size()!=sizeof(h)+h.payloadBytes)return false;
    auto checked=bytes;const auto crc=h.crc;h.crc=0;std::memcpy(checked.data(),&h,sizeof(h));
    if(gpuCheckpointPayloadCrc32(checked.data(),checked.size())!=crc)return false;
    std::vector<uint8_t> next(bytes.begin()+sizeof(h),bytes.end());
    tick=h.tick;bursts=h.bursts;payload.swap(next);return true;
}
}
