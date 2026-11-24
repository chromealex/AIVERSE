// NEREID -> TerraWeave soil bridge. Authored water ECS remains the only source
// of current/shoreline contact; this engine helper never owns water bodies.
// One deterministic tick: sample immutable ECS water, evolve local soil,
// commit bounded Earthworks vertex edits, then emit vegetation support events.
#pragma once
#include "engine/world/TerraWeaveEarthworks.h"
#include "engine/world/TerraWeaveRootEvents.h"
#include "engine/world/TerraWeaveGrassCover.h"
#include "engine/world/TerraWeaveErosionZones.h"
#include "engine/world/nereid/NereidErosion.h"
#include <algorithm>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <utility>
#include <vector>

namespace aiverse::terraweave {

// The host derives this probe from the previous completed ECS water tick.
// For a shoreline sample, sample NEREID on the water side of the bank;
// do NOT infer wetness by looking only at ground height vs ocean level.
struct NereidBankProbe {
    EarthVertex vertex{};
    lib::nereid::WaterSample water{};
    lib::nereid::ShoreSoil soil=lib::nereid::ShoreSoil::Loam;
    int32_t bedrockCm=-1000000;
    uint32_t waveEnergyPermille=0;
    uint32_t wetContactPermille=0;
    uint32_t slopePermille=0;
    uint32_t rootBindingPermille=0;
    // Local erodibility (x1000) from authored erosion zones; multiplies the
    // tick's ShoreErosionSettings::ratePermille (result clamped to 10000).
    uint32_t ratePermille=1000;
};

// Authored zones -> probe soil + local coefficient (TerraWeaveErosionZones.h).
// Earth vertices are grid indices: world cm = index * spacingCm.
inline void applyErosionZones(std::vector<NereidBankProbe>& probes,
                              const ErosionZoneSet& zones,int32_t spacingCm) {
    if(zones.empty())return;
    if(spacingCm<=0)throw std::invalid_argument("erosion zones: earth spacing");
    for(auto& probe:probes){
        const auto hit=zones.at(probe.vertex.x*spacingCm,probe.vertex.z*spacingCm);
        if(!hit.inside)continue;
        probe.ratePermille=hit.ratePermille;
        if(hit.soil)probe.soil=*hit.soil;
    }
}

struct NereidBankSampleState {
    EarthVertex vertex{};
    lib::nereid::ShoreErosionCell soil{};
};

struct BankTick {
    EarthworkChange earth{};
    PlantSupportUpdate vegetation{};
    std::vector<GrassCoverChange> grassCoverage{};
    uint64_t completedTick=0;
    uint32_t samples=0;
    uint32_t activeBankSamples=0;
    uint64_t removedCm=0;
    uint64_t depositedCm=0;
};

// Owns only local erosion accumulators. The terrain owner retains Earthworks,
// and a native game retains authoritative NEREID water components + ECS ticks.
class NereidEarthworksBridge {
public:
    static constexpr size_t kMaxSampleCount=16384;
    explicit NereidEarthworksBridge(EarthworksLayer& earth)
        : earth_(earth) {}

    size_t trackedBankSamples() const {return cells_.size();}
    uint64_t lastTick() const {return lastTick_;}
    // The authoritative Earthworks reference is NOT swapped. Use only after
    // staged soil restoration has been validated and committed together.
    void exchangeSnapshotState(NereidEarthworksBridge& other) noexcept {
        cells_.swap(other.cells_);
        std::swap(lastTick_,other.lastTick_);
        std::swap(tickStarted_,other.tickStarted_);
    }
    std::vector<NereidBankSampleState> snapshot() const {
        std::vector<NereidBankSampleState> out;
        out.reserve(cells_.size());
        for(const auto& [key,cell]:cells_)out.push_back({key,cell});
        return out;
    }
    void setSnapshot(const std::vector<NereidBankSampleState>& snapshot,
                     uint64_t lastTick=0) {
        if(snapshot.size()>kMaxSampleCount)
            throw std::length_error("NEREID bank soil snapshot budget");
        std::map<EarthVertex,lib::nereid::ShoreErosionCell> next;
        for(const auto& entry:snapshot) {
            // Validate saved heights against the terrain replayed by the host.
            if(earth_.heightAt(entry.vertex)!=entry.soil.groundCm)
                throw std::invalid_argument("NEREID bank snapshot terrain revision mismatch");
            lib::nereid::validateShoreErosion(
                entry.soil,{},lib::nereid::ShoreErosionSettings{});
            if(!next.emplace(entry.vertex,entry.soil).second)
                throw std::invalid_argument("NEREID duplicated bank sample");
        }
        cells_.swap(next);
        lastTick_=lastTick;
        tickStarted_=lastTick!=0;
    }

    // Strictly one owner-thread call per completed authoritative tick.
    // Callers may pass the SAME world and inputs in replay order and obtain
    // exactly the same changed heights/fall events on all platforms.
    BankTick step(const std::vector<NereidBankProbe>& probes,
                  uint64_t completedTick,uint32_t dtMs,
                  RootSupportRegistry* roots=nullptr,
                  lib::nereid::ShoreErosionSettings options={},
                  GrassCoverRegistry* grass=nullptr) {
        if(tickStarted_ && completedTick<=lastTick_)
            throw std::invalid_argument("NEREID stale/duplicate erosion tick");
        if(probes.size()>kMaxSampleCount)
            throw std::length_error("NEREID bank erosion probe budget");
        if(dtMs==0 || dtMs>1000)
            throw std::invalid_argument("NEREID erosion dtMs");
        // Canonical input order, reject duplicates (double erosion on a
        // single soil sample would make tick composition order-dependent).
        std::map<EarthVertex,NereidBankProbe> ordered;
        for(const auto& input:probes)
            if(!ordered.emplace(input.vertex,input).second)
                throw std::invalid_argument("NEREID duplicated erosion probe");
        if(ordered.size()+cells_.size()>kMaxSampleCount*2u)
            throw std::length_error("NEREID erosion accumulation budget");

        auto next=cells_; // Stage candidate state before touching Earthworks
        std::vector<EarthVertexEdit> edits;
        edits.reserve(probes.size());
        BankTick output;
        output.completedTick=completedTick;
        output.samples=static_cast<uint32_t>(probes.size());
        for(const auto& [key,probe]:ordered) {
            const int32_t before=earth_.heightAt(key);
            lib::nereid::ShoreErosionCell cell{};
            if(const auto it=cells_.find(key);it!=cells_.end())
                cell=it->second;
            cell.groundCm=before;
            cell.bedrockCm=probe.bedrockCm;
            cell.soil=probe.soil;
            cell.rootBindingPermille=probe.rootBindingPermille;

            lib::nereid::ShoreErosionForcing force{};
            // The ECS water sample is a value snapshot from completedTick,
            // never a query against live mutable water objects on a worker.
            if(probe.water.found){
                force.flow=probe.water.flow;
                force.wetContactPermille=probe.wetContactPermille;
                force.waveEnergyPermille=probe.waveEnergyPermille;
                force.bankSlopePermille=probe.slopePermille;
            }
            if(probe.ratePermille>10000u)
                throw std::invalid_argument("NEREID erosion probe rate");
            auto local=options;
            local.ratePermille=uint32_t(std::min<uint64_t>(10000u,
                uint64_t(options.ratePermille)*probe.ratePermille/1000u));
            const auto outcome=lib::nereid::erodeShore(cell,force,dtMs,local);
            next.insert_or_assign(key,outcome.cell);
            if(force.wetContactPermille>0)++output.activeBankSamples;
            output.removedCm+=outcome.removedCm;
            output.depositedCm+=outcome.depositedCm;
            if(outcome.changed)
                edits.push_back({key,before,outcome.cell.groundCm});
        }
        if(next.size()>kMaxSampleCount)
            throw std::length_error("NEREID retained shore sample budget");

        // Atomic terrain transaction. Source materials/carries commit only if
        // all terrain edits pass revision, bedrock and allocation preflights.
        output.earth=earth_.applyEdits(edits,earth_.revision());
        cells_.swap(next);
        lastTick_=completedTick;
        tickStarted_=true;

        if(grass && !output.earth.vertices.empty())
            output.grassCoverage=grass->applyEarthwork(
                output.earth,earth_.spacingCm());
        if(roots && !output.earth.vertices.empty()){
            // Post-commit read of the *same* terrain owner, never a second
            // renderer/physics height map. World boundary throws return an
            // unknown sample, conservatively leaving uncertain plants up.
            const SoilHeightSampler ground=[this](int64_t x,int64_t z)
                    ->std::optional<int32_t> {
                if(x<=-kWorldLimitCm+earth_.spacingCm() ||
                   z<=-kWorldLimitCm+earth_.spacingCm() ||
                   x>=kWorldLimitCm-earth_.spacingCm() ||
                   z>=kWorldLimitCm-earth_.spacingCm())
                    return std::nullopt;
                return earth_.sampleHeightCm(x,z);
            };
            output.vegetation=roots->applyGroundChange(
                ground,output.earth,completedTick);
        }
        return output;
    }

private:
    EarthworksLayer& earth_;
    std::map<EarthVertex,lib::nereid::ShoreErosionCell> cells_;
    uint64_t lastTick_=0;
    bool tickStarted_=false;
};

} // namespace aiverse::terraweave
