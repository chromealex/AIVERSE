// Headless runner: bot-vs-bot matches, replay recording and verification for any game project.
//   aiverse_headless --project templates/skirmish [--map maps/valley.json] --seed 7 --record m.rpl
//   aiverse_headless --replay m.rpl
#include <algorithm>
#include "engine/data/JsonCompose.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "engine/core/Random.h"
#include "engine/sim/Replay.h"
#include "engine/sim/Simulation.h"
#include "modules/rts/RtsAI.h"
#include "modules/rts/RtsModule.h"
#include "modules/rts/RtsTiles.h"
#include "modules/rts/RtsScript.h"
#include "modules/rts/RtsProgression.h"
#include "modules/rts/gen/MapGen.h"

using namespace aiverse;
namespace fs = std::filesystem;

static std::unique_ptr<GameModule> makeModule(const std::string& name) {
    if (name == "rts") return std::make_unique<game::RtsModule>();
    return nullptr;
}

// Map JSON with tile-map terrain applied (2D games: the tileset decides cell kinds/levels/fields).
static Json loadMap(const std::string& mapPath, const std::string& projectDir) {
    Json map = Json::parseFile(mapPath);
    std::string err;
    auto read = [&](const std::string& p, std::string& out) {
        std::ifstream f(projectDir + "/" + p, std::ios::binary);
        if (!f) return false;
        out.assign(std::istreambuf_iterator<char>(f), {});
        return true;
    };
    if (!game::applyTilesToMap(map, read, &err)) throw std::runtime_error(err);
    return map;
}

// rules.json with the game's scripts embedded (RtsScript.h)
static Json loadRules(const std::string& rulesPath, const Json& map, const std::string& projectDir) {
    Json rules = readComposedJson(rulesPath, fileReader());   // + included files, extends applied
    std::string err;
    auto read = [&](const std::string& p, std::string& out) {
        std::ifstream f(projectDir + "/" + p, std::ios::binary);
        if (!f) return false;
        out.assign(std::istreambuf_iterator<char>(f), {});
        return true;
    };
    if (!game::embedScripts(rules, map, read, &err)) throw std::runtime_error(err);
    return rules;
}

// Script log lines (errors always, prints unless --quiet).
static void attachScriptLog(Simulation& sim, bool quiet) {
    auto* m = sim.module().name() == "rts" ? static_cast<game::RtsModule*>(&sim.module()) : nullptr;
    if (!m || !m->scripts()) return;
    for (auto& e : m->scripts()->log()) if (e.error || !quiet) std::printf("[script%s] %s\n", e.error ? " error" : "", e.text.c_str());
    m->scripts()->onLog = [quiet](const game::ScriptLogEntry& e) {
        if (e.error || !quiet) std::printf("[script%s t=%u] %s\n", e.error ? " error" : "", e.tick, e.text.c_str());
    };
}

struct Args {
    std::string project = "templates/skirmish", map, replay, record;
    std::string generate, symmetry = "auto", genOut, genParams;   // --generate PRESET: a generated map (template = the map)
    int players = 0;
    uint64_t seed = 1;
    uint32_t maxSeconds = 1800;
    std::string saveFile, loadFile;   // save states (Simulation::serialize)
    uint32_t saveAt = 0;              // seconds
    bool verifyRestore = false;
    uint32_t restoreAt = 0, restoreTicks = 100; // bounded forward interval, then both restoration paths
    int benchPaths = 0;   // >0: time that many path queries (HPA* vs plain A*) and exit
    int benchBody = -1;
    std::string tickTimes;  // --tick-times FILE: wall time of every simulation step, "tick microseconds" per line
    bool profile = false;   // --profile: per-system wall time at the end of the match
    int benchGroup = 0;   // >0: spawn that many units of player 1, order them to one far goal, time movement (--bench-flow on|off)
    int benchFlow = -1;   // -1: rules default, 0: off, 1: on (minGroup 1)   // planets: body index for --bench-paths (default: first surface body)
    bool quiet = false;
    bool dump = false;    // print every player's objects by type at the end (data debugging)
};

static void dumpTypes(Simulation& sim) {
    auto* m = dynamic_cast<game::RtsModule*>(&sim.module());
    for (auto& p : sim.players()) {
        std::map<std::string, int> n;
        sim.world().each<game::Unit, Owner>([&](Entity, game::Unit& u, Owner& o) { if (o.player == p.slot) n[m->rules().types[u.type].id]++; });
        std::printf("  %s:", p.name.c_str());
        for (auto& [k, v] : n) std::printf(" %s=%d", k.c_str(), v);
        std::printf("\n    income:");
        for (size_t r = 0; r < m->rules().resources.size(); ++r)
            std::printf(" %s=%.2f/s", m->rules().resources[r].id.c_str(), m->state(sim).players[p.slot].income[r].toDouble());
        std::printf("  supply %d/%d", int(m->state(sim).players[p.slot].supplyUsed.toInt()), int(m->state(sim).players[p.slot].supplyCap.toInt()));
        if (!m->state(sim).grid.foundation.empty()) {
            int paved = 0;
            for (auto f : m->state(sim).grid.foundation) if (f == p.slot) ++paved;
            std::printf("  concrete %d", paved);
        }
        std::printf("\n");
        sim.world().each<game::Unit, Owner, game::Footprint>([&](Entity, game::Unit& u, Owner& o, game::Footprint& f) {
            if (o.player == p.slot) std::printf("    %s [%d,%d %dx%d]\n", m->rules().types[u.type].id.c_str(), f.x, f.y, f.w, f.h);
        });
        sim.world().each<game::Unit, Owner, Transform, game::Orders>([&](Entity, game::Unit& u, Owner& o, Transform& t, game::Orders& ord) {
            if (o.player != p.slot || !m->rules().types[u.type].worker) return; // workers
            std::printf("    %s at (%.1f, %.1f) order=%d state=%d\n", m->rules().types[u.type].id.c_str(), t.pos.x.toDouble(), t.pos.z.toDouble(),
                        ord.queue.empty() ? -1 : int(ord.queue.front().type), ord.queue.empty() ? -1 : int(ord.queue.front().state));
        });
    }
}

static void status(Simulation& sim) {
    auto* m = dynamic_cast<game::RtsModule*>(&sim.module());
    auto& gs = m->state(sim);
    std::printf("  t=%4us", sim.tick() / sim.tickRate());
    for (auto& p : sim.players()) {
        int units = 0, structs = 0;
        sim.world().each<game::Unit, Owner>([&](Entity, game::Unit& u, Owner& o) {
            if (o.player != p.slot) return;
            if (m->rules().types[u.type].structure) ++structs; else ++units;
        });
        int garrison = 0;
        sim.world().each<game::Garrison, Owner>([&](Entity, game::Garrison& g, Owner& o) { if (o.player == p.slot) garrison += g.count; });
        std::printf(" | %-5s u=%3d s=%2d", p.name.c_str(), units, structs);
        if (garrison) std::printf(" g=%3d", garrison);
        for (size_t r = 0; r < m->rules().resources.size(); ++r)
            std::printf(" %s=%lld", m->rules().resources[r].id.c_str(), (long long)gs.players[p.slot].stock[r].toInt());
        if (!p.alive) std::printf(" (out)");
    }
    std::printf("\n");
}

// Path query benchmark: random start/goal pairs on passable ground cells (fixed seed), the same queries with
// the map's pathing mode (HPA* on big maps) and with plain A* (any length, node limit as in the game).
static int benchPaths(Simulation& sim, const Args& a) {
    auto* m = dynamic_cast<game::RtsModule*>(&sim.module());
    game::MapGrid& g = m->state(sim).grid;
    const uint8_t layer = game::kLayerGround;
    Random rng(a.seed);
    std::vector<std::pair<Vec3, Vec3>> q;
    if (g.surf) {   // planets: random cell pairs on the same body (the first body with a surface unless --bench-body)
        int body = a.benchBody;
        if (body < 0) for (size_t b = 0; b < g.surf->bodies.size() && body < 0; ++b) if (g.surf->bodies[b].hasSurface()) body = int(b);
        const game::BodyDef& bd = g.surf->bodies[size_t(body)];
        const int N = bd.faceSize;
        for (int tries = 0; int(q.size()) < a.benchPaths && tries < a.benchPaths * 1000; ++tries) {
            const int c0 = g.surf->gridCell(body, int(rng.below(6)), int(rng.below(uint32_t(N))), int(rng.below(uint32_t(N))));
            const int c1 = g.surf->gridCell(body, int(rng.below(6)), int(rng.below(uint32_t(N))), int(rng.below(uint32_t(N))));
            if (g.passable(c0 % g.w, c0 / g.w, layer) && g.passable(c1 % g.w, c1 / g.w, layer)) q.push_back({g.centerOf(c0), g.centerOf(c1)});
        }
        std::printf("Body %s: faceSize %d, %d cells\n", bd.id.c_str(), N, 6 * N * N);
    }
    while (!g.surf && int(q.size()) < a.benchPaths) {
        int x0 = int(rng.below(uint32_t(g.w))), y0 = int(rng.below(uint32_t(g.h))), x1 = int(rng.below(uint32_t(g.w))), y1 = int(rng.below(uint32_t(g.h)));
        if (g.passable(x0, y0, layer) && g.passable(x1, y1, layer)) q.push_back({g.cellCenter(x0, y0), g.cellCenter(x1, y1)});
    }
    auto t0 = std::chrono::steady_clock::now();
    g.updateClearance();
    double clearMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    auto runAll = [&](const char* name, int minCells, int maxNodes = 40000) {
        g.hpaMinCells = minCells;
        g.sphereHpaMinCells = minCells;
        std::vector<Vec3> path;
        int found = 0;
        double total = 0, worst = 0;
        size_t points = 0;
        double length = 0;
        for (auto& [s, e] : q) {
            auto t = std::chrono::steady_clock::now();
            if (g.findPath(s, e, layer, path, maxNodes, 1)) ++found;
            double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t).count();
            total += ms; worst = std::max(worst, ms); points += path.size();
            Vec3 p = s;
            for (auto& wp : path) { length += (g.surf ? (wp - p).length() : (wp - p).xz().length()).toDouble(); p = wp; }
        }
        std::printf("%-6s %d queries: found=%d avg=%.2f ms max=%.2f ms waypoints=%zu length=%.0f\n", name, int(q.size()), found, total / double(q.size()), worst, points, length);
    };
    std::printf("Map %dx%d, heightfield=%d, clearance pass %.1f ms\n", g.w, g.h, int(g.hf), clearMs);
    auto t1 = std::chrono::steady_clock::now();
    runAll("hpa", 0);  // first query builds the abstract graph
    std::printf("       (graph build + queries %.0f ms, hpa=%d fallback=%d)\n",
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t1).count(), g.hpaStats[0], g.hpaStats[1]);
    runAll("hpa", 0);
    runAll("astar", 1 << 30);
    runAll("a*full", 1 << 30, g.w * g.h);   // reference: unbounded A* (which goals are reachable at all)
    return 0;
}


static void printProfile(Simulation& sim) {
    int64_t tot = 0;
    for (size_t i = 0; i < sim.systemCount(); ++i) tot += sim.systemTime(i);
    std::printf("Profile (%u ticks, systems %.0f ms, %.3f ms/tick):\n", sim.tick(), tot / 1e6, sim.tick() ? tot / 1e6 / sim.tick() : 0.0);
    std::vector<size_t> order(sim.systemCount());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::sort(order.begin(), order.end(), [&](size_t x, size_t y) { return sim.systemTime(x) > sim.systemTime(y); });
    for (size_t i : order)
        if (sim.systemTime(i) * 200 >= tot)
            std::printf("  %-16s %8.0f ms  %6.3f ms/tick  %5.1f%%\n", sim.systemName(i).c_str(), sim.systemTime(i) / 1e6,
                        sim.systemTime(i) / 1e6 / std::max<uint32_t>(1, sim.tick()), tot ? 100.0 * sim.systemTime(i) / tot : 0.0);
}

// Group movement benchmark: N units of the first mobile combat type of player 1 spread over a block, one move order to
// the far corner (flat) / the antipode (planet). Reports ticks/s, path ms per tick, arrivals and the field cache size.
static int benchGroup(Simulation& sim, const Args& a) {
    auto* m = dynamic_cast<game::RtsModule*>(&sim.module());
    auto& gs = m->state(sim);
    game::MapGrid& g = gs.grid;
    int type = -1;
    for (size_t t = 0; t < m->rules().types.size() && type < 0; ++t) {
        const auto& ut = m->rules().types[t];
        if (!ut.structure && ut.speed.raw > 0 && !(ut.layer & game::kLayerAir) && !ut.weapons.empty() && ut.radius <= Fixed::fromRatio(1, 2) && !ut.worker) type = int(t);
    }
    if (type < 0) { std::printf("no unit type\n"); return 1; }
    const uint8_t layer = m->rules().types[size_t(type)].pathLayer;
    // start block and goal: flat = corners, planet = face 4 centre -> face 5 centre
    std::vector<int> cells;
    int goal = -1;
    const int side = int(std::ceil(std::sqrt(double(a.benchGroup)) * 1.6));
    if (g.surf) {
        const int body = gs.surface->startBody, N = g.surf->bodies[size_t(body)].faceSize;
        for (int y = 0; y < N && int(cells.size()) < a.benchGroup * 3; ++y)
            for (int x = 0; x < N; ++x) {
                const int c = g.surf->gridCell(body, 4, (N - std::min(side, N)) / 2 + x % std::min(side, N), (N - std::min(side, N)) / 2 + y);
                if (x < std::min(side, N) && g.passable(c % g.w, c / g.w, layer)) cells.push_back(c);
            }
        for (int r = 0; r < N / 2 && goal < 0; ++r) { const int c = g.surf->gridCell(body, 5, N / 2 + r, N / 2); if (g.passable(c % g.w, c / g.w, layer)) goal = c; }
    } else {
        for (int y = 4; y < g.h && int(cells.size()) < a.benchGroup * 3; ++y)
            for (int x = 4; x < 4 + side && x < g.w; ++x) if (g.passable(x, y, layer)) cells.push_back(g.index(x, y));
        // goal well inside the map (a crowd needs room around it), the start block in the opposite corner
        for (int r = g.w / 8; r < g.w / 2 && goal < 0; ++r) if (g.passable(g.w - r, g.h - r, layer)) goal = g.index(g.w - r, g.h - r);
    }
    if (goal < 0 || int(cells.size()) < a.benchGroup) { std::printf("no room for %d units\n", a.benchGroup); return 1; }
    Command c;
    c.player = 1; c.type = game::kCmdMove;
    for (int i = 0; i < a.benchGroup; ++i) {
        const int cell = cells[size_t(i) * cells.size() / size_t(a.benchGroup)];
        c.units.push_back(m->spawn(sim, uint16_t(type), 1, g.centerOf(cell)));
    }
    c.point = g.centerOf(goal);
    const Vec3 goalPos = c.point;
    // arrived = inside the area a crowd of this size occupies (~0.8 x 0.8 units per body) plus a margin
    const Fixed arriveR = Fixed::fromRatio(int64_t(std::sqrt(double(a.benchGroup)) * 60) + 600, 100);
    sim.submit(c);
    auto clock = [] { return int64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()); };
    g.clockNs = clock;
    const uint32_t ticks = a.maxSeconds * sim.tickRate();
    const auto t0 = std::chrono::steady_clock::now();
    size_t peakFields = 0, peakBytes = 0;
    uint32_t done = 0;
    int64_t peakPathNs = 0, movementNs = 0;
    for (uint32_t t = 0; t < ticks; ++t) {
        const int64_t before = g.pathNs, s0 = clock();
        sim.step();
        movementNs += clock() - s0;
        peakPathNs = std::max(peakPathNs, g.pathNs - before);
        peakFields = std::max(peakFields, g.flowCount()); peakBytes = std::max(peakBytes, g.flowBytes());
        if (t % 20 == 0 && a.quiet == false && t % 400 == 0) {
            Fixed sum{};
            int alive = 0;
            for (Entity e : c.units) if (sim.world().alive(e)) { sum += (sim.world().get<Transform>(e).pos - goalPos).length(); ++alive; }
            std::printf("  t=%3us mean distance to goal %.1f\n", t / sim.tickRate(), alive ? (sum / alive).toDouble() : 0.0);
        }
        if (t % 20 == 0) {
            int near = 0;
            for (Entity e : c.units) if (sim.world().alive(e) && (sim.world().get<Transform>(e).pos - goalPos).length() < arriveR) ++near;
            if (near >= a.benchGroup * 9 / 10 && !done) done = t;
        }
    }
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    int near = 0;
    for (Entity e : c.units) if (sim.world().alive(e) && (sim.world().get<Transform>(e).pos - goalPos).length() < arriveR) ++near;
    if (std::getenv("AIVERSE_BENCH_HIST")) {   // where the stragglers are: distance histogram, moving / stopped
        int hist[12][2] = {};
        for (Entity e : c.units) {
            if (!sim.world().alive(e)) continue;
            const int b = std::min(11, int((sim.world().get<Transform>(e).pos - goalPos).length().toInt() / 10));
            hist[b][sim.world().get<game::Mobile>(e).moving ? 1 : 0]++;
        }
        for (int b = 0; b < 12; ++b) std::printf("  dist %3d-%3d: stopped %d moving %d\n", b * 10, b * 10 + 10, hist[b][0], hist[b][1]);
    }
    (void)movementNs;
    if (a.profile) printProfile(sim);
    std::printf("Group %d x %s on %dx%d%s: %u ticks %.0f ms = %.0f ticks/s | path total %.0f ms, %.3f ms/tick, worst tick %.1f ms | arrived %d/%d (90%% at %.0f s) | fields peak %zu, %.2f MB | %016llx\n",
                a.benchGroup, m->rules().types[size_t(type)].id.c_str(), g.w, g.h, g.surf ? " planet" : "", ticks, ms, ticks / (ms / 1000.0),
                double(g.pathNs) / 1e6, double(g.pathNs) / 1e6 / ticks, double(peakPathNs) / 1e6, near, a.benchGroup, done ? double(done) / sim.tickRate() : -1.0,
                peakFields, double(peakBytes) / 1048576.0, (unsigned long long)sim.checksum());
    return 0;
}

// External input producers are not part of SimSnapshot. Copy them so both
// continuations submit identical inputs rather than restarting bot decisions.
static bool verifyRestore(Simulation& sim, std::vector<game::RtsAI>& bots,
                          const SimSnapshot& snapshot, const std::string& bytes,
                          const std::vector<game::RtsAI>& savedBots, uint64_t initialHash,
                          const std::vector<uint64_t>& expected, const SimConfig& cfg,
                          const Json& rules, const Json& map) {
    for (int mode=0;mode<3;++mode) {
        const char* name=mode==0 ? "snapshot" : mode==1 ? "serialized" : "fresh serialized";
        std::unique_ptr<Simulation> fresh;
        Simulation* target=&sim;
        if (mode==2) {
            fresh=std::make_unique<Simulation>(cfg,makeModule(sim.module().name()));
            fresh->setup(rules,map);attachScriptLog(*fresh,true);target=fresh.get();
        }
        if (mode==0) target->loadState(snapshot);
        else {
            std::string error;
            if (!target->deserialize(bytes,&error)) { std::printf("Restore FAILED (%s): %s\n",name,error.c_str());return false; }
        }
        bots=savedBots;
        if (target->checksum()!=initialHash) { std::printf("Restore FAILED (%s): initial checksum\n",name);return false; }
        for (uint64_t checksum:expected) {
            for (auto& bot:bots) bot.update(*target);
            target->step();
            if (target->checksum()!=checksum) { std::printf("Restore DESYNC (%s) at tick %u\n",name,target->tick());return false; }
        }
        auto* rm=dynamic_cast<game::RtsModule*>(&target->module());
        if (rm && rm->scripts() && rm->scripts()->errorCount()>0) { std::printf("Restore FAILED (%s): script errors\n",name);return false; }
        std::printf("Restore OK (%s): %zu per-tick checks, from tick %u\n",name,expected.size(),snapshot.tick);
    }
    return true;
}

static int play(const Args& a) {
    Json manifest = Json::parseFile(a.project + "/game.json");
    std::string rulesPath = a.project + "/" + manifest.getStr("rules", "rules.json");
    std::string mapPath = a.project + "/" + (a.map.empty() ? manifest["maps"][size_t(0)].getStr("file") : a.map);
    std::string module = manifest.getStr("module", "rts");

    SimConfig cfg;
    cfg.seed = a.seed;
    Simulation sim(cfg, makeModule(module));
    if (!a.generate.empty()) {
        // the map with a "generate" block is written next to the replay (or the scratch output) so that
        // --replay regenerates exactly the same terrain from the recorded seed
        Json m = Json::parseFile(mapPath);
        Json g = Json::object();
        g.set("preset", Json::string(a.generate));
        g.set("seed", Json::string("match"));
        g.set("players", Json::number(int64_t(a.players > 0 ? a.players : std::max<int>(2, int(m["players"].size())))));
        g.set("symmetry", Json::string(a.symmetry));
        if (!a.genParams.empty()) g.set("params", Json::parse(a.genParams));
        g.set("template", Json::string(fs::relative(mapPath, a.project).generic_string()));
        m.set("generate", g);
        mapPath = (a.record.empty() ? (fs::temp_directory_path() / ("aiverse_gen_" + a.generate + "_" + std::to_string(a.seed) + "_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".json")).string() : a.record + ".map.json");
        std::ofstream(mapPath) << m.dump();
    }
    Json mapJson = loadMap(mapPath, a.project);
    const auto setupT0 = std::chrono::steady_clock::now();
    Json rulesJson = loadRules(rulesPath, mapJson, a.project);
    if (a.benchFlow >= 0) {   // benchmark switch: shared flow fields for every group order (on) or none (off)
        Json ff = Json::object();
        ff.set("minGroup", Json::number(int64_t(a.benchFlow ? 2 : 0)));
        rulesJson.member("movement").set("flowField", ff);
    }
    sim.setup(rulesJson, mapJson);
    attachScriptLog(sim, a.quiet);
    if (!a.quiet && (a.benchPaths > 0 || mapJson["world"].isObject()))   // planets / benchmarks: setup cost incl. the HPA* graphs
        std::printf("Setup: %.0f ms\n", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - setupT0).count());
    if (auto* rm = dynamic_cast<game::RtsModule*>(&sim.module()); rm && rm->generatedMap()) {
        const Json& gm = *rm->generatedMap();
        if (!a.quiet) {
            const Json& gi = gm["generated"];
            std::string size = std::to_string(gm["size"][size_t(0)].integer()) + "x" + std::to_string(gm["size"][size_t(1)].integer());
            if (gm["world"].isObject()) {   // planet maps: bodies and face sizes
                size = gi.getStr("layout");
                for (auto& b : gm["world"]["bodies"].items())
                    if (b.has("faceSize")) size += " " + b.getStr("id") + ":" + std::to_string(b.getInt("faceSize"));
            }
            std::printf("Generated: preset=%s seed=%lld players=%lld symmetry=%s size=%s objects=%zu hash=%016llx\n", gi.getStr("preset").c_str(),
                        (long long)gi.getInt("seed"), (long long)gi.getInt("players"), gi.getStr("symmetry").c_str(), size.c_str(),
                        gm["entities"].size(), (unsigned long long)gen::mapHash(gm));
            if (gi.has("paths")) std::printf("  paths %s  connected=%d\n", gi["paths"].dump(-1).c_str(), int(gi.getBool("connected")));
            for (auto& w : gi["warnings"].items()) std::printf("  warning: %s\n", w.str().c_str());
        }
        if (!a.genOut.empty()) { std::ofstream(a.genOut) << gm.dump(); if (!a.quiet) std::printf("Generated map written: %s\n", a.genOut.c_str()); }
    }
    if (a.maxSeconds == 0) return 0;   // --max-seconds 0: set up (and generate) only

    if (a.profile) sim.setProfileClock([] { return int64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()); });
    if (a.benchPaths > 0) return benchPaths(sim, a);
    if (a.benchGroup > 0) return benchGroup(sim, a);
    std::vector<game::RtsAI> bots;
    for (auto& p : sim.players()) bots.emplace_back(p.slot, a.seed);

    Replay rep;
    rep.module = module; rep.rulesPath = rulesPath; rep.mapPath = mapPath;
    rep.dataHash = hashGameData(rulesPath, mapPath);
    rep.seed = a.seed; rep.tickRate = cfg.tickRate; rep.inputDelay = cfg.inputDelayTicks;

    if (!a.loadFile.empty()) {
        std::ifstream f(a.loadFile, std::ios::binary);
        std::string bytes((std::istreambuf_iterator<char>(f)), {}), err;
        if (!sim.deserialize(bytes, &err)) { std::printf("Load failed: %s\n", err.c_str()); return 2; }
        if (!a.quiet) std::printf("Loaded %s: t=%us\n", a.loadFile.c_str(), sim.tick() / sim.tickRate());
        // the bots' own state (waves, routes, timers), saved next to the simulation (agent-ai-terrain)
        if (std::ifstream af(a.loadFile + ".ai", std::ios::binary); af) {
            Json st = Json::parse(std::string((std::istreambuf_iterator<char>(af)), {}));
            for (auto& b : bots) {
                const std::string hex = st.getStr(std::to_string(b.player()), "");
                std::string raw;
                for (size_t i = 0; i + 1 < hex.size(); i += 2) raw.push_back(char(std::stoi(hex.substr(i, 2), nullptr, 16)));
                if (!hex.empty() && !b.loadState(raw)) { std::printf("Load failed: bot %d state\n", int(b.player())); return 2; }
            }
        }
    }
    std::map<uint16_t, int> ev;
    const uint32_t maxTicks = a.maxSeconds * sim.tickRate();
    if (a.verifyRestore && (a.restoreTicks==0 || a.restoreTicks>100000 || uint64_t(a.restoreAt)*sim.tickRate()+a.restoreTicks>maxTicks || uint64_t(a.restoreAt)*sim.tickRate()<sim.tick())) {
        std::fprintf(stderr,"restore verification requires 1..100000 ticks within the match interval\n");return 2;
    }
    SimSnapshot restoreSnapshot;
    std::vector<game::RtsAI> restoreBots;
    std::vector<uint64_t> restoreChecks;
    std::string restoreBytes;
    uint64_t restoreHash=0;
    bool restoreSaved=false,restoreVerified=false;
    std::vector<std::pair<uint32_t, int64_t>> stepTimes;   // --tick-times
    auto t0 = std::chrono::steady_clock::now();
    if (!a.quiet) std::printf("Match: %s  seed=%llu\n", mapPath.c_str(), (unsigned long long)a.seed);
    while (sim.result() == 0 && sim.tick() < maxTicks) {
        if (a.verifyRestore && !restoreSaved && sim.tick()==uint64_t(a.restoreAt)*sim.tickRate()) {
            sim.saveState(restoreSnapshot);restoreBytes=sim.serialize();restoreHash=sim.checksum();restoreBots=bots;restoreSaved=true;
        }
        for (auto& b : bots) b.update(sim);
        if (!a.tickTimes.empty() || game::luaProfileEnabled()) {
            game::luaProfileTickBegin();
            const auto s0 = std::chrono::steady_clock::now();
            sim.step();
            const int64_t us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - s0).count();
            game::luaProfileTickEnd(sim.tick(), us);
            if (!a.tickTimes.empty()) stepTimes.emplace_back(sim.tick(), us);
        } else sim.step();
        for (auto& e : sim.events()) ev[e.type]++;
        if (restoreSaved && !restoreVerified) {
            restoreChecks.push_back(sim.checksum());
            if (restoreChecks.size()>=a.restoreTicks || sim.result()!=0) {
                if (!verifyRestore(sim,bots,restoreSnapshot,restoreBytes,restoreBots,restoreHash,restoreChecks,cfg,rulesJson,mapJson)) return 1;
                restoreVerified=true;
            }
        }
        if (sim.tick() % rep.checksumInterval == 0) rep.checksums.emplace_back(sim.tick(), sim.checksum());
        if (!a.quiet && sim.tick() % (sim.tickRate() * 60) == 0) status(sim);
        if (a.saveAt && sim.tick() == a.saveAt * sim.tickRate() && !a.saveFile.empty()) {
            std::string bytes = sim.serialize();
            std::ofstream(a.saveFile, std::ios::binary) << bytes;
            Json st = Json::object();
            for (auto& b : bots) {
                const std::string raw = b.saveState();
                std::string hex;
                static const char* d = "0123456789abcdef";
                for (unsigned char ch : raw) { hex.push_back(d[ch >> 4]); hex.push_back(d[ch & 15]); }
                st.set(std::to_string(b.player()), Json::string(hex));
            }
            std::ofstream(a.saveFile + ".ai", std::ios::binary) << st.dump();
            std::printf("Saved %s: t=%us, %zu bytes, checksum %016llx\n", a.saveFile.c_str(), a.saveAt, bytes.size(), (unsigned long long)sim.checksum());
        }
    }
    if (a.verifyRestore) {
        if (!restoreVerified) { std::fprintf(stderr,"restore verification interval was not reached\n");return 2; }
        auto* rm=dynamic_cast<game::RtsModule*>(&sim.module());
        if (rm && rm->scripts() && rm->scripts()->errorCount()>0) { std::fprintf(stderr,"restore verification encountered script errors\n");return 1; }
    }
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    rep.endTick = sim.tick();
    rep.finalChecksum = sim.checksum();
    rep.commands = sim.executedCommands();
    if (!a.quiet) status(sim);
    int res = sim.result();
    std::string winner = res == 0 ? "none (time limit)" : res < 0 ? "draw" : "?";
    if (res > 0) for (auto& p : sim.players()) if (p.team == res && p.alive) { winner = p.name; break; }
    std::printf("Result: winner=%s  %.0f game-s  commands=%zu  sim=%.0f ms (%.0fx realtime)\n", winner.c_str(),
                double(sim.tick()) / sim.tickRate(), rep.commands.size(), ms, double(sim.tick()) / sim.tickRate() * 1000.0 / (ms > 0 ? ms : 1));
    std::printf("Events: spawned=%d died=%d attacks=%d trained=%d built=%d gathered=%d captured=%d\n",
                ev[game::kEvSpawned], ev[game::kEvDied], ev[game::kEvAttack], ev[game::kEvTrainDone], ev[game::kEvBuildDone],
                ev[game::kEvGathered], ev[game::kEvCaptured]);
    if (auto* rm = dynamic_cast<game::RtsModule*>(&sim.module()); rm && rm->raid() && rm->raid()->active())   // extraction raids
        if (auto* rs = rm->raid()->store(sim))
            for (size_t i = 0; i < rs->players.size(); ++i)
                std::printf("Raid receipt: %s\n", rs->receipts[i].empty() ? rm->raid()->receiptJson(sim, rs->players[i]).c_str() : rs->receipts[i].c_str());
    if (auto* rm = dynamic_cast<game::RtsModule*>(&sim.module()); rm && rm->progression() && rm->progression()->active())   // in-match progression
        if (auto* ps = rm->progression()->store(sim))
            for (auto& [player, p] : ps->perks.players) {
                std::string perks;
                for (auto& o : p.mods) {
                    auto d = rm->progression()->rules().defs.perks.find(o.perk);
                    perks += (perks.empty() ? "" : ",") + (d != rm->progression()->rules().defs.perks.end() && !d->second.key.empty() ? d->second.key : std::to_string(o.perk));
                }
                uint32_t recruits = 0;
                for (auto [slot, n] : p.recruits) if (slot > 0) recruits += n;
                std::printf("Progression: player=%lld level=%u xp=%lld kills=%lld perks=[%s] recruits=%u orbs=%zu\n", (long long)player, p.level, (long long)p.xp,
                            (long long)(size_t(player) < ps->kills.size() ? ps->kills[size_t(player)] : 0), perks.c_str(), recruits, ps->pickups.orbs.size());
            }
    if (a.dump) dumpTypes(sim);
    if (a.profile) printProfile(sim);
    game::luaProfileWrite();
    if (!a.tickTimes.empty()) {
        std::ofstream f(a.tickTimes);
        for (auto& [t, us] : stepTimes) f << t << ' ' << us << '\n';
    }
    std::printf("Checksum: %016llx\n", (unsigned long long)rep.finalChecksum);
    if (!a.record.empty()) {
        if (!rep.save(a.record)) return 2;
        std::printf("Replay saved: %s\n", a.record.c_str());
    }
    return 0;
}

static int replay(const Args& a) {
    Replay rep;
    std::string err;
    if (!rep.load(a.replay, &err)) { std::fprintf(stderr, "replay: %s\n", err.c_str()); return 2; }
    if (hashGameData(rep.rulesPath, rep.mapPath) != rep.dataHash) { std::fprintf(stderr, "replay: data files changed\n"); return 2; }
    SimConfig cfg;
    cfg.seed = rep.seed; cfg.tickRate = rep.tickRate; cfg.inputDelayTicks = rep.inputDelay;
    Simulation sim(cfg, makeModule(rep.module));
    {
        const std::string dir = fs::path(rep.rulesPath).parent_path().string();
        Json map = loadMap(rep.mapPath, dir);
        sim.setup(loadRules(rep.rulesPath, map, dir), map);
    }
    for (auto& c : rep.commands) sim.submit(c);
    size_t ci = 0;
    while (sim.tick() < rep.endTick) {
        sim.step();
        if (ci < rep.checksums.size() && rep.checksums[ci].first == sim.tick()) {
            if (sim.checksum() != rep.checksums[ci].second) { std::printf("DESYNC at tick %u\n", sim.tick()); return 1; }
            ++ci;
        }
    }
    bool ok = sim.checksum() == rep.finalChecksum;
    std::printf("Replay %s: %u ticks, %zu commands, %zu checkpoints\n", ok ? "OK" : "MISMATCH", sim.tick(), rep.commands.size(), ci);
    return ok ? 0 : 1;
}

#include "FpsRunner.h"

int main(int argc, char** argv) {
    Args a;
    for (int i = 1; i < argc; ++i) {
        std::string k = argv[i];
        auto next = [&]() -> std::string { if (i + 1 >= argc) std::exit(2); return argv[++i]; };
        if (k == "--project") a.project = next();
        else if (k == "--map") a.map = next();
        else if (k == "--seed") a.seed = std::stoull(next());
        else if (k == "--max-seconds") a.maxSeconds = uint32_t(std::stoul(next()));
        else if (k == "--record") a.record = next();
        else if (k == "--save") a.saveFile = next();
        else if (k == "--save-at") a.saveAt = uint32_t(std::stoul(next()));
        else if (k == "--load") a.loadFile = next();
        else if (k == "--verify-restore-at") { a.verifyRestore=true;a.restoreAt=uint32_t(std::stoul(next())); }
        else if (k == "--verify-restore-ticks") { a.verifyRestore=true;a.restoreTicks=uint32_t(std::stoul(next())); }
        else if (k == "--replay") a.replay = next();
        else if (k == "--quiet") a.quiet = true;
        else if (k == "--profile") a.profile = true;
        else if (k == "--tick-times") a.tickTimes = next();
        else if (k == "--dump") a.dump = true;
        else if (k == "--bench-paths") a.benchPaths = std::stoi(next());
        else if (k == "--bench-body") a.benchBody = std::stoi(next());
        else if (k == "--bench-group") a.benchGroup = std::stoi(next());
        else if (k == "--bench-flow") a.benchFlow = next() == "on" ? 1 : 0;
        else if (k == "--planet-hpa") {
#if defined(_WIN32)
            _putenv_s("AIVERSE_PLANET_HPA", next() == "off" ? "0" : "1");
#else
            setenv("AIVERSE_PLANET_HPA", next() == "off" ? "0" : "1", 1);
#endif
        }   // force planet HPA* on / off
        else if (k == "--generate") a.generate = next();
        else if (k == "--players") a.players = std::stoi(next());
        else if (k == "--symmetry") a.symmetry = next();
        else if (k == "--gen-params") a.genParams = next();
        else if (k == "--gen-out") a.genOut = next();
        else { std::printf("usage: aiverse_headless --project DIR [--map FILE] [--seed N] [--record F] [--save F --save-at S] [--load F] [--tick-times F] [--verify-restore-at S --verify-restore-ticks N] [--generate PRESET [--players N] [--symmetry S] [--gen-params JSON] [--gen-out F]] | --replay F\n"); return 2; }
    }
    if (a.verifyRestore && (!a.replay.empty() || a.benchPaths>0 || a.benchGroup>0 || a.maxSeconds==0)) { std::fprintf(stderr,"restore verification requires a normal match\n");return 2; }
    try {
        if(a.replay.empty()&&Json::parseFile(a.project+"/game.json").getStr("module","rts")=="fps")return playFps(a);
        return a.replay.empty() ? play(a) : replay(a);
    }
    catch (const std::exception& e) { std::fprintf(stderr, "error: %s\n", e.what()); return 2; }
}
