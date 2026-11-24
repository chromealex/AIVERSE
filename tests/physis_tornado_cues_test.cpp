// PHYSIS tornado presentation: frame contents (funnel, debris, vortex wind),
// SpectraFX funnel / debris planner, positional roar planner, built-in
// effects and synthesized sound (CPU only; no device, no audio hardware).
#include "engine/world/PhysisTornadoCues.h"
#include "engine/world/PhysisNatureRuntimeConfig.h"
#include "engine/physis/PhysisTornado.h"
#include "engine/fx/FxEffect.h"
#include "engine/audio/Synth.h"
#include <cstdio>
#include <set>

using namespace aiverse;
using namespace aiverse::physis;
namespace {
int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("TORNADO CUES FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

World stormWorld(std::uint32_t durationTicks = 0) {
    Settings s; s.width = 16; s.height = 12; s.cellSizeMeters = 4; s.seed = 3;
    World w; initialize(w, s);
    StormEvent e;
    e.center = {Fixed::fromInt(32), Fixed{}, Fixed::fromInt(24)};
    e.eyeRadiusMeters = Fixed::fromInt(3); e.outerRadiusMeters = Fixed::fromInt(14);
    e.heightMeters = Fixed::fromInt(60); e.tangentialWind = Fixed::fromInt(45);
    e.updraft = Fixed::fromInt(18); e.durationTicks = durationTicks;
    TornadoTrack t;
    t.mode = TornadoPathMode::Waypoints; t.speedPerTick = Fixed::fromRatio(1, 10);
    t.waypoints = {{Fixed::fromInt(50), Fixed{}, Fixed::fromInt(24)}};
    t.maxDebris = 16; t.debrisPerTick = Fixed::fromRatio(1, 2);
    (void)spawnTornado(w, e, t);
    for (int i = 0; i < 30; ++i) { advanceEnvironmentWithStorms(w); (void)advanceTornadoes(w); }
    return w;
}

world::NaturePresentationPolicy policy() {
    world::NaturePresentationPolicy p; p.centerX = 32; p.centerZ = 24; p.radiusMeters = 40;
    return p;
}

void frameContents() {
    const World w = stormWorld();
    const auto f = world::buildNaturePresentation(w, policy());
    CHECK(f.valid);
    CHECK(f.tornadoes.size() == 1);
    if (!f.tornadoes.empty()) {
        const auto& t = f.tornadoes[0];
        CHECK(t.x > 32.f && t.x < 36.f);                 // moved along +X (3 m in 30 ticks)
        CHECK(t.moveX > 0.05f && t.moveX < 0.15f);
        CHECK(t.intensity > 0.99f && t.coreRadius == 3.f && t.outerRadius == 14.f);
    }
    CHECK(!f.debris.empty());
    CHECK(f.maxWindSpeed > 20.f);                         // vortex wind reaches vegetation lattice
    // Same world, same frame (deterministic presentation input).
    const auto g = world::buildNaturePresentation(w, policy());
    CHECK(g.tornadoes.size() == f.tornadoes.size() && g.debris.size() == f.debris.size());
    // Vegetation wind field picks up the swirl.
    const auto field = world::forestWindFieldFromNature(f, 16);
    CHECK(field != nullptr);
}

void fxPlanner() {
    World w = stormWorld(40);
    world::TornadoFxPlanner planner;
    const auto f = world::buildNaturePresentation(w, policy());
    const auto first = planner.plan(f, 0.f, 0.05);
    std::size_t funnelStarts = 0, debrisStarts = 0;
    for (const auto& c : first) {
        CHECK(c.op == world::TornadoFxOp::Start);
        CHECK(std::isfinite(c.x) && std::isfinite(c.y) && c.scale > 0);
        (c.layer == world::TornadoFxLayer::Funnel ? funnelStarts : debrisStarts)++;
    }
    CHECK(funnelStarts == 1);
    CHECK(debrisStarts > 0);
    // Between ticks: moves only (extrapolated forward), no restarts.
    const auto again = planner.plan(f, 0.025f, 0.05);
    float x0 = 0, x1 = 0;
    for (const auto& c : first) if (c.layer == world::TornadoFxLayer::Funnel) x0 = c.x;
    for (const auto& c : again) {
        CHECK(c.op == world::TornadoFxOp::Move);
        if (c.layer == world::TornadoFxLayer::Funnel) x1 = c.x;
    }
    CHECK(x1 > x0);
    // After its life the tornado leaves the frame: funnel stops.
    for (int i = 0; i < 40; ++i) { advanceEnvironmentWithStorms(w); (void)advanceTornadoes(w); }
    const auto later = world::buildNaturePresentation(w, policy());
    CHECK(later.tornadoes.empty());
    bool stopped = false;
    for (const auto& c : planner.plan(later, 0.f, 0.05))
        if (c.layer == world::TornadoFxLayer::Funnel) { CHECK(c.op == world::TornadoFxOp::Stop); stopped = true; }
    CHECK(stopped);
    CHECK(planner.active(world::TornadoFxLayer::Funnel) == 0);
    planner.clear();
    CHECK(planner.active(world::TornadoFxLayer::Debris) == 0);
}

void audioPlanner() {
    const World w = stormWorld();
    const auto f = world::buildNaturePresentation(w, policy());
    world::TornadoAudioPlanner planner;
    auto a = planner.plan(f, 40, 2, 24, 0.016f);
    CHECK(a.size() == 1 && a[0].op == world::TornadoAudioOp::Start && a[0].volume > 0);
    auto b = planner.plan(f, 40, 2, 24, 0.1f);
    CHECK(b.size() == 1 && b[0].op == world::TornadoAudioOp::Update && b[0].volume >= a[0].volume);
    auto far = planner.plan(f, 5000, 2, 24, 0.1f);           // out of earshot
    CHECK(far.size() == 1 && far[0].op == world::TornadoAudioOp::Stop);
    CHECK(planner.voices() == 0);
}

void builtinsParse() {
    for (const char* text : {world::physisTornadoEffectJson(), world::physisTornadoDebrisEffectJson(),
                             world::physisTornadoFireEffectJson(), world::physisTornadoSprayEffectJson(),
                             world::physisTornadoEmberEffectJson(), world::physisTornadoScarEffectJson()}) {
        fx::EffectDef def;
        std::vector<std::string> errors;
        const bool ok = fx::parseEffect(Json::parse(text), "builtin", def, errors);
        for (const auto& e : errors) std::printf("  effect error: %s\n", e.c_str());
        CHECK(ok && errors.empty());
        CHECK(def.looping());
    }
    Json bank;
    CHECK(world::addPhysisTornadoSounds(bank) == 2);   // roar + flame roar
    CHECK(world::addPhysisTornadoSounds(bank) == 0);
    {
        std::vector<audio::SynthLayer> fire; std::string err;
        CHECK(audio::parseSynth(bank["sounds"]["physis_firestorm_loop"]["synth"], fire, err));
        CHECK(fire.size() == 3);
    }
    std::vector<audio::SynthLayer> layers; std::string error;
    CHECK(audio::parseSynth(bank["sounds"]["physis_tornado_loop"]["synth"], layers, error));
    if (!error.empty()) std::printf("  synth error: %s\n", error.c_str());
    CHECK(layers.size() == 3);
    CHECK(audio::renderSynth(layers, 22050, 5).size() > 22050);
    // Presentation switch.
    const auto cfg = world::natureRuntimeConfigFromMap(Json::parse(
        R"({"physis":{"presentation":{"tornado":false}}})"));
    CHECK(cfg && !cfg->tornado && cfg->fx);
}
// Fire whirl / waterspout / scour presentation: frame values, layer blend,
// embers, scars, flame-roar voice by fireness.
World natureWorld(bool water) {
    Settings s; s.width = 16; s.height = 12; s.cellSizeMeters = 4; s.seed = 3;
    World w; initialize(w, s);
    DisasterConfig frozen; frozen.fireSpreadRate = Fixed{}; frozen.fireGrowthRate = Fixed{};
    frozen.fuelConsumptionRate = Fixed{};
    initializeDisasters(w, frozen);
    StormEvent e;
    e.center = {Fixed::fromInt(32), Fixed{}, Fixed::fromInt(24)};
    e.eyeRadiusMeters = Fixed::fromInt(3); e.outerRadiusMeters = Fixed::fromInt(14);
    e.heightMeters = Fixed::fromInt(60); e.tangentialWind = Fixed::fromInt(45);
    e.updraft = Fixed::fromInt(18);
    TornadoTrack t;
    t.mode = TornadoPathMode::Waypoints; t.speedPerTick = Fixed::fromRatio(1, 20);
    t.waypoints = {{Fixed::fromInt(44), Fixed{}, Fixed::fromInt(24)}};
    TornadoNature n = defaultTornadoNature(20);
    if (!water) n.heat = Fixed::fromInt(500);
    (void)spawnTornado(w, e, t, n);
    TornadoHooks h;
    if (water) h.waterDepth = [](void*, Fixed, Fixed) { return Fixed::fromInt(2); };
    for (int i = 0; i < 80; ++i) { advanceEnvironmentWithStormsAndDisasters(w); (void)advanceTornadoes(w, h); }
    return w;
}

void natureCues() {
    const World fire = natureWorld(false);
    const auto f = world::buildNaturePresentation(fire, policy());
    CHECK(f.tornadoes.size() == 1);
    if (f.tornadoes.empty()) return;
    CHECK(f.tornadoes[0].fireness > 0.6f);
    CHECK(f.tornadoes[0].spray == 0.f && !f.tornadoes[0].waterspout);
    CHECK(!f.embers.empty());
    CHECK(!f.scour.empty());
    world::TornadoFxPlanner planner;
    std::size_t fireStarts = 0, embers = 0, scars = 0;
    float dustAlpha = -1, fireAlpha = 0;
    for (const auto& c : planner.plan(f, 0.f, 0.05)) {
        if (c.layer == world::TornadoFxLayer::FireFunnel) { ++fireStarts; fireAlpha = c.tint[3]; }
        if (c.layer == world::TornadoFxLayer::Funnel) dustAlpha = c.tint[3];
        if (c.layer == world::TornadoFxLayer::Ember) ++embers;
        if (c.layer == world::TornadoFxLayer::Scar) ++scars;
    }
    CHECK(fireStarts == 1 && fireAlpha > 0.6f);
    CHECK(dustAlpha < 0.5f);                        // dust funnel faded (or gone: -1)
    CHECK(embers > 0);
    CHECK(scars > 0);
    // Less fire -> lower fire level: restart with a smaller alpha.
    auto weaker = f;
    weaker.tornadoes[0].fireness = 0.2f;
    float weakAlpha = 0;
    for (const auto& c : planner.plan(weaker, 0.f, 0.05))
        if (c.layer == world::TornadoFxLayer::FireFunnel && c.op == world::TornadoFxOp::Start) weakAlpha = c.tint[3];
    CHECK(weakAlpha > 0.f && weakAlpha < fireAlpha);
    // Flame roar voice: louder with more fire.
    world::TornadoAudioPlanner audio;
    std::size_t fireVoices = 0;
    for (const auto& c : audio.plan(f, 32, 2, 24, 0.016f))
        if (c.sound == world::TornadoSound::FireRoar) ++fireVoices;
    CHECK(fireVoices == 1);
    for (int i = 0; i < 30; ++i) (void)audio.plan(f, 32, 2, 24, 0.1f);
    const float loud = audio.fireVolume(f.tornadoes[0].key);
    world::TornadoAudioPlanner quiet;
    for (int i = 0; i < 30; ++i) (void)quiet.plan(weaker, 32, 2, 24, 0.1f);
    CHECK(loud > quiet.fireVolume(f.tornadoes[0].key));
    auto noFire = f; noFire.tornadoes[0].fireness = 0;
    std::size_t stops = 0;
    for (const auto& c : audio.plan(noFire, 32, 2, 24, 0.1f))
        if (c.sound == world::TornadoSound::FireRoar && c.op == world::TornadoAudioOp::Stop) ++stops;
    CHECK(stops == 1 && audio.fireVoices() == 0);

    // Waterspout: spray layer, no fire.
    const World sea = natureWorld(true);
    const auto g = world::buildNaturePresentation(sea, policy());
    CHECK(g.tornadoes.size() == 1);
    if (g.tornadoes.empty()) return;
    CHECK(g.tornadoes[0].waterspout && g.tornadoes[0].spray > 0.3f);
    CHECK(g.scour.empty());
    world::TornadoFxPlanner spout;
    std::size_t sprays = 0, fires = 0;
    for (const auto& c : spout.plan(g, 0.f, 0.05)) {
        if (c.layer == world::TornadoFxLayer::Spray) ++sprays;
        if (c.layer == world::TornadoFxLayer::FireFunnel) ++fires;
    }
    CHECK(sprays == 1 && fires == 0);
}
} // namespace

int main() {
    frameContents();
    fxPlanner();
    audioPlanner();
    builtinsParse();
    natureCues();
    if (failures) { std::printf("TORNADO CUES: %d failures\n", failures); return 1; }
    std::printf("TORNADO CUES: OK\n");
    return 0;
}
