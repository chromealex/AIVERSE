// PHYSIS -> nature presentation host model (fire on grass driven by wind).
#include "engine/world/PhysisNaturePresentation.h"
#include "engine/physis/PhysisClimate.h"
#include <cstdio>
#include <set>

using namespace aiverse;
using namespace aiverse::physis;
namespace {
int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("NATURE PRESENTATION FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

World makeWorld(int windSign) {
    Settings s;
    s.width = 24; s.height = 9; s.cellSizeMeters = 8; s.seed = 42;
    s.baseTemperature = Fixed::fromInt(28); s.baseHumidity = Fixed{};
    World w;
    initialize(w, s);
    initializeDisasters(w);
    for (Entity cell : w.resourceConst<State>()->cells) {
        ClimateZone zone;
        zone.prevailingWind = {Fixed::fromInt(14 * windSign), Fixed{}};
        zone.humidityOffset = -Fixed::fromInt(1);
        w.add<ClimateZone>(cell, zone);
    }
    // Let the cell wind converge towards the prevailing wind first.
    for (int i = 0; i < 90; ++i) advanceEnvironmentWithDisasters(w);
    CHECK(ignite(w, 12, 4));
    for (int i = 0; i < 45; ++i) advanceEnvironmentWithDisasters(w);
    return w;
}

world::NaturePresentationPolicy around(float x, float z) {
    world::NaturePresentationPolicy p;
    p.centerX = x; p.centerZ = z; p.radiusMeters = 200;
    return p;
}

void basics() {
    World none;
    CHECK(!world::buildNaturePresentation(none).valid);
    World w = makeWorld(1);
    const auto tickBefore = w.resourceConst<State>()->tick;
    const auto frame = world::buildNaturePresentation(w, around(100, 36));
    CHECK(frame.valid && frame.hasDisasters);
    CHECK(w.resourceConst<State>()->tick == tickBefore); // read-only
    CHECK(frame.width == 24 && frame.height == 9);
    CHECK(!frame.flames.empty());
    CHECK(!frame.clusters.empty());
    CHECK(frame.flames.size() <= 192);
    CHECK(frame.maxWindSpeed > 8.f);
    CHECK(frame.meanWindX > 8.f);
    // Ignition centre burned (charred), far upwind edge still fresh.
    const auto origin = frame.sample(12 * 8 + 4, 4 * 8 + 4);
    CHECK(origin.valid && origin.charred > 0.05f);
    const auto upwindEdge = frame.sample(1 * 8 + 4, 4 * 8 + 4);
    CHECK(upwindEdge.valid && upwindEdge.charred < origin.charred);
    // Fire moved downwind: flame-weighted centroid right of ignition.
    double wsum = 0, xsum = 0;
    for (const auto& f : frame.flames) {
        CHECK(f.key != 0);
        CHECK(f.intensity >= 0 && f.intensity <= 1 && f.smoke >= 0 && f.smoke <= 1);
        wsum += f.intensity + f.smoke; xsum += f.x * (f.intensity + f.smoke);
    }
    CHECK(wsum > 0 && xsum / wsum > 12 * 8 + 4);
    // Keys sorted and unique.
    std::set<std::uint64_t> keys;
    for (std::size_t i = 0; i < frame.flames.size(); ++i) {
        keys.insert(frame.flames[i].key);
        if (i) CHECK(frame.flames[i - 1].key < frame.flames[i].key);
    }
    CHECK(keys.size() == frame.flames.size());
    // Deterministic rebuild, including from a COW copy.
    World copy = w;
    const auto again = world::buildNaturePresentation(copy, around(100, 36));
    CHECK(again.flames.size() == frame.flames.size());
    for (std::size_t i = 0; i < std::min(again.flames.size(), frame.flames.size()); ++i)
        CHECK(again.flames[i].key == frame.flames[i].key &&
              again.flames[i].intensity == frame.flames[i].intensity);
    // Stable identity across one tick: most flame points persist.
    advanceEnvironmentWithDisasters(copy);
    const auto next = world::buildNaturePresentation(copy, around(100, 36));
    std::size_t kept = 0;
    for (const auto& f : next.flames) kept += keys.count(f.key);
    CHECK(kept * 2 > std::min(next.flames.size(), frame.flames.size()));
}

void windMirror() {
    World left = makeWorld(-1);
    const auto frame = world::buildNaturePresentation(left, around(100, 36));
    double wsum = 0, xsum = 0;
    for (const auto& f : frame.flames) { wsum += f.intensity + f.smoke; xsum += f.x * (f.intensity + f.smoke); }
    CHECK(wsum > 0 && xsum / wsum < 12 * 8 + 4);
    CHECK(frame.meanWindX < -8.f);
}

void budgetsAndSignature() {
    World w = makeWorld(1);
    auto p = around(100, 36);
    p.maxFlames = 10; p.maxLatticeSamples = 128; p.maxClusters = 2;
    const auto small = world::buildNaturePresentation(w, p);
    CHECK(small.flames.size() <= 10);
    CHECK(small.clusters.size() <= 2);
    CHECK(small.flameSpacing > 8.f / 6.f);
    // Region is bounded regardless of radius.
    auto huge = around(100, 36); huge.radiusMeters = 1e6f; huge.maxRegionCells = 4;
    const auto bounded = world::buildNaturePresentation(w, huge);
    CHECK(bounded.width <= 4 && bounded.height <= 4);
    // Signature: burned area differs from an untouched one; repeatable.
    const auto f = world::buildNaturePresentation(w, around(100, 36));
    const auto burned = f.signature(96, 32, 112, 48);
    CHECK(burned == f.signature(96, 32, 112, 48));
    CHECK(burned != f.signature(0, 0, 16, 16));
    bool threw = false;
    try { auto bad = around(0, 0); bad.radiusMeters = -1; (void)world::buildNaturePresentation(w, bad); }
    catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);
    // Blend is a convex combination.
    world::NatureGroundSample a, b; a.valid = b.valid = true; a.fire = 0; b.fire = 1;
    CHECK(std::abs(world::blendNatureSample(a, b, .25f).fire - .25f) < 1e-6f);
}

void withoutDisastersOnlyWind() {
    Settings s; s.width = 4; s.height = 4; s.cellSizeMeters = 16;
    World w; initialize(w, s);
    const auto f = world::buildNaturePresentation(w, around(32, 32));
    CHECK(f.valid && !f.hasDisasters && f.flames.empty() && f.clusters.empty());
    CHECK(f.sample(32, 32).valid);
}
}

int main() {
    basics();
    windMirror();
    budgetsAndSignature();
    withoutDisastersOnlyWind();
    std::printf("PHYSIS nature presentation: %s\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
