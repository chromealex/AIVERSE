// Dedicated game server: lobby, matchmaking and the input relay for network matches.
//   aiverse_server [--config server.json] [--udp-port N] [--ws-port N] [--replays DIR] [--ratings FILE] [--quiet]
//                  [--bw-node node.json]   (also a Bunker Wars battle node, docs/meta-bunkerwars.md)
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
#include "engine/net/server/Server.h"
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
        else {
            std::printf("usage: aiverse_server [--config server.json] [--udp-port N] [--ws-port N] [--replays DIR] [--ratings FILE] [--quiet] [--bw-node node.json]\n");
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
    net::Server server(cfg);
    std::string err;
    if (!server.listen(&err)) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 1; }
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
