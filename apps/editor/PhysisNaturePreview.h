// Editor «Живая природа» (2026-11-21): the real PHYSIS wildfire/wind picture in
// the Scene view WITHOUT starting the game. Fire spreads across TerraWeave
// grass, wind bends grass/flames/smoke, burned ground stays charred, and the
// fire and wind are audible.
//
// Sources (never both):
//   * Live: the running Simulation already has PHYSIS -> shown READ-ONLY,
//     no ticking, no ignition (live game ECS is never mutated by the editor).
//   * Preview: a PRIVATE aiverse::World built from the map's authored
//     `physis` block (climate, authored wind/storm/gravity fields) at a
//     detailed preview resolution, plus a prevailing wind the user steers.
//     It is presentation scaffolding, not the game simulation, and is
//     never saved or sent anywhere.
//
// Rendering reuses the shared paths: TerraWeave GrassPatchRenderer (grass
// overlay), the editor's scene fx::ParticleSystem (SpectraFX CPU particles +
// fire lights), PRISMA for bloom. Audio uses a preview-owned AudioEngine.
#pragma once
#include "apps/editor/core/MapDoc.h"
#include "apps/editor/AuthoredWaterQuery.h"
#include "apps/editor/PhysisAuthoring.h"
#include "apps/editor/TerraWeavePlacementMask.h"
#include "engine/physis/PhysisProjectConfig.h"
#include "engine/physis/PhysisStorm.h"
#include "engine/physis/PhysisClimate.h"
#include "engine/world/PhysisNatureFxHost.h"
#include "engine/world/PhysisNatureAudioHost.h"
#include "engine/world/PhysisNatureGrass.h"
#include "engine/render/Math3D.h"
#include <optional>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace aiverse::editor {

struct PhysisNaturePreviewSettings {
    bool enabled = false;
    bool playing = true;          // private preview only
    bool igniteOnClick = false;   // LMB in the Scene ignites (private preview)
    float speed = 1;              // simulated ticks multiplier
    float ticksPerSecond = 8;
    float windDirectionDeg = 0;   // direction the wind blows TOWARDS, 0 = +X
    float windSpeed = 9;          // m/s
    float humidity = 0.12f;       // 0..1 base humidity of the private preview
    int previewCellMeters = 4;    // 0 = the map's authored PHYSIS grid
    float radiusMeters = 110;     // presentation region around the camera
    bool grass = true, flames = true, sound = true;
    int grassQuality = 1;         // 0 low, 1 medium, 2 high
    float shoreSandMeters = 7;    // bare sand band along authored water: no grass, no fuel
    float volume = 0.8f;
};

class PhysisNaturePreview {
public:
    using HeightAt = std::function<float(float x, float z)>;
    using Retire = std::function<void(std::unique_ptr<render::Model>&)>;
    using Reader = std::function<bool(const std::string&, std::string&)>;

    PhysisNaturePreviewSettings settings;

    bool usingLive() const { return usingLive_; }
    bool hasPreviewWorld() const { return bool(world_); }
    // Private preview World for read-only diagnostics (heatmap overlay).
    const World* previewWorld() const { return settings.enabled && !usingLive_ ? world_.get() : nullptr; }
    const std::string& status() const { return status_; }
    std::uint64_t tick() const { return frame_ ? frame_->tick : 0; }
    std::size_t flames() const { return fxHost_.activeFlames(); }
    std::size_t smoke() const { return fxHost_.activeSmoke(); }
    std::size_t voices() const { return audioHost_.voices(); }
    std::size_t grassPatches() const { return grass_.residentPatches(); }
    float burningArea() const { return frame_ ? frame_->burningArea : 0; }
    float cellMeters() const { return frame_ ? frame_->cellSize : 0; }
    const world::NaturePresentationFrame* frame() const { return frame_.get(); }

    void requestReset() { resetRequested_ = true; }
    // Authored NEREID water (AuthoredWaterQuery::fromDoc): grass and fuel stay
    // out of it. A changed revision rebuilds the preview World and grass.
    void setWater(AuthoredWaterQuery water) {
        if (water.revision() != water_.revision()) {
            water_ = std::move(water);
            grassFilterDirty_ = true;
        }
    }

    // PHYSIS wind at the camera target for every model with hierarchical wind
    // masks (Renderer3D::setSceneWind). Zero when the preview is off.
    render::wind::SceneWind sceneWind(double seconds) const {
        render::wind::SceneWind w;
        if (!settings.enabled || !frame_ || !frame_->valid) return w;
        const auto at = frame_->sample(frameX_, frameZ_);
        w.x = at.valid ? at.windX : frame_->meanWindX;
        w.z = at.valid ? at.windZ : frame_->meanWindZ;
        w.seconds = float(std::fmod(seconds, 3600.0));
        w.gust = std::clamp(frame_->maxWindSpeed / 30.f, 0.f, 1.f);
        return w;
    }

    // Ignite at scene metres (private preview only). Returns false + reason.
    bool ignite(float x, float z, std::string* why = nullptr) {
        if (usingLive_) { if (why) *why = "live game simulation is read-only"; return false; }
        if (!world_) { if (why) *why = "nature preview is not running"; return false; }
        const auto* state = world_->resourceConst<physis::State>();
        if (!state || !std::isfinite(x) || !std::isfinite(z)) { if (why) *why = "outside PHYSIS grid"; return false; }
        const float cs = float(state->settings.cellSizeMeters);
        const auto cx = std::int32_t(std::floor(x / cs)), cz = std::int32_t(std::floor(z / cs));
        bool any = false;
        // A small ignition patch looks like a real start (not one cell).
        for (int dz = -1; dz <= 1; ++dz)
            for (int dx = -1; dx <= 1; ++dx)
                any = physis::ignite(*world_, cx + dx, cz + dz,
                    (dx == 0 && dz == 0) ? Fixed::fromInt(1) : Fixed::fromRatio(1, 2)) || any;
        if (!any && why) *why = "nothing to burn here (outside grid or already burned)";
        frameDirty_ = true;
        return any;
    }

    // Wind sliders changed: steer the private preview's prevailing wind.
    void applyWindSettings() {
        if (!world_) return;
        const auto wind = windVector();
        world_->eachExisting<physis::ClimateZone>([&](Entity, physis::ClimateZone& z) {
            z.prevailingWind = wind;
            z.humidityOffset = Fixed{};
        });
    }

    // Between Renderer3D::beginScene and render. `live` may be null.
    void draw(gfx::Device& device, render::Renderer3D& renderer, fx::ParticleSystem& ps,
              const Reader& reader, const MapDoc& doc, const World* live,
              const render::RTSCamera& cam, double timeSeconds, const HeightAt& heightAt,
              const Retire& retire) {
        if (!settings.enabled) { if (active_) release(device, ps); return; }
        if (!heightAt) return;
        active_ = true;
        const float dt = lastTime_ < 0 ? 0.f
            : float(std::clamp(timeSeconds - lastTime_, 0.0, 0.25));
        lastTime_ = timeSeconds;

        const World* source = prepareSource(ps, doc, live, heightAt);
        if (!usingLive_ && settings.playing && world_) {
            accumulator_ += dt * std::clamp(settings.speed, 0.f, 8.f) *
                            std::clamp(settings.ticksPerSecond, 1.f, 60.f);
            int steps = 0;
            while (accumulator_ >= 1.f && steps < 8) {
                stepPreview();
                accumulator_ -= 1.f; ++steps; frameDirty_ = true;
            }
            if (steps == 8) accumulator_ = 0;
        }
        if (!source) return;

        // ---- presentation frame (rebuilt per tick or when the camera moved)
        const auto* state = source->resourceConst<physis::State>();
        const float cs = state ? float(state->settings.cellSizeMeters) : 1.f;
        const float cx = cam.target.x, cz = cam.target.z;
        if (!frame_ || frameDirty_ || frame_->tick != (state ? state->tick : 0) ||
            std::abs(cx - frameX_) > std::max(4.f, cs) || std::abs(cz - frameZ_) > std::max(4.f, cs)) {
            world::NaturePresentationPolicy p;
            p.centerX = cx; p.centerZ = cz;
            p.radiusMeters = std::clamp(settings.radiusMeters, 30.f, 400.f);
            p.groundHeight = heightAt;
            p.maxRegionCells = 128;
            try {
                frame_ = std::make_shared<const world::NaturePresentationFrame>(
                    world::buildNaturePresentation(*source, p));
                status_.clear();
            } catch (const std::exception& e) {
                status_ = e.what();
                frame_.reset();
                return;
            }
            frameX_ = cx; frameZ_ = cz;
            frameDirty_ = false;
            if (settings.grass) world::applyPhysisGrassPresentation(grass_, frame_);
        }
        if (!frame_ || !frame_->valid) return;

        // ---- grass (TerraWeave patches around the camera, editor-only)
        if (settings.grass) {
            if (retire && !retireSet_) { grass_.setModelRetirement(retire); retireSet_ = true; }
            drawGrass(device, renderer, doc, cx, cz, heightAt, timeSeconds);
        } else if (grass_.residentPatches()) grass_.shutdown(device);

        // ---- SpectraFX flames, embers, smoke, fire light
        if (settings.flames) {
            if (!fxReady_) {
                std::vector<std::string> errors;
                fxReady_ = fxHost_.init(ps, reader, &errors);
                if (!fxReady_ && !errors.empty()) status_ = "fx: " + errors.front();
            }
            if (fxReady_) fxHost_.update(ps, *frame_);
        } else if (fxHost_.activeFlames() || fxHost_.activeSmoke()) fxHost_.reset(ps);

        // ---- audio
        if (settings.sound) {
            ensureAudio(reader);
            if (audio_) {
                const auto eye = cam.eye();
                audio::ListenerPose pose;
                pose.pos[0] = eye.x; pose.pos[1] = eye.y; pose.pos[2] = eye.z;
                float fx = cam.target.x - eye.x, fy = cam.target.y - eye.y, fz = cam.target.z - eye.z;
                const float len = std::sqrt(fx * fx + fy * fy + fz * fz);
                if (len > 1e-3f) { pose.forward[0] = fx / len; pose.forward[1] = fy / len; pose.forward[2] = fz / len; }
                pose.hearing = std::max(1.f, cam.distance / 30.f);
                audio_->setListener(pose);
                audio_->setMasterVolume(std::clamp(settings.volume, 0.f, 1.f));
                // Fire is heard from the camera target (what you look at),
                // positional panning uses the real eye pose.
                audioHost_.update(*audio_, *frame_, cam.target.x, cam.target.y, cam.target.z, dt);
                audio_->update(dt);
            }
        } else if (audio_) { audioHost_.reset(*audio_); audio_->update(dt); }
    }

    // Chooses live (read-only) vs private preview World and (re)builds the
    // preview when the map's PHYSIS block, size or detail changed. No GPU work.
    const World* prepareSource(fx::ParticleSystem& ps, const MapDoc& doc, const World* live,
                               const HeightAt& heightAt) {
        const bool liveHasPhysis = live && live->resourceConst<physis::State>();
        if (liveHasPhysis != usingLive_) { frameDirty_ = true; resetPresentation(ps); }
        usingLive_ = liveHasPhysis;
        if (usingLive_) return live;
        const std::string key = configKey(doc);
        if (!world_ || resetRequested_ || key != builtKey_) {
            rebuild(doc, heightAt);
            builtKey_ = key;
            resetRequested_ = false;
            resetPresentation(ps);
        }
        return world_.get();
    }

    // Advance the private preview by N committed ticks (agents/tests).
    int advance(int ticks) {
        if (usingLive_ || !world_) return 0;
        ticks = std::clamp(ticks, 0, 600);
        for (int i = 0; i < ticks; ++i) stepPreview();
        if (ticks) frameDirty_ = true;
        return ticks;
    }

    void release(gfx::Device& device, fx::ParticleSystem& ps) {
        resetPresentation(ps);
        grass_.shutdown(device);
        if (audio_) { audioHost_.reset(*audio_); audio_->shutdown(); audio_.reset(); }
        world_.reset();
        frame_.reset();
        fxReady_ = false;
        active_ = false;
        lastTime_ = -1;
        builtKey_.clear();
    }

private:
    Vec2 windVector() const {
        const double a = double(settings.windDirectionDeg) * 3.14159265358979 / 180.0;
        const double s = std::clamp(double(settings.windSpeed), 0.0, 60.0);
        return {Fixed::fromRaw(std::int64_t(std::llround(std::cos(a) * s * double(Fixed::kOne)))),
                Fixed::fromRaw(std::int64_t(std::llround(std::sin(a) * s * double(Fixed::kOne))))};
    }

    std::string configKey(const MapDoc& doc) const {
        return physis_authoring::read(doc).dump(-1) + "|" + std::to_string(water_.revision()) + "|" + std::to_string(int(settings.shoreSandMeters * 10)) + "|" + std::to_string(doc.data.w) + "x" +
               std::to_string(doc.data.h) + "|" + std::to_string(settings.previewCellMeters) +
               "|" + std::to_string(settings.humidity);
    }

    void resetPresentation(fx::ParticleSystem& ps) {
        fxHost_.reset(ps);
        if (audio_) audioHost_.reset(*audio_);
        frame_.reset();
        frameDirty_ = true;
    }

    void rebuild(const MapDoc& doc, const HeightAt& heightAt) {
        world_ = std::make_unique<World>();
        accumulator_ = 0;
        Json map = Json::object();
        const Json& authored = physis_authoring::read(doc);
        if (!authored.isNull()) map.set("physis", authored);
        std::optional<physis::InstallOptions> options;
        try { options = physis::optionsFromMap(map); }
        catch (const std::exception& e) { status_ = std::string("map PHYSIS: ") + e.what(); }
        physis::Settings grid;
        grid.seed = options ? options->climate.seed : 1;
        grid.baseTemperature = options ? options->climate.baseTemperature : Fixed::fromInt(24);
        grid.baseHumidity = Fixed::fromRaw(std::int64_t(std::clamp(settings.humidity, 0.f, 1.f) * float(Fixed::kOne)));
        const int detail = std::clamp(settings.previewCellMeters, 0, 64);
        if (options && detail == 0) {
            grid = options->climate;
        } else {
            // Detailed preview grid over the map (1 map cell = 1 m).
            const int w = std::max(1, doc.data.w), h = std::max(1, doc.data.h);
            int cs = std::max(1, detail ? detail : 4);
            while ((w + cs - 1) / cs > 256 || (h + cs - 1) / cs > 256) cs *= 2;
            grid.originX = 0; grid.originZ = 0;
            grid.width = (w + cs - 1) / cs; grid.height = (h + cs - 1) / cs;
            grid.cellSizeMeters = cs;
        }
        physis::initialize(*world_, grid);
        // Grassland fuel: fast front, short residence time, so the fire
        // travels as a front and leaves charred ground behind it.
        physis::DisasterConfig fire;
        fire.fireSpreadRate = Fixed::fromRatio(1, 9);
        fire.fireGrowthRate = Fixed::fromRatio(1, 12);
        fire.fuelConsumptionRate = Fixed::fromRatio(1, 14);
        if (options && options->enableDisasters && detail == 0) fire = options->disasters;
        physis::initializeDisasters(*world_, fire);
        const auto wind = windVector();
        const auto* state = world_->resourceConst<physis::State>();
        for (Entity e : state->cells) {
            auto& cell = world_->get<physis::Cell>(e);
            const float x = (float(cell.x) + .5f) * float(grid.cellSizeMeters);
            const float z = (float(cell.z) + .5f) * float(grid.cellSizeMeters);
            float y = heightAt(x, z);
            if (!std::isfinite(y)) y = 0;
            cell.elevation = Fixed::fromRaw(std::int64_t(std::clamp(y, -100000.f, 100000.f) * float(Fixed::kOne)));
            physis::ClimateZone zone;
            zone.prevailingWind = wind;
            world_->add<physis::ClimateZone>(e, zone);
            world_->get<physis::Atmosphere>(e).wind = wind; // no spin-up wait
            // Authored NEREID water: no fuel in the channel, wet banks.
            if (!water_.empty()) {
                const float edge = water_.edgeDistance(x, z);
                const float half = float(grid.cellSizeMeters) * 0.5f;
                if (auto* hazard = world_->tryGet<physis::HazardCell>(e)) {
                    if (edge <= half) { hazard->fuel = Fixed{}; hazard->fire = Fixed{};
                                        hazard->waterDepth = Fixed::fromRatio(1, 2); }
                    else if (edge <= settings.shoreSandMeters) { hazard->fuel = Fixed{}; hazard->fire = Fixed{}; }   // bare sand
                }
                if (auto* surface = world_->tryGet<physis::Surface>(e)) {
                    if (edge <= half) surface->wetness = Fixed::fromInt(1);
                    else if (edge <= half + 6.f) surface->wetness = std::max(surface->wetness, Fixed::fromRatio(3, 5));
                }
            }
        }
        // Authored wind/storm/gravity volumes affect wind and flames too.
        if (options && !authored.isNull()) {
            try { physis::spawnFieldAssets(*world_, physis::parseFieldAssets(authored, *options)); }
            catch (const std::exception& e) { status_ = std::string("map PHYSIS fields: ") + e.what(); }
        }
        hasStorms_ = false;
        world_->eachExistingConst<physis::StormEvent>([&](Entity, const physis::StormEvent&) { hasStorms_ = true; });
    }

    void stepPreview() {
        if (hasStorms_)
            physis::advanceEnvironmentWithStormsAndDisasters(*world_, physis::FloodMode::ExternalAuthority);
        else
            physis::advanceEnvironmentWithDisasters(*world_, physis::FloodMode::ExternalAuthority);
    }

    void drawGrass(gfx::Device& device, render::Renderer3D& renderer, const MapDoc& doc,
                   float cx, float cz, const HeightAt& heightAt, double timeSeconds) {
        static constexpr std::int64_t kPatchCm = 1200;
        terraweave::GrassRenderQuality q;
        q.maxBladesPerPatch = settings.grassQuality <= 0 ? 1024 : settings.grassQuality == 1 ? 2560 : 4608;
        q.maxGpuPatches = 128;
        q.maxUploadsPerFrame = 6;
        if (q.maxBladesPerPatch != grass_.quality().maxBladesPerPatch) grass_.setQuality(q);
        const float radius = settings.grassQuality >= 2 ? 66.f : 56.f;
        const std::int64_t mapW = std::int64_t(std::max(1, doc.data.w)) * 100;
        const std::int64_t mapH = std::int64_t(std::max(1, doc.data.h)) * 100;
        const std::int64_t pcx = std::int64_t(std::floor(cx * 100.f / float(kPatchCm)));
        const std::int64_t pcz = std::int64_t(std::floor(cz * 100.f / float(kPatchCm)));
        const int r = int(std::ceil(radius * 100.f / float(kPatchCm)));
        patches_.clear();
        for (int dz = -r; dz <= r && patches_.size() < q.maxGpuPatches; ++dz)
            for (int dx = -r; dx <= r && patches_.size() < q.maxGpuPatches; ++dx) {
                if (dx * dx + dz * dz > r * r) continue;
                terraweave::GrassPatch g;
                g.bounds.minX = std::max<std::int64_t>(0, (pcx + dx) * kPatchCm);
                g.bounds.minZ = std::max<std::int64_t>(0, (pcz + dz) * kPatchCm);
                g.bounds.maxX = std::min<std::int64_t>(mapW, (pcx + dx + 1) * kPatchCm);
                g.bounds.maxZ = std::min<std::int64_t>(mapH, (pcz + dz + 1) * kPatchCm);
                if (g.bounds.maxX - g.bounds.minX < 100 || g.bounds.maxZ - g.bounds.minZ < 100) continue;
                const auto mx = (g.bounds.minX + g.bounds.maxX) / 2, mz = (g.bounds.minZ + g.bounds.maxZ) / 2;
                if (!terraWeavePreviewAllowsPlacement(doc.data, mx, mz, 100, 0, 150)) continue;
                g.key = physis::mix64(0x475241535350ull ^ (std::uint64_t(pcx + dx) << 32) ^
                                      std::uint64_t(std::uint32_t(pcz + dz)));
                if (!g.key) g.key = 1;
                g.seed = g.key * 0x9e3779b97f4a7c15ull;
                const auto area = (g.bounds.maxX - g.bounds.minX) * (g.bounds.maxZ - g.bounds.minZ) / 10000;
                g.maximumBlades = std::uint32_t(std::clamp<std::int64_t>(area * 40, 64, 262144));
                patches_.push_back(g);
            }
        const terraweave::GrassSoilHeight ground = [&heightAt](std::int64_t x, std::int64_t z) {
            float y = heightAt(float(x) * 0.01f, float(z) * 0.01f);
            if (!std::isfinite(y)) y = 0;
            return std::int32_t(std::llround(double(std::clamp(y, -9000.f, 9000.f)) * 100.0));
        };
        if (grassFilterDirty_ || grassFilterSand_ != settings.shoreSandMeters) {
            grassFilterDirty_ = false;
            grassFilterSand_ = settings.shoreSandMeters;
            if (water_.empty()) grass_.setPlacementFilter({});
            else grass_.setPlacementFilter([water = water_, sand = std::max(0.4f, settings.shoreSandMeters)](std::int64_t x, std::int64_t z) {
                return water.edgeDistance(float(x) * 0.01f, float(z) * 0.01f) > sand;
            });
            grass_.invalidateAll();
        }
        try {
            grass_.update(device, patches_, ground);
        } catch (const std::exception& e) {
            status_ = std::string("grass: ") + e.what();
            return;
        }
        world::applyPhysisGrassWind(grass_, *frame_, cx, cz, timeSeconds);
        grass_.draw(renderer, {});
    }

    void ensureAudio(const Reader& reader) {
        if (audio_) return;
        audio_ = std::make_unique<audio::AudioEngine>();
        audio::AudioConfig cfg;
        cfg.maxVoices = 12;
        audio_->init(cfg);
        Json bank = Json::object();
        // Project recordings named physis_fire_loop / physis_wind_loop win.
        std::string text;
        if (reader && reader("audio.json", text)) {
            try {
                const Json project = Json::parse(text);
                Json& sounds = bank.member("sounds");
                for (const char* name : {"physis_fire_loop", "physis_wind_loop"})
                    if (project["sounds"].has(name)) sounds.set(name, project["sounds"][name]);
                if (project.has("spatial")) bank.set("spatial", project["spatial"]);
            } catch (const std::exception&) {}
        }
        world::addPhysisNatureSounds(bank);
        std::string error;
        if (!audio_->loadBank(bank, reader, &error)) status_ = "audio: " + error;
    }

    std::unique_ptr<World> world_;
    AuthoredWaterQuery water_;
    bool grassFilterDirty_ = true;
    float grassFilterSand_ = -1;
    std::shared_ptr<const world::NaturePresentationFrame> frame_;
    std::string builtKey_, status_;
    bool usingLive_ = false, resetRequested_ = false, frameDirty_ = true;
    bool fxReady_ = false, active_ = false, retireSet_ = false, hasStorms_ = false;
    double lastTime_ = -1;
    float accumulator_ = 0, frameX_ = 0, frameZ_ = 0;
    terraweave::GrassPatchRenderer grass_;
    std::vector<terraweave::GrassPatch> patches_;
    world::PhysisNatureFxHost fxHost_;
    world::PhysisNatureAudioHost audioHost_;
    std::unique_ptr<audio::AudioEngine> audio_;
};

} // namespace aiverse::editor
