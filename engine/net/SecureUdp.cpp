#ifndef __EMSCRIPTEN__
#include "SecureUdp.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <map>
#include "LatestChannel.h"
#include "Reliable.h"

namespace aiverse::net {

namespace {
constexpr uint8_t kPing = 0xA0, kRequest = 0xA1, kChallenge = 0xA2, kResponse = 0xA3, kData = 0xA4, kPong = 0xA5;
constexpr uint8_t kWireVersion = 1;
constexpr size_t kRequestSize = 1000;     // padding: a spoofed request never gets a bigger answer
constexpr size_t kPingSize = 32;
constexpr size_t kHead = 1 + 4 + 8;       // kind, session id, sequence
constexpr size_t kChannelHeader = 7;      // ReliableChannel / LatestChannel packet header we replace
constexpr size_t kMaxPending = 256;
constexpr uint64_t kPendingMs = 5000;
enum Inner : uint8_t { kReliable = 1, kLatest = 2, kBye = 3, kAccept = 4 };

void put32(uint8_t* p, uint32_t v) { for (int i = 0; i < 4; ++i) p[i] = uint8_t(v >> (8 * i)); }
void put64(uint8_t* p, uint64_t v) { for (int i = 0; i < 8; ++i) p[i] = uint8_t(v >> (8 * i)); }
uint32_t get32(const uint8_t* p) { uint32_t v = 0; for (int i = 0; i < 4; ++i) v |= uint32_t(p[i]) << (8 * i); return v; }
uint64_t get64(const uint8_t* p) { uint64_t v = 0; for (int i = 0; i < 8; ++i) v |= uint64_t(p[i]) << (8 * i); return v; }

Nonce seqNonce(uint8_t kind, uint32_t sid, uint64_t seq) {
    Nonce n{};
    put64(n.data(), seq); put32(n.data() + 8, sid); n[12] = kind;
    return n;
}

// Fresh keys for every handshake (the ticket keys only protect the challenge): sequence nonces then
// never repeat under one key, even when the same ticket reconnects many times.
Key deriveKey(const Key& base, const char* label, uint32_t sid, const uint8_t salt[16]) {
    uint8_t msg[3 + 4 + 16];
    std::memcpy(msg, label, 3); put32(msg + 3, sid); std::memcpy(msg + 7, salt, 16);
    auto h = keyedHash(base, msg, sizeof(msg), 32);
    Key k; std::memcpy(k.data(), h.data(), 32);
    wipe(h.data(), h.size());
    return k;
}

struct ReplayWindow {
    uint64_t highest = 0;
    uint64_t bits[4] = {};      // 256 sequence numbers below and at `highest`
    bool any = false;
    bool fresh(uint64_t s) const {
        if (!any || s > highest) return true;
        if (highest - s >= 256) return false;
        const uint64_t d = highest - s;
        return !(bits[d / 64] & (uint64_t(1) << (d % 64)));
    }
    void mark(uint64_t s) {
        if (!any) { any = true; highest = s; bits[0] = 1; return; }
        if (s > highest) {
            const uint64_t shift = s - highest;
            if (shift >= 256) { bits[0] = bits[1] = bits[2] = bits[3] = 0; }
            else {
                for (uint64_t k = 0; k < shift; ++k) {   // shift the 256-bit set left by one, `shift` times (shift is small)
                    for (int w = 3; w > 0; --w) bits[w] = (bits[w] << 1) | (bits[w - 1] >> 63);
                    bits[0] <<= 1;
                }
            }
            highest = s;
            bits[0] |= 1;
            return;
        }
        const uint64_t d = highest - s;
        bits[d / 64] |= uint64_t(1) << (d % 64);
    }
};

// Encrypted packet: kind | sid | seq | cipher | mac (header authenticated as associated data).
Bytes sealPacket(uint8_t kind, uint32_t sid, uint64_t seq, const Key& key, const uint8_t* plain, size_t n) {
    uint8_t head[kHead];
    head[0] = kind; put32(head + 1, sid); put64(head + 5, seq);
    Bytes out(head, head + kHead);
    Bytes c = aeadSeal(key, seqNonce(kind, sid, seq), head, kHead, plain, n);
    out.insert(out.end(), c.begin(), c.end());
    return out;
}

bool openPacket(const uint8_t* d, size_t n, const Key& key, Bytes& plain) {
    if (n < kHead + kMacSize) return false;
    return aeadOpen(key, seqNonce(d[0], get32(d + 1), get64(d + 5)), d, kHead, d + kHead, n - kHead, plain);
}

struct Session {
    DatagramPort* port = nullptr;
    Address addr;
    uint32_t sid = 0;
    Key sendKey{}, recvKey{};
    uint64_t sendSeq = 0;
    ReplayWindow window;
    uint8_t cookie[16] = {};
    ReliableChannel chan;
    LatestChannel latest;
    uint64_t lastHeard = 0;
    bool closed = false, accepted = false;
    SecureStats* stats = nullptr;
    uint64_t matchId = 0;
    std::string playerId;

    ~Session() { wipe(sendKey.data(), 32); wipe(recvKey.data(), 32); }

    void raw(const Bytes& b) {
        if (!port) return;
        port->sendTo(addr, b.data(), b.size());
        if (stats) { ++stats->packetsOut; stats->bytesOut += b.size(); }
    }
    void sendInner(uint8_t inner, const uint8_t* body, size_t n, uint8_t kind = kData) {
        Bytes plain(1 + n);
        plain[0] = inner;
        if (n) std::memcpy(plain.data() + 1, body, n);
        raw(sealPacket(kind, sid, sendSeq++, sendKey, plain.data(), plain.size()));
    }
    void flush(uint64_t now) {
        auto strip = [&](uint8_t inner) {
            return [this, inner](const Bytes& b) { if (b.size() >= kChannelHeader) sendInner(inner, b.data() + kChannelHeader, b.size() - kChannelHeader); };
        };
        chan.flush(now, 0, strip(kReliable));
        latest.flush(0, strip(kLatest));
    }
    void bye() { if (!closed && accepted) for (int i = 0; i < 2; ++i) sendInner(kBye, nullptr, 0); closed = true; }
    // Payload of an authenticated packet.
    void deliver(const Bytes& plain, uint64_t now) {
        lastHeard = now;
        chan.heard(now);
        if (plain.empty()) return;
        ByteReader r(plain.data() + 1, plain.size() - 1);
        switch (plain[0]) {
        case kReliable: chan.onData(r, now); break;
        case kLatest: latest.receive(r); break;
        case kBye: closed = true; break;
        default: break;
        }
    }
};

class SecureSessionConn : public Conn {
public:
    explicit SecureSessionConn(std::shared_ptr<Session> s) : s_(std::move(s)) {}
    void send(const Bytes& msg) override { if (!s_->closed) s_->chan.queue(msg); }
    void sendLatest(const Bytes& msg) override { if (!s_->closed) s_->latest.queue(msg); }
    bool recv(Bytes& msg) override { return s_->chan.recv(msg) || s_->latest.recv(msg); }
    uint32_t rttMs() const override { return s_->chan.rttMs(); }
protected:
    std::shared_ptr<Session> s_;
};
} // namespace

// ---------------------------------------------------------------- client
namespace {
class SecureClientConn final : public SecureSessionConn {
public:
    SecureClientConn(std::unique_ptr<DatagramPort> port, const Address& server, const ClientTicket& t, SecureLinkConfig cfg)
        : SecureSessionConn(std::make_shared<Session>()), port_(std::move(port)), ticket_(t), cfg_(cfg) {
        s_->port = port_.get();
        s_->addr = server;
        s_->stats = &stats_;
        s_->chan.setRedundancy(cfg.redundancyMs);
        if (!port_ || ticket_.sealed.empty() || ticket_.sealed.size() > kRequestSize - 4) state_ = ConnState::Closed;
    }
    ~SecureClientConn() override { close(); }
    ConnState state() const override { return state_; }
    std::string describe() const override { return "secure udp " + s_->addr.str(); }
    void close() override {
        if (state_ == ConnState::Connected) s_->bye();
        s_->closed = true;
        state_ = ConnState::Closed;
        wipe(ticket_.claims.c2s.data(), 32); wipe(ticket_.claims.s2c.data(), 32);
    }

    void update(uint64_t now) override {
        if (state_ == ConnState::Closed) return;
        if (!started_) { started_ = now; s_->lastHeard = now; }
        uint8_t buf[2048];
        Address from;
        int n;
        while ((n = port_->recvFrom(from, buf, sizeof(buf))) >= 0) {
            if (!(from == s_->addr) || n < 1) continue;
            ++stats_.packetsIn; stats_.bytesIn += uint64_t(n);
            if (buf[0] == kChallenge && !accepted()) onChallenge(buf, size_t(n));
            else if (buf[0] == kData && haveSession_) onData(buf, size_t(n), now);
        }
        if (s_->closed) { state_ = ConnState::Closed; return; }
        if (state_ == ConnState::Connecting) {
            if (now - started_ > cfg_.connectTimeoutMs) { state_ = ConnState::Closed; return; }
            if (now - lastTry_ >= cfg_.retryMs) { lastTry_ = now; haveSession_ ? sendResponse() : sendRequest(); }
            return;
        }
        if (s_->chan.failed() || now - s_->lastHeard > cfg_.silenceTimeoutMs) { state_ = ConnState::Closed; return; }
        s_->flush(now);
    }

private:
    bool accepted() const { return state_ == ConnState::Connected; }
    void sendRequest() {
        Bytes p(kRequestSize, 0);
        p[0] = kRequest; p[1] = kWireVersion;
        p[2] = uint8_t(ticket_.sealed.size()); p[3] = uint8_t(ticket_.sealed.size() >> 8);
        std::memcpy(p.data() + 4, ticket_.sealed.data(), ticket_.sealed.size());
        s_->raw(p);
    }
    void sendResponse() { s_->sendInner(0, s_->cookie, 16, kResponse); }
    void onChallenge(const uint8_t* d, size_t n) {
        // kind | sid | nonce24 | cipher(salt16 + cookie16) | mac
        if (n != 1 + 4 + 24 + 32 + kMacSize) return;
        Nonce nonce; std::memcpy(nonce.data(), d + 5, 24);
        Bytes plain;
        if (!aeadOpen(ticket_.claims.s2c, nonce, d, 5, d + 29, n - 29, plain) || plain.size() != 32) { ++stats_.badPackets; return; }
        const uint32_t sid = get32(d + 1);
        if (haveSession_ && sid == s_->sid) return;   // resent challenge
        s_->sid = sid;
        s_->sendKey = deriveKey(ticket_.claims.c2s, "c2s", sid, plain.data());
        s_->recvKey = deriveKey(ticket_.claims.s2c, "s2c", sid, plain.data());
        std::memcpy(s_->cookie, plain.data() + 16, 16);
        s_->sendSeq = 0;
        s_->window = {};
        wipe(plain.data(), plain.size());
        haveSession_ = true;
        sendResponse();
    }
    void onData(const uint8_t* d, size_t n, uint64_t now) {
        if (n < kHead || get32(d + 1) != s_->sid) return;
        const uint64_t seq = get64(d + 5);
        if (!s_->window.fresh(seq)) { ++stats_.replays; return; }
        Bytes plain;
        if (!openPacket(d, n, s_->recvKey, plain)) { ++stats_.badPackets; return; }
        s_->window.mark(seq);
        if (state_ == ConnState::Connecting) { state_ = ConnState::Connected; s_->accepted = true; }
        s_->deliver(plain, now);
    }

    std::unique_ptr<DatagramPort> port_;
    ClientTicket ticket_;
    SecureLinkConfig cfg_;
    SecureStats stats_;
    ConnState state_ = ConnState::Connecting;
    bool haveSession_ = false;
    uint64_t started_ = 0, lastTry_ = 0;
};
} // namespace

std::unique_ptr<Conn> connectSecure(std::unique_ptr<DatagramPort> port, const Address& server, const ClientTicket& ticket, SecureLinkConfig cfg) {
    return std::make_unique<SecureClientConn>(std::move(port), server, ticket, cfg);
}

std::unique_ptr<Conn> connectSecure(const ClientTicket& ticket, SecureLinkConfig cfg) {
    const auto colon = ticket.server.rfind(':');
    if (colon == std::string::npos) return nullptr;
    std::string host = ticket.server.substr(0, colon);
    if (host.size() > 2 && host.front() == '[' && host.back() == ']') host = host.substr(1, host.size() - 2);
    const int port = std::atoi(ticket.server.c_str() + colon + 1);
    Address a;
    if (port <= 0 || port > 65535 || !Address::resolve(host, uint16_t(port), a)) return nullptr;
    auto sock = openUdpPort(0, a.family());
    if (!sock) return nullptr;
    return connectSecure(std::move(sock), a, ticket, cfg);
}

// ---------------------------------------------------------------- server
namespace {
class SecurePeerConn final : public SecureSessionConn {
public:
    SecurePeerConn(std::shared_ptr<Session> s, SecureLinkConfig cfg) : SecureSessionConn(std::move(s)), cfg_(cfg) {}
    ~SecurePeerConn() override { close(); }
    ConnState state() const override { return s_->closed ? ConnState::Closed : ConnState::Connected; }
    std::string describe() const override { return "secure udp " + s_->addr.str() + " player " + s_->playerId; }
    void close() override { s_->bye(); }
    void update(uint64_t now) override {
        if (s_->closed || !s_->port) return;
        if (s_->chan.failed() || now - s_->lastHeard > cfg_.silenceTimeoutMs) { s_->closed = true; return; }
        s_->flush(now);
    }
private:
    SecureLinkConfig cfg_;
};

struct Pending {
    uint32_t sid = 0;
    Address addr;
    TicketClaims claims;
    uint8_t salt[16] = {}, cookie[16] = {};
    std::vector<uint8_t> ticketId;      // keyed hash of the sealed ticket: resend instead of a new handshake
    Bytes challenge;                    // the exact packet, resent on a repeated request
    uint64_t created = 0;
};
} // namespace

struct SecureListener::Impl {
    SecureServerConfig cfg;
    std::unique_ptr<DatagramPort> port;
    std::map<uint32_t, std::weak_ptr<Session>> sessions;
    std::map<uint32_t, Pending> pending;
    SecureStats stats;
    ~Impl() { for (auto& [id, w] : sessions) if (auto s = w.lock()) s->port = nullptr; }

    uint32_t newSid() {
        uint32_t s = 0;
        do secureRandom(&s, sizeof(s)); while (!s || sessions.count(s) || pending.count(s));
        return s;
    }
    void raw(const Address& to, const Bytes& b) { port->sendTo(to, b.data(), b.size()); ++stats.packetsOut; stats.bytesOut += b.size(); }

    void onRequest(const uint8_t* d, size_t n, const Address& from, uint64_t now) {
        if (n != kRequestSize || d[1] != kWireVersion) { ++stats.badPackets; return; }
        const size_t len = size_t(d[2]) | size_t(d[3]) << 8;
        if (len + 4 > n) { ++stats.badPackets; return; }
        Bytes sealed(d + 4, d + 4 + len);
        auto id = keyedHash(cfg.serverKey, sealed.data(), sealed.size(), 16);
        for (auto& [sid, p] : pending)
            if (p.addr == from && p.ticketId == id) { raw(from, p.challenge); return; }   // our challenge was lost
        auto claims = openTicket(cfg.serverKey, sealed, cfg.clock());
        if (!claims || (cfg.admit && !cfg.admit(*claims))) { ++stats.rejectedTickets; return; }
        if (pending.size() >= kMaxPending) {   // drop the oldest half-open handshake
            auto oldest = pending.begin();
            for (auto it = pending.begin(); it != pending.end(); ++it) if (it->second.created < oldest->second.created) oldest = it;
            pending.erase(oldest);
        }
        Pending p;
        p.sid = newSid(); p.addr = from; p.claims = *claims; p.ticketId = id; p.created = now;
        secureRandom(p.salt, 16); secureRandom(p.cookie, 16);
        uint8_t head[5]; head[0] = kChallenge; put32(head + 1, p.sid);
        Nonce nonce; secureRandom(nonce.data(), nonce.size());
        uint8_t plain[32]; std::memcpy(plain, p.salt, 16); std::memcpy(plain + 16, p.cookie, 16);
        Bytes c = aeadSeal(claims->s2c, nonce, head, 5, plain, 32);
        p.challenge.assign(head, head + 5);
        p.challenge.insert(p.challenge.end(), nonce.begin(), nonce.end());
        p.challenge.insert(p.challenge.end(), c.begin(), c.end());
        raw(from, p.challenge);
        pending.emplace(p.sid, std::move(p));
    }

    void onResponse(const uint8_t* d, size_t n, const Address& from, uint64_t now, std::vector<SecureAccepted>& fresh) {
        if (n < kHead) return;
        const uint32_t sid = get32(d + 1);
        Bytes plain;
        if (auto it = sessions.find(sid); it != sessions.end()) {   // our Accept was lost: say it again
            auto s = it->second.lock();
            if (!s || s->closed) return;
            const uint64_t seq = get64(d + 5);
            if (!s->window.fresh(seq) || !openPacket(d, n, s->recvKey, plain) || plain.size() != 17 ||
                !equalBytes(plain.data() + 1, s->cookie, 16)) { ++stats.badPackets; return; }
            s->window.mark(seq);
            s->lastHeard = now;
            s->sendInner(kAccept, nullptr, 0);
            return;
        }
        auto it = pending.find(sid);
        if (it == pending.end()) return;
        Pending& p = it->second;
        const Key c2s = deriveKey(p.claims.c2s, "c2s", sid, p.salt);
        if (!openPacket(d, n, c2s, plain) || plain.size() != 17 || !equalBytes(plain.data() + 1, p.cookie, 16)) { ++stats.badPackets; return; }
        if (sessions.size() >= cfg.maxSessions) return;
        // The same player again (new ticket handshake after a drop): the old link is dead.
        for (auto& [osid, w] : sessions)
            if (auto o = w.lock(); o && o->matchId == p.claims.matchId && o->playerId == p.claims.playerId) o->closed = true;
        auto s = std::make_shared<Session>();
        s->port = port.get(); s->addr = from; s->sid = sid; s->stats = &stats;
        s->sendKey = deriveKey(p.claims.s2c, "s2c", sid, p.salt);
        s->recvKey = c2s;
        std::memcpy(s->cookie, p.cookie, 16);
        s->window.mark(get64(d + 5));
        s->lastHeard = now; s->accepted = true;
        s->matchId = p.claims.matchId; s->playerId = p.claims.playerId;
        s->chan.setRedundancy(cfg.link.redundancyMs);
        s->chan.heard(now);
        s->sendInner(kAccept, nullptr, 0);
        sessions[sid] = s;
        ++stats.handshakes;
        SecureAccepted a;
        a.claims = p.claims;
        wipe(a.claims.c2s.data(), 32); wipe(a.claims.s2c.data(), 32);   // the game never needs the keys
        a.conn = std::make_unique<SecurePeerConn>(s, cfg.link);
        fresh.push_back(std::move(a));
        pending.erase(it);
    }

    void onData(const uint8_t* d, size_t n, const Address& from, uint64_t now) {
        if (n < kHead) return;
        auto it = sessions.find(get32(d + 1));
        if (it == sessions.end()) return;
        auto s = it->second.lock();
        if (!s || s->closed) return;
        const uint64_t seq = get64(d + 5);
        if (!s->window.fresh(seq)) { ++stats.replays; return; }
        Bytes plain;
        if (!openPacket(d, n, s->recvKey, plain)) { ++stats.badPackets; return; }
        const bool newest = !s->window.any || seq > s->window.highest;
        s->window.mark(seq);
        if (!(from == s->addr) && newest) { s->addr = from; ++stats.migrations; }   // NAT rebinding / network change
        s->deliver(plain, now);
    }
};

SecureListener::SecureListener(SecureServerConfig cfg) : impl_(std::make_unique<Impl>()) { impl_->cfg = std::move(cfg); }
SecureListener::~SecureListener() = default;

bool SecureListener::open(uint16_t port, bool loopbackOnly) {
    auto p = openUdpPort(port, 0, loopbackOnly);
    if (!p) return false;
    impl_->port = std::move(p);
    return true;
}

void SecureListener::attach(std::unique_ptr<DatagramPort> port) { impl_->port = std::move(port); }
uint16_t SecureListener::port() const { return impl_->port ? impl_->port->localPort() : 0; }
const SecureStats& SecureListener::stats() const { return impl_->stats; }
size_t SecureListener::sessions() const {
    size_t n = 0;
    for (auto& [id, w] : impl_->sessions) if (auto s = w.lock(); s && !s->closed) ++n;
    return n;
}

std::vector<SecureAccepted> SecureListener::poll(uint64_t now) {
    std::vector<SecureAccepted> fresh;
    auto& I = *impl_;
    if (!I.port) return fresh;
    for (auto it = I.sessions.begin(); it != I.sessions.end();) it = it->second.expired() ? I.sessions.erase(it) : std::next(it);
    for (auto it = I.pending.begin(); it != I.pending.end();) it = now - it->second.created > kPendingMs ? I.pending.erase(it) : std::next(it);
    uint8_t buf[2048];
    Address from;
    int n;
    for (int budget = 0; budget < 8192 && (n = I.port->recvFrom(from, buf, sizeof(buf))) >= 1; ++budget) {
        ++I.stats.packetsIn; I.stats.bytesIn += uint64_t(n);
        switch (buf[0]) {
        case kPing:
            if (n == int(kPingSize)) {
                ++I.stats.pings;
                uint8_t pong[10]; pong[0] = kPong; std::memcpy(pong + 1, buf + 1, 8); pong[9] = load_;
                I.port->sendTo(from, pong, sizeof(pong));
            }
            break;
        case kRequest: I.onRequest(buf, size_t(n), from, now); break;
        case kResponse: I.onResponse(buf, size_t(n), from, now, fresh); break;
        case kData: I.onData(buf, size_t(n), from, now); break;
        default: ++I.stats.badPackets; break;
        }
    }
    return fresh;
}

void sendBattlePing(DatagramPort& port, const Address& server, uint64_t nowMs) {
    uint8_t p[kPingSize] = {};
    p[0] = kPing; put64(p + 1, nowMs);
    port.sendTo(server, p, sizeof(p));
}

bool readBattlePong(const uint8_t* d, size_t n, uint64_t nowMs, uint32_t& rttMs, uint8_t& load) {
    if (n != 10 || d[0] != kPong) return false;
    const uint64_t sent = get64(d + 1);
    if (sent > nowMs || nowMs - sent > 60000) return false;
    rttMs = uint32_t(nowMs - sent); load = d[9];
    return true;
}

// ---------------------------------------------------------------- real UDP
namespace {
class UdpPort final : public DatagramPort {
public:
    bool open(uint16_t port, int family, bool loopbackOnly) { return sock_.open(port, family, loopbackOnly); }
    bool sendTo(const Address& to, const uint8_t* data, size_t n) override { return sock_.sendTo(to, data, n); }
    int recvFrom(Address& from, uint8_t* buf, size_t cap) override { return sock_.recvFrom(from, buf, cap); }
    uint16_t localPort() const override { return sock_.localPort(); }
private:
    UdpSocket sock_;
};
} // namespace

std::unique_ptr<DatagramPort> openUdpPort(uint16_t port, int family, bool loopbackOnly) {
    auto p = std::make_unique<UdpPort>();
    if (!p->open(port, family, loopbackOnly)) return nullptr;
    return p;
}

} // namespace aiverse::net
#endif
