// aiverse_battle: tools for hosted battles (docs/battle-net.md).
//   aiverse_battle keygen                                   new 32-byte key (hex) for server / control keys
//   aiverse_battle ticket --key-file F --match ID --player P --slot N --compat KEY [--ttl SEC] [--server host:port]
//   aiverse_battle control --key-file F --address host:port (allocate FILE.json | release ID | status | reports)
//   aiverse_battle ping host:port                           unconnected battle ping (server selection)
//   aiverse_battle load [--matches N] [--players N] [--seconds S] [--profile lan|wifi|4g|3g|bad] [--loss P]
//                       [--rtt MS] [--jitter MS] [--tick-rate N] [--suspend] [--rebind] [--seed N] [--json]
//                       [--project DIR [--map FILE] [--rules-patch JSON]]   RTS top-down project instead of HeroDuel
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include "BattleLoad.h"
#include "RtsBattle.h"
#include "engine/data/Json.h"
#include "engine/net/MatchTicket.h"
#include "engine/net/SecureUdp.h"
#include "engine/net/Socket.h"
#include "engine/net/server/BattleControl.h"

using namespace aiverse;

static int usage() {
    std::printf("usage: aiverse_battle keygen | ticket ... | control ... | ping host:port | load ... (see docs/battle-net.md)\n");
    return 2;
}

static bool readKey(const std::string& path, net::Key& key) {
    std::ifstream f(path);
    std::string hex;
    f >> hex;
    auto k = net::keyFromHex(hex);
    if (!k) { std::fprintf(stderr, "error: %s does not hold a 64-digit hex key\n", path.c_str()); return false; }
    key = *k;
    return true;
}

static bool splitHostPort(const std::string& s, std::string& host, uint16_t& port) {
    const auto c = s.rfind(':');
    if (c == std::string::npos) return false;
    host = s.substr(0, c);
    if (host.size() > 2 && host.front() == '[') host = host.substr(1, host.size() - 2);
    const int p = std::atoi(s.c_str() + c + 1);
    if (p <= 0 || p > 65535) return false;
    port = uint16_t(p);
    return true;
}

static int control(const std::vector<std::string>& args) {
    std::string keyFile, address, op, arg;
    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--key-file" && i + 1 < args.size()) keyFile = args[++i];
        else if (args[i] == "--address" && i + 1 < args.size()) address = args[++i];
        else if (op.empty()) op = args[i];
        else arg = args[i];
    }
    net::Key key;
    std::string host; uint16_t port = 0;
    if (keyFile.empty() || !readKey(keyFile, key) || !splitHostPort(address, host, port) || op.empty()) return usage();
    Json req = Json::object();
    req.set("op", Json::string(op));
    req.set("ts", Json::string(std::to_string(net::unixMs())));
    uint8_t nonce[12]; net::secureRandom(nonce, sizeof(nonce));
    req.set("nonce", Json::string(net::toHexString(nonce, sizeof(nonce))));
    if (op == "allocate") req.set("match", Json::parseFile(arg));
    if (op == "release") req.set("matchId", Json::string(arg));
    net::Address a;
    if (!net::Address::resolve(host, port, a)) { std::fprintf(stderr, "error: cannot resolve %s\n", host.c_str()); return 1; }
    net::TcpSocket sock;
    if (!sock.connect(a)) { std::fprintf(stderr, "error: cannot connect\n"); return 1; }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    int st;
    while ((st = sock.pollConnected()) == 0 && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    if (st != 1) { std::fprintf(stderr, "error: cannot connect to %s\n", address.c_str()); return 1; }
    std::string out = net::signControl(key, req.dump(-1)) + "\n", in;
    while (!out.empty() && std::chrono::steady_clock::now() < deadline) {
        int w = sock.send(reinterpret_cast<const uint8_t*>(out.data()), out.size());
        if (w < 0) return 1;
        out.erase(0, size_t(w));
    }
    uint8_t buf[4096];
    while (in.find('\n') == std::string::npos && std::chrono::steady_clock::now() < deadline) {
        int n = sock.recv(buf, sizeof(buf));
        if (n < 0) break;
        if (n == 0) { std::this_thread::sleep_for(std::chrono::milliseconds(5)); continue; }
        in.append(reinterpret_cast<char*>(buf), size_t(n));
    }
    std::string json;
    if (!net::verifyControl(key, in.substr(0, in.find('\n')), json)) { std::fprintf(stderr, "error: no valid signed answer\n"); return 1; }
    std::printf("%s\n", Json::parse(json).dump(2).c_str());
    return Json::parse(json).getBool("ok") ? 0 : 1;
}

static int ping(const std::string& address) {
    std::string host; uint16_t port = 0;
    net::Address a;
    if (!splitHostPort(address, host, port) || !net::Address::resolve(host, port, a)) return usage();
    auto sock = net::openUdpPort(0, a.family());
    if (!sock) return 1;
    for (int i = 0; i < 20; ++i) {
        net::sendBattlePing(*sock, a, net::nowMs());
        for (int k = 0; k < 50; ++k) {
            uint8_t buf[64]; net::Address from; int n;
            while ((n = sock->recvFrom(from, buf, sizeof(buf))) >= 0) {
                uint32_t rtt; uint8_t load;
                if (from == a && net::readBattlePong(buf, size_t(n), net::nowMs(), rtt, load)) { std::printf("%s: %u ms, load %u%%\n", address.c_str(), rtt, load); return 0; }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    std::printf("%s: no answer\n", address.c_str());
    return 1;
}

int main(int argc, char** argv) {
    if (argc < 2) return usage();
    const std::string cmd = argv[1];
    std::vector<std::string> args(argv + 2, argv + argc);
    auto value = [&](const std::string& k, const std::string& def = "") {
        for (size_t i = 0; i + 1 < args.size(); ++i) if (args[i] == k) return args[i + 1];
        return def;
    };
    auto flag = [&](const std::string& k) { for (auto& a : args) if (a == k) return true; return false; };
    if (cmd == "keygen") { auto k = net::randomKey(); std::printf("%s\n", net::toHexString(k.data(), k.size()).c_str()); return 0; }
    if (cmd == "ticket") {
        net::Key key;
        if (value("--key-file").empty() || !readKey(value("--key-file"), key)) return usage();
        net::TicketClaims c;
        c.matchId = std::strtoull(value("--match").c_str(), nullptr, 10);
        c.playerId = value("--player");
        c.slot = uint8_t(std::atoi(value("--slot", "1").c_str()));
        c.compatHash = net::compatibilityHash(value("--compat"));
        c.expiresAtMs = net::unixMs() + uint64_t(std::atoll(value("--ttl", "3600").c_str())) * 1000;
        if (!c.matchId || c.playerId.empty() || value("--compat").empty()) return usage();
        std::printf("%s\n", net::issueTicket(key, c, value("--server", "127.0.0.1:27017")).encode().c_str());
        return 0;
    }
    if (cmd == "control") return control(args);
    if (cmd == "ping" && !args.empty()) return ping(args[0]);
    if (cmd == "load") {
        battle::BattleLoadConfig cfg;
        cfg.matches = uint32_t(std::atoi(value("--matches", "8").c_str()));
        cfg.playersPerMatch = uint32_t(std::atoi(value("--players", "2").c_str()));
        cfg.seconds = uint32_t(std::atoi(value("--seconds", "30").c_str()));
        cfg.tickRate = uint32_t(std::atoi(value("--tick-rate", "20").c_str()));
        cfg.seed = std::strtoull(value("--seed", "1").c_str(), nullptr, 10);
        bool found = true;
        cfg.link = net::linkPreset(value("--profile", "bad"), &found);
        if (!found) { std::fprintf(stderr, "unknown profile\n"); return 2; }
        if (!value("--loss").empty()) cfg.link.lossPercent = uint32_t(std::atoi(value("--loss").c_str()));
        if (!value("--rtt").empty()) cfg.link.latencyMs = uint32_t(std::atoi(value("--rtt").c_str())) / 2;
        if (!value("--jitter").empty()) cfg.link.jitterMs = uint32_t(std::atoi(value("--jitter").c_str()));
        cfg.suspendOne = flag("--suspend");
        cfg.rebindOne = flag("--rebind");
        if (!value("--project").empty()) {   // an RTS top-down project (rules "topDown") instead of the HeroDuel stand-in
            battle::RtsBattleOptions o;
            o.project = value("--project"); o.map = value("--map"); o.rulesPatch = value("--rules-patch");
            try { cfg.game = battle::rtsBattleGame(o); }
            catch (const std::exception& e) { std::fprintf(stderr, "error: %s\n", e.what()); return 1; }
        }
        auto r = battle::runBattleLoad(cfg);
        if (flag("--json")) {
            Json j = Json::object();
            j.set("matches", Json::number(int64_t(r.matches))); j.set("finished", Json::number(int64_t(r.finished)));
            j.set("consensus", Json::number(int64_t(r.consensus))); j.set("desyncs", Json::number(int64_t(r.desyncs)));
            j.set("rollbacks", Json::number(int64_t(r.rollbacks))); j.set("maxRollback", Json::number(int64_t(r.maxRollback)));
            j.set("aheadAvg", Json::number(r.aheadAvg, 2)); j.set("aheadMax", Json::number(int64_t(r.aheadMax)));
            j.set("serverUpdateP99Ms", Json::number(r.serverUpdateP99Ms, 3)); j.set("serverCpuShare", Json::number(r.serverCpuShare, 4));
            j.set("upBytesPerSec", Json::number(r.upBytesPerSec, 0)); j.set("downBytesPerSec", Json::number(r.downBytesPerSec, 0));
            j.set("corrections", Json::number(int64_t(r.corrections))); j.set("maxCorrection", Json::number(double(r.maxCorrection), 3));
            j.set("maxRawJump", Json::number(double(r.maxRawJump), 3)); j.set("maxDrawnJump", Json::number(double(r.maxDrawnJump), 3));
            j.set("summary", Json::string(r.summary()));
            std::printf("%s\n", j.dump(2).c_str());
        } else std::printf("%s\n", r.summary().c_str());
        return r.desyncs || r.finished < r.matches ? 1 : 0;
    }
    return usage();
}
