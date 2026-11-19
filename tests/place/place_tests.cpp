// Physics sandbox place over replica (docs/replica.md "First milestone"): 16 clients on SimNet "4g",
// convergence, prediction, late join, pushes, spawn rate limits, server tick time, hostile input.
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <random>
#include <string>
#include <vector>
#include "apps/place/PlaceSim.h"

using namespace aiverse;
using namespace aiverse::place;

namespace {
int g_failed = 0, g_checks = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_failed; std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
std::vector<std::pair<const char*, std::function<void()>>>& registry() { static std::vector<std::pair<const char*, std::function<void()>>> r; return r; }
struct Reg { Reg(const char* n, std::function<void()> f) { registry().push_back({n, std::move(f)}); } };
#define TEST(name) static void name(); static Reg reg_##name(#name, name); static void name()

std::string g_scenePath;
Json exampleScene() {
    std::ifstream f(g_scenePath, std::ios::binary);
    return Json::parse(std::string((std::istreambuf_iterator<char>(f)), {}));
}
// A flat test place: ground, one box in front of the spawn point (looking along -Z), a far pile.
Json smallScene() {
    return Json::parse(R"({"tickRate": 60, "substeps": 4,
      "place": {"spawns": [[0, 0.05, 0]], "spawnBox": {"perSecond": 2, "burst": 3, "maxPerPlayer": 5}},
      "bodies": [
        {"id": "ground", "motion": "static", "position": [0, -0.5, 0], "size": [80, 1, 80]},
        {"id": "crate", "position": [0, 0.35, -1.9], "size": [0.7, 0.7, 0.7], "density": 250},
        {"id": "far", "position": [20, 0.35, 20], "size": [0.7, 0.7, 0.7]}]})");
}

// Server + loopback clients on a virtual clock, input chosen by the test.
struct Loop {
    PlaceServer server;
    std::vector<std::unique_ptr<PlaceClient>> clients;
    uint64_t now = 1000, ticks = 0;
    std::function<Input(size_t)> input = [](size_t) { return Input{}; };
    explicit Loop(const Json& scene) : server(scene) {}
    PlaceClient& add(net::LoopbackOptions lo = {20, 5, 1}) {
        lo.seed = clients.size() + 1;
        auto [a, b] = net::makeLoopbackPair(lo);
        server.attach(std::move(a));
        clients.push_back(std::make_unique<PlaceClient>(std::move(b)));
        return *clients.back();
    }
    void frame() {
        ++ticks;
        now = 1000 + ticks * 1000 / 60;
        server.update(now);
        for (size_t i = 0; i < clients.size(); ++i) { clients[i]->update(now, input(i)); clients[i]->advanceSmoothing(1000.0f / 60); }
    }
    void run(double s) { for (int i = 0; i < int(s * 60); ++i) frame(); }
    size_t bodiesSeen(const PlaceClient& c) const {
        size_t n = 0;
        for (auto id : c.replica().objects()) n += c.replica().classOf(id) == c.schema().body;
        return n;
    }
};

float dist(b3Vec3 a, b3Vec3 b) { return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z)); }

// A load test runs many thousands of *virtual* 60 Hz ticks. Print an
// inexpensive checkpoint every five virtual seconds, not per frame.
// fflush makes these updates immediately visible to a directly invoked
// executable and to verbose CTest; no simulation/physics load is reduced.
using PlaceClock = std::chrono::steady_clock;
void placeProgress(const char* phase, const SimPlace& sim, double finish,
                   PlaceClock::time_point started) {
    const auto& stats = sim.server.stats();
    const double wall = std::chrono::duration<double>(
        PlaceClock::now() - started).count();
    std::printf("  [place progress] %-23s virtual %5.1f/%5.1f s | wall %6.1f s | "
                "server avg %6.3f ms worst %6.3f ms | clients %zu | bodies %zu\n",
                phase, sim.seconds(), finish, wall, stats.tickMsAvg(),
                stats.tickMsWorst, sim.clients.size(), sim.server.bodies().size());
    const auto& p = stats.phases;
    if (p.samples) {
        const double d = double(p.samples);
        std::printf("  [place phases] poll %.3f + players %.3f + Box3D %.3f + "
                    "sync %.3f + broadcast %.3f ms/tick (%llu samples)\n",
                    p.pollMs/d, p.playersMs/d, p.physicsMs/d,
                    p.syncMs/d, p.broadcastMs/d,
                    (unsigned long long)p.samples);
    }
    std::fflush(stdout);
}

void runPlaceProgress(SimPlace& sim, double seconds, const char* phase) {
    const double finish = sim.seconds() + seconds;
    const int frames = int(seconds * 60 + 0.5); // same rounding as SimPlace::run
    const auto started = PlaceClock::now();
    for (int i = 0; i < frames; ++i) {
        sim.frame();
        if ((i + 1) % 300 == 0 || i + 1 == frames)
            placeProgress(phase, sim, finish, started);
    }
}
}

TEST(sixteen_clients_over_4g_converge_predict_and_late_join) {
    Json scene = exampleScene();
    SimPlace sim(scene, 11);
    // Measure the server phases without altering simulation, clients or the
    // strict <5ms budget. No profiling or output in the hot per-frame loop.
    sim.server.setTickPhaseProfiling(true);
    // 15 wandering / pushing / spawning bots and one steady walker patrolling open static ground.
    for (int i = 0; i < 15; ++i) sim.add("4g", std::make_unique<Bot>(uint32_t(100 + i)));
    auto probeBot = std::make_unique<Bot>(7, std::vector<b3Vec3>{{37, 0, 27}, {43, 0, 27}, {43, 0, 33}, {37, 0, 33}});
    PlaceClient& probe = sim.add("4g", std::move(probeBot));
    sim.run(1);
    // Bodies settle while the players walk over from the spawn; pushes are measured from here.
    std::map<replica::ObjectId, b3Vec3> start;
    for (auto id : sim.server.bodies()) start[id] = sim.server.scene().position(sim.server.bodyHandle(id));
    sim.server.resetTickTiming();
    size_t late = 0;
    uint64_t probeCorrectionsAtArrival = 0;
    float probeMaxAfterArrival = 0;
    bool probeArrived = false;
    const auto loadStarted = PlaceClock::now();
    while (sim.seconds() < 30) {
        if (!late && sim.seconds() >= 20) { late = sim.clients.size(); sim.add("4g", std::make_unique<Bot>(999)); }
        sim.frame();
        // The probe walks from the spawn to its square first; measure once it patrols there.
        auto p = probe.predicted().position;
        if (!probeArrived && p.x > 36 && p.z > 26 && p.z < 34) { probeArrived = true; probeCorrectionsAtArrival = probe.stats().corrections; }
        if (probeArrived) probeMaxAfterArrival = std::max(probeMaxAfterArrival, probe.stats().lastCorrection);
        const auto frame = uint64_t(std::llround(sim.seconds() * 60.0));
        if (frame && frame % 300 == 0)
            placeProgress("16 clients / 4g / +late", sim, 30, loadStarted);
    }
    double tickAvg = sim.server.stats().tickMsAvg(), tickWorst = sim.server.stats().tickMsWorst;
    sim.botsActive = false;
    runPlaceProgress(sim, 6, "quiet convergence");
    bool ready = true;
    float worst = 0, meanAwake = 0;
    size_t missing = 0, extra = 0, awake = 0;
    for (auto& e : sim.clients) {
        if (!e.client->ready()) std::printf("    not ready: slot %u connected %d replica ready %d scene %d own %u\n", e.client->replica().slot(), e.client->connected(), e.client->replica().ready(), e.client->scene() != nullptr, e.client->own());
        ready &= e.client->ready();
        auto c = sim.convergence(*e.client);
        worst = std::max(worst, c.worst);
        awake = c.awake;
        meanAwake = std::max(meanAwake, c.meanAwake);
        missing += c.missing;
        extra += c.extra;
    }
    auto lateC = sim.convergence(*sim.clients[late].client);
    size_t moved = 0;
    for (auto& [id, p] : start) if (sim.server.bodyHandle(id) && dist(p, sim.server.scene().position(sim.server.bodyHandle(id))) > 0.5f) ++moved;
    const auto& ss = sim.server.stats();
    const auto& ps = probe.stats();
    double bytes = 0;
    for (auto& e : sim.clients) bytes += double(e.client->replica().stats().bytesIn);
    bytes /= double(sim.clients.size()) * sim.seconds();
    std::printf("    %zu bodies, tick avg %.3f ms worst %.3f ms, %.0f B/s per client, convergence: resting bodies worst %.4f m, %zu awake mean %.3f m (missing %zu, extra %zu)\n",
                sim.server.bodies().size(), tickAvg, tickWorst, bytes, worst, awake, meanAwake, missing, extra);
    std::printf("    late joiner: worst %.4f m, missing %zu, extra %zu; %zu scene bodies moved > 0.5 m; %llu pushes, %llu boxes spawned\n", lateC.worst,
                lateC.missing, lateC.extra, moved, (unsigned long long)ss.pushes, (unsigned long long)ss.spawned);
    std::printf("    probe on static ground: %s, %llu reconciles, corrections > 1 cm %llu (%llu while patrolling), max while patrolling %.4f m, mean %.5f m\n",
                probeArrived ? "patrolled" : "NOT ARRIVED", (unsigned long long)ps.reconciles, (unsigned long long)ps.corrections,
                (unsigned long long)(ps.corrections - probeCorrectionsAtArrival), probeMaxAfterArrival, ps.reconciles ? ps.sumCorrection / double(ps.reconciles) : 0.0);
    CHECK(ready);
    CHECK(missing == 0 && extra == 0);
    CHECK(worst < 0.02f);
    CHECK(lateC.missing == 0 && lateC.extra == 0 && lateC.worst < 0.02f);
    CHECK(moved >= 10);
    CHECK(ss.spawned > 0);
    CHECK(probeArrived);
    // On static ground a correction only comes from an input frame lost in every redundant copy (the server
    // skips it): one step of walking, 7.5 cm. Corrections stay rare and that small.
    CHECK(probeMaxAfterArrival < 0.16f);
    CHECK(ps.corrections - probeCorrectionsAtArrival <= 3);
    CHECK(ps.sumCorrection / double(std::max<uint64_t>(1, ps.reconciles)) < 0.01);
    CHECK(tickAvg < 5.0);
}

TEST(example_scene_settles_without_players) {
    Json scene = exampleScene();
    PlaceServer server(scene);
    server.setTickPhaseProfiling(true);
    std::map<physics::BodyHandle, b3Vec3> start;
    for (auto id : server.bodies()) start[server.bodyHandle(id)] = server.scene().position(server.bodyHandle(id));
    uint64_t now = 1000;
    for (int i = 0; i < 600; ++i) server.update(now += 16);
    size_t moved = 0, awake = 0, bodies = 0;
    for (auto& [h, p] : start) {
        if (server.scene().motion(h) != physics::Motion::Dynamic) continue;
        ++bodies;
        moved += dist(p, server.scene().position(h)) > 0.3f;
        awake += server.scene().awake(h);
    }
    std::printf("    %zu dynamic bodies: %zu moved > 0.3 m, %zu awake after 10 s, tick avg %.3f ms\n", bodies, moved, awake, server.stats().tickMsAvg());
    const auto& phases = server.stats().phases;
    CHECK(phases.samples == 600);
    CHECK(phases.pollMs >= 0 && phases.playersMs >= 0 &&
          phases.physicsMs >= 0 && phases.syncMs >= 0 &&
          phases.broadcastMs >= 0);
    const double phaseSum = phases.pollMs + phases.playersMs +
        phases.physicsMs + phases.syncMs + phases.broadcastMs;
    CHECK(phaseSum <= server.stats().tickMsSum + 1.0);
    CHECK(bodies == 500);
    CHECK(moved <= 10);
    CHECK(awake <= 25);
}

TEST(push_action_and_walking_move_boxes) {
    Loop w(smallScene());
    auto& c = w.add();
    w.run(1);
    CHECK(c.ready());
    auto crate = w.server.scene().find("crate");
    b3Vec3 before = w.server.scene().position(crate);
    int n = 0;
    w.input = [&](size_t) { Input in; in.move.yaw = 0; if (n++ % 30 == 0) in.actions = kActionPush; return in; };
    w.run(0.6);
    b3Vec3 after = w.server.scene().position(crate);
    std::printf("    push moved the crate %.2f m along -Z (%llu pushes)\n", before.z - after.z, (unsigned long long)w.server.stats().pushes);
    CHECK(before.z - after.z > 0.5f);
    CHECK(w.server.stats().pushedBodies >= 1);
    // the cooldown limits pushes: 0.6 s at one request every 30 ticks, cooldown 18 ticks -> every request goes through
    CHECK(w.server.stats().pushes >= 1 && w.server.stats().pushes <= 2);
    // The client sees the crate move too (newest snapshot).
    w.input = [](size_t) { Input in; return in; };
    w.run(2);
    replica::ObjectId crateId = 0;
    for (auto id : w.server.bodies()) if (w.server.bodyHandle(id) == crate) crateId = id;
    auto seen = c.replica().latest(crateId, Schema::kBodyPos).f;
    auto at = w.server.scene().position(crate);
    CHECK(std::fabs(seen[2] - at.z) < 0.02f);
    // Walking into a light box pushes it as well (stepCharacter pushes -> Scene::applyPushes).
    auto far = w.server.scene().find("far");
    b3Vec3 f0 = w.server.scene().position(far);
    w.input = [&](size_t) {
        Input in;
        auto p = c.predicted().position;
        auto t = w.server.scene().position(far);
        in.move.yaw = std::atan2(-(t.x - p.x), -(t.z - p.z));
        in.move.moveZ = 1;
        return in;
    };
    w.run(8);
    CHECK(dist(f0, w.server.scene().position(far)) > 0.5f);
}

TEST(spawn_box_messages_work_and_are_rate_limited) {
    Loop w(smallScene());
    auto& c = w.add();
    w.run(1);
    CHECK(c.ready());
    size_t base = w.bodiesSeen(c);
    for (int i = 0; i < 10; ++i) c.spawnBox();      // burst 3: 7 refused
    w.run(1.5);
    CHECK(w.server.stats().spawned == 3);
    CHECK(w.server.stats().spawnDenied == 7);
    CHECK(c.stats().spawnDenied == 7);
    CHECK(w.bodiesSeen(c) == base + 3);
    // 2 per second refill: 6 s later the bucket is full again (burst 3)
    w.run(6);
    for (int i = 0; i < 4; ++i) c.spawnBox();
    w.run(1);
    CHECK(w.server.stats().spawned == 6);
    // At most maxPerPlayer (5) of one player's boxes live: the oldest makes room.
    for (int k = 0; k < 4; ++k) { c.spawnBox(); w.run(0.6); }
    w.run(1);
    std::printf("    spawned %llu, refused %llu, bodies %zu (base %zu)\n", (unsigned long long)w.server.stats().spawned,
                (unsigned long long)w.server.stats().spawnDenied, w.server.bodies().size(), base);
    CHECK(w.server.bodies().size() == base + 5);
    CHECK(w.bodiesSeen(c) == base + 5);
    // A spawned box falls and lands (a real dynamic body; spawned ones stack in front of the player).
    auto ids = w.server.bodies();
    auto h = w.server.bodyHandle(ids.back());
    w.run(3);
    CHECK(w.server.scene().position(h).y < 2.5f && w.server.scene().position(h).y > 0.2f);
}

TEST(prediction_correction_small_on_static_ground) {
    // A walker far from the boxes circling on open ground: only quantization is left to correct.
    Json scene = smallScene();
    Json place = scene["place"];
    place.set("spawns", Json::parse("[[-20, 0.05, -20]]"));
    scene.set("place", place);
    Loop w(scene);
    auto& c = w.add({60, 20, 1});
    w.run(1);
    CHECK(c.ready());
    int t = 0;
    w.input = [&](size_t) {   // circles with runs and jumps, away from the boxes
        Input in;
        ++t;
        in.move.yaw = 3.9f + float(t) * 0.02f;
        in.move.moveZ = 1;
        in.move.run = (t / 120) % 2;
        in.move.jump = t % 100 == 0;
        return in;
    };
    w.run(15);
    const auto& s = c.stats();
    float d = dist(c.predicted().position, w.server.characterState(c.replica().slot())->position);
    std::printf("    %llu reconciles, %llu over 1 cm, mean %.5f m, max %.4f m; predicted vs server now %.3f m\n", (unsigned long long)s.reconciles,
                (unsigned long long)s.corrections, s.sumCorrection / double(std::max<uint64_t>(1, s.reconciles)), s.maxCorrection, d);
    CHECK(s.reconciles > 200);
    CHECK(s.maxCorrection < 0.02f);
    CHECK(s.corrections == 0);
}

namespace {
struct Share {
    uint64_t reconciles = 0, corrections = 0;
    double sum = 0;
    float max = 0;
    void add(const PlaceClientStats& s, const PlaceClientStats& from = {}) {
        reconciles += s.reconciles - from.reconciles;
        corrections += s.corrections - from.corrections;
        sum += s.sumCorrection - from.sumCorrection;
        max = std::max(max, s.maxCorrection);
    }
    double percent() const { return reconciles ? 100.0 * double(corrections) / double(reconciles) : 0; }
    double mean() const { return reconciles ? sum / double(reconciles) : 0; }
    void print(const char* what) const {
        std::printf("    %-26s %6llu reconciles, %5.2f %% over 1 cm, mean %.4f m, max %.3f m\n", what, (unsigned long long)reconciles, percent(), mean(), max);
    }
};
// A low kinematic platform (walkable from the ground by the step-up) shuttling 8 m along X, a box pile beside it.
Json riderScene() {
    std::string bodies = R"({"id": "ground", "motion": "static", "position": [0, -0.5, 0], "size": [80, 1, 80]},
        {"id": "platform", "motion": "kinematic", "position": [0, 0.125, -6], "size": [4, 0.25, 4], "color": [0.9, 0.6, 0.2]})";
    for (int i = 0; i < 12; ++i)
        bodies += ",{\"id\": \"b" + std::to_string(i) + "\", \"position\": [" + std::to_string(-6 + (i % 3) * 0.75) + ", " +
                  std::to_string(0.35 + (i / 6) * 0.7) + ", " + std::to_string(-6 + (i / 3 % 2) * 0.75) + "], \"size\": [0.7, 0.7, 0.7], \"density\": 200}";
    return Json::parse(R"({"tickRate": 60, "substeps": 4, "place": {"spawns": [[0, 0.05, 0]],
        "movers": [{"body": "platform", "offset": [8, 0, 0], "period": 8}]}, "bodies": [)" + bodies + "]}");
}
}

// Prediction near bodies and other characters (docs/replica.md § Prediction against bodies): a walker on open
// ground, bots pushing piles all the time (example scene, 4g), a rider standing / walking / jumping on a moving
// platform. Prints the share of reconciles that corrected the prediction by more than 1 cm, for the full
// prediction world and for kinematic stand-ins only, and how fast the drawn position moves from corrections.
TEST(prediction_near_dynamic_obstacles) {
    auto pushers = [&](bool simulate, Share& walker, Share& bots, float& visible99, float& visibleMax) {
        SimPlace sim(exampleScene(), 21);
        sim.server.setTickPhaseProfiling(true);
        for (int i = 0; i < 15; ++i) sim.add("4g", std::make_unique<Bot>(uint32_t(300 + i)));
        sim.add("4g", std::make_unique<Bot>(7, std::vector<b3Vec3>{{37, 0, 27}, {43, 0, 27}, {43, 0, 33}, {37, 0, 33}}));
        for (auto& e : sim.clients) e.client->simulateBodies = e.client->stepOthers = simulate;
        // Correction motion of the camera per frame: camera position minus predicted.
        std::vector<float> visible;
        std::vector<b3Vec3> lastOffset(15);
        std::vector<uint64_t> lastReconciles(15);
        const auto started = PlaceClock::now();
        for (int f = 0; f < 30 * 60; ++f) {
            sim.frame();
            for (size_t i = 0; i < 15; ++i) {
                auto& c = *sim.clients[i].client;
                if (!c.ready()) continue;
                b3Vec3 d = c.cameraPosition(), p = c.predicted().position;
                b3Vec3 off{d.x - p.x, d.y - p.y, d.z - p.z};
                // A reconcile frame moves the offset by the correction itself (the drawn position stays): skip it.
                if (f > 60 && c.stats().reconciles == lastReconciles[i]) visible.push_back(dist(off, lastOffset[i]));
                lastOffset[i] = off;
                lastReconciles[i] = c.stats().reconciles;
            }
            if ((f + 1) % 300 == 0)
                placeProgress(simulate ? "predicted dynamic bodies" :
                                          "kinematic stand-ins", sim, 30, started);
        }
        for (size_t i = 0; i < 15; ++i) bots.add(sim.clients[i].client->stats());
        walker.add(sim.clients.back().client->stats());
        std::sort(visible.begin(), visible.end());
        visible99 = visible.empty() ? 0 : visible[visible.size() * 99 / 100];
        visibleMax = visible.empty() ? 0 : visible.back();
        auto& ss = sim.server.stats();
        std::printf("    server input buffer: %llu inputs, %llu underruns, %llu catch-ups\n", (unsigned long long)ss.inputs,
                    (unsigned long long)ss.inputUnderruns, (unsigned long long)ss.inputCatchUps);
        CHECK(ss.inputUnderruns + ss.inputCatchUps < ss.inputs / 100);
    };
    Share walker, bots, walkerK, botsK;
    float v99 = 0, vMax = 0, v99K = 0, vMaxK = 0;
    pushers(true, walker, bots, v99, vMax);
    pushers(false, walkerK, botsK, v99K, vMaxK);

    auto rider = [&](bool simulate) {
        Share out;
        Json scene = riderScene();
        SimPlace sim(scene, 5);
        PlaceClient& c = sim.add("4g", nullptr);
        c.simulateBodies = c.stepOthers = simulate;
        const auto platform = sim.server.scene().find("platform");
        bool aboard = false;
        PlaceClientStats from;
        int t = 0;
        sim.clients.back().script = [&](PlaceClient& cl) {
            Input in;
            if (!cl.ready()) return in;
            ++t;
            // Decisions from the server's state: what the bot does must not depend on the prediction it measures.
            const auto* st = sim.server.characterState(cl.replica().slot());
            if (!st) return in;
            auto p = st->position;
            auto q = sim.server.scene().position(platform);
            float dx = q.x - p.x, dz = q.z - p.z;
            bool on = st->ground == platform;
            if (on && !aboard) { aboard = true; from = cl.stats(); }
            if (!aboard || (!on && dx * dx + dz * dz > 1.5f * 1.5f && st->grounded)) {
                in.move.yaw = std::atan2(-dx, -dz);   // walk (back) onto the platform
                in.move.moveZ = 1;
                return in;
            }
            int phase = (t / 120) % 3;              // stand, walk a circle, jump in place
            in.move.yaw = float(t) * 0.03f;
            if (phase == 1) { in.move.moveZ = 0.5f; if (dx * dx + dz * dz > 1.0f) { in.move.yaw = std::atan2(-dx, -dz); in.move.moveZ = 0.6f; } }
            if (phase == 2) in.move.jump = t % 50 == 0;
            return in;
        };
        runPlaceProgress(sim, 40, simulate ? "moving platform / sim" :
                                                    "moving platform / stand-ins");
        CHECK(aboard);
        // The client's prediction world has the platform where the server has it (exactly, from the tick).
        auto* cp = c.staticScene();
        CHECK(cp && cp->find("platform") != physics::kNoBody);
        b3Vec3 at = moverPosition({0, 0.125f, -6}, {8, 0, 0}, 8, sim.server.replica().serverTick(), sim.server.scene().dt());
        CHECK(dist(at, sim.server.scene().position(platform)) < 1e-4f);
        out.add(c.stats(), from);
        out.max = c.stats().maxCorrection;
        return out;
    };
    Share ride = rider(true);
    walker.print("walker on open ground:");
    bots.print("15 bots pushing piles:");
    ride.print("platform rider:");
    walkerK.print("  stand-ins only, walker:");
    botsK.print("  stand-ins only, bots:");
    std::printf("    camera correction motion per frame: 99th percentile %.4f m, max %.3f m (stand-ins only %.4f / %.3f)\n", v99, vMax, v99K, vMaxK);
    CHECK(walker.corrections <= 3);
    CHECK(ride.percent() < 3);
    CHECK(bots.mean() < 0.035);           // was 0.059 m against the static copy only
    CHECK(bots.percent() < botsK.percent() + 1);
    CHECK(v99 < 0.05f && v99 < v99K);     // the camera glides corrections in (150 ms half-life), no jumps
}

TEST(malformed_input_and_messages_never_crash_the_server) {
    Loop w(smallScene());
    auto& good = w.add();
    auto& evil = w.add();
    // A raw peer that never speaks the protocol and sends garbage.
    auto [a, b] = net::makeLoopbackPair();
    w.server.attach(std::move(a));
    std::mt19937 rng(5);
    w.run(1);
    CHECK(good.ready() && evil.ready());
    for (int i = 0; i < 600; ++i) {
        net::Bytes junk(rng() % 300);
        for (auto& x : junk) x = uint8_t(rng());
        b->send(junk);
        b->update(w.now);
        evil.replica().sendInput(junk);                                  // hostile input frames of any size
        net::Bytes msg(rng() % 64);
        for (auto& x : msg) x = uint8_t(rng());
        evil.replica().send(uint16_t(rng() % 6), msg);                   // hostile remote messages
        if (i % 50 == 0) evil.replica().send(kChannelSpawnBox, net::Bytes(5000, 0xFF));
        w.frame();
    }
    w.input = [](size_t) { return Input{}; };
    w.run(3);
    CHECK(good.ready());
    CHECK(w.server.players() >= 1);
    CHECK(w.server.stats().badMessages > 0);
    auto st = w.server.characterState(evil.replica().slot());
    CHECK(!st || (std::isfinite(st->position.x) && std::isfinite(st->position.y) && std::fabs(st->position.x) < 1000));
    std::printf("    %llu inputs, %llu bad frames / messages, %llu spawned, %llu refused\n", (unsigned long long)w.server.stats().inputs,
                (unsigned long long)w.server.stats().badMessages, (unsigned long long)w.server.stats().spawned,
                (unsigned long long)w.server.stats().spawnDenied);
}

TEST(scene_config_errors_are_readable) {
    auto bad = [](const char* text) {
        try { PlaceServer s(Json::parse(text)); } catch (const JsonError& e) { return std::string(e.what()); }
        return std::string();
    };
    CHECK(bad(R"({"bodies": [], "place": {"movers": [{"body": "nope", "offset": [1, 0, 0]}]}})").find("mover body 'nope'") != std::string::npos);
    CHECK(bad(R"({"bodies": [], "place": {"spawns": []}})").find("spawns") != std::string::npos);
    CHECK(bad(R"({"bodies": [], "place": {"spawnBox": {"size": [0, 1, 1]}}})").find("spawnBox.size") != std::string::npos);
    CHECK(bad(R"({"bodies": [{"id": "a", "shape": "cone"}]})").find("unsupported shape") != std::string::npos);
}

int main(int argc, char** argv) {
    g_scenePath = argc > 1 ? argv[1] : "examples/place/scene.json";
    std::string only = argc > 2 ? argv[2] : "";
    size_t selected = 0;
    for (auto& entry : registry())
        if (only.empty() || only == entry.first) ++selected;
    size_t ordinal = 0;
    for (auto& [name, fn] : registry()) {
        if (!only.empty() && only != name) continue;
        ++ordinal;
        const auto started = PlaceClock::now();
        std::printf("[place %zu/%zu] START %s\n", ordinal, selected, name);
        std::fflush(stdout);
        int before = g_failed;
        fn();
        const double wall = std::chrono::duration<double>(
            PlaceClock::now() - started).count();
        std::printf("[place %zu/%zu] %s %s | wall %.1f s\n",
                    ordinal, selected, g_failed == before ? "PASS" : "FAIL",
                    name, wall);
        std::fflush(stdout);
    }
    std::printf("%d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
