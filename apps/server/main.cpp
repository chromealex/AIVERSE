// Dedicated game server: lobby, matchmaking and the input relay for network matches.
//   aiverse_server [--config server.json] [--udp-port N] [--ws-port N] [--replays DIR] [--ratings FILE] [--quiet]
// Battle server mode (hosted matches from an orchestrator, docs/battle-net.md):
//   [--battle-port N --server-key-file F] [--control-port N --control-key-file F] [--max-matches N]
//   [--public-address host:port] [--hosted-only] [--loopback]
//   [--bw-node node.json]   (also a Bunker Wars battle node, docs/meta-bunkerwars.md)
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <thread>

#include "engine/data/Json.h"
#include "apps/server/GameData.h"
#include "engine/net/server/BattleControl.h"
#include "engine/net/server/Server.h"
#include <fstream>
#include <memory>
#ifndef __EMSCRIPTEN__
#include "engine/meta/bw/BwNode.h"
#endif
#include "modules/rts/RtsModule.h"

using namespace aiverse;

static std::atomic<bool> g_stop{false};

int main(int argc, char** argv) {
    std::string configPath;
    net::ServerConfig cfg;
    bool haveConfig = false;
    for (int i = 1; i < argc; ++i) if (std::string(argv[i]) == "--config" && i + 1 < argc) { configPath = argv[++i]; haveConfig = true; }
    try {
        if (haveConfig) cfg = net::loadServerConfig(Json::parseFile(configPath));
    } catch (const std::exception& e) {
        std::fprintf(stderr, "config: %s\n", e.what());
        return 2;
    }
    if (cfg.queues.empty()) cfg.queues.push_back({});   // a default 1v1 queue
    // This build knows the RTS module: leaving = surrender, and the server can decide unsettled matches
    // by re-simulating them on the game data listed under "games".
    if (!cfg.surrenderCommand) cfg.surrenderCommand = game::kCmdSurrender;
    cfg.arbiter = netgame::makeArbiter(cfg.gameDirs);
    uint16_t battlePort = 0, controlPort = 0;
    std::string serverKeyFile, controlKeyFile, publicAddress;
    uint32_t maxMatches = 64;
    bool loopback = false;
    std::string bwNodePath;
    for (int i = 1; i < argc; ++i) {
        std::string k = argv[i];
        auto next = [&]() -> std::string { if (i + 1 >= argc) { std::fprintf(stderr, "missing value for %s\n", k.c_str()); std::exit(2); } return argv[++i]; };
        if (k == "--config") ++i;
        else if (k == "--udp-port") cfg.udpPort = uint16_t(std::stoi(next()));
        else if (k == "--ws-port") cfg.wsPort = uint16_t(std::stoi(next()));
        else if (k == "--replays") cfg.replayDir = next();
        else if (k == "--ratings") cfg.ratingsFile = next();
        else if (k == "--quiet") cfg.log = false;
        else if (k == "--bw-node") bwNodePath = next();
        else if (k == "--battle-port") battlePort = uint16_t(std::stoi(next()));
        else if (k == "--server-key-file") serverKeyFile = next();
        else if (k == "--control-port") controlPort = uint16_t(std::stoi(next()));
        else if (k == "--control-key-file") controlKeyFile = next();
        else if (k == "--max-matches") maxMatches = uint32_t(std::stoi(next()));
        else if (k == "--public-address") publicAddress = next();
        else if (k == "--hosted-only") cfg.hostedOnly = true;
        else if (k == "--loopback") loopback = true;
        else {
            std::printf("usage: aiverse_server [--config server.json] [--udp-port N] [--ws-port N] [--replays DIR] [--ratings FILE] [--quiet]\n"
                        "       bunker wars node: --bw-node node.json\n"
                        "       battle mode: --battle-port N --server-key-file F [--control-port N --control-key-file F]\n"
                        "                    [--max-matches N] [--public-address host:port] [--hosted-only] [--loopback]\n");
            return 2;
        }
    }
    std::signal(SIGINT, [](int) { g_stop = true; });
    std::signal(SIGTERM, [](int) { g_stop = true; });

#ifndef __EMSCRIPTEN__
    std::unique_ptr<meta::bw::BattleNode> bwNode;
    if (!bwNodePath.empty()) {
        try {
            auto nc = meta::bw::NodeConfig::parse(Json::parseFile(bwNodePath));
            if (nc.clientPort == 0) nc.clientPort = cfg.udpPort;
            bwNode = std::make_unique<meta::bw::BattleNode>(nc);
            bwNode->install(cfg);
        } catch (const std::exception& e) {
            std::fprintf(stderr, "bw node config: %s\n", e.what());
            return 2;
        }
    }
#endif
    // Keys are files (mode 0600), never command-line values: arguments show up in process lists.
    auto readKey = [](const std::string& path, net::Key& key) {
        std::ifstream f(path);
        std::string hex;
        f >> hex;
        auto k = net::keyFromHex(hex);
        if (!k) { std::fprintf(stderr, "error: %s does not hold a 64-digit hex key (aiverse_battle keygen)\n", path.c_str()); return false; }
        key = *k;
        return true;
    };
    net::Server server(cfg);
    std::string err;
    if ((cfg.udpPort || cfg.wsPort) && !server.listen(&err)) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 1; }
    std::unique_ptr<net::BattleControl> control;
    if (battlePort) {
        net::Key serverKey;
        if (serverKeyFile.empty() || !readKey(serverKeyFile, serverKey)) return 2;
        if (!server.listenSecure(battlePort, serverKey, loopback, &err)) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 1; }
        net::wipe(serverKey.data(), serverKey.size());
        if (controlPort) {
            net::BattleControlConfig bc;
            if (controlKeyFile.empty() || !readKey(controlKeyFile, bc.controlKey)) return 2;
            bc.maxMatches = maxMatches;
            bc.publicAddress = publicAddress.empty() ? "127.0.0.1:" + std::to_string(server.securePort()) : publicAddress;
            control = std::make_unique<net::BattleControl>(server, bc, cfg.gameDirs);
            if (!control->listen(controlPort, loopback, &err)) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 1; }
        }
        std::printf("battle mode: secure udp %u, control %u, capacity %u matches%s\n", server.securePort(), control ? control->port() : 0,
                    maxMatches, cfg.hostedOnly ? ", hosted only" : "");
    }
#ifndef __EMSCRIPTEN__
    if (bwNode) {
        bwNode->attach(server);
        if (!bwNode->listen(&err)) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 1; }
        std::printf("bunker wars battle node: internal API up\n");
    }
#endif
    std::printf("%s: udp %u, websocket %u, %zu queue(s)%s%s\n", cfg.name.c_str(), server.udpPort(), server.wsPort(), cfg.queues.size(),
                cfg.replayDir.empty() ? "" : ", replays in ", cfg.replayDir.c_str());
    std::fflush(stdout);

    uint64_t lastStats = net::nowMs();
    while (!g_stop) {
        const uint64_t now = net::nowMs();
        server.update(now);
        if (control) control->update(now);
#ifndef __EMSCRIPTEN__
        if (bwNode) bwNode->update(now);
#endif
        if (now - lastStats >= 60000) {
            lastStats = now;
            auto s = server.stats();
            std::printf("stats: %u clients, %u rooms, %u matches running, %u queued, %u started, %u finished, %u desyncs\n", s.clients, s.rooms,
                        s.activeMatches, s.queued, s.matchesStarted, s.matchesFinished, s.desyncs);
            std::fflush(stdout);
        }
        // 1 ms keeps relay latency negligible next to the 50 ms tick; an idle server costs ~nothing.
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    std::printf("shutting down\n");
    return 0;
}
