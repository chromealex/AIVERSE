// Same-build presentation checkpoint, not an authoritative savegame.
#pragma once
#include "SpectraCheckpointExport.h"
#include <string>
#include <cmath>
namespace aiverse::spectra {
inline constexpr size_t kRestoreCheckpointMaxBytes=1024*1024;
struct RestoreCheckpointHeader {
    uint32_t magic=0x52465053u,version=1,slots=0,freeCount=0;
    uint32_t identityBytes=0,payloadBytes=0,crc=0,reserved=0;
};
static_assert(sizeof(RestoreCheckpointHeader)==32);
inline bool encodeRestoreCheckpoint(const GpuCheckpoint& c,const std::string& identity,std::vector<uint8_t>& out){
    const size_t n=c.slots.size(),f=c.freeSlotOrder.size();
    if(!c.complete||!c.hostStateComplete||!n||n>4096||f>n||
       c.remainingLifetime.size()!=n||c.sourceTokens.size()!=n||identity.empty()||identity.size()>4096)return false;
    RestoreCheckpointHeader h;
    h.slots=uint32_t(n);h.freeCount=uint32_t(f);h.identityBytes=uint32_t(identity.size());
    std::vector<uint8_t> bytes(sizeof(h));
    auto append=[&](const void* p,size_t len){if(!len)return;const auto* b=static_cast<const uint8_t*>(p);bytes.insert(bytes.end(),b,b+len);};
    append(identity.data(),identity.size());
    append(c.slots.data(),n*sizeof(GpuCheckpointParticle));
    append(c.remainingLifetime.data(),n*sizeof(float));
    append(c.sourceTokens.data(),n*sizeof(uint64_t));
    append(c.freeSlotOrder.data(),f*sizeof(uint32_t));
    append(c.birthParameters.data(),8*sizeof(float));
    const GpuCheckpointRngTrailer rng{c.shaderBirthSeed,c.shaderBirthOrdinal,c.shaderBirthSeedEnabled?1u:0u};
    append(&rng,sizeof(rng));
    if(bytes.size()>kRestoreCheckpointMaxBytes)return false;
    h.payloadBytes=uint32_t(bytes.size()-sizeof(h));h.crc=gpuCheckpointPayloadCrc32(bytes.data()+sizeof(h),h.payloadBytes);
    std::memcpy(bytes.data(),&h,sizeof(h));out.swap(bytes);return true;
}
inline bool decodeRestoreCheckpoint(const std::vector<uint8_t>& bytes,const std::string& expectedIdentity,GpuCheckpoint& out){
    if(bytes.size()<sizeof(RestoreCheckpointHeader)||bytes.size()>kRestoreCheckpointMaxBytes)return false;
    RestoreCheckpointHeader h{};std::memcpy(&h,bytes.data(),sizeof(h));
    if(h.magic!=0x52465053u||h.version!=1||h.reserved||!h.slots||h.slots>4096||h.freeCount>h.slots||
       !h.identityBytes||h.identityBytes>4096||h.identityBytes!=expectedIdentity.size())return false;
    const size_t n=h.slots,f=h.freeCount;
    const size_t expected=h.identityBytes+n*(sizeof(GpuCheckpointParticle)+sizeof(float)+sizeof(uint64_t))+f*sizeof(uint32_t)+8*sizeof(float)+sizeof(GpuCheckpointRngTrailer);
    if(h.payloadBytes!=expected||bytes.size()!=sizeof(h)+expected||gpuCheckpointPayloadCrc32(bytes.data()+sizeof(h),expected)!=h.crc)return false;
    size_t pos=sizeof(h);
    if(std::memcmp(bytes.data()+pos,expectedIdentity.data(),h.identityBytes)!=0)return false;
    pos+=h.identityBytes;
    GpuCheckpoint c;
    c.slots.resize(n);c.remainingLifetime.resize(n);c.sourceTokens.resize(n);c.freeSlotOrder.resize(f);
    auto read=[&](void* p,size_t len){if(len)std::memcpy(p,bytes.data()+pos,len);pos+=len;};
    read(c.slots.data(),n*sizeof(GpuCheckpointParticle));read(c.remainingLifetime.data(),n*sizeof(float));
    read(c.sourceTokens.data(),n*sizeof(uint64_t));read(c.freeSlotOrder.data(),f*sizeof(uint32_t));
    read(c.birthParameters.data(),8*sizeof(float));
    GpuCheckpointRngTrailer rng{};read(&rng,sizeof(rng));if(rng.enabled>1)return false;
    std::vector<bool> free(n,false);
    for(auto slot:c.freeSlotOrder){if(slot>=n||free[slot])return false;free[slot]=true;}
    for(size_t i=0;i<n;++i)if(!std::isfinite(c.remainingLifetime[i])||free[i]!=(c.remainingLifetime[i]<=0.f))return false;
    c.shaderBirthSeed=rng.seed;c.shaderBirthOrdinal=rng.ordinal;c.shaderBirthSeedEnabled=rng.enabled!=0;
    c.complete=c.hostStateComplete=true;out=std::move(c);return true;
}
}
