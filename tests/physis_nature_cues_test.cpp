// PHYSIS nature cues: SpectraFX / audio planners, built-in effects and
// sounds, and the TerraWeave grass burn/wind overlay (CPU model only).
#include "engine/world/PhysisNatureCues.h"
#include "engine/world/PhysisNatureGrass.h"
#include "engine/physis/PhysisClimate.h"
#include "engine/fx/FxEffect.h"
#include "engine/audio/Synth.h"
#include <cstdio>
#include <set>

using namespace aiverse;
using namespace aiverse::physis;
namespace {
int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("NATURE CUES FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

World burningWorld() {
    Settings s;
    s.width = 16; s.height = 8; s.cellSizeMeters = 8; s.seed = 9;
    s.baseTemperature = Fixed::fromInt(30); s.baseHumidity = Fixed{};
    World w; initialize(w, s); initializeDisasters(w);
    for (Entity cell : w.resourceConst<State>()->cells) {
        ClimateZone z; z.prevailingWind = {Fixed::fromInt(10), Fixed{}};
        z.humidityOffset = -Fixed::fromInt(1);
        w.add<ClimateZone>(cell, z);
    }
    for (int i = 0; i < 60; ++i) advanceEnvironmentWithDisasters(w);
    CHECK(ignite(w, 5, 4));
    for (int i = 0; i < 20; ++i) advanceEnvironmentWithDisasters(w);
    return w;
}
world::NaturePresentationPolicy policy() {
    world::NaturePresentationPolicy p; p.centerX = 64; p.centerZ = 32; p.radiusMeters = 120;
    return p;
}

void fxPlanner() {
    World w = burningWorld();
    const auto frame = world::buildNaturePresentation(w, policy());
    CHECK(!frame.flames.empty());
    world::NatureFxPolicy fp; fp.maxFlames = 40; fp.maxSmoke = 12;
    world::NatureFxPlanner planner(fp);
    auto first = planner.plan(frame);
    std::size_t starts = 0;
    for (const auto& c : first) {
        CHECK(c.op == world::NatureFxOp::Start);
        starts += 1;
        CHECK(c.scale > 0 && std::isfinite(c.x) && std::isfinite(c.y));
    }
    CHECK(planner.active(world::NatureFxLayer::Flame) <= 40);
    CHECK(planner.active(world::NatureFxLayer::Smoke) <= 12);
    CHECK(starts == planner.active(world::NatureFxLayer::Flame) + planner.active(world::NatureFxLayer::Smoke));
    // Same frame again: no churn.
    CHECK(planner.plan(frame).empty());
    // Next tick: bounded churn, every Stop refers to a tracked key.
    advanceEnvironmentWithDisasters(w);
    const auto next = world::buildNaturePresentation(w, policy());
    std::set<std::uint64_t> stopped;
    for (const auto& c : planner.plan(next))
        if (c.op == world::NatureFxOp::Stop) stopped.insert(c.key);
    CHECK(planner.active(world::NatureFxLayer::Flame) <= 40);
    // Invalid frame clears everything.
    auto cleared = planner.plan(world::NaturePresentationFrame{});
    CHECK(planner.active(world::NatureFxLayer::Flame) == 0 && planner.active(world::NatureFxLayer::Smoke) == 0);
    for (const auto& c : cleared) CHECK(c.op == world::NatureFxOp::Stop);
}

void builtinEffectsParse() {
    for (const char* text : {world::physisFlameEffectJson(), world::physisSmokeEffectJson()}) {
        fx::EffectDef def;
        std::vector<std::string> errors;
        const bool ok = fx::parseEffect(Json::parse(text), "builtin", def, errors);
        for (const auto& e : errors) std::printf("  effect error: %s\n", e.c_str());
        CHECK(ok && errors.empty());
        CHECK(def.looping());
    }
}

void audioPlanner() {
    World w = burningWorld();
    const auto frame = world::buildNaturePresentation(w, policy());
    CHECK(!frame.clusters.empty());
    world::NatureAudioPolicy ap; ap.maxFireVoices = 2;
    world::NatureAudioPlanner planner(ap);
    const auto c0 = frame.clusters.front();
    auto cmds = planner.plan(frame, c0.x, c0.y + 2, c0.z, 1.f / 60);
    std::size_t fireStarts = 0; bool wind = false;
    for (const auto& c : cmds) {
        if (c.sound == world::NatureSound::FireLoop && c.op == world::NatureAudioOp::Start) {
            ++fireStarts; CHECK(c.positional && c.volume > 0 && c.volume <= 0.15f + 1e-6f);
        }
        if (c.sound == world::NatureSound::WindLoop) { wind = true; CHECK(!c.positional); }
    }
    CHECK(fireStarts >= 1 && fireStarts <= 2);
    CHECK(wind && planner.windVoice());
    // Volume ramps up smoothly, never jumps above target.
    float last = 0;
    for (int i = 0; i < 120; ++i)
        for (const auto& c : planner.plan(frame, c0.x, c0.y + 2, c0.z, 1.f / 30))
            if (c.sound == world::NatureSound::FireLoop && c.key == c0.key) {
                CHECK(c.volume + 1e-5f >= last); last = c.volume; CHECK(c.volume <= 1.f);
            }
    CHECK(last > 0.15f);
    // Far listener: fire voices stop.
    std::size_t stops = 0;
    for (const auto& c : planner.plan(frame, 1e5f, 0, 1e5f, 1.f / 30))
        if (c.sound == world::NatureSound::FireLoop) { CHECK(c.op == world::NatureAudioOp::Stop); ++stops; }
    CHECK(stops >= 1 && planner.fireVoices() == 0);
    planner.clear();
    CHECK(!planner.windVoice());
}

void soundBank() {
    Json bank = Json::parse(R"({"sounds":{"physis_wind_loop":{"file":"wind.ogg"}}})");
    CHECK(world::addPhysisNatureSounds(bank) == 1);       // project wind wins
    CHECK(bank["sounds"]["physis_wind_loop"].getStr("file") == "wind.ogg");
    CHECK(bank["sounds"]["physis_fire_loop"].getBool("loop"));
    std::vector<audio::SynthLayer> layers; std::string error;
    CHECK(audio::parseSynth(bank["sounds"]["physis_fire_loop"]["synth"], layers, error));
    CHECK(layers.size() == 3);
    const auto pcm = audio::renderSynth(layers, 22050, 7);
    CHECK(pcm.size() > 22050);
    Json empty;
    CHECK(world::addPhysisNatureSounds(empty) == 2);
    CHECK(world::addPhysisNatureSounds(empty) == 0);
}

void grassOverlay() {
    terraweave::GrassPatch patch;
    patch.key = 77; patch.seed = 3;
    patch.bounds = {0, 0, 1600, 1600};
    patch.maximumBlades = 4096;
    const terraweave::GrassSoilHeight flat = [](int64_t, int64_t) { return 0; };
    terraweave::GrassRenderQuality q; q.maxBladesPerPatch = 512;
    const auto legacy = terraweave::makeGrassPatchModel(patch, flat, q);
    CHECK(legacy && legacy->meshes.size() == 1 && legacy->meshes[0].primitives.size() == 1);
    // Empty overlay object (no fire, no sway) is bit-identical to legacy.
    terraweave::GrassPresentationOptions none;
    const auto same = terraweave::makeGrassPatchModel(patch, flat, q, {}, &none);
    CHECK(same && same->vertices.size() == legacy->vertices.size());
    bool identical = same && same->vertices.size() == legacy->vertices.size();
    for (std::size_t i = 0; identical && i < legacy->vertices.size(); ++i)
        identical = legacy->vertices[i].color == same->vertices[i].color &&
                    legacy->vertices[i].pos.y == same->vertices[i].pos.y;
    CHECK(identical);
    // Left half burned out, right half burning.
    terraweave::GrassPresentationOptions burn;
    burn.windSway = true;
    burn.fire = [](int64_t x, int64_t) {
        terraweave::GrassFireSample s;
        if (x < 800) s.charPermille = 1000; else { s.firePermille = 900; s.charPermille = 200; }
        return s;
    };
    const auto m = terraweave::makeGrassPatchModel(patch, flat, q, {}, &burn);
    CHECK(m && m->materials.size() == 2);
    CHECK(m->materials[0].shaderGraph != nullptr);
    CHECK(m->meshes[0].primitives.size() == 2);
    const auto& glow = m->meshes[0].primitives.back();
    CHECK(glow.material == 1 && glow.indexCount > 0);
    CHECK(m->materials[1].emissive.x > 0.5f);
    float maxLeft = 0, maxRight = 0, legacyMax = 0;
    for (const auto& v : legacy->vertices) legacyMax = std::max(legacyMax, v.pos.y);
    for (const auto& v : m->vertices) {
        // Classify away from the x = 8 m burn line (blades bend a few cm).
        if (v.pos.x < 7.5f) maxLeft = std::max(maxLeft, v.pos.y);
        else if (v.pos.x > 8.5f) maxRight = std::max(maxRight, v.pos.y);
        const bool top = v.uv.y > 0.5f;
        CHECK(((v.color >> 24) == 0xffu) == top);   // sway weight root->tip
    }
    CHECK(maxLeft < legacyMax * 0.3f);   // stubble
    CHECK(maxRight > legacyMax * 0.5f);
    // PHYSIS frame -> renderer overlay wiring.
    World w = burningWorld();
    auto frame = std::make_shared<world::NaturePresentationFrame>(
        world::buildNaturePresentation(w, policy()));
    const auto s = world::grassFireSample(*frame, 5 * 8 + 4, 4 * 8 + 4);
    CHECK(s.charPermille > 0 || s.firePermille > 0);
    terraweave::GrassPatchRenderer renderer;
    world::applyPhysisGrassPresentation(renderer, frame);
    world::applyPhysisGrassWind(renderer, *frame, 40, 32, 1.25);
    CHECK(renderer.residentPatches() == 0);
}
}

int main() {
    fxPlanner();
    builtinEffectsParse();
    audioPlanner();
    soundBank();
    grassOverlay();
    std::printf("PHYSIS nature cues: %s\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
