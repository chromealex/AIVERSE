// PHYSIS nature -> SpectraFX CPU particles (fire, embers, smoke, fire light).
// Device-thread adapter for NatureFxPlanner; owns no gfx::Device and never
// touches ECS. Use with the shared scene fx::ParticleSystem (editor gameFx,
// native client), so PRISMA bloom / fog / sorting stay the existing ones.
#pragma once
#include "engine/world/PhysisNatureCues.h"
#include "engine/world/PhysisTornadoCues.h"
#include "engine/fx/ParticleSystem.h"
#include <algorithm>
#include <iterator>
#include <map>
#include <string>
#include <vector>

namespace aiverse::world {

class PhysisNatureFxHost {
public:
    explicit PhysisNatureFxHost(NatureFxPolicy policy = {}) : planner_(policy) {}

    // Resolves fx/physis_flame.json / fx/physis_smoke.json from the project,
    // else defines the built-in effects. Safe to call again after reloadAll().
    // `projectFiles` is the same reader the ParticleSystem uses; without it
    // the built-ins are always used.
    bool init(fx::ParticleSystem& ps, const fx::FileReader& projectFiles = {},
              std::vector<std::string>* errors = nullptr) {
        flameFx_ = resolve(ps, projectFiles, "physis_flame", physisFlameEffectJson(), errors);
        smokeFx_ = resolve(ps, projectFiles, "physis_smoke", physisSmokeEffectJson(), errors);
        tornadoFx_ = resolve(ps, projectFiles, "physis_tornado", physisTornadoEffectJson(), errors);
        debrisFx_ = resolve(ps, projectFiles, "physis_tornado_debris", physisTornadoDebrisEffectJson(), errors);
        layerFx_[std::size_t(TornadoFxLayer::Funnel)] = tornadoFx_;
        layerFx_[std::size_t(TornadoFxLayer::Debris)] = debrisFx_;
        layerFx_[std::size_t(TornadoFxLayer::FireFunnel)] =
            resolve(ps, projectFiles, "physis_tornado_fire", physisTornadoFireEffectJson(), errors);
        layerFx_[std::size_t(TornadoFxLayer::Spray)] =
            resolve(ps, projectFiles, "physis_tornado_spray", physisTornadoSprayEffectJson(), errors);
        layerFx_[std::size_t(TornadoFxLayer::Ember)] =
            resolve(ps, projectFiles, "physis_tornado_ember", physisTornadoEmberEffectJson(), errors);
        layerFx_[std::size_t(TornadoFxLayer::Scar)] =
            resolve(ps, projectFiles, "physis_tornado_scar", physisTornadoScarEffectJson(), errors);
        return flameFx_ >= 0 && smokeFx_ >= 0;   // tornado effects are optional
    }

    // Tornado funnels + flying debris (every render frame; `since` = seconds
    // since the frame's tick, for smooth extrapolation).
    void updateTornadoes(fx::ParticleSystem& ps, const NaturePresentationFrame& frame,
                         float since, double secondsPerTick) {
        if (std::all_of(std::begin(layerFx_), std::end(layerFx_), [](int id) { return id < 0; })) return;
        applyTornado(ps, tornado_.plan(frame, since, secondsPerTick));
    }
    std::size_t activeFunnels() const { return layer(TornadoFxLayer::Funnel).size(); }
    std::size_t activeDebris() const { return layer(TornadoFxLayer::Debris).size(); }
    std::size_t activeTornadoLayer(TornadoFxLayer l) const { return layer(l).size(); }

    // Once per render frame after a new presentation frame (or the same one).
    // windScale maps PHYSIS m/s onto the particle wind force.
    void update(fx::ParticleSystem& ps, const NaturePresentationFrame& frame,
                float windScale = 0.35f) {
        if (flameFx_ < 0 || smokeFx_ < 0) return;
        if (frame.valid)
            ps.setWind({frame.meanWindX * windScale, 0, frame.meanWindZ * windScale});
        apply(ps, planner_.plan(frame));
    }

    // Disable / map change / rollback discontinuity: stop emitting, let the
    // live particles fade (kill=true removes them at once).
    void reset(fx::ParticleSystem& ps, bool kill = false) {
        for (const auto& c : planner_.clear()) {
            auto& map = c.layer == NatureFxLayer::Flame ? flames_ : smoke_;
            const auto it = map.find(c.key);
            if (it == map.end()) continue;
            if (kill) ps.kill(it->second); else ps.stop(it->second);
        }
        flames_.clear(); smoke_.clear();
        for (const auto& c : tornado_.clear()) {
            auto& map = layer(c.layer);
            const auto it = map.find(c.key);
            if (it == map.end()) continue;
            if (kill) ps.kill(it->second); else ps.stop(it->second);
        }
        for (auto& m : tornadoHandles_) m.clear();
        ps.setWind({0, 0, 0});
    }

    std::size_t activeFlames() const { return flames_.size(); }
    std::size_t activeSmoke() const { return smoke_.size(); }

private:
    static int resolve(fx::ParticleSystem& ps, const fx::FileReader& files, const char* name,
                       const char* builtin, std::vector<std::string>* errors) {
        std::string text;
        if (files && files(std::string("fx/") + name + ".json", text)) {
            const int id = ps.effect(name);   // project override (errors stay in ps.errors())
            if (id >= 0) return id;
        }
        fx::EffectDef def;
        std::vector<std::string> parseErrors;
        if (!fx::parseEffect(Json::parse(builtin), std::string("builtin:") + name, def, parseErrors)) {
            if (errors) errors->insert(errors->end(), parseErrors.begin(), parseErrors.end());
            return -1;
        }
        return ps.define(name, def);
    }

    void apply(fx::ParticleSystem& ps, const std::vector<NatureFxCommand>& commands) {
        for (const auto& c : commands) {
            auto& map = c.layer == NatureFxLayer::Flame ? flames_ : smoke_;
            const int effect = c.layer == NatureFxLayer::Flame ? flameFx_ : smokeFx_;
            switch (c.op) {
            case NatureFxOp::Start: {
                if (auto it = map.find(c.key); it != map.end()) ps.stop(it->second);
                const auto h = ps.play(effect, {c.x, c.y, c.z}, {0, 1, 0}, c.scale,
                                       {c.tint[0], c.tint[1], c.tint[2], c.tint[3]});
                if (h.valid()) map[c.key] = h; else map.erase(c.key);
                break;
            }
            case NatureFxOp::Move:
                if (auto it = map.find(c.key); it != map.end()) ps.move(it->second, {c.x, c.y, c.z});
                break;
            case NatureFxOp::Stop:
                if (auto it = map.find(c.key); it != map.end()) { ps.stop(it->second); map.erase(it); }
                break;
            }
        }
    }

    void applyTornado(fx::ParticleSystem& ps, const std::vector<TornadoFxCommand>& commands) {
        for (const auto& c : commands) {
            auto& map = layer(c.layer);
            const int effect = layerFx_[std::size_t(c.layer)];
            switch (c.op) {
            case TornadoFxOp::Start: {
                if (auto it = map.find(c.key); it != map.end()) ps.stop(it->second);
                if (effect < 0) break;
                const auto h = ps.play(effect, {c.x, c.y, c.z}, {0, 1, 0}, c.scale,
                                       {c.tint[0], c.tint[1], c.tint[2], c.tint[3]});
                if (h.valid()) map[c.key] = h; else map.erase(c.key);
                break;
            }
            case TornadoFxOp::Move:
                if (auto it = map.find(c.key); it != map.end()) ps.move(it->second, {c.x, c.y, c.z});
                break;
            case TornadoFxOp::Stop:
                if (auto it = map.find(c.key); it != map.end()) { ps.stop(it->second); map.erase(it); }
                break;
            }
        }
    }

    NatureFxPlanner planner_;
    TornadoFxPlanner tornado_;
    std::map<std::uint64_t, fx::FxHandle>& layer(TornadoFxLayer l) { return tornadoHandles_[std::size_t(l)]; }
    const std::map<std::uint64_t, fx::FxHandle>& layer(TornadoFxLayer l) const { return tornadoHandles_[std::size_t(l)]; }
    int flameFx_ = -1, smokeFx_ = -1, tornadoFx_ = -1, debrisFx_ = -1;
    int layerFx_[kTornadoFxLayers] = {-1, -1, -1, -1, -1, -1};
    std::map<std::uint64_t, fx::FxHandle> flames_, smoke_;
    std::map<std::uint64_t, fx::FxHandle> tornadoHandles_[kTornadoFxLayers];
};

} // namespace aiverse::world
