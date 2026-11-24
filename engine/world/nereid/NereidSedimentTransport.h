// NEREID Stage 58: conservative deterministic suspended-sediment advection
// between bank cells. This is NOT a new water solver: NEREID ECS supplies
// stable water-body identity and Fixed X/Z flow for each cell. Equal-area
// cells exchange integer cm-equivalent sediment mass; all updates are
// simultaneous and no soil height is changed by transport itself.
#pragma once
#include "NereidErosion.h"
#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <set>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace aiverse::lib::nereid {

struct SedimentTransportCell {
    int64_t gridX=0,gridZ=0;
    uint32_t suspendedCm=0;   // canonical WaterBankSoilComponent field
    uint32_t carryMicroCm=0;  // persisted 1/1,000,000-cm fractional mass
    uint32_t wetContactPermille=0;
    Vec3 flow{};              // from WaterContactComponent, Fixed m/s
    Entity waterBody=kNullEntity;
};
struct SedimentTransportSettings {
    uint32_t maxTransferCmPerTick=32; // cap erosion bursts and bandwidth
};
struct SedimentTransfer {
    int64_t fromX=0,fromZ=0,toX=0,toZ=0;
    uint32_t amountCm=0;
};
// Directed and generation-safe NEREID confluence/cascade connection. A
// downstream neighbour on a different body is accepted ONLY when NEREID
// explicitly publishes inlet -> outlet linkage, never by spatial guesswork.
struct SedimentWaterLink {
    Entity inlet=kNullEntity,outlet=kNullEntity;
};
struct SedimentTransportStep {
    std::vector<SedimentTransportCell> cells; // sorted by grid (X,Z)
    std::vector<SedimentTransfer> transfers;
    uint64_t totalBeforeCm=0,totalAfterCm=0,totalMovedCm=0;
};

namespace sediment_detail {
inline uint64_t absoluteRaw(int64_t value){
    return value<0 ? uint64_t(0)-uint64_t(value) : uint64_t(value);
}
inline uint32_t absoluteFlowCmPerSec(Fixed flow){
    const uint64_t raw=absoluteRaw(flow.raw);
    const uint64_t speed=raw>50000ull*Fixed::kOne/100 ?
        50000ull:(raw*100ull+Fixed::kOne/2)/Fixed::kOne;
    return uint32_t(speed);
}
} // namespace sediment_detail

inline SedimentTransportStep advectSuspendedSediment(
        std::vector<SedimentTransportCell> input,
        int32_t gridSpacingCm,uint32_t dtMs,
        SedimentTransportSettings settings={},
        std::span<const SedimentWaterLink> linkedBodies={}) {
    if(gridSpacingCm<10 || gridSpacingCm>10000 ||
       dtMs==0 || dtMs>1000 ||
       settings.maxTransferCmPerTick>1000u ||
       input.size()>16384u)
        throw std::invalid_argument("NEREID sediment transport limits");
    if(linkedBodies.size()>1024u)
        throw std::length_error("NEREID sediment water confluence link budget");
    std::set<std::pair<uint64_t,uint64_t>> allowedLinks;
    for(const auto& link:linkedBodies){
        if(!link.inlet.valid() || !link.outlet.valid() ||
           link.inlet==link.outlet)
            throw std::invalid_argument("NEREID invalid sediment water linkage");
        allowedLinks.insert({
            link.inlet.packed(),link.outlet.packed()});
    }
    // Canonical order is independent of ECS allocation / current job order.
    std::sort(input.begin(),input.end(),
        [](const auto& a,const auto& b){
            return a.gridX==b.gridX ?
                a.gridZ<b.gridZ : a.gridX<b.gridX;
        });
    std::map<std::pair<int64_t,int64_t>,size_t> lookup;
    SedimentTransportStep result;
    result.cells=std::move(input);
    for(size_t i=0;i<result.cells.size();++i){
        const auto& cell=result.cells[i];
        if(cell.suspendedCm>1000000u ||
           cell.carryMicroCm>=1000000u ||
           cell.wetContactPermille>1000u ||
           cell.gridX<-100000000 || cell.gridX>100000000 ||
           cell.gridZ<-100000000 || cell.gridZ>100000000 ||
           !lookup.emplace(
               std::make_pair(cell.gridX,cell.gridZ),i).second)
            throw std::invalid_argument("NEREID duplicate/invalid sediment cell");
        result.totalBeforeCm+=cell.suspendedCm;
    }
    std::vector<uint32_t> outgoing(result.cells.size(),0u);
    std::vector<uint32_t> incoming(result.cells.size(),0u);
    for(size_t i=0;i<result.cells.size();++i){
        auto& source=result.cells[i];
        if(!source.waterBody.valid() ||
           !source.wetContactPermille || !source.suspendedCm)
            continue;
        const auto xRaw=source.flow.x.raw,zRaw=source.flow.z.raw;
        const bool alongX=sediment_detail::absoluteRaw(xRaw)>=
                          sediment_detail::absoluteRaw(zRaw);
        const int64_t direction=alongX ?
            (xRaw>0?1:(xRaw<0?-1:0)) :
            (zRaw>0?1:(zRaw<0?-1:0));
        if(!direction)continue;
        const int64_t targetX=source.gridX+(alongX?direction:0);
        const int64_t targetZ=source.gridZ+(alongX?0:direction);
        const auto found=lookup.find({targetX,targetZ});
        if(found==lookup.end())continue;
        const size_t j=found->second;
        const auto& target=result.cells[j];
        if(!target.waterBody.valid() || !target.wetContactPermille ||
           (target.waterBody!=source.waterBody &&
            !allowedLinks.contains({
                source.waterBody.packed(),target.waterBody.packed()})))
            continue;

        // Avoid integer "dead zones" at low flow (e.g. 1 cm/s at a
        // 100cm cell and 16ms tick). Keep sub-centimetre mass in MICRO-cm
        // units, using Fixed raw velocity before rounding to cm/s.
        // 1m/s on a 1m grid over 16ms -> fraction 16000/1e6.
        constexpr uint64_t kMicro=1000000ull;
        const uint64_t raw=sediment_detail::absoluteRaw(
            alongX?source.flow.x.raw:source.flow.z.raw);
        const uint64_t bounded=std::min<uint64_t>(
            raw,50000ull*Fixed::kOne/100);
        const uint64_t fraction=std::min<uint64_t>(kMicro,
            bounded*100ull*dtMs*1000ull/
            (uint64_t(Fixed::kOne)*uint64_t(gridSpacingCm)));
        const uint64_t request=
            uint64_t(source.suspendedCm)*fraction+
            source.carryMicroCm;
        const uint32_t desired=uint32_t(std::min<uint64_t>(
            settings.maxTransferCmPerTick,request/kMicro));
        source.carryMicroCm=uint32_t(request%kMicro);
        // Conservative target capacity, intentionally not counting its
        // simultaneous outgoing transfer as free space. This costs some
        // throughput at a full estuary but guarantees no overflow.
        const uint32_t available=1000000u-target.suspendedCm-
            incoming[j];
        const uint32_t moved=std::min({
            desired,source.suspendedCm,available});
        if(!moved)continue;
        outgoing[i]+=moved;
        incoming[j]+=moved;
        result.totalMovedCm+=moved;
        result.transfers.push_back({
            source.gridX,source.gridZ,targetX,targetZ,moved});
    }
    for(size_t i=0;i<result.cells.size();++i){
        auto& cell=result.cells[i];
        cell.suspendedCm=cell.suspendedCm-outgoing[i]+incoming[i];
        result.totalAfterCm+=cell.suspendedCm;
    }
    if(result.totalAfterCm!=result.totalBeforeCm)
        throw std::logic_error("NEREID sediment mass is not conserved");
    return result;
}

// Persisted separately from bank soil to avoid changing the existing
// WaterBankSoilComponent schema. The native ECS stage stores one value per
// bank-cell entity, NEVER per blade/plant. Register before Simulation setup.
struct WaterBankSedimentTransportComponent {
    uint32_t carryMicroCm=0;
    uint32_t lastTransportTick=UINT32_MAX;
};
inline void io(Archive& a,WaterBankSedimentTransportComponent& c){
    a(c.carryMicroCm,c.lastTransportTick);
}

} // namespace aiverse::lib::nereid
