// battle_net_simulate: hosted battles over a simulated mobile network (docs/battle-net.md). Lets an
// agent check the network settings of a direct-control battle (tick rate, input delay, rollback
// window) against loss / RTT / jitter before anything is deployed. Nothing is saved.
#include <algorithm>
#include "Workspace.h"
#include "apps/battle/BattleLoad.h"

namespace aiverse::editor {

namespace {
Json simulate(const Json& p) {
    battle::BattleLoadConfig cfg;
    bool found = true;
    cfg.link = net::linkPreset(p.getStr("profile", "bad"), &found);
    if (!found) throw std::runtime_error("profile: lan, wifi, 4g, 3g or bad");
    if (p.has("loss")) cfg.link.lossPercent = uint32_t(std::clamp<int64_t>(p.getInt("loss"), 0, 60));
    if (p.has("rttMs")) cfg.link.latencyMs = uint32_t(std::clamp<int64_t>(p.getInt("rttMs"), 0, 1000)) / 2;
    if (p.has("jitterMs")) cfg.link.jitterMs = uint32_t(std::clamp<int64_t>(p.getInt("jitterMs"), 0, 500));
    cfg.matches = uint32_t(std::clamp<int64_t>(p.getInt("matches", 2), 1, 16));
    cfg.playersPerMatch = uint32_t(std::clamp<int64_t>(p.getInt("players", 2), 1, 8));
    cfg.seconds = uint32_t(std::clamp<int64_t>(p.getInt("seconds", 15), 3, 120));
    cfg.tickRate = uint32_t(std::clamp<int64_t>(p.getInt("tickRate", 20), 5, 60));
    cfg.seed = uint64_t(p.getInt("seed", 1));
    cfg.suspendOne = p.getBool("suspend");
    cfg.rebindOne = p.getBool("rebind");
    cfg.settings = net::battleSettings(cfg.tickRate);
    if (p.has("inputDelay")) { cfg.settings.inputDelay = uint32_t(std::clamp<int64_t>(p.getInt("inputDelay"), 0, 10)); cfg.customSettings = true; }
    if (p.has("rollbackWindow")) { cfg.settings.rollbackWindow = uint32_t(std::clamp<int64_t>(p.getInt("rollbackWindow"), 0, 60)); cfg.customSettings = true; }
    if (p.has("lagToleranceMs")) { cfg.settings.lagToleranceMs = uint32_t(std::clamp<int64_t>(p.getInt("lagToleranceMs"), 0, 2000)); cfg.customSettings = true; }
    const auto r = battle::runBattleLoad(cfg);
    Json j = Json::object();
    auto n = [&](const char* k, int64_t v) { j.set(k, Json::number(v)); };
    auto d = [&](const char* k, double v, int dec = 2) { j.set(k, Json::number(v, dec)); };
    n("matches", r.matches); n("finished", r.finished); n("resultsAgreed", r.consensus); n("desyncs", r.desyncs);
    n("rollbacks", int64_t(r.rollbacks)); n("maxRollbackTicks", r.maxRollback); n("stalledUpdates", int64_t(r.stalls));
    d("aheadOfConfirmationAvgTicks", r.aheadAvg); n("aheadOfConfirmationMaxTicks", r.aheadMax);
    d("serverUpdateP99Ms", r.serverUpdateP99Ms, 3); d("serverCpuShare", r.serverCpuShare, 4);
    d("upBytesPerSecPerPlayer", r.upBytesPerSec, 0); d("downBytesPerSecPerPlayer", r.downBytesPerSec, 0);
    n("reconnects", int64_t(r.reconnects)); n("migrations", int64_t(r.migrations));
    Json s = Json::object();
    s.set("tickRate", Json::number(int64_t(cfg.settings.tickRate)));
    s.set("inputDelay", Json::number(int64_t(cfg.settings.inputDelay)));
    s.set("rollbackWindow", Json::number(int64_t(cfg.settings.rollbackWindow)));
    s.set("lagToleranceMs", Json::number(int64_t(cfg.settings.lagToleranceMs)));
    j.set("settings", s);
    // Plain-language hints for the agent.
    std::string hint;
    if (r.desyncs) hint = "Desync: the simulation is not deterministic for this input.";
    else if (r.stalls > r.predictedTicks / 20) hint = "The local player often waits for confirmations: raise rollbackWindow or lagToleranceMs.";
    else if (r.maxRollback + 1 >= cfg.settings.rollbackWindow) hint = "Rollbacks reach the window: raise rollbackWindow if weak devices can re-simulate that many ticks.";
    else hint = "Settings hold on this link.";
    j.set("hint", Json::string(hint));
    j.set("summary", Json::string(r.summary()));
    return j;
}
} // namespace

void registerBattleNetOps(Workspace& ws) {
    ws.registerOp({"battle_net_simulate",
        "Network check of direct-control battles (hosted battle server + secure UDP + rollback) on a simulated mobile link: "
        "bot players in a reference top-down duel, all on a virtual clock. 'profile' lan|wifi|4g|3g|bad (bad = 20% loss, "
        "150 ms RTT, jitter), overrides loss / rttMs / jitterMs; matches, players, seconds, tickRate; optional inputDelay / "
        "rollbackWindow / lagToleranceMs (default: the engine's mobile battle settings); 'suspend' backgrounds one phone for "
        "6 s, 'rebind' changes another's address. Reports finished matches, agreed results, desyncs, rollbacks, how far "
        "clients run ahead, server cost and bytes per player. Nothing is saved.",
        Json::parse(R"({"properties": {"profile": {"type": "string"}, "loss": {"type": "integer"}, "rttMs": {"type": "integer"},
            "jitterMs": {"type": "integer"}, "matches": {"type": "integer"}, "players": {"type": "integer"},
            "seconds": {"type": "integer"}, "tickRate": {"type": "integer"}, "inputDelay": {"type": "integer"},
            "rollbackWindow": {"type": "integer"}, "lagToleranceMs": {"type": "integer"}, "suspend": {"type": "boolean"},
            "rebind": {"type": "boolean"}, "seed": {"type": "integer"}}})"),
        [](const Json& p) { return simulate(p); }});
}

} // namespace aiverse::editor
