#pragma once
// Opt-in module command adapter; no new network/replay transport.
#include "engine/world/TerraWeaveCavernsEcsAuthority.h"
#include <functional>
#include <optional>

namespace aiverse::terraweave::caverns {
// One wire command = one stamp operation. The owning GameModule reserves
// its own command type. Split unsigned IDs/revisions into two uint32 words
// so the signed int64 argument transport never narrows an identity.
inline std::optional<CaveStampCommand> decodeCaveStampCommand(
    const Command& input,uint16_t commandType) {
    if(input.type!=commandType||input.args.size()!=10)return std::nullopt;
    const auto& a=input.args;
    const auto word=[](int64_t x){return x>=0&&x<=UINT32_MAX;};
    if(!word(a[0])||!word(a[1])||!word(a[3])||!word(a[4])||
       a[2]<0||a[2]>2||a[8]<0||a[8]>1000000||a[9]<0||a[9]>1)
        return std::nullopt;
    const uint64_t revision=uint64_t(a[0])|(uint64_t(a[1])<<32);
    const uint64_t id=uint64_t(a[3])|(uint64_t(a[4])<<32);
    const auto kind=static_cast<CaveStampEditKind>(a[2]);
    if(!revision||(kind==CaveStampEditKind::Add?id!=0:id==0))return std::nullopt;
    if(kind==CaveStampEditKind::Remove){
        if(a[5]||a[6]||a[7]||a[8]||a[9])return std::nullopt;
    }else if(a[5]<-kLimitCm||a[5]>kLimitCm||
             a[6]<-kLimitCm||a[6]>kLimitCm||
             a[7]<-kLimitCm||a[7]>kLimitCm||!a[8])return std::nullopt;
    CaveStampCommand result;
    result.expectedRevision=revision;
    result.edits.push_back({kind,{id,{a[5],a[6],a[7]},int32_t(a[8]),
                                      static_cast<CaveStampMode>(a[9])}});
    return result;
}
using CaveStampPermission=std::function<bool(const World&,const Command&,
                                             const CaveStampCommand&)>;
inline std::optional<CaveAuthorityChange> applyCaveSimulationCommand(
    Simulation& sim,const Command& input,uint16_t commandType,
    const CaveAuthorityConfig& cfg,const CaveStampPermission& authorize) {
    if(!authorize||input.tick!=sim.tick())return std::nullopt;
    auto decoded=decodeCaveStampCommand(input,commandType);
    if(!decoded||!authorize(sim.world(),input,*decoded))return std::nullopt;
    try{
        return applyAuthorizedCaveStampCommand(sim.world(),cfg,*decoded,sim.tick());
    }catch(const std::invalid_argument&){return std::nullopt;}
     catch(const std::out_of_range&){return std::nullopt;}
     catch(const std::length_error&){return std::nullopt;}
}
} // namespace aiverse::terraweave::caverns
