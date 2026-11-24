// PHYSIS nature runtime host (2026-11-22): one object that any game client,
// editor play view or tool calls once per rendered frame. It finds out by
// itself whether the World runs PHYSIS (installed from the map "physis" block,
// see engine/physis/PhysisAutoInstall.cpp) and then drives, read-only:
//   * SpectraFX  — flames, embers, smoke, fire light (PhysisNatureFxHost);
//   * audio      — positional fire loops + wind bed (PhysisNatureAudioHost);
//   * TerraWeave — vegetation sway wind, uniform + spatial lattice (WorldScene);
//   * NEREID     — visual wind on water surfaces (WorldScene);
//   * PRISMA     — physical sky coverage / haze / cloud drift (PhysisSkyBridge),
//                  only when the map already enabled the physical sky.
// Each target is optional (null pointer = skipped) and each part can be
// switched off by data ("physis.presentation", PhysisNatureRuntimeConfig.h).
// Genre independent: no RTS/FPS types; coordinates are scene metres == PHYSIS
// metres. Never installs, advances or writes PHYSIS, TerraWeave or NEREID ECS.
#pragma once
#include "engine/world/PhysisNatureRuntimeConfig.h"
#include "engine/world/PhysisNatureFxHost.h"
#include "engine/world/PhysisNatureAudioHost.h"
#include "engine/world/PhysisSkyBridge.h"
#include "engine/world/NereidPhysisWindBridge.h"
#include "engine/world/WorldScene.h"
#include <cstdio>
#include <memory>
#include <string>

namespace aiverse::world {

class PhysisNatureRuntime {
public:
    struct Targets {
        fx::ParticleSystem* particles = nullptr;
        fx::FileReader files;                 // project files for fx/physis_*.json overrides
        audio::AudioEngine* audio = nullptr;  // bank should contain addPhysisNatureSounds()
        WorldScene* scene = nullptr;          // TerraWeave vegetation + NEREID water
        render::Prisma* prisma = nullptr;     // physical sky
    };
    struct View {
        float focusX = 0, focusY = 0, focusZ = 0; // camera target, scene metres
        float dt = 0;                             // seconds since last frame (0 = paused)
        double secondsPerTick = 0.05;             // simulation tick length
        std::uint64_t sourceToken = 1;            // changes on map load / restore
    };
    struct Stats {
        bool active = false;
        bool rebuilt = false;
        std::uint64_t tick = 0;
        std::size_t flames = 0, smoke = 0, voices = 0;
        std::size_t tornadoes = 0, debris = 0;   // live funnel / debris effects
        std::size_t fireFunnels = 0, sprays = 0, embers = 0, scars = 0;
        float maxFireness = 0, maxSpray = 0;     // of tornadoes in the frame
        float meanWindX = 0, meanWindZ = 0;
    };

    // Reads the map's "physis" block. Returns true when the presentation is on.
    // Malformed presentation values switch it off and are reported in error().
    bool start(const Json& map, const Targets& t) {
        stop(t);
        error_.clear();
        try {
            const auto c = natureRuntimeConfigFromMap(map);
            if (!c || !c->any()) return false;
            config_ = *c;
        } catch (const std::exception& e) {
            error_ = e.what();
            std::fprintf(stderr, "[physis] presentation disabled: %s\n", e.what());
            return false;
        }
        configured_ = true;
        if (t.prisma) authoredSky_ = t.prisma->physicalSky();
        return true;
    }

    // Once per rendered frame, after the simulation advanced and the scene's
    // update; before particles are updated/drawn. Cheap when nothing changed:
    // the frame is rebuilt only on a new tick or when the focus moved a cell.
    Stats update(const World& world, const View& v, const Targets& t) {
        Stats s;
        const auto* state = world.resourceConst<physis::State>();
        if (!configured_ || !state) return s;
        s.active = true;
        s.tick = state->tick;
        if (state->tick != lastTick_) { lastTick_ = state->tick; sinceTick_ = 0; }
        else if (std::isfinite(v.dt) && v.dt > 0) sinceTick_ += v.dt;
        const float cs = float(std::max(1, state->settings.cellSizeMeters));
        const float moveLimit = std::max(4.f, cs);
        if (!frame_ || frame_->tick != state->tick ||
            std::abs(v.focusX - frameX_) > moveLimit || std::abs(v.focusZ - frameZ_) > moveLimit) {
            NaturePresentationPolicy p;
            p.centerX = std::isfinite(v.focusX) ? v.focusX : 0.f;
            p.centerZ = std::isfinite(v.focusZ) ? v.focusZ : 0.f;
            p.radiusMeters = config_.radiusMeters;
            p.maxRegionCells = config_.maxRegionCells;
            try {
                frame_ = std::make_shared<const NaturePresentationFrame>(
                    buildNaturePresentation(world, p));
            } catch (const std::exception& e) {
                error_ = e.what();
                frame_.reset();
                return s;
            }
            frameX_ = p.centerX; frameZ_ = p.centerZ;
            s.rebuilt = true;
            if (frame_->valid) publishWind(world, *frame_, v, t);
        }
        if (!frame_ || !frame_->valid) return s;
        s.meanWindX = frame_->meanWindX;
        s.meanWindZ = frame_->meanWindZ;

        if (config_.fx && t.particles) {
            if (!fxTried_) {
                fxTried_ = true;
                std::vector<std::string> errors;
                fxReady_ = fx_.init(*t.particles, t.files, &errors);
                if (!fxReady_ && !errors.empty()) error_ = "fx: " + errors.front();
            }
            if (fxReady_) fx_.update(*t.particles, *frame_, config_.fxWindScale);
            if (fxReady_ && config_.tornado)
                fx_.updateTornadoes(*t.particles, *frame_, sinceTick_, v.secondsPerTick);
        }
        if (config_.audio && t.audio)
            audio_.update(*t.audio, *frame_, v.focusX, v.focusY, v.focusZ, std::max(0.f, v.dt));
        if (config_.sky && t.prisma && authoredSky_.enabled) {
            const Vec3 at{Fixed::fromInt(std::int64_t(std::lround(frameX_))), Fixed{},
                          Fixed::fromInt(std::int64_t(std::lround(frameZ_)))};
            applyPhysisSkyFrame(*t.prisma,
                sky_.update(world, at, v.sourceToken, v.secondsPerTick, authoredSky_));
        }
        s.flames = fx_.activeFlames();
        s.smoke = fx_.activeSmoke();
        s.voices = audio_.voices();
        s.tornadoes = fx_.activeFunnels();
        s.debris = fx_.activeDebris();
        s.fireFunnels = fx_.activeTornadoLayer(TornadoFxLayer::FireFunnel);
        s.sprays = fx_.activeTornadoLayer(TornadoFxLayer::Spray);
        s.embers = fx_.activeTornadoLayer(TornadoFxLayer::Ember);
        s.scars = fx_.activeTornadoLayer(TornadoFxLayer::Scar);
        for (const auto& t : frame_->tornadoes) {
            s.maxFireness = std::max(s.maxFireness, t.fireness);
            s.maxSpray = std::max(s.maxSpray, t.spray);
        }
        return s;
    }

    // Map unload / menu / disconnect: stops effects and sounds, restores the
    // authored sky and clears published wind.
    void stop(const Targets& t) {
        if (t.particles && fxReady_) fx_.reset(*t.particles);
        if (t.audio) audio_.reset(*t.audio);
        if (configured_ && t.scene) {
            // Only the visual wind we published; authored NEREID wind zones stay.
            if (config_.waterWind && haveWater_) t.scene->setNereidVisualWind({});
            if (config_.vegetationWind && haveWind_) {
                t.scene->setVegetationWind(authoredWind_);   // what the map had before
                t.scene->setVegetationWindField(nullptr);
            }
        }
        if (configured_ && t.prisma && config_.sky && authoredSky_.enabled) {
            t.prisma->setPhysicalSky(authoredSky_);
            t.prisma->clearPhysicalSkyAdvection();
        }
        sky_.reset();
        frame_.reset();
        lastField_.reset();
        configured_ = fxReady_ = fxTried_ = false;
        lastTick_ = ~std::uint64_t(0); sinceTick_ = 0;
        haveWind_ = haveWater_ = false;
    }

    bool configured() const { return configured_; }
    const NatureRuntimeConfig& config() const { return config_; }
    const std::string& error() const { return error_; }
    std::shared_ptr<const NaturePresentationFrame> frame() const { return frame_; }

private:
    void publishWind(const World& world, const NaturePresentationFrame& f, const View& v,
                     const Targets& t) {
        if (!t.scene) return;
        if (config_.vegetationWind) {
            if (!haveWind_) authoredWind_ = t.scene->vegetationWind();
            const auto wind = forestWindFromNature(f, authoredWind_);
            if (!haveWind_ || !(wind == lastWind_)) {
                t.scene->setVegetationWind(wind);
                lastWind_ = wind; haveWind_ = true;
            }
            auto field = forestWindFieldFromNature(f, config_.windFieldNodes);
            if (!sameForestWindField(field.get(), lastField_.get())) {
                t.scene->setVegetationWindField(field);
                lastField_ = std::move(field);
            }
        }
        if (config_.waterWind) {
            physis::NatureFieldSample sample;
            const Vec3 at{Fixed::fromInt(std::int64_t(std::lround(v.focusX))), Fixed{},
                          Fixed::fromInt(std::int64_t(std::lround(v.focusZ)))};
            if (physis::sampleNatureField(world, at, sample)) {
                // A tornado near the view roughens the water even when the
                // camera focus is outside its ring (one uniform water wind).
                tornadoWaterWind(f, v, sample.windVelocity);
                const float speed = float(std::hypot(sample.windVelocity.x.toDouble(),
                                                     sample.windVelocity.z.toDouble()));
                const Fixed gust = Fixed::fromRatio(
                    std::int64_t(nature_runtime_detail::gustPermille(speed)), 1000);
                applyNereidPhysisWindSample(*t.scene, sample.windVelocity, gust);
                haveWater_ = true;
            }
        }
    }

    static void tornadoWaterWind(const NaturePresentationFrame& f, const View& v, Vec3& wind) {
        double best = std::hypot(wind.x.toDouble(), wind.z.toDouble());
        for (const auto& t : f.tornadoes) {
            const double dx = double(v.focusX - t.x), dz = double(v.focusZ - t.z);
            const double d = std::hypot(dx, dz), reach = double(t.outerRadius) * 3.0;
            if (!(reach > 0) || d >= reach) continue;
            const double speed = double(t.tangentialWind) * (1.0 - d / reach);
            if (speed <= best) continue;
            // Tangent of the swirl at the focus (counter-clockwise around +Y).
            double tx = d > 1e-3 ? -dz / d : 1.0, tz = d > 1e-3 ? dx / d : 0.0;
            if (t.clockwise) { tx = -tx; tz = -tz; }
            const auto fx = [](double m) { return Fixed::fromRaw(std::int64_t(std::llround(m * double(Fixed::kOne)))); };
            wind = {fx(tx * speed), wind.y, fx(tz * speed)};
            best = speed;
        }
    }

    NatureRuntimeConfig config_{};
    std::uint64_t lastTick_ = ~std::uint64_t(0);
    float sinceTick_ = 0;
    bool configured_ = false;
    std::string error_;
    std::shared_ptr<const NaturePresentationFrame> frame_;
    float frameX_ = 0, frameZ_ = 0;
    PhysisNatureFxHost fx_;
    bool fxReady_ = false, fxTried_ = false;
    PhysisNatureAudioHost audio_;
    PhysisSkyBinding sky_;
    render::PhysicalSkySettings authoredSky_{};
    terraweave::ForestWindState lastWind_{}, authoredWind_{};
    bool haveWind_ = false, haveWater_ = false;
    std::shared_ptr<const terraweave::ForestWindField> lastField_;
};

} // namespace aiverse::world
