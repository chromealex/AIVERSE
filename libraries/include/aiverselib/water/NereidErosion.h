// AIVERSE NEREID: deterministic shoreline erosion/deposition policy.
// Pure authoritative fixed/integer mechanics. The host samples ECS water
// on a completed simulation tick, updates its terrain owner and schedules
// collider/navmesh/visual dirty regions. This module never mutates an ECS
// world, WorldScene, render buffers or gameplay from a worker thread.
#pragma once
#include <aiverselib/water/NereidWater.h>
#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace aiverse::lib::nereid {

enum class ShoreSoil : uint8_t {
    Sand=0, Silt=1, Loam=2, Clay=3, Gravel=4, Rock=5
};

// Values are per terrain sample (equal-area cells). "suspended" represents
// sediment temporarily held in that cell's water column; erosion/deposition
// transfers *the same centimetre-equivalent volume* between earth and water.
struct ShoreErosionCell {
    ShoreSoil soil=ShoreSoil::Loam;
    int32_t groundCm=0;
    int32_t bedrockCm=-20000; // ground may never fall below this plane
    uint32_t suspendedCm=0;   // 0..1e6 for bounded mass accounting
    uint32_t erosionCarryMilliCm=0;    // 0..999; preserves sub-cm progress
    uint32_t depositionCarryMilliCm=0; // 0..999
    uint32_t rootBindingPermille=0;    // vegetation root reinforcement 0..1000
};

// This is a *snapshot* produced by NEREID's existing WaterSample query.
// "contactPermille" must be zero outside the real wet bank/splash zone.
// The same surface X/Z may erode at a bank and deposit in a quiet backwater.
// No camera/time/FP source determines simulation results.
struct ShoreErosionForcing {
    Vec3 flow{};              // existing NEREID fixed metres per second
    uint32_t waveEnergyPermille=0; // 0..1000, optional host PHYSIS forcing
    uint32_t wetContactPermille=0; // 0..1000, wet bank fraction
    uint32_t bankSlopePermille=0;  // 0..8000, rise/run
    // Independent from tree/shrub ROOTS. Recovered grass cover, litter and
    // biological crust shield the topsoil from surface shear/wave splash,
    // but do NOT give grass physical anchor roots.
    uint32_t surfaceShieldPermille=0; // 0..1000, stage64 opt-in
};

struct ShoreErosionSettings {
    uint32_t maxCutCmPerStep=8;         // <=100 cm
    uint32_t maxDepositCmPerStep=8;     // <=100 cm
    uint32_t ratePermille=1000;         // speed multiplier, <=10000
};

struct ShoreErosionResult {
    ShoreErosionCell cell{};
    uint32_t removedCm=0;
    uint32_t depositedCm=0;
    bool changed=false;
};

// Tunable material classes; sand/silt erode first, gravel/clay resist,
// rock is an explicit non-erodible boundary. Rates are intentionally bounded.
inline uint32_t shoreCriticalSpeedCmPerSec(ShoreSoil soil) {
    switch(soil) {
        case ShoreSoil::Sand:return 35;
        case ShoreSoil::Silt:return 45;
        case ShoreSoil::Loam:return 85;
        case ShoreSoil::Clay:return 130;
        case ShoreSoil::Gravel:return 165;
        case ShoreSoil::Rock:return UINT32_MAX;
    }
    throw std::invalid_argument("NEREID unknown shore soil");
}

inline void validateShoreErosion(const ShoreErosionCell& cell,
                                const ShoreErosionForcing& forcing,
                                const ShoreErosionSettings& settings) {
    (void)shoreCriticalSpeedCmPerSec(cell.soil);
    if(cell.bedrockCm>cell.groundCm || cell.suspendedCm>1000000u ||
       cell.erosionCarryMilliCm>=1000u ||
       cell.depositionCarryMilliCm>=1000u ||
       cell.rootBindingPermille>1000u ||
       forcing.waveEnergyPermille>1000u ||
       forcing.wetContactPermille>1000u ||
       forcing.bankSlopePermille>8000u ||
       forcing.surfaceShieldPermille>1000u ||
       settings.maxCutCmPerStep>100u ||
       settings.maxDepositCmPerStep>100u ||
       settings.ratePermille>10000u)
        throw std::invalid_argument("NEREID invalid shore erosion parameters");
}

// Quantize fixed m/s to centimetres/s using int64 arithmetic only.
// The 1.5x L-infinity norm is a conservative, cheap flow-magnitude proxy;
// avoid sqrt/float in any authoritative simulation or saved result.
inline uint32_t shoreFlowSpeedCmPerSec(Vec3 flow) {
    const auto cmPerSec=[](Fixed f)->uint64_t{
        const uint64_t raw=f.raw<0 ? 0ull-uint64_t(f.raw):uint64_t(f.raw);
        // Saturate instead of overflowing raw*100 for hostile/supersonic input.
        if(raw>uint64_t(50000)*Fixed::kOne/100)return 50000u;
        return (raw*100u+Fixed::kOne/2u)/Fixed::kOne;
    };
    const uint64_t a=cmPerSec(flow.x),b=cmPerSec(flow.z);
    return uint32_t(std::min<uint64_t>(50000,
        std::max(a,b)+std::min(a,b)/2));
}

// One bounded deterministic simulation tick, dtMs 1..1000. A suspended
// sediment unit is exactly one ground centimetre of equal sample area.
// There is intentionally no authoritative claim of fluid dynamics here:
// currents/waves originate in existing NEREID/PHYSIS components, and this
// conservative bank material policy adds terrain deformation to a host ECS
// system once per simulation tick. A callback/adapter commits results.
inline ShoreErosionResult erodeShore(const ShoreErosionCell& source,
                                    const ShoreErosionForcing& forcing,
                                    uint32_t dtMs,
                                    ShoreErosionSettings settings={}) {
    validateShoreErosion(source,forcing,settings);
    if(dtMs==0 || dtMs>1000)
        throw std::invalid_argument("NEREID shore erosion timestep");
    ShoreErosionResult out;
    out.cell=source;
    if(forcing.wetContactPermille==0)return out;

    const uint32_t speed=shoreFlowSpeedCmPerSec(forcing.flow);
    const uint32_t critical=shoreCriticalSpeedCmPerSec(source.soil);
    const uint32_t excess=speed>critical?speed-critical:0u;
    // Roots protect the bank without changing water velocity. A steep bank
    // erodes more rapidly; wave splash can erode low-flow sand shores.
    const uint32_t shock=excess+
        forcing.waveEnergyPermille/5u+
        forcing.bankSlopePermille/80u;
    const uint64_t exposure=uint64_t(forcing.wetContactPermille)*
                            (1000u-source.rootBindingPermille);
    const uint64_t rate=uint64_t(shock)*settings.ratePermille*exposure*
                        (1000u-forcing.surfaceShieldPermille)/
                        10000000000ull; // milli-cm/second, pure integer
    const uint64_t requested=rate*dtMs/1000u+
                             source.erosionCarryMilliCm;
    const uint32_t potential=uint32_t(std::min<uint64_t>(
        requested/1000u,1000000u));
    const int64_t soilDepth=int64_t(source.groundCm)-source.bedrockCm;
    const uint32_t safeCut=uint32_t(std::min<int64_t>(
        std::max<int64_t>(0,soilDepth),
        std::min<uint32_t>(potential,settings.maxCutCmPerStep)));
    const uint32_t sedimentRoom=1000000u-source.suspendedCm;
    out.removedCm=std::min(safeCut,sedimentRoom);
    out.cell.groundCm-=int32_t(out.removedCm);
    out.cell.suspendedCm+=out.removedCm;
    // A budget cap intentionally drops excess fractional progress rather than
    // accumulating an unbounded "erosion debt" after a huge flood.
    out.cell.erosionCarryMilliCm=uint32_t(requested%1000u);

    // Quiet water deposits carried sediment back onto the exposed shore.
    // Keep this mutually exclusive with active erosion in the same tick.
    if(out.removedCm==0 && source.suspendedCm>0 && speed<critical) {
        const uint64_t depositionRate=uint64_t(critical-speed)*
            forcing.wetContactPermille*settings.ratePermille/100000u;
        const uint64_t request=depositionRate*dtMs/1000u+
            source.depositionCarryMilliCm;
        const uint32_t n=uint32_t(std::min<uint64_t>(
            request/1000u,settings.maxDepositCmPerStep));
        const int64_t space=int64_t(std::numeric_limits<int32_t>::max())-
                            out.cell.groundCm;
        out.depositedCm=uint32_t(std::min<uint64_t>({
            n,source.suspendedCm,uint64_t(std::max<int64_t>(0,space))}));
        out.cell.groundCm+=int32_t(out.depositedCm);
        out.cell.suspendedCm-=out.depositedCm;
        out.cell.depositionCarryMilliCm=uint32_t(request%1000u);
    }
    out.changed=out.removedCm>0 || out.depositedCm>0;
    return out;
}

} // namespace aiverse::lib::nereid
