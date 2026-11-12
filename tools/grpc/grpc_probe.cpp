// aiverse_grpc_probe: checks a Drone Fall (extraction_rts) meta backend from the command line.
//   aiverse_grpc_probe --endpoint https://entrypoint.example/ [--version 1.0] [--platform ios|android|steam]
//                      [--protocol web|h2] [--ca roots.pem] [--login [--device-id ID]] [--servers]
// Without --login only EntrypointService.GetServiceMap runs (read-only). --login performs a device
// grant (creates / reuses the account of that device id) and --servers lists battle servers.
#include "engine/grpc/ext/ExtMeta.h"
#include "entrypoint.grpc.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <thread>

using namespace aiverse::grpc;

int main(int argc, char **argv) {
    ext::ExtMetaConfig cfg;
    cfg.clientVersion = "probe";
    bool login = false, servers = false;
    std::string deviceId;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : std::string(); };
        if (a == "--endpoint") cfg.endpointUrl = next();
        else if (a == "--version") cfg.clientVersion = next();
        else if (a == "--platform") {
            const std::string p = next();
            cfg.platform = p == "ios" ? ext::ClientPlatform::Ios : p == "android" ? ext::ClientPlatform::Android
                         : p == "steam" ? ext::ClientPlatform::Steam : ext::ClientPlatform::Unspecified;
        } else if (a == "--protocol") cfg.protocol = next() == "h2" ? Protocol::Http2 : Protocol::GrpcWeb;
        else if (a == "--ca") {
            std::ifstream f(next());
            std::stringstream ss;
            ss << f.rdbuf();
            cfg.tls.caPem = ss.str();
        } else if (a == "--login") login = true;
        else if (a == "--servers") servers = login = true;
        else if (a == "--device-id") deviceId = next();
        else {
            std::fprintf(stderr, "usage: %s --endpoint URL [--version V] [--platform ios|android|steam] [--protocol web|h2] "
                                 "[--ca roots.pem] [--login [--device-id ID]] [--servers]\n", argv[0]);
            return 2;
        }
    }
    if (cfg.endpointUrl.empty()) {
        std::fprintf(stderr, "--endpoint is required\n");
        return 2;
    }
    cfg.load = [&](const std::string &k) { return k == "auth_id" ? deviceId : std::string(); };
    cfg.save = [&](const std::string &k, const std::string &v) {
        if (k == "auth_id")
            std::printf("device id: %s\n", v.c_str());
    };
    auto wait = [](auto &&poll, auto &&done, int seconds) {
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
        while (!done() && std::chrono::steady_clock::now() < end) {
            poll();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return done();
    };
    if (!login) {
        // Read-only: GetServiceMap only.
        ChannelConfig cc;
        cc.target = cfg.endpointUrl;
        cc.protocol = cfg.protocol;
        cc.tls = cfg.tls;
        Channel ch(cc);
        bool done = false;
        int rc = 1;
        extraction_rts::entrypoint::GetServiceMapRequest req;
        req.client_version = cfg.clientVersion;
        req.platform = extraction_rts::entrypoint::Platform(int(cfg.platform));
        CallOptions o;
        o.timeoutMs = 15000;
        extraction_rts::entrypoint::EntrypointService::Client(ch).GetServiceMap(
            req, [&](const Status &s, extraction_rts::entrypoint::GetServiceMapResponse &&r) {
                done = true;
                if (!s.ok())
                    std::printf("GetServiceMap failed: %s\n", s.str().c_str());
                else if (r.has_success()) {
                    std::printf("identity_url: %s\nmatchmaking_url: %s\n", r.success.identity_url.c_str(),
                                r.success.matchmaking_url.c_str());
                    rc = 0;
                } else if (r.has_incompatible_version()) {
                    std::printf("incompatible version: %s\n", r.incompatible_version.error_message.c_str());
                    rc = 3;
                } else {
                    std::printf("empty response\n");
                }
            }, o);
        wait([&] { ch.poll(); }, [&] { return done; }, 20);
        if (!done)
            std::printf("timed out (%s)\n", ch.lastTransportError().c_str());
        return rc;
    }
    ext::ExtMetaClient client(cfg);
    client.connect();
    wait([&] { client.poll(); }, [&] { return client.status() != ext::ConnectionStatus::Connecting; }, 40);
    std::printf("status: %s %s\n", ext::connectionStatusName(client.status()), client.lastError().c_str());
    if (client.status() != ext::ConnectionStatus::Connected)
        return 1;
    std::printf("identity_url: %s\nmatchmaking_url: %s\nuser_id: %s\ntoken expires_at: %lld\n",
                client.identityUrl().c_str(), client.matchmakingUrl().c_str(), client.userId().c_str(),
                static_cast<long long>(client.tokenExpiresAt()));
    if (servers) {
        // Fetch the server list only (unreachable pings -> the attempt stops before FindMatch).
        client.matchmake("square", [](const ext::BattleServer &, std::function<void(int)> done) { done(-1); });
        wait([&] { client.poll(); }, [&] {
            auto s = client.findMatchStatus();
            return s != ext::FindMatchStatus::Fetching && s != ext::FindMatchStatus::Pinging;
        }, 30);
        for (const auto &s : client.battleServers())
            std::printf("battle server %s %s:%d\n", s.id.c_str(), s.address.c_str(), s.port);
        std::printf("matchmaking: %s %s\n", ext::findMatchStatusName(client.findMatchStatus()), client.lastError().c_str());
    }
    return 0;
}
