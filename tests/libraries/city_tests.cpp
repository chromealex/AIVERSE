// Tests of the city packages: world-calendar, weather-timeline, vehicle-fx, swept-shapes, derived-keys,
// city-traffic routes.
#include "engine/core/SweptShapes.h"
#include "engine/mechanics/traffic/TrafficRoutes.h"
#include <aiverselib/mechanics/vehicle-fx/TyreTrails.h>
#include <aiverselib/mechanics/vehicle-fx/VehicleLights.h>
#include <aiverselib/runtime/DerivedKeys.h>
#include <aiverselib/systems/Calendar.h>
#include <aiverselib/systems/WeatherTimeline.h>
#include <cmath>
#include <cstdio>
#include <map>

using namespace aiverse;
using namespace aiverse::lib;

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

namespace {

// The engine's PCG32 interface: below / range.
struct TestRng {
    std::uint64_t s = 0x853c49e6748fea9bull;
    std::uint32_t next() { s = s * 6364136223846793005ull + 1442695040888963407ull; return std::uint32_t(s >> 33); }
    std::uint32_t below(std::uint32_t n) { return n ? next() % n : 0; }
    std::int32_t range(std::int32_t lo, std::int32_t hi) { return lo + std::int32_t(below(std::uint32_t(hi - lo + 1))); }
};

void calendar() {
    using namespace systems;
    CalendarConfig c{600, 8 * 60, 0, 5};   // 600 ticks a day (10 per game hour... 1440/600 min per tick)
    auto t = calendarAt(0, c);
    CHECK(t.day == 0 && t.minute == 480 && t.weekday == 0);
    t = calendarAt(600 * 9 + 300, c);   // nine and a half days later
    CHECK(t.day == 9 && t.minute == 480 + 720 && t.weekday == 2);
    t = calendarAt(600 + 400, c);       // 8:00 + 960 min = 0:00 of day 2
    CHECK(t.day == 2 && t.minute == 0);
    // inverse: the first tick that shows a minute
    for (std::int64_t m : {480, 481, 1439, 2000, 5000}) {
        auto tick = calendarTickOf(m, c);
        CHECK(calendarAbsoluteMinute(tick, c) >= m);
        if (tick) CHECK(calendarAbsoluteMinute(tick - 1, c) < m);
    }
    // the original formula of the city game: (startMinute + tick * 1440 / dayTicks) / 1440
    for (std::uint32_t tick : {0u, 1u, 599u, 600u, 12345u, 99999u})
        CHECK(calendarAt(tick, c).day == (std::int64_t(c.startMinute) + std::int64_t(tick) * 1440 / c.dayTicks) / 1440);

    // fixed and rotating weeks
    CHECK(workDay(0, 3, false) && workDay(4, 3, false) && !workDay(5, 3, false) && !workDay(6, 3, false));
    for (int person = 0; person < 7; ++person) {
        int worked = 0;
        for (int day = 0; day < 7; ++day) {
            worked += workDay(day, person, true);
            CHECK(workDay(day, person, true) == (((day % 7) + person % 7) % 7 < 5));   // original rotation
        }
        CHECK(worked == 5);
    }
    // overnight shift 22:00 -> 6:00 belongs to its starting day
    WorkShift night{22 * 60, 6 * 60};
    CHECK(overnight(night) && shiftLength(night) == 480);
    CHECK(shiftDay(3, 3 * 60, night) == 2 && shiftDay(3, 23 * 60, night) == 3);
    CHECK(onShift(4, 2 * 60, night, 0, false));            // Friday 2:00: Thursday's shift
    CHECK(onShift(4, 23 * 60, night, 0, false));           // Friday 23:00
    CHECK(onShift(5, 2 * 60, night, 0, false));            // Saturday 2:00: still Friday's shift
    CHECK(!onShift(5, 23 * 60, night, 0, false));          // Saturday is off
    CHECK(minutesUntilShift(5, 23 * 60, night, 0, false) == 2 * 1440 - 60);   // next: Monday 22:00
    CHECK(minutesUntilShift(0, 21 * 60, night, 0, false) == 60);
    WorkShift day{9 * 60, 17 * 60};
    CHECK(minutesUntilShift(0, 10 * 60, day, 0, false) == 0);
    CHECK(minutesUntilShift(0, 18 * 60, day, 0, false) == 15 * 60);
    CalendarConfig never{1440, 0, 0, 0};
    CHECK(minutesUntilShift(0, 0, day, 0, false, never) == -1);
    // waking hours
    CHECK(awakeMinutesLeft(7 * 60, day) == 0 && awakeMinutesLeft(20 * 60, day) == 120);
    CHECK(awakeMinutesLeft(10 * 60, night) == 0 && awakeMinutesLeft(13 * 60, night) == 9 * 60);
    // needs
    CHECK(needUrgency(720, 0, 720) == 1000 && needUrgency(0, 100, 720) == 0 && needUrgency(99999, 0, 10) == 2000);
    std::array<int, 3> u{300, 900, 900};
    CHECK(mostUrgent(u) == 1 && mostUrgent(u, 1000) == -1);
    // daily errands: deterministic, capped, once a day
    DailyErrandConfig ec;
    DailyErrands a, b;
    TestRng r1, r2;
    planDailyErrands(a, 3, 9 * 60, 400, ec, r1);
    planDailyErrands(b, 3, 9 * 60, 400, ec, r2);
    CHECK(a == b && !a.pending.empty());
    for (auto& e : a.pending) CHECK(e.due >= 9 * 60 && e.due <= 19 * 60);
    auto before = a;
    planDailyErrands(a, 3, 12 * 60, 400, ec, r1);   // same day: unchanged
    CHECK(a == before);
    auto due = dueErrands(a, 19 * 60, 0, ec);
    CHECK(int(due.size()) == std::min<int>(ec.maxActive, int(a.pending.size())));
    for (auto i : due) markStarted(a, i);
    CHECK(a.startedToday == int(due.size()));
    CHECK(dueErrands(a, 19 * 60, ec.maxActive, ec).empty());
    int started = a.startedToday;
    for (int round = 0; round < 50; ++round) for (auto i : dueErrands(a, 19 * 60, 0, ec)) { markStarted(a, i); ++started; }
    CHECK(a.startedToday == std::min<int>(ec.maxPerDay, int(a.pending.size())) && started == a.startedToday);
}

// The city game's original weather formula (before it became this package), for the default config.
struct OriginalWeather {
    float rain = 0, wind = 0, wet = 0, puddles = 0;
    static std::uint32_t hash(std::uint32_t n) { n ^= n >> 16; n *= 0x7feb352du; n ^= n >> 15; n *= 0x846ca68bu; return n ^ (n >> 16); }
    static double rainStart(std::int64_t s) { return double(s) * 3600 + 600 + hash(std::uint32_t(s) ^ 0x54a3u) % 1200; }
    static double rainDuration(std::int64_t s) { return 90 + hash(std::uint32_t(s) ^ 0x781bu) % 91; }
    static float envelope(double age, double d) { return float(std::clamp(std::min(age / 12., (d - age) / 18.), 0., 1.)); }
    static OriginalWeather at(double seconds) {
        OriginalWeather w;
        w.wet = float(std::max(0., 1 - seconds / 720.)); w.puddles = float(std::max(0., 1 - seconds / 900.));
        const auto slot = std::int64_t(seconds / 3600);
        for (auto i = std::max<std::int64_t>(0, slot - 1); i <= slot; ++i) {
            double age = seconds - rainStart(i), duration = rainDuration(i);
            if (age >= 0) {
                w.rain = std::max(w.rain, envelope(age, duration));
                w.wet = std::max(w.wet, float(age <= duration ? std::min(1., age / 45.) : std::max(0., 1 - (age - duration) / 720.)));
                w.puddles = std::max(w.puddles, float(age <= duration ? std::min(1., age / 80.) : std::max(0., 1 - (age - duration) / 900.)));
            }
            double gust = seconds - (double(i) * 3600 + 2500 + hash(std::uint32_t(i) ^ 0x193u) % 400);
            w.wind = std::max(w.wind, envelope(gust, 100 + hash(std::uint32_t(i) ^ 0x992u) % 61));
        }
        w.wind = std::max(w.wind, w.rain * .65f);
        return w;
    }
};

void weather() {
    using namespace systems;
    int rainy = 0;
    for (double s = 0; s < 4 * 3600; s += 7.25) {
        auto a = weatherAt(s);
        auto b = OriginalWeather::at(s);
        CHECK(a.rain == b.rain && a.wind == b.wind && a.wet == b.wet && a.puddles == b.puddles);
        rainy += a.rain > 0.5f;
        CHECK(a.rain >= 0 && a.rain <= 1 && a.wet >= 0 && a.wet <= 1);
    }
    CHECK(rainy > 0);
    // a shower in slot 1: wet while it rains, dries afterwards
    const double start = weatherShowerStart(1), duration = weatherShowerDuration(1);
    CHECK(weatherAt(start + duration / 2).rain > 0.5f && weatherAt(start + duration).wet > 0.9f);
    CHECK(weatherAt(start + duration + 1000).wet == 0);
    // other seeds move the showers; no rain at all with rainChance 0
    WeatherTimelineConfig other; other.seed = 7;
    CHECK(weatherShowerStart(1, other) != start || weatherShowerDuration(1, other) != duration);
    WeatherTimelineConfig dry; dry.rainChance = 0; dry.openWet = false;
    for (double s = 0; s < 3 * 3600; s += 13) CHECK(weatherAt(s, dry).rain == 0 && weatherAt(s, dry).wet == 0);
    CHECK(weatherAt(-1).rain == 0 && weatherAt(NAN).wind == 0);
    CHECK(std::abs(weatherDayPhase(0, 1440, 720) - 0.5) < 1e-12 && std::abs(weatherDayPhase(360, 1440, 720) - 0.75) < 1e-12);
}

void vehicleFx() {
    using namespace mechanics;
    VehicleLights l;
    l.motion(10, 9, 9, 0.1, 5.0, false);    // -10 per second: hard braking
    CHECK(l.braking && l.level("brake", true, 5.1) == 1);
    l.motion(9, 9, 9, 0.1, 5.5, false);
    CHECK(!l.braking);
    l.motion(1, 1, -1, 0.1, 6, false);
    CHECK(l.reversing && l.level("reverse", false, 6) == 1);
    l.left = true;
    CHECK(l.level("indicator_front", true, 0.1) == 1 && l.level("indicator_front", true, 0.5) == 0);
    CHECK(l.level("indicator_rear", false, 0.1) == 0);
    l.hazards = true;
    CHECK(l.level("indicator_rear", false, 0.1) == 1);
    CHECK(l.level("beacon_l", true, 0.05) == 0);
    l.beacons = true;
    CHECK(l.level("beacon_l", true, 0.05) == 1 && l.level("beacon_l", true, 0.15) == 0 && l.level("beacon_l", true, 0.25) == 1);
    CHECK(l.level("strobe", false, 0.05) == 0 && l.level("strobe", false, 0.45) == 1);   // right side 0.4 s later
    CHECK(l.level("taxi_available", true, 0) == 1 && l.level("roof_sign", true, 0) == 1);
    CHECK(vehicleLampOf("unknown") == VehicleLamp::None && l.level("unknown", true, 0) == 0);
    l.indicateFromSteer(0.5f);
    CHECK(l.right && !l.left);

    TyreTrails t;
    CHECK(TyreTrails::slipping(10, 0, 0, true, true, 0) && !TyreTrails::slipping(10, 0, 0, false, true, 0));
    CHECK(!TyreTrails::slipping(2, 1, 1, true, true, 0) && TyreTrails::slipping(10, 0.2f, 0.3f, true, false, 0));
    for (int i = 0; i < 100; ++i) t.sample(1, {float(i) * 0.1f, 0, 0}, true, double(i) * 0.01);
    CHECK(t.segments.size() == 99);
    t.sample(1, {50, 0, 0}, true, 2.0);   // jump: breaks, no segment
    CHECK(t.segments.size() == 99);
    t.sample(1, {50.01f, 0, 0}, true, 2.0);   // shorter than minStep: merged
    CHECK(t.segments.size() == 99);
    CHECK(t.fade(t.segments.front(), 0) == 1 && std::abs(t.fade(t.segments.front(), t.segments.front().born + 30) - 0.5f) < 1e-6f);
    t.expire(40);
    CHECK(t.segments.empty());
    TyreTrailsConfig small; small.limit = 10;
    for (int i = 0; i < 100; ++i) t.sample(2, {float(i), 0, 0}, true, 0, small);
    CHECK(t.segments.size() == 10);
    t.keepOnly({});
    CHECK(t.wheels.empty());
    auto p = wheelContact({0, 0, 0}, 0, 1, 1, 2, 0, 0);   // facing +z, right = +x: front-left wheel at (-1, 2)
    CHECK(p.x == -1 && p.z == 2 && wheelContact({0, 0, 0}, 0, 1, 1, 2, 3, 0).x == 1);
}

Fixed f(int n) { return Fixed::fromInt(n); }
Fixed q(int a, int b) { return Fixed::fromRatio(a, b); }

void swept() {
    // pedestrian 5 cells ahead of a car moving +x at 2 cells/s: relative velocity -2
    auto w = sweptBoxCircle({f(5), {}}, {f(-2), {}}, {f(1), {}}, f(1), q(1, 2), q(1, 5), f(3));
    CHECK(w.hit && fxAbs(w.enter - q(19, 10)) < q(1, 1000) && w.exit == f(3));
    CHECK(!sweptBoxCircle({f(5), {}}, {f(-2), {}}, {f(1), {}}, f(1), q(1, 2), q(1, 5), f(1)).hit);   // too far for 1 s
    CHECK(!sweptBoxCircle({f(5), f(3)}, {f(-2), {}}, {f(1), {}}, f(1), q(1, 2), q(1, 5), f(10)).hit); // passes beside
    CHECK(sweptBoxCircle({{}, {}}, {}, {f(1), {}}, f(1), q(1, 2), q(1, 5), f(1)).hit);                // standing inside
    CHECK(!sweptBoxCircle({f(5), {}}, {f(2), {}}, {f(1), {}}, f(1), q(1, 2), q(1, 5), f(10)).hit);    // moving away

    std::vector<AxisBox> boxes{inflate(axisBoxMilli(2000, 0, 1000, 4000), q(1, 10))};   // wall x 1.5..2.5, z -2..2
    CHECK(boxes[0].minX == q(14, 10) && boxes[0].maxZ == q(21, 10));
    Fixed tx = f(3), tz = f(1);
    slideMove(f(1), {}, tx, tz, boxes);
    CHECK(tx == q(14, 10) && tz == f(1));   // stopped at the face, slides along z
    tx = f(1); tz = f(3);
    slideMove({}, {}, tx, tz, boxes);
    CHECK(tx == f(1) && tz == f(3));        // not in the way
    std::vector<Vec2> line{{f(1), f(1)}, {f(4), f(2)}, {f(3), f(-1)}};
    auto b = polylineBounds<Vec2>(line, q(1, 2));
    CHECK(b.valid && b.box.minX == q(1, 2) && b.box.maxZ == q(5, 2) && b.box.minZ == q(-3, 2));
    CHECK(mayTouch(b, f(4), f(2)) && !mayTouch(b, f(6), f(2)) && mayTouch(b, f(6), f(2), f(2)));
    CHECK(mayTouch(polylineBounds<Vec2>({}, {}), f(100), f(100)));
}

// A std::map with the erase / insert counters NodeCache needs.
struct CountedMap : std::map<std::string, int> {
    std::uint64_t e = 0, i = 0;
    std::uint64_t erasures() const { return e; }
    std::uint64_t inserts() const { return i; }
};

void derivedKeys() {
    runtime::DenseKeys dense("resident_", "_home");
    CHECK(dense(7) == "resident_7_home" && &dense(7) == &dense(7) && dense(0) == "resident_0_home");
    runtime::SparseKeys sparse("car_", {}, 2);
    CHECK(sparse(123456789) == "car_123456789");
    sparse(1); sparse(2); sparse(3);
    CHECK(sparse.keys.size() <= 3 && sparse(9) == "car_9");
    CountedMap vars;
    vars["a1"] = 1;
    runtime::NodeCache<2> cache;
    auto key = [](std::size_t f) { return f == 0 ? std::string("a1") : std::string("b1"); };
    auto p = cache.lookup(vars, 1, key);
    CHECK(p[0] && *p[0] == 1 && !p[1]);
    vars["b1"] = 2; ++vars.i;
    p = cache.lookup(vars, 1, key);
    CHECK(p[1] && *p[1] == 2);
    vars.erase("a1"); ++vars.e;
    p = cache.lookup(vars, 1, key);
    CHECK(!p[0] && p[1] && *p[1] == 2);
}

void trafficRoutes() {
    using namespace mechanics;
    // J0 --narrow (2 cells)-- J3 --destination piece
    //  \--wide W1-- J1 --wide W2--/
    TrafficNet net;
    auto piece = [](int width, int j0, int j1) { TrafficPiece p; p.across0 = 0; p.across1 = width - 1; p.lanes = 1; p.junction = {j0, j1}; return p; };
    net.pieces = {piece(2, 0, 3), piece(4, 0, 1), piece(4, 1, 3), piece(2, 3, -1)};
    net.junctions.resize(4);
    net.junctions[0].piece = {-1, 0, -1, 1};          // east: narrow, south: wide
    net.junctions[1].piece = {-1, -1, 1, 2};
    net.junctions[3].piece = {0, 3, -1, 2};
    CHECK(trafficCarriageway(net, net.pieces[0]) == f(2) && trafficCarriageway(net, net.pieces[1]) == f(4));
    CHECK(trafficWideExit(net, 0, kTrafficWest, -1, 3, kTrafficEast) == kTrafficSouth);
    CHECK(trafficWideExit(net, 0, kTrafficWest, -1, 3, kTrafficEast, f(2)) == kTrafficEast);   // narrow allowed: shorter
    CHECK(trafficWideExit(net, 3, kTrafficWest, 0, 3, kTrafficNorth) == kTrafficEast);          // destination attached
    CHECK(trafficWideExit(net, 0, kTrafficWest, 0, 0, kTrafficEast) == kTrafficEast);           // already there

    // detour around a wall 1 cell ahead, from (0, 0) to (3, 0)
    auto clear = [](Vec2 p) { return !(p.x >= f(1) && p.x <= f(2) && p.y >= f(-2) && p.y <= f(2)); };
    auto route = trafficLocalDetour({}, {f(3), {}}, clear);
    CHECK(route.size() >= 3 && route.front() == Vec2{});
    CHECK((route.back() - Vec2{f(3), {}}).lengthSq() <= q(9, 4));
    for (auto& p : route) CHECK(clear(p));
    auto again = trafficLocalDetour({}, {f(3), {}}, clear);
    CHECK(again == route);
    auto boxed = [](Vec2 p) { return fxAbs(p.x) < q(1, 2) && fxAbs(p.y) < q(1, 2); };
    CHECK(trafficLocalDetour({}, {f(3), {}}, boxed).empty());
}

} // namespace

int main() {
    calendar();
    weather();
    vehicleFx();
    swept();
    derivedKeys();
    trafficRoutes();
    if (failures) { std::printf("%d failure(s)\n", failures); return 1; }
    std::printf("city library tests passed\n");
    return 0;
}
