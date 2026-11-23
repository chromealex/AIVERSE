#pragma once
#include "SpectraCheckpointFile.h"
namespace aiverse::spectra {
struct TimelineCheckpointHeader {
    uint32_t magic=0x54465053u,version=1,tick=0,bursts=0,bytes=0,crc=0;
};
static_assert(sizeof(TimelineCheckpointHeader)==24);
inline bool encodeTimelineCheckpoint(const std::vector<uint8_t>& state,uint32_t tick,uint32_t bursts,std::vector<uint8_t>& out){
    if(state.empty()||state.size()>kRestoreCheckpointMaxBytes||tick>3600)return false;
    TimelineCheckpointHeader h;h.tick=tick;h.bursts=bursts;h.bytes=uint32_t(state.size());
    std::vector<uint8_t> next(sizeof(h)+state.size());
    std::memcpy(next.data(),&h,sizeof(h));std::memcpy(next.data()+sizeof(h),state.data(),state.size());
    // Cover clock metadata and payload, with CRC field zeroed.
    h.crc=gpuCheckpointPayloadCrc32(next.data(),next.size());
    std::memcpy(next.data(),&h,sizeof(h));out.swap(next);return true;
}
inline bool decodeTimelineCheckpoint(const std::vector<uint8_t>& input,std::vector<uint8_t>& state,uint32_t& tick,uint32_t& bursts){
    if(input.size()<sizeof(TimelineCheckpointHeader)||input.size()>kRestoreCheckpointMaxBytes+sizeof(TimelineCheckpointHeader))return false;
    TimelineCheckpointHeader h{};std::memcpy(&h,input.data(),sizeof(h));
    if(h.magic!=0x54465053u||h.version!=1||h.tick>3600||!h.bytes||h.bytes!=input.size()-sizeof(h))return false;
    auto copy=input;const auto crc=h.crc;h.crc=0;std::memcpy(copy.data(),&h,sizeof(h));
    if(gpuCheckpointPayloadCrc32(copy.data(),copy.size())!=crc)return false;
    std::vector<uint8_t> next(input.begin()+sizeof(h),input.end());
    state.swap(next);tick=h.tick;bursts=h.bursts;return true;
}
}
