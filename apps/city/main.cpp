// aiverse_city: the city server and its load tools (docs/city-server.md).
//   aiverse_city load  --project DIR [--map FILE] [--players N] [--seconds S] [--radius R] [--rate HZ]
//                      [--replicate all|involved] [--rules-patch JSON]      one process, virtual time, as fast as it goes
//   aiverse_city serve --project DIR [--map FILE] --port P [--seats N] [--seconds S] [--accounts FILE] ...   real UDP, real time
//                      accounts: --accounts FILE (a JSON file) or --meta-config FILE --meta-state FILE (the meta service);
//                      --absent-seconds N (default a week): who was away longer is sold out, checked every --sweep-seconds
//   aiverse_city bots  --project DIR [--map FILE] --connect HOST:PORT [--seats N] --players N [--seconds S]
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <thread>
#include "engine/net/HeroInput.h"
#include "engine/net/Udp.h"
#include "engine/meta/Modules.h"
#include "engine/net/city/CityMeta.h"
#include "engine/net/city/CityMirror.h"
#include "engine/net/city/CityNet.h"
#include "modules/rts/RtsModule.h"
#include "modules/rts/RtsScript.h"
#include "modules/rts/RtsTiles.h"
#include "modules/rts/RtsTopDown.h"
#include "modules/rts/RtsTraffic.h"

using namespace aiverse;
namespace city = aiverse::net::city;

namespace {
struct Args {
    std::string cmd, project, map, connect, accounts, metaConfig, metaState, rulesPatch, replicate = "all";
    int64_t absentSeconds = 604800, sweepSeconds = 3600;
    int mirror = 0;   // bots: the first client also keeps a local simulation that shows the city (as the game client does)
    int players = 8, seats = 0, seconds = 20, radius = 40, rate = 10, port = 47800;
    uint64_t seed = 1;
};
void merge(Json& into, const Json& patch) {
    if (!patch.isObject() || !into.isObject()) { into = patch; return; }
    for (auto& [k, v] : patch.members()) {
        Json* cur = into.find(k);
        if (cur && cur->isObject() && v.isObject()) merge(*cur, v);
        else into.set(k, v);
    }
}
// The project's map with `seats` heroes: its own hero first, the others at crossroads / doors spread over the city.
void load(const Args& a, Json& rules, Json& map) {
    const std::string dir = a.project;
    auto read = [dir](const std::string& p, std::string& out) {
        std::ifstream f(dir + "/" + p, std::ios::binary);
        if (!f) return false;
        out.assign(std::istreambuf_iterator<char>(f), {});
        return true;
    };
    const Json manifest = Json::parseFile(dir + "/game.json");
    map = Json::parseFile(dir + "/" + (a.map.empty() ? manifest["maps"][size_t(0)].getStr("file") : a.map));
    std::string err;
    if (!game::applyTilesToMap(map, read, &err)) throw std::runtime_error("map: " + err);
    rules = Json::parseFile(dir + "/" + manifest.getStr("rules", "rules.json"));
    if (!a.rulesPatch.empty()) merge(rules, Json::parse(a.rulesPatch));
    if (!game::embedScripts(rules, map, read, &err)) throw std::runtime_error("scripts: " + err);

    city::prepare(rules, map, a.seats);
}
city::Options options(const Args& a) {
    city::Options o;
    o.radius = a.radius; o.snapshotRate = a.rate;
    o.replicate = a.replicate == "involved" ? city::Replicate::Involved : city::Replicate::All;
    return o;
}
double rssMb() {
    std::ifstream f("/proc/self/status");
    std::string line;
    while (std::getline(f, line)) if (line.rfind("VmRSS:", 0) == 0) return std::atof(line.c_str() + 6) / 1024.0;
    return 0;
}
struct Thumbs { uint64_t next = 0; uint32_t rng = 1; topdown::Input in; };
uint32_t roll(uint32_t& s) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
// A wandering player: a new direction every 0.5-2.5 s, stands a fifth of the time, shoots now and then.
bool wander(Thumbs& t, uint64_t now, uint8_t slot, Command& out) {
    if (now < t.next) return false;
    t.next = now + 500 + roll(t.rng) % 2000;
    topdown::Input in;
    in.moveAngle = int32_t(roll(t.rng) & 0xFFFF);
    in.moveMag = roll(t.rng) % 5 ? 1000 : 0;
    in.aimAngle = in.moveAngle;
    in.fire = roll(t.rng) % 40 == 0;
    in.aim = in.fire;
    if (in == t.in) return false;
    t.in = in;
    out = net::heroInputCommand(slot, in);
    return true;
}
double ms(std::chrono::steady_clock::duration d) { return std::chrono::duration<double, std::milli>(d).count(); }

int runLoad(const Args& a) {
    Json rules, map;
    Args b = a;
    b.seats = std::max(a.seats, a.players);
    load(b, rules, map);
    const auto t0 = std::chrono::steady_clock::now();
    city::Server server(rules, map, a.seed, options(a));
    const double setupMs = ms(std::chrono::steady_clock::now() - t0);
    const auto seats = server.seats();
    if (seats.size() < size_t(a.players)) { std::fprintf(stderr, "only %zu seats\n", seats.size()); return 1; }
    std::vector<std::unique_ptr<city::Client>> clients;
    std::vector<Thumbs> thumbs(size_t(a.players));
    for (int i = 0; i < a.players; ++i) {
        auto pair = net::makeLoopbackPair();
        if (!server.attach(seats[size_t(i)], std::move(pair.first), "bot-" + std::to_string(i + 1))) { std::fprintf(stderr, "attach %d failed\n", i); return 1; }
        clients.push_back(std::make_unique<city::Client>(std::move(pair.second), server.content()));
        thumbs[size_t(i)].rng = uint32_t(a.seed * 2654435761u + uint32_t(i) * 40503u + 1);
    }
    Simulation& sim = server.simulation();
    const uint32_t rate = sim.tickRate(), ticks = uint32_t(a.seconds) * rate, warm = 2 * rate;
    std::vector<double> stepMs;
    uint64_t inView = 0, samples = 0;
    for (uint32_t t = 0; t < ticks + warm; ++t) {
        const uint64_t now = uint64_t(t) * 1000 / rate;
        for (size_t i = 0; i < clients.size(); ++i) {
            clients[i]->poll(now);
            Command c;
            if (clients[i]->ready() && wander(thumbs[i], now, clients[i]->slot(), c)) clients[i]->submit(c);
        }
        server.poll(now);
        const auto from = std::chrono::steady_clock::now();
        server.step();
        if (t >= warm) stepMs.push_back(ms(std::chrono::steady_clock::now() - from));
        if (t >= warm && t % rate == 0) for (auto& c : clients) { inView += c->entities().size(); ++samples; }
    }
    for (auto& c : clients) c->poll(uint64_t(ticks + warm) * 1000 / rate);
    std::sort(stepMs.begin(), stepMs.end());
    double sum = 0;
    for (double v : stepMs) sum += v;
    const double avg = sum / double(stepMs.size()), p99 = stepMs[stepMs.size() * 99 / 100], worst = stepMs.back();
    uint64_t in = 0, out = 0, snaps = 0;
    for (auto& c : clients) { in += c->bytesIn(); out += c->bytesOut(); snaps += c->snapshots(); }
    const double secs = double(ticks + warm) / rate, n = double(a.players);
    const city::Stats& s = server.stats();
    size_t cars = 0, people = 0, units = 0;
    sim.world().each<game::Unit, Transform>([&](Entity, game::Unit&, Transform&) { ++units; });
    if (auto* tr = server.module().traffic(); tr && tr->active()) if (auto* ts = tr->store(sim)) { cars = ts->cars.size(); people = ts->people.size(); }
    std::printf("{\"players\": %d, \"replicate\": \"%s\", \"radius\": %d, \"rate\": %d, \"seconds\": %d, \"setupMs\": %.0f, "
                "\"stepAvgMs\": %.3f, \"stepP99Ms\": %.3f, \"stepMaxMs\": %.3f, \"broadcastAvgMs\": %.3f, \"cpuShare\": %.3f, "
                "\"downBytesPerSec\": %.0f, \"upBytesPerSec\": %.0f, \"snapshotsPerSec\": %.1f, \"entitiesPerSnapshot\": %.1f, \"inView\": %.1f, "
                "\"events\": %llu, \"rejected\": %llu, \"trafficCars\": %zu, \"trafficPeople\": %zu, \"entities\": %zu, \"rssMb\": %.0f}\n",
                a.players, a.replicate.c_str(), a.radius, a.rate, a.seconds, setupMs, avg, p99, worst,
                double(s.broadcastNs) / 1e6 / double(std::max<uint64_t>(1, s.snapshots / uint64_t(a.players))), avg * rate / 1000.0,
                double(in) / secs / n, double(out) / secs / n, double(snaps) / secs / n,
                s.snapshots ? double(s.entities) / double(s.snapshots) : 0.0, samples ? double(inView) / double(samples) : 0.0,
                (unsigned long long)s.events, (unsigned long long)s.rejected, cars, people, units, rssMb());
    std::vector<std::pair<uint64_t, uint16_t>> top;
    for (auto& [type, count] : s.byType) top.push_back({count, type});
    std::sort(top.rbegin(), top.rend());
    std::fprintf(stderr, "snapshot entries by type:");
    for (size_t i = 0; i < top.size() && i < 10; ++i) std::fprintf(stderr, " %s %.1f%%", server.module().rules().types[top[i].second].id.c_str(), 100.0 * double(top[i].first) / double(std::max<uint64_t>(1, s.entities)));
    std::fprintf(stderr, "\n");
    return 0;
}

uint64_t nowMs() { return uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()); }

int runServe(const Args& a) {
    Json rules, map;
    Args b = a;
    b.seats = std::max(a.seats, 1);
    load(b, rules, map);
    std::unique_ptr<city::FileAccounts> accounts;
    std::unique_ptr<meta::Service> metaService;
    std::unique_ptr<city::MetaAccounts> metaAccounts;
    city::AccountStore* store = nullptr;
    if (!a.metaConfig.empty()) {   // the meta service in this process, its state in one file (a real host keeps it in a database)
        auto modules = meta::modules::all();
        meta::modules::world(modules);
        Json state;
        if (std::ifstream f(a.metaState, std::ios::binary); f) { const std::string text((std::istreambuf_iterator<char>(f)), {}); if (!text.empty()) state = Json::parse(text); }
        const std::string path = a.metaState;
        metaService = std::make_unique<meta::Service>(modules, meta::ServiceConfig::parse(Json::parseFile(a.metaConfig)), state, [path](int64_t, const Json& next) {
            if (path.empty()) return true;
            std::ofstream f(path + ".tmp", std::ios::binary | std::ios::trunc);
            f << next.dump(-1);
            f.close();
            return f.good() && std::rename((path + ".tmp").c_str(), path.c_str()) == 0;
        });
        metaAccounts = std::make_unique<city::MetaAccounts>(*metaService);
        store = metaAccounts.get();
    } else if (!a.accounts.empty()) {
        accounts = std::make_unique<city::FileAccounts>(a.accounts);
        store = accounts.get();
    }
    city::Server server(rules, map, a.seed, options(a), store);
    auto sweep = [&] {   // who was away too long: the property goes back to the city, the money waits for him
        const auto away = metaAccounts ? metaAccounts->absent(a.absentSeconds) : accounts ? accounts->absent(a.absentSeconds) : std::vector<std::string>{};
        size_t sold = 0;
        for (auto& player : away) sold += server.sellOut(player);
        if (sold) std::printf("sold out %zu absent players\n", sold);
    };
    sweep();
    net::UdpListener listener;
    if (!listener.open(uint16_t(a.port), true)) { std::fprintf(stderr, "cannot open UDP port %d\n", a.port); return 1; }
    std::printf("city server: port %u, %zu seats, content %016llx\n", listener.port(), server.seats().size(), (unsigned long long)server.content());
    std::fflush(stdout);
    Simulation& sim = server.simulation();
    const uint32_t rate = sim.tickRate();
    const uint64_t start = nowMs();
    uint64_t guests = 0, lastReport = start, late = 0, lastSweep = start;
    double stepSum = 0, stepMax = 0;
    uint32_t steps = 0;
    size_t peak = 0;
    city::Stats before = server.stats();
    for (;;) {
        const uint64_t now = nowMs();
        if (a.seconds > 0 && now - start > uint64_t(a.seconds) * 1000) break;
        for (auto& conn : listener.poll(now)) {
            const uint8_t seat = server.freeSeat();
            if (!seat || !server.attach(seat, std::move(conn), "guest-" + std::to_string(++guests))) std::printf("refused a connection: no free seat\n");
        }
        peak = std::max(peak, server.clients());
        server.poll(now);
        if (a.sweepSeconds > 0 && now - lastSweep >= uint64_t(a.sweepSeconds) * 1000) { lastSweep = now; sweep(); }
        const uint64_t due = uint64_t(sim.tick() + 1) * 1000 / rate;
        if (now - start >= due) {
            if (now - start > due + 1000 / rate) ++late;
            const auto from = std::chrono::steady_clock::now();
            server.step();
            const double d = ms(std::chrono::steady_clock::now() - from);
            stepSum += d; stepMax = std::max(stepMax, d); ++steps;
        } else std::this_thread::sleep_for(std::chrono::milliseconds(1));
        if (now - lastReport >= 5000) {
            const city::Stats& s = server.stats();
            const double secs = double(now - lastReport) / 1000.0, n = double(std::max<size_t>(1, peak));
            std::printf("t=%4llus clients %3zu step avg %.2f ms max %.2f ms late %llu | down %.0f B/s per client, %.1f entities per snapshot | rss %.0f MB\n",
                        (unsigned long long)((now - start) / 1000), server.clients(), steps ? stepSum / steps : 0.0, stepMax, (unsigned long long)late,
                        double(s.snapshotBytes + s.reliableBytes - before.snapshotBytes - before.reliableBytes) / secs / n,
                        s.snapshots > before.snapshots ? double(s.entities - before.entities) / double(s.snapshots - before.snapshots) : 0.0, rssMb());
            std::fflush(stdout);
            before = s; lastReport = now; stepSum = 0; stepMax = 0; steps = 0; late = 0; peak = server.clients();
        }
    }
    server.saveAll();
    return 0;
}

int runBots(const Args& a) {
    Json rules, map;
    Args b = a;
    b.seats = std::max(a.seats, 1);
    load(b, rules, map);
    const uint64_t content = city::contentId(rules, map);
    const size_t colon = a.connect.rfind(':');
    if (colon == std::string::npos) { std::fprintf(stderr, "--connect HOST:PORT\n"); return 1; }
    const std::string host = a.connect.substr(0, colon);
    const uint16_t port = uint16_t(std::atoi(a.connect.c_str() + colon + 1));
    std::vector<std::unique_ptr<city::Client>> clients;
    std::vector<Thumbs> thumbs(size_t(a.players));
    for (int i = 0; i < a.players; ++i) {
        auto conn = net::connectUdp(host, port);
        if (!conn) { std::fprintf(stderr, "cannot connect\n"); return 1; }
        clients.push_back(std::make_unique<city::Client>(std::move(conn), content));
        thumbs[size_t(i)].rng = uint32_t(a.seed * 2654435761u + uint32_t(i) * 40503u + 1);
    }
    std::unique_ptr<Simulation> local;
    std::unique_ptr<city::Mirror> mirror;
    if (a.mirror) {
        local = std::make_unique<Simulation>(SimConfig{}, std::make_unique<game::RtsModule>());
        local->setup(rules, map);
        mirror = std::make_unique<city::Mirror>(*local, *clients[0]);
    }
    const uint64_t start = nowMs();
    uint64_t inView = 0, samples = 0, lastSample = start, lastFrame = start;
    for (;;) {
        const uint64_t now = nowMs();
        if (now - start > uint64_t(a.seconds) * 1000) break;
        if (mirror) { clients[0]->poll(now); mirror->update(double(now - lastFrame) / 1000.0); lastFrame = now; }
        for (size_t i = 0; i < clients.size(); ++i) {
            clients[i]->poll(now);
            Command c;
            if (clients[i]->ready() && wander(thumbs[i], now, clients[i]->slot(), c)) clients[i]->submit(c);
        }
        if (now - lastSample >= 1000) { lastSample = now; for (auto& c : clients) if (c->ready()) { inView += c->entities().size(); ++samples; } }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    size_t ready = 0;
    uint64_t in = 0, out = 0, snaps = 0;
    for (auto& c : clients) { ready += c->ready(); in += c->bytesIn(); out += c->bytesOut(); snaps += c->snapshots(); }
    const double secs = double(a.seconds), n = double(std::max<size_t>(1, ready));
    std::printf("{\"bots\": %d, \"ready\": %zu, \"downBytesPerSec\": %.0f, \"upBytesPerSec\": %.0f, \"snapshotsPerSec\": %.1f, \"inView\": %.1f}\n",
                a.players, ready, double(in) / secs / n, double(out) / secs / n, double(snaps) / secs / n, samples ? double(inView) / double(samples) : 0.0);
    if (mirror) {
        size_t units = 0, moving = 0;
        local->world().each<game::Unit, Transform>([&](Entity e, game::Unit& u, Transform& t) {
            (void)e;
            if (!static_cast<game::RtsModule&>(local->module()).rules().types[u.type].structure) { ++units; moving += t.prevPos.x != t.pos.x || t.prevPos.z != t.pos.z; }
        });
        const Entity hero = mirror->hero();
        const Vec3 at = hero.valid() ? local->world().get<Transform>(hero).pos : Vec3{};
        std::printf("{\"mirror\": true, \"shown\": %zu, \"told\": %zu, \"localUnits\": %zu, \"moving\": %zu, \"hero\": [%.1f, %.1f], \"cash\": %.0f, \"localTick\": %u}\n",
                    mirror->shown(), clients[0]->entities().size(), units, moving, at.x.toDouble(), at.z.toDouble(),
                    Fixed::fromRaw(clients[0]->account().cash).toDouble(), unsigned(local->tick()));
        mirror.reset();
    }
    return ready == clients.size() ? 0 : 2;
}
}

int main(int argc, char** argv) {
    Args a;
    if (argc > 1) a.cmd = argv[1];
    for (int i = 2; i + 1 < argc; i += 2) {
        const std::string k = argv[i], v = argv[i + 1];
        if (k == "--project") a.project = v;
        else if (k == "--map") a.map = v;
        else if (k == "--players") a.players = std::atoi(v.c_str());
        else if (k == "--seats") a.seats = std::atoi(v.c_str());
        else if (k == "--seconds") a.seconds = std::atoi(v.c_str());
        else if (k == "--radius") a.radius = std::atoi(v.c_str());
        else if (k == "--rate") a.rate = std::atoi(v.c_str());
        else if (k == "--port") a.port = std::atoi(v.c_str());
        else if (k == "--seed") a.seed = uint64_t(std::atoll(v.c_str()));
        else if (k == "--connect") a.connect = v;
        else if (k == "--mirror") a.mirror = std::atoi(v.c_str());
        else if (k == "--accounts") a.accounts = v;
        else if (k == "--meta-config") a.metaConfig = v;
        else if (k == "--meta-state") a.metaState = v;
        else if (k == "--absent-seconds") a.absentSeconds = std::atoll(v.c_str());
        else if (k == "--sweep-seconds") a.sweepSeconds = std::atoll(v.c_str());
        else if (k == "--replicate") a.replicate = v;
        else if (k == "--rules-patch") a.rulesPatch = v;
        else { std::fprintf(stderr, "unknown option %s\n", k.c_str()); return 1; }
    }
    try {
        if (a.project.empty()) throw std::runtime_error("--project DIR");
        if (a.cmd == "load") return runLoad(a);
        if (a.cmd == "serve") return runServe(a);
        if (a.cmd == "bots") return runBots(a);
        std::fprintf(stderr, "usage: aiverse_city load|serve|bots --project DIR ... (docs/city-server.md)\n");
        return 1;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "aiverse_city: %s\n", e.what());
        return 1;
    }
}
