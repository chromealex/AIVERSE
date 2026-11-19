#include "engine/physis/PhysisClimate.h"
#include <cstdio>

using namespace aiverse;
using namespace aiverse::physis;
namespace {
int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("PHYSIS CLIMATE FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

bool identical(const World& a, const World& b, int x) {
    Sample u{}, v{};
    if (!sample(a, x, 0, u) || !sample(b, x, 0, v)) return false;
    return u.atmosphere.temperature == v.atmosphere.temperature &&
           u.atmosphere.humidity == v.atmosphere.humidity &&
           u.atmosphere.wind == v.atmosphere.wind &&
           u.atmosphere.pressure == v.atmosphere.pressure &&
           u.atmosphere.rain == v.atmosphere.rain &&
           u.atmosphere.snow == v.atmosphere.snow &&
           u.surface.wetness == v.surface.wetness &&
           u.surface.snowpack == v.surface.snowpack;
}

void seasons() {
    ClimateZone zone;
    zone.yearTicks = 100;
    zone.phaseTicks = 0;
    CHECK(seasonSignal(0, zone) == Fixed::fromInt(-1));
    CHECK(seasonSignal(25, zone) == Fixed{});
    CHECK(seasonSignal(50, zone) == Fixed::fromInt(1));
    CHECK(seasonSignal(75, zone) == Fixed{});
    CHECK(seasonSignal(100, zone) == Fixed::fromInt(-1));
    zone.yearTicks = 0;
    CHECK(seasonSignal(123, zone) == Fixed{});
}

void regionsAndMovingFronts() {
    Settings config;
    config.width = 3;
    config.height = 1;
    config.seed = 889;
    config.baseTemperature = Fixed::fromInt(-5);
    config.baseHumidity = Fixed::fromInt(1);
    World a, b;
    initialize(a, config);
    initialize(b, config);
    ClimateZone hot;
    hot.temperatureOffset = Fixed::fromInt(40);
    hot.seasonTemperatureAmplitude = Fixed{};
    hot.seasonHumidityAmplitude = Fixed{};
    hot.yearTicks = 100;
    for (World* world : {&a, &b}) {
        world->add<ClimateZone>(cellAt(*world, 0, 0), hot);
        Entity f = world->create();
        WeatherFront front;
        front.center = {Fixed::fromInt(0), Fixed{}};
        front.velocity = {Fixed::fromRatio(1, 16), Fixed{}};
        front.radiusCells = Fixed::fromInt(3);
        front.humidityDelta = Fixed::fromRatio(1, 5);
        front.windDelta = {Fixed::fromInt(2), Fixed::fromInt(1)};
        front.precipitationBoost = Fixed::fromRatio(1, 4);
        world->add<WeatherFront>(f, front);
    }
    Sample original{};
    CHECK(sample(a, 0, 0, original));
    for (int tick = 0; tick < 150; ++tick) {
        advanceClimate(a);
        advanceClimate(b);
    }
    CHECK(a.resourceConst<State>()->tick == 150);
    for (int x = 0; x < 3; ++x) CHECK(identical(a, b, x));
    Sample warm{}, cold{};
    CHECK(sample(a, 0, 0, warm));
    CHECK(sample(a, 2, 0, cold));
    CHECK(warm.atmosphere.temperature > cold.atmosphere.temperature);
    CHECK(cold.atmosphere.rain == Fixed{});
    CHECK(cold.atmosphere.snow >= Fixed{});
    CHECK(warm.atmosphere.temperature > original.atmosphere.temperature);
    // Snapshots include the moving front and all regional climate data.
    World saved = a;
    advanceClimate(a);
    Sample after{};
    CHECK(sample(a, 0, 0, after));
    a = saved;
    advanceClimate(a);
    CHECK(sample(a, 0, 0, warm));
    CHECK(warm.atmosphere.temperature == after.atmosphere.temperature);
    CHECK(warm.atmosphere.humidity == after.atmosphere.humidity);
}
void noGenresOrRenderRequired() {
    struct IndependentGameplayData { std::int32_t data = 99; };
    World world;
    Settings config;
    config.width = 1; config.height = 1;
    initialize(world, config);
    auto otherEntity = world.create();
    world.add<IndependentGameplayData>(otherEntity, IndependentGameplayData{});
    advanceClimate(world);
    CHECK(world.get<IndependentGameplayData>(otherEntity).data == 99);
}
}
int main() {
    seasons();
    regionsAndMovingFronts();
    noGenresOrRenderRequired();
    std::printf("PHYSIS climate: %s\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
