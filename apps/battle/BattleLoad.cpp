#include "BattleLoad.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include "HeroDuel.h"
#include "engine/core/Random.h"
#include "engine/net/NetClient.h"
#include "engine/net/SecureUdp.h"
#include "engine/net/Smoothing.h"

namespace aiverse::battle {

namespace {
using Clock = std::chrono::steady_clock;

struct Bot {
    uint64_t matchId = 0;
    uint8_t slot = 0;
    int players = 2;
    std::string ip;
    uint16_t port = 0;
    net::ClientTicket ticket;
    std::unique_ptr<net::NetClient> net;
    std::unique_ptr<Simulation> sim;
    BotThumbs thumbs;
    bool ended = false, suspended = false, corrupt = false, corrupted = false;
    net::RollbackSession* hooked = nullptr;   // the session the rollback hook is installed on
    std::unique_ptr<net::CorrectionSmoother> smoother;
    std::map<uint64_t, std::pair<float, float>> before, drawn, raw;
    uint32_t reconnects = 0;
};

uint64_t mapHash(const std::string& json) { Hasher h; h.bytes(json.data(), json.size()); return h.value(); }

// HeroDuel (HeroDuel.h) behind the BattleGame interface: loadouts = (hp, damage).
class HeroDuelGame final : public BattleGame {
public:
    std::string id() const override { return "heroduel"; }
    std::string mapJson(uint32_t players, uint32_t ticks) const override { return heroDuelMap(int(players), int(ticks), 1000000); }
    std::vector<uint8_t> loadout(uint32_t p) const override { return {uint8_t(100 + p * 10), uint8_t(10 + p)}; }
    std::unique_ptr<Simulation> create(const SimConfig& cfg, const net::MatchPayloadMsg* payload, const std::string& map) override {
        std::map<uint8_t, std::pair<int32_t, int32_t>> loadouts;
        if (payload) for (auto& pl : payload->players) if (pl.loadout.size() >= 2) loadouts[pl.slot] = {pl.loadout[0], pl.loadout[1]};
        auto sim = std::make_unique<Simulation>(cfg, std::make_unique<HeroDuelModule>(loadouts));
        sim->setup(Json::object(), Json::parse(map));
        return sim;
    }
    std::vector<uint8_t> resultBlob(Simulation& sim) override { return HeroDuelModule::resultBlob(sim); }
    void bot(Simulation& sim, uint8_t slot, BotThumbs& b, uint64_t now, std::vector<Command>& out) override {
        // Bot "thumbs": wander, aim at the nearest enemy, fire when close, dash now and then.
        if (now >= b.nextTurn) {
            const float a = float(b.rng.below(3600)) / 3600.0f * 6.2831853f;
            const bool stop = b.rng.below(5) == 0;
            b.mx = stop ? 0 : std::cos(a); b.mz = stop ? 0 : std::sin(a);
            b.nextTurn = now + 300 + b.rng.below(1200);
            if (b.rng.below(6) == 0) out.push_back(net::heroAbilityCommand(slot, topdown::Ability{}));
        }
        if (now < b.nextInput) return;
        b.nextInput = now + 50;
        float ax = 0, az = 0, best = 1e9f;
        Hero* me = HeroDuelModule::hero(sim, slot);
        if (me) sim.world().each<Hero>([&](Entity, Hero& h) {
            if (h.team == me->team || h.respawn) return;
            const float dx = float((h.pos.x - me->pos.x).toDouble()), dz = float((h.pos.y - me->pos.y).toDouble());
            const float d = dx * dx + dz * dz;
            if (d < best) { best = d; ax = dx; az = dz; }
        });
        if (auto s = b.quant.update(b.mx, b.mz, ax, az, best < 144.0f)) out.push_back(net::heroInputCommand(slot, *s));
    }
    void heroPositions(Simulation& sim, std::map<uint64_t, std::pair<float, float>>& out) override {
        sim.world().each<Hero>([&](Entity, Hero& h) { if (!h.respawn) out[h.slot] = {float(h.pos.x.toDouble()), float(h.pos.y.toDouble())}; });
    }
    void corrupt(Simulation& sim, uint8_t slot) override { if (Hero* h = HeroDuelModule::hero(sim, slot)) h->deaths += 1; }
};
} // namespace

std::unique_ptr<BattleGame> heroDuelGame() { return std::make_unique<HeroDuelGame>(); }

std::string BattleLoadResult::summary() const {
    char b[1400];
    std::snprintf(b, sizeof(b),
                  "matches %u, finished %u, results agreed %u, aborted %u, desyncs %u, broken %u | rollbacks %llu (max %u ticks), "
                  "resimulated %llu, stalls %llu, ahead avg %.1f max %u ticks | server update p50 %.3f ms p99 %.3f ms max %.3f ms, "
                  "cpu %.2f%% | per player up %.0f B/s down %.0f B/s | handshakes %llu, migrations %llu, reconnects %llu | "
                  "hero corrections %llu (max %.2f, snapped %llu), frame jump raw %.2f drawn %.2f | %.0f ms wall",
                  matches, finished, consensus, aborted, desyncs, brokenSessions, (unsigned long long)rollbacks, maxRollback,
                  (unsigned long long)resimulatedTicks, (unsigned long long)stalls, aheadAvg, aheadMax, serverUpdateP50Ms, serverUpdateP99Ms,
                  serverUpdateMaxMs, serverCpuShare * 100, upBytesPerSec, downBytesPerSec, (unsigned long long)handshakes,
                  (unsigned long long)migrations, (unsigned long long)reconnects, (unsigned long long)corrections, maxCorrection,
                  (unsigned long long)snaps, maxRawJump, maxDrawnJump, wallMs);
    return b;
}

BattleLoadResult runBattleLoad(const BattleLoadConfig& cfg) {
    const auto wall0 = Clock::now();
    BattleLoadResult res;
    net::SimNetwork simnet(cfg.seed);
    const net::Key key = net::randomKey();
    const uint64_t unixBase = 1'800'000'000'000ull;
    uint64_t now = 1000;
    simnet.setTime(now);

    net::ServerConfig scfg;
    scfg.name = "battle-load";
    scfg.udpPort = 0; scfg.wsPort = 0;
    scfg.hostedOnly = true;
    scfg.log = false;
    scfg.reconnectGraceSec = 60;
    net::Server server(scfg);
    server.setTicketClock([&] { return unixBase + now; });
    const net::Address serverAddr = net::SimNetwork::address("10.0.0.1", 27015);
    server.attachSecure(simnet.open("10.0.0.1", 27015), key);

    std::shared_ptr<BattleGame> game = cfg.game ? cfg.game : std::shared_ptr<BattleGame>(heroDuelGame());
    const std::string compat = net::compatibilityKey("load-1.0", net::kProtocolVersion, game->id());
    const uint32_t ticks = cfg.seconds * cfg.tickRate;
    const std::string mapJson = game->mapJson(cfg.playersPerMatch, ticks);
    std::vector<std::unique_ptr<Bot>> bots;
    for (uint32_t m = 0; m < cfg.matches; ++m) {
        net::MatchAllocation a;
        a.matchId = 1000 + m;
        a.gameId = "com.aiverse." + game->id();
        a.compatKey = compat; a.mapId = game->id();
        a.map.file = game->id(); a.map.title = game->id(); a.map.dataHash = mapHash(mapJson);
        for (uint32_t p = 0; p < cfg.playersPerMatch; ++p) a.map.slotTeams.push_back(uint8_t(p % 2 + 1));
        a.settings = cfg.customSettings ? cfg.settings : net::battleSettings(cfg.tickRate);
        a.seed = cfg.seed * 7919 + m;
        a.allowedCommands = game->commands();   // the hero commands only: anything else is dropped and counted
        a.joinTimeoutMs = 15000;
        a.maxDurationSec = cfg.seconds + 60;
        for (uint32_t p = 0; p < cfg.playersPerMatch; ++p) {
            net::MatchAllocation::Player ap;
            ap.playerId = "p" + std::to_string(m) + "-" + std::to_string(p);
            ap.info.slot = uint8_t(p + 1); ap.info.team = uint8_t(p % 2 + 1); ap.info.name = "Bot " + std::to_string(p + 1);
            ap.info.loadout = game->loadout(p);
            a.players.push_back(ap);
        }
        std::string err;
        if (!server.allocateMatch(a, &err)) { std::fprintf(stderr, "allocate: %s\n", err.c_str()); continue; }
        ++res.matches;
        for (auto& ap : a.players) {
            auto b = std::make_unique<Bot>();
            b->matchId = a.matchId; b->slot = ap.info.slot; b->players = int(cfg.playersPerMatch);
            b->ip = "10.1." + std::to_string(m / 200) + "." + std::to_string(m % 200 * 1 + 1);
            b->port = uint16_t(40000 + ap.info.slot * 100);
            net::TicketClaims c;
            c.matchId = a.matchId; c.playerId = ap.playerId; c.slot = ap.info.slot; c.compatHash = net::compatibilityHash(compat);
            c.expiresAtMs = unixBase + now + uint64_t(cfg.seconds + 600) * 1000;
            b->ticket = net::issueTicket(key, c, "10.0.0.1:27015");
            b->thumbs.rng.reseed(cfg.seed * 131 + a.matchId * 7 + ap.info.slot);
            Bot* raw = b.get();
            auto connect = [&simnet, &serverAddr, &cfg, raw]() -> std::unique_ptr<net::Conn> {
                ++raw->reconnects;
                return net::connectSecure(simnet.open(raw->ip, raw->port++, cfg.link), serverAddr, raw->ticket);
            };
            b->net = std::make_unique<net::NetClient>(connect(), net::ClientIdentity{ap.info.name, a.gameId, 0});
            b->reconnects = 0;
            b->net->setReconnect(connect, 120000);
            bots.push_back(std::move(b));
        }
    }
    if (cfg.desyncOne && bots.size() > 1) bots[1]->corrupt = true;

    std::vector<double> updateMs;
    double serverCpuMs = 0;
    uint64_t aheadSum = 0, aheadSamples = 0;
    const uint64_t endAt = now + uint64_t(cfg.seconds) * 1000 + 30000;
    const uint64_t suspendAt = now + uint64_t(cfg.seconds) * 1000 / 3, rebindAt = now + uint64_t(cfg.seconds) * 1000 / 2;
    Bot* sleeper = cfg.suspendOne && cfg.playersPerMatch >= 1 && !bots.empty() ? bots[0].get() : nullptr;
    Bot* mover = cfg.rebindOne && bots.size() > 1 ? bots[1].get() : nullptr;
    bool rebound = false;
    const uint64_t startNow = now;

    while (now < endAt) {
        now += cfg.stepMs;
        simnet.setTime(now);
        const auto t0 = Clock::now();
        server.update(now);
        const double ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
        updateMs.push_back(ms);
        serverCpuMs += ms;

        if (sleeper && !sleeper->suspended && now >= suspendAt && now < suspendAt + cfg.suspendMs && !sleeper->ended) {
            sleeper->suspended = true; sleeper->net->suspend(now);
            simnet.setSuspended(net::SimNetwork::address(sleeper->ip, uint16_t(sleeper->port - 1)), true);
        } else if (sleeper && sleeper->suspended && now >= suspendAt + cfg.suspendMs) {
            simnet.setSuspended(net::SimNetwork::address(sleeper->ip, uint16_t(sleeper->port - 1)), false);
            sleeper->suspended = false; sleeper->net->resume(now);
        }
        if (mover && !rebound && now >= rebindAt) {
            simnet.rebind(net::SimNetwork::address(mover->ip, uint16_t(mover->port - 1)), "10.9.9.9", uint16_t(mover->port + 500));
            rebound = true;
        }

        bool allEnded = true;
        for (auto& bp : bots) {
            Bot& b = *bp;
            if (b.suspended) { allEnded &= b.ended; continue; }   // the OS froze the app
            b.net->update(now);
            for (auto& e : b.net->takeEvents()) if (e.kind == net::NetEvent::Kind::MatchEnded) b.ended = true;
            if (b.net->matchReady()) {
                b.sim = game->create(b.net->simConfig(), b.net->matchPayload(), mapJson);
                Simulation* sim = b.sim.get();
                BattleGame* g = game.get();
                b.net->setResultReporter([sim, g] { return g->resultBlob(*sim); });
                b.net->attach(*b.sim);
            }
            if (b.sim && b.net->session() && b.hooked != b.net->session()) {
                // Presentation: rollback corrections of heroes go through the correction smoother (Smoothing.h).
                b.hooked = b.net->session();
                if (!b.smoother) b.smoother = std::make_unique<net::CorrectionSmoother>(net::SmoothingConfig{cfg.smoothingHalfLifeMs, 4.0f});
                Bot* bp2 = &b;
                BattleGame* g = game.get();
                b.net->session()->setRollbackHook([bp2, g, &res](bool before) {
                    if (before) { bp2->before.clear(); g->heroPositions(*bp2->sim, bp2->before); return; }
                    std::map<uint64_t, std::pair<float, float>> after;
                    g->heroPositions(*bp2->sim, after);
                    for (auto& [id, p0] : bp2->before) {
                        auto it = after.find(id);
                        if (it == after.end()) continue;
                        const float dx = p0.first - it->second.first, dz = p0.second - it->second.second;
                        const float d = std::sqrt(dx * dx + dz * dz);
                        if (d < 1e-4f) continue;
                        if (d > 4.0f) ++res.snaps; else ++res.corrections;
                        res.maxCorrection = std::max(res.maxCorrection, d);
                        bp2->smoother->correct(id, dx, dz);
                    }
                });
            }
            if (b.sim && b.net->session() && !b.ended) {
                std::vector<Command> cmds;
                game->bot(*b.sim, b.slot, b.thumbs, now, cmds);
                for (auto& c : cmds) { c.player = b.slot; b.sim->submit(c); }
                b.net->advance(double(cfg.stepMs) / 1000.0);
                // what a player would see: sim position + smoothing offset, per frame
                b.smoother->advance(float(cfg.stepMs));
                std::map<uint64_t, std::pair<float, float>> pos;
                game->heroPositions(*b.sim, pos);
                for (auto& [id, p] : pos) {
                    float ox, oz;
                    b.smoother->offset(id, ox, oz);
                    const std::pair<float, float> d{p.first + ox, p.second + oz};
                    auto jump = [](const std::pair<float, float>& u, const std::pair<float, float>& v) { return std::hypot(u.first - v.first, u.second - v.second); };
                    if (auto r = b.raw.find(id); r != b.raw.end() && jump(r->second, p) < 4.0f) res.maxRawJump = std::max(res.maxRawJump, jump(r->second, p));
                    if (auto r = b.drawn.find(id); r != b.drawn.end() && jump(r->second, d) < 4.0f) res.maxDrawnJump = std::max(res.maxDrawnJump, jump(r->second, d));
                    b.raw[id] = p; b.drawn[id] = d;
                }
                // Forced desync (tests): change confirmed state, so no rollback can undo it.
                if (b.corrupt && !b.corrupted && b.sim->tick() >= 40 && b.net->session()->confirmedNext() >= b.sim->tick()) {
                    game->corrupt(*b.sim, b.slot); b.corrupted = true;
                }
                if (auto* s = b.net->session()) {
                    const uint32_t ahead = s->tick() > s->confirmedNext() ? s->tick() - s->confirmedNext() : 0;
                    aheadSum += ahead; ++aheadSamples; res.aheadMax = std::max(res.aheadMax, ahead);
                }
            }
            allEnded &= b.ended;
        }
        if (allEnded && !bots.empty()) break;
    }
    // Let the last MatchEnd / reports flush.
    for (int i = 0; i < 20; ++i) { now += cfg.stepMs; simnet.setTime(now); server.update(now); }

    for (auto& bp : bots) {
        if (auto* s = bp->net->session()) {
            auto& st = s->stats();
            res.rollbacks += st.rollbacks; res.resimulatedTicks += st.resimulatedTicks; res.predictedTicks += st.predictedTicks;
            res.stalls += st.stallUpdates; res.maxRollback = std::max(res.maxRollback, st.maxRollback);
            res.brokenSessions += s->broken() ? 1 : 0;
        }
        res.reconnects += bp->reconnects;
    }
    res.reports = server.takeReports();
    for (auto& r : res.reports) { if (r.result && !r.aborted) ++res.finished; if (r.consensus) ++res.consensus; if (r.aborted) ++res.aborted; }
    res.desyncs = server.stats().desyncs;
    const auto ss = server.secureStats();
    res.handshakes = ss.handshakes; res.migrations = ss.migrations; res.rejected = ss.rejectedTickets;
    const double simSec = double(now - startNow) / 1000.0;
    const double players = double(std::max<size_t>(1, bots.size()));
    res.upBytesPerSec = double(ss.bytesIn) / simSec / players;
    res.downBytesPerSec = double(ss.bytesOut) / simSec / players;
    std::sort(updateMs.begin(), updateMs.end());
    if (!updateMs.empty()) {
        res.serverUpdateP50Ms = updateMs[updateMs.size() / 2];
        res.serverUpdateP99Ms = updateMs[std::min(updateMs.size() - 1, updateMs.size() * 99 / 100)];
        res.serverUpdateMaxMs = updateMs.back();
    }
    res.serverCpuShare = serverCpuMs / (simSec * 1000.0);
    res.aheadAvg = aheadSamples ? double(aheadSum) / double(aheadSamples) : 0;
    res.wallMs = std::chrono::duration<double, std::milli>(Clock::now() - wall0).count();
    return res;
}

} // namespace aiverse::battle
