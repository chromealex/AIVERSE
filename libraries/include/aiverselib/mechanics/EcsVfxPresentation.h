// AIVERSE SpectraFX: universal ECS-to-VFX presentation bridge, genre-neutral.
// ECS owns data-only effect declarations; renderer owns FxHandles.
// Never write renderer state into World or affect gameplay checksums.
#pragma once
#include "engine/mechanics/Namespace.h"

#include "engine/core/ECS.h"
#include "engine/sim/Simulation.h"
#include "engine/fx/ParticleSystem.h"
#include "engine/fx/SpectraDataInterfaces.h"
#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace aiverse::lib::mechanics {

// Opt-in per-Entity visual effect. Only continuous/looped assets should be
// attached here. One-shots belong to event bindings instead.
struct SpectraSourceComponent {
    std::string effect;           // fx/<effect>.json, or graph compiled with ParticleSystem::define()
    Fixed scale = 1_fx;           // deterministic data, converted to float only by the presentation layer
    uint32_t revision = 0;       // increment to restart the emitter after authoring/property changes
    bool enabled = true;
    bool killOnDetach = false;   // false: existing particles fade naturally after stop()
    std::map<std::string,Fixed> parameters;
};
inline bool resolveSpectraSourceParameters(const SpectraSourceComponent& source,
    const spectra::InstanceParameterTable& table,std::array<float,8>& values){
    values={};
    if(source.parameters.size()>8)return false;
    for(const auto& [name,def]:table){
        if(def.slot>=values.size())return false;
        values[def.slot]=def.initial;
    }
    for(const auto& [name,value]:source.parameters){
        const auto it=table.find(name);
        const float scalar=float(value.toDouble());
        if(it==table.end()||!spectra::checkInstanceParameterValue(it->second,scalar))return false;
        values[it->second.slot]=scalar;
    }
    return true;
}

// Presentation-only mapping: game-specific render pose policy is supplied by
// the native host. Does NOT assume actors, RTS models, FPS cameras or physics.
struct SpectraEventBinding {
    std::string effect;
    Fixed scale = 1_fx;
};

class SpectraPresentationSystem {
public:
    using ResolvePose = std::function<bool(const World&, Entity, fx::float3& position,
                                            fx::float3& direction)>;

    explicit SpectraPresentationSystem(fx::ParticleSystem& particles) : ps_(particles) {}
    SpectraPresentationSystem(const SpectraPresentationSystem&) = delete;
    SpectraPresentationSystem& operator=(const SpectraPresentationSystem&) = delete;

    void bindInstanceParameters(const std::string& effect,spectra::InstanceParameterTable table){
        schemas_[effect]=std::move(table);
        dataBindings_.erase(effect);
    }
    // Register the existing graph-v2 declaration as one validated unit.
    bool bindGraphDataInterfaces(const std::string& effect,const Json& graph,
                                 std::vector<std::string>& errors) {
        spectra::InstanceParameterTable table;
        spectra::DataInterfaceBindings bindings;
        if(!spectra::loadInstanceParameterTable(graph["instanceParameters"],table,errors) ||
           !spectra::loadDataInterfaceBindings(graph["dataInterfaces"],table,bindings,errors))
            return false;
        schemas_[effect]=std::move(table);
        dataBindings_[effect]=std::move(bindings);
        return true;
    }
    // Owner thread, before sync/event submission. Registry outlives use.
    void setDataInterfaceRegistry(const spectra::DataInterfaceRegistry* registry) {
        dataRegistry_=registry;
    }
    const std::string& dataInterfaceError() const {return dataError_;}

    // Called each presentation frame. Does not advance particles or mutate ECS.
    // Call ParticleSystem::setView/update/render from the host's normal view loop.
    void sync(const World& world, const ResolvePose& resolve) {
        dataError_.clear();
        std::unordered_set<uint64_t> present;
        world.eachExistingConst<SpectraSourceComponent>(
            [&](Entity entity, const SpectraSourceComponent& source) {
                if (!world.alive(entity) || !source.enabled || source.effect.empty())
                    return;
                const uint64_t key = entity.packed();
                fx::float3 pos{}, dir{0, 1, 0};
                if (!resolve || !resolve(world, entity, pos, dir)) return;
                std::array<float,8> values{};
                const auto schema=schemas_.find(source.effect);
                if(schema!=schemas_.end()){
                    if(!resolveSpectraSourceParameters(source,schema->second,values))return;
                }else if(!source.parameters.empty())return;
                // Sample once per source per presentation sync. This snapshot
                // feeds subsequent CPU births; already-live particles keep theirs.
                auto sampled=values;
                if(!sample(source.effect,pos,values,sampled))return;
                present.insert(key);
                auto it = attached_.find(key);
                if (it != attached_.end() &&
                    (it->second.effect != source.effect ||
                     it->second.revision != source.revision ||
                     it->second.scaleRaw != source.scale.raw ||
                     it->second.parameters != values)) {
                    release(it->second);
                    attached_.erase(it);
                    it = attached_.end();
                }
                if (it == attached_.end()) {
                    Attached state;
                    state.effect = source.effect;
                    state.scaleRaw = source.scale.raw;
                    state.revision = source.revision;
                    state.killOnDetach = source.killOnDetach;
                    state.parameters=values;
                    state.sampled=sampled;
                    state.handle = ps_.playWithProgramParameters(ps_.effect(source.effect),pos,dir,float(source.scale.toDouble()),{1,1,1,1},sampled);
                    attached_.emplace(key, std::move(state));
                } else {
                    it->second.killOnDetach = source.killOnDetach;
                    auto& state=it->second;
                    if (!state.handle.valid() || !ps_.playing(state.handle)) {
                        state.handle=ps_.playWithProgramParameters(ps_.effect(source.effect),pos,dir,float(source.scale.toDouble()),{1,1,1,1},sampled);
                        state.sampled=sampled;
                    } else {
                        bool accepted=true;
                        for(uint32_t slot=0;slot<8;++slot)
                            if(sampled[slot]!=state.sampled[slot] &&
                               !ps_.setProgramParameter(state.handle,slot,sampled[slot],false)) {
                                accepted=false;break;
                            }
                        if(!accepted) {
                            // Fail closed before the next ParticleSystem update.
                            ps_.stop(state.handle);
                            dataError_="SpectraFX CPU source parameter update rejected";
                        } else {
                            state.sampled=sampled;
                            ps_.move(state.handle,pos,dir);
                        }
                    }
                }
            });
        for (auto it = attached_.begin(); it != attached_.end();) {
            if (present.count(it->first)) { ++it; continue; }
            release(it->second);
            it = attached_.erase(it);
        }
    }

    // A host calls this ONCE after each committed gameplay tick. SimEvent.type
    // remains module-defined; any genre may supply its own binding map.
    // Repeated ticks are ignored, preventing duplicate events during multiple
    // render frames per tick. Rollback/replay requires reset() before replay.
    void consumeTickEvents(uint32_t tick, const std::vector<SimEvent>& events,
                           const std::map<uint16_t, SpectraEventBinding>& bindings) {
        if (haveTick_ && tick <= lastTick_) return;
        haveTick_ = true;
        lastTick_ = tick;
        for (const auto& event : events) {
            auto it = bindings.find(event.type);
            if (it == bindings.end() || it->second.effect.empty()) continue;
            const Vec3& point = event.pos;
            const fx::float3 position{float(point.x.toDouble()),float(point.y.toDouble()),float(point.z.toDouble())};
            std::array<float,8> base{};
            if(const auto schema=schemas_.find(it->second.effect);schema!=schemas_.end()) {
                SpectraSourceComponent defaults;
                if(!resolveSpectraSourceParameters(defaults,schema->second,base))continue;
            }
            auto sampled=base;
            if(!sample(it->second.effect,position,base,sampled))continue;
            const fx::FxHandle handle=ps_.playWithProgramParameters(ps_.effect(it->second.effect),
                position,{0,1,0},float(it->second.scale.toDouble()),{1,1,1,1},sampled);
            if (handle.valid()) {
                // Keep recent one-shot handles so rollback can remove their
                // remaining live particles, even after emission has ended.
                if (oneShots_.size() < kMaxTrackedCues) oneShots_.push_back(handle);
                else {
                    oneShots_[oneShotCursor_] = handle;  // bounded O(1) ring
                    oneShotCursor_ = (oneShotCursor_ + 1) % kMaxTrackedCues;
                }
            }
        }
    }

    // Before a new match or after game-state rollback: kill view handles and
    // allow tick events to be presented from the restored tick again.
    void reset() {
        for (auto& [key, state] : attached_) {
            (void)key;
            ps_.kill(state.handle);
        }
        attached_.clear();
        for (const auto handle : oneShots_) ps_.kill(handle);
        oneShots_.clear();
        oneShotCursor_ = 0;
        haveTick_ = false;
        lastTick_ = 0;
        dataRegistry_=nullptr;
        dataError_.clear();
    }
    size_t activeAttachments() const { return attached_.size(); }
    uint32_t lastPresentedTick() const { return lastTick_; }

private:
    struct Attached {
        fx::FxHandle handle{};
        std::string effect;
        int64_t scaleRaw = 0;
        uint32_t revision = 0;
        bool killOnDetach = false;
        std::array<float,8> parameters{},sampled{};
    };
    bool sample(const std::string& effect,fx::float3 position,
                const std::array<float,8>& base,std::array<float,8>& output) {
        const auto binding=dataBindings_.find(effect);
        if(binding==dataBindings_.end() || binding->second.empty())return true;
        const auto schema=schemas_.find(effect);
        if(schema==schemas_.end())return false;
        std::string error;
        if(!spectra::sampleDataInterfaces(binding->second,schema->second,dataRegistry_,
                                          position,base,output,error)) {
            dataError_=std::move(error);return false;
        }
        return true;
    }
    void release(const Attached& state) {
        if (state.killOnDetach) ps_.kill(state.handle);
        else ps_.stop(state.handle);
    }
    static constexpr size_t kMaxTrackedCues = 65536;
    fx::ParticleSystem& ps_;
    std::map<std::string,spectra::InstanceParameterTable> schemas_;
    std::map<std::string,spectra::DataInterfaceBindings> dataBindings_;
    const spectra::DataInterfaceRegistry* dataRegistry_=nullptr;
    std::string dataError_;
    std::unordered_map<uint64_t, Attached> attached_;
    std::vector<fx::FxHandle> oneShots_;
    size_t oneShotCursor_ = 0;
    uint32_t lastTick_ = 0;
    bool haveTick_ = false;
};


// Early API aliases deliberately refer to the SAME component types, so the
// ECS component type ID and presentation World queries remain identical.
using VfxSourceComponent = SpectraSourceComponent;
using VfxEventBinding = SpectraEventBinding;
using VfxPresentationSystem = SpectraPresentationSystem;

} // namespace aiverse::lib::mechanics
