// aiverse_place: multiplayer physics sandbox on net::replica + physics::Scene (docs/replica.md § Place hosting).
//   serve  --scene FILE --key HEX64|--key-file F [--port N] [--ws-port N] [--reconnect-seconds 30] [--seconds S]
//   serve  --scene FILE --insecure ...                      dev: guests, plain UDP
//   ticket --key HEX64|--key-file F --player ID [--seconds 3600] [--server host:port]
//   keygen                                                  a random server key (hex)
//   client --connect ADDR [--ticket T|--ticket-file F] [--player NAME] [--insecure] [--lang en|ru]
//   bots   --connect ADDR [--count N] [--seconds S] [--key ...] [--insecure] [--drop-at S] ...   headless bot clients
//   load   [--scene FILE] [--clients N] [--seconds S] [--profile 4g] [--late S]   everything in one process over SimNet
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <thread>
#include "apps/place/PlaceLink.h"
#include "apps/place/PlaceSim.h"
#include "engine/net/Udp.h"
#include "engine/net/WebSocket.h"

using namespace aiverse;
using namespace aiverse::place;

#ifdef AIVERSE_PLACE_CLIENT
int runPlaceClient(LinkConfig link, const std::string& language, bool suspendOnFocus, const char* argv0);   // PlaceView.cpp
#endif

namespace {
struct Args {
    std::vector<std::string> v;
    std::string get(const std::string& key, const std::string& def) const {
        for (size_t i = 1; i + 1 < v.size(); ++i) if (v[i] == key) return v[i + 1];
        return def;
    }
    double num(const std::string& key, double def) const {
        std::string s = get(key, "");
        if (s.empty()) return def;
        char* end = nullptr;
        double d = std::strtod(s.c_str(), &end);
        if (!end || *end) throw std::runtime_error("invalid number for " + key + ": " + s);
        return d;
    }
};

const char* kUsage =
    "usage: aiverse_place serve --scene FILE (--key HEX64 | --key-file F | --insecure) [--port 27300] [--ws-port N]\n"
    "                           [--reconnect-seconds 30] [--seconds S]\n"
    "       aiverse_place ticket (--key HEX64 | --key-file F) --player ID [--seconds 3600] [--server host:port]\n"
    "       aiverse_place keygen\n"
    "       aiverse_place client --connect ADDR [--ticket T | --ticket-file F] [--player NAME] [--insecure] [--lang en|ru]\n"
    "                            [--suspend-on-focus-loss]\n"
    "       aiverse_place bots --connect ADDR [--count 8] [--seconds 60] [--key HEX64 | --key-file F | --tickets FILE]\n"
    "                          [--insecure] [--prefix bot] [--drop-at S] [--drop-count 1]\n"
    "       aiverse_place load [--scene FILE] [--clients 16] [--seconds 30] [--profile 4g] [--late 20]\n"
    "ADDR: host:port (encrypted UDP with a ticket; plain UDP with --insecure) or ws://host:port (wss:// via a TLS proxy)\n";

Json readScene(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot read scene " + path);
    std::string text((std::istreambuf_iterator<char>(f)), {});
    return Json::parse(text);
}

std::string readText(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot read " + path);
    std::string text((std::istreambuf_iterator<char>(f)), {});
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ')) text.pop_back();
    return text;
}

// --key HEX64 or --key-file F (keeps the key off the command line, like aiverse_battle).
std::optional<net::Key> serverKey(const Args& a) {
    std::string hex = a.get("--key", "");
    std::string file = a.get("--key-file", "");
    if (!file.empty()) hex = readText(file);
    if (hex.empty()) return std::nullopt;
    auto k = net::keyFromHex(hex);
    if (!k) throw std::runtime_error("the server key must be 64 hex digits (aiverse_place keygen makes one)");
    return k;
}

// Only graphical Place client builds consume ticket decoding.
// AIVERSE_BUILD_CLIENT=OFF intentionally omits this code and its warning.
#ifdef AIVERSE_PLACE_CLIENT
std::optional<net::ClientTicket> clientTicket(const Args& a) {
    std::string text = a.get("--ticket", "");
    std::string file = a.get("--ticket-file", "");
    if (!file.empty()) text = readText(file);
    if (text.empty()) return std::nullopt;
    auto t = net::ClientTicket::decode(text);
    if (!t) throw std::runtime_error("malformed ticket (aiverse_place ticket prints one)");
    return t;
}
#endif

bool flag(const Args& a, const char* name) { return std::find(a.v.begin(), a.v.end(), name) != a.v.end(); }

void say(const std::string& line) { std::printf("%s\n", line.c_str()); std::fflush(stdout); }

int serve(const Args& a) {
    std::string file = a.get("--scene", "");
    if (file.empty()) throw std::runtime_error(kUsage);
    uint16_t port = uint16_t(a.num("--port", 27300));
    int wsPort = int(a.num("--ws-port", 0));
    double seconds = a.num("--seconds", 0);
    const bool insecure = flag(a, "--insecure");
    auto key = serverKey(a);
    if (!key && !insecure)
        throw std::runtime_error("serve needs --key HEX64 / --key-file F (tickets, encrypted UDP) or --insecure (dev: guests, plain UDP)");
    PlaceServer server(readScene(file));
    server.setReconnectSeconds(a.num("--reconnect-seconds", 30));
    server.onEvent = say;

    GateConfig gc;
    gc.key = key;
    PlaceGate gate(gc);
    RateLimiter handshakes(1, 5);   // SecureUdp handshakes per player id (reconnect storms, ticket sharing)
    std::unique_ptr<net::SecureListener> secure;
    net::UdpListener udp;
    net::WsListener ws;
    if (insecure) {
        if (!udp.open(port)) throw std::runtime_error("cannot bind UDP port " + std::to_string(port));
    } else {
        net::SecureServerConfig sc;
        sc.serverKey = *key;
        sc.admit = [&handshakes, &server](const net::TicketClaims& c) {
            return c.matchId == kPlaceMatch && server.admits(c.playerId) && handshakes.allow(c.playerId, net::nowMs());
        };
        secure = std::make_unique<net::SecureListener>(sc);
        if (!secure->open(port)) throw std::runtime_error("cannot bind UDP port " + std::to_string(port));
    }
    if (wsPort && !ws.open(uint16_t(wsPort))) throw std::runtime_error("cannot bind WebSocket port " + std::to_string(wsPort));
    std::printf("place server: %s UDP %u%s%s, %d Hz, %zu bodies, reconnect window %.0f s\n", insecure ? "plain" : "encrypted", unsigned(port),
                wsPort ? ", WebSocket " : "", wsPort ? std::to_string(wsPort).c_str() : "", server.scene().tickRate(), server.bodies().size(),
                a.num("--reconnect-seconds", 30));
    if (!key) std::printf("WARNING: no --key: anybody can join as a guest (dev mode only)\n");
    else std::printf("admission: tickets required (aiverse_place ticket --key-file ... --player ID)\n");
    if (insecure) std::printf("WARNING: --insecure: plain UDP, nothing is encrypted or authenticated by the transport\n");
    if (wsPort) std::printf("note: WebSocket is plain ws://; on the Internet put a TLS proxy in front (wss://, docs/replica.md § Place hosting)\n");
    std::fflush(stdout);
    const double step = 1000.0 / server.scene().tickRate();
    uint64_t start = net::nowMs(), previous = start, lastReport = start;
    double acc = 0;
    uint64_t lastBytes = 0;
    auto admit = [&](std::unique_ptr<net::Conn> c, const std::string& player, const std::string& via) {
        std::string what = c->describe();
        if (!server.attach(std::move(c), player)) say("server full: refused " + player);
        else if (!player.empty()) say("admitted " + player + " over " + via + " (" + what + ")");
    };
    while (!seconds || double(net::nowMs() - start) < seconds * 1000) {
        uint64_t now = net::nowMs();
        acc += double(now - previous);
        previous = now;
        if (secure) for (auto& s : secure->poll(now)) admit(std::move(s.conn), s.claims.playerId, "encrypted UDP");
        if (insecure) for (auto& c : udp.poll(now)) gate.add(std::move(c), now);
        if (wsPort) for (auto& c : ws.poll(now)) gate.add(std::move(c), now);
        for (auto& g : gate.poll(now)) admit(std::move(g.conn), g.player, g.guest ? "the gate (guest)" : "the gate (ticket)");
        int budget = 8;
        while (acc >= step && budget--) { server.update(now); acc -= step; }
        if (budget < 0) acc = 0;   // fell behind: drop time rather than spiral
        if (now - lastReport >= 5000) {
            const auto& s = server.stats();
            const auto& rs = server.replica().stats();
            const auto& gs = gate.stats();
            uint64_t bytes = rs.snapshotBytes + rs.reliableBytes;
            size_t players = server.players();
            std::printf("players %zu (away %zu), bodies %zu, tick avg %.3f ms worst %.3f ms, %.0f B/s per client, spawned %llu refused %llu | "
                        "joined %llu resumed %llu replaced %llu expired %llu | gate admitted %llu bad ticket %llu rate-limited %llu",
                        players, server.away(), server.bodies().size(), s.tickMsAvg(), s.tickMsWorst,
                        players ? double(bytes - lastBytes) / 5.0 / double(players) : 0.0, (unsigned long long)s.spawned,
                        (unsigned long long)s.spawnDenied, (unsigned long long)s.joined, (unsigned long long)s.resumed, (unsigned long long)s.replaced,
                        (unsigned long long)s.expired, (unsigned long long)gs.admitted, (unsigned long long)(gs.badTicket + gs.needTicket),
                        (unsigned long long)gs.rateLimited);
            if (secure) std::printf(" | secure handshakes %llu rejected tickets %llu", (unsigned long long)secure->stats().handshakes,
                                    (unsigned long long)secure->stats().rejectedTickets);
            std::printf("\n");
            std::fflush(stdout);
            server.resetTickTiming();
            lastBytes = bytes;
            lastReport = now;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return 0;
}

int ticket(const Args& a) {
    auto key = serverKey(a);
    std::string player = a.get("--player", "");
    if (!key || player.empty() || player.size() > 64) throw std::runtime_error(kUsage);
    std::printf("%s\n", placeTicket(*key, player, a.num("--seconds", 3600), a.get("--server", "")).encode().c_str());
    return 0;
}

int keygen() {
    net::Key k = net::randomKey();
    std::printf("%s\n", net::toHexString(k.data(), k.size()).c_str());
    return 0;
}

int bots(const Args& a) {
    std::string address = a.get("--connect", "");
    if (address.empty()) throw std::runtime_error(kUsage);
    int count = int(a.num("--count", 8));
    double seconds = a.num("--seconds", 60);
    double dropAt = a.num("--drop-at", -1);
    int dropCount = int(a.num("--drop-count", 1));
    bool insecure = flag(a, "--insecure");
    std::string prefix = a.get("--prefix", "bot");
    auto key = serverKey(a);   // test convenience: bots issue their own tickets
    std::vector<std::string> tickets;
    if (!a.get("--tickets", "").empty()) {
        std::ifstream f(a.get("--tickets", ""));
        for (std::string line; std::getline(f, line);) if (!line.empty()) tickets.push_back(line);
    }
    struct Entry { std::unique_ptr<PlaceLink> link; std::unique_ptr<Bot> bot; };
    std::vector<Entry> list;
    for (int i = 0; i < count; ++i) {
        std::optional<net::ClientTicket> t;
        std::string name = prefix + "-" + std::to_string(i + 1);
        if (key) t = placeTicket(*key, name, seconds + 3600);
        else if (size_t(i) < tickets.size()) t = net::ClientTicket::decode(tickets[size_t(i)]);
        LinkConfig lc = linkConfig(address, t, name, insecure);
        lc.seed = uint64_t(i + 1);
        list.push_back({std::make_unique<PlaceLink>(lc), std::make_unique<Bot>(uint32_t(1000 + i))});
    }
    const double step = 1000.0 / 60;
    uint64_t start = net::nowMs(), previous = start, lastReport = start;
    double acc = 0;
    bool dropped = false;
    auto report = [&](const char* what) {
        int ready = 0, resumed = 0;
        double rtt = 0, best = 0, snaps = 0, in = 0, mean = 0;
        uint64_t corr = 0, rec = 0, reconnects = 0, attempts = 0;
        float worst = 0;
        for (auto& e : list) {
            reconnects += e.link->reconnects();
            attempts += e.link->attempts();
            resumed += e.link->resumedCharacter();
            PlaceClient* c = e.link->client();
            if (!c) continue;
            const auto& s = c->stats();
            ++ready;
            rtt += s.rttMs; best += e.link->bestPingMs(); snaps += s.snapshotsPerSecond; in += s.bytesInPerSecond;
            corr += s.corrections; rec += s.reconciles; mean += s.sumCorrection; worst = std::max(worst, s.maxCorrection);
        }
        double n = std::max(1, ready);
        std::printf("%s: %d/%zu playing, ping %.0f ms (best %.0f), %.1f snapshots/s, %.0f B/s in per client, %llu corrections > 1 cm of %llu, mean %.4f m, "
                    "max %.3f m | dials %llu, reconnects %llu, resumed characters %d\n",
                    what, ready, list.size(), rtt / n, best / n, snaps / n, in / n, (unsigned long long)corr, (unsigned long long)rec,
                    rec ? mean / double(rec) : 0.0, worst, (unsigned long long)attempts, (unsigned long long)reconnects, resumed);
        for (auto& e : list)
            if (e.link->status() == LinkStatus::Rejected) std::printf("  rejected: %s\n", gateRejectText(e.link->rejectReason()));
        std::fflush(stdout);
    };
    while (double(net::nowMs() - start) < seconds * 1000) {
        uint64_t now = net::nowMs();
        acc += double(now - previous);
        previous = now;
        if (dropAt >= 0 && !dropped && double(now - start) >= dropAt * 1000) {
            dropped = true;
            for (int i = 0; i < dropCount && i < int(list.size()); ++i) {
                list[size_t(i)].link->drop(now);
                std::printf("dropped the connection of %s-%d\n", prefix.c_str(), i + 1);
            }
            std::fflush(stdout);
        }
        int budget = 8;
        bool ticked = false;
        while (acc >= step && budget--) {
            for (auto& e : list) {
                PlaceClient* c = e.link->client();
                e.link->update(now, c ? e.bot->think(*c, true) : Input{});
                if (c) c->advanceSmoothing(float(step));
            }
            acc -= step;
            ticked = true;
        }
        if (budget < 0) acc = 0;
        if (!ticked) for (auto& e : list) e.link->poll(now);
        if (now - lastReport >= 5000) { report("bots"); lastReport = now; }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    report("final");
    return 0;
}

int load(const Args& a) {
    LoadOptions o;
    o.clients = int(a.num("--clients", o.clients));
    o.seconds = a.num("--seconds", o.seconds);
    o.profile = a.get("--profile", o.profile);
    o.lateJoinAt = a.num("--late", o.seconds > 20 ? 20 : o.seconds * 2 / 3);
    bool found = false;
    net::linkPreset(o.profile, &found);
    if (!found) throw std::runtime_error("unknown link profile " + o.profile);
    std::string file = a.get("--scene", "examples/place/scene.json");
    Json scene = readScene(file);
    std::printf("load: %d clients over SimNet \"%s\", %.0f s, late join at %.0f s, scene %s\n", o.clients, o.profile.c_str(), o.seconds, o.lateJoinAt,
                file.c_str());
    std::fflush(stdout);
    auto r = runLoad(scene, o);
    std::printf("%s", r.text().c_str());
    return r.allReady && r.lateJoinSame ? 0 : 1;
}

int command(int argc, char** argv) {
    Args a;
    for (int i = 1; i < argc; ++i) a.v.push_back(argv[i]);
    std::string cmd = a.v.empty() ? "" : a.v[0];
    if (cmd == "serve") return serve(a);
    if (cmd == "bots") return bots(a);
    if (cmd == "load") return load(a);
    if (cmd == "ticket") return ticket(a);
    if (cmd == "keygen") return keygen();
    if (cmd == "client") {
#ifdef AIVERSE_PLACE_CLIENT
        auto ticket = clientTicket(a);
        std::string address = a.get("--connect", "");
        if (address.empty() && !ticket) throw std::runtime_error(kUsage);
        return runPlaceClient(linkConfig(address, ticket, a.get("--player", ""), flag(a, "--insecure")), a.get("--lang", ""),
                              flag(a, "--suspend-on-focus-loss"), argv[0]);
#else
        throw std::runtime_error("this build has no graphical client (desktop builds only)");
#endif
    }
    throw std::runtime_error(kUsage);
}
}

#ifdef AIVERSE_PLACE_CLIENT
int aiverseMain(int argc, char** argv) {
#else
int main(int argc, char** argv) {
#endif
    try {
        return command(argc, argv);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "%s\n", e.what());
        return 2;
    }
}
