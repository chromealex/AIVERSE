#include "MatchTicket.h"
#include <chrono>
#include <cstring>
#include "../core/Hash.h"
#include "../sim/Command.h"

namespace aiverse::net {

namespace {
constexpr uint8_t kSealedMagic[4] = {'A', 'I', 'V', 'T'};
constexpr size_t kMaxPlayerId = 64;

void putKey(ByteWriter& w, const Key& k) { w.buf.insert(w.buf.end(), k.begin(), k.end()); }
bool getKey(ByteReader& r, Key& k) { for (auto& b : k) b = r.u8(); return r.ok(); }

Bytes publicHeader(uint64_t matchId, uint64_t expires, uint64_t compat) {
    ByteWriter w;
    w.buf.insert(w.buf.end(), kSealedMagic, kSealedMagic + 4);
    w.u32(kTicketVersion); w.u64(matchId); w.u64(expires); w.u64(compat);
    return w.buf;
}
} // namespace

uint64_t unixMs() {
    using namespace std::chrono;
    return uint64_t(duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());
}

ClientTicket issueTicket(const Key& serverKey, TicketClaims claims, const std::string& serverAddress) {
    if (isZero(claims.c2s)) claims.c2s = randomKey();
    if (isZero(claims.s2c)) claims.s2c = randomKey();
    if (claims.playerId.size() > kMaxPlayerId) claims.playerId.resize(kMaxPlayerId);
    ByteWriter priv;
    priv.str(claims.playerId); priv.u8(claims.slot); putKey(priv, claims.c2s); putKey(priv, claims.s2c);
    Nonce nonce;
    secureRandom(nonce.data(), nonce.size());
    const Bytes head = publicHeader(claims.matchId, claims.expiresAtMs, claims.compatHash);
    Bytes sealed = head;   // sealed = public header || nonce || cipher || mac
    sealed.insert(sealed.end(), nonce.begin(), nonce.end());
    Bytes c = aeadSeal(serverKey, nonce, head.data(), head.size(), priv.buf.data(), priv.buf.size());
    sealed.insert(sealed.end(), c.begin(), c.end());
    wipe(priv.buf.data(), priv.buf.size());
    ClientTicket t;
    t.server = serverAddress;
    t.claims = claims;
    t.sealed = std::move(sealed);
    return t;
}

std::optional<TicketClaims> openTicket(const Key& serverKey, const Bytes& sealed, uint64_t nowUnixMs) {
    constexpr size_t kHead = 4 + 4 + 8 + 8 + 8;
    if (sealed.size() < kHead + 24 + kMacSize || sealed.size() > 512) return std::nullopt;
    if (std::memcmp(sealed.data(), kSealedMagic, 4) != 0) return std::nullopt;
    ByteReader r(sealed.data() + 4, kHead - 4);
    if (r.u32() != kTicketVersion) return std::nullopt;
    TicketClaims c;
    c.matchId = r.u64(); c.expiresAtMs = r.u64(); c.compatHash = r.u64();
    if (!r.ok() || nowUnixMs >= c.expiresAtMs) return std::nullopt;
    Nonce nonce;
    std::memcpy(nonce.data(), sealed.data() + kHead, 24);
    Bytes plain;
    if (!aeadOpen(serverKey, nonce, sealed.data(), kHead, sealed.data() + kHead + 24, sealed.size() - kHead - 24, plain)) return std::nullopt;
    ByteReader p(plain.data(), plain.size());
    c.playerId = p.str(); c.slot = p.u8();
    const bool ok = getKey(p, c.c2s) && getKey(p, c.s2c) && p.atEnd() && !c.playerId.empty() && c.playerId.size() <= kMaxPlayerId && c.slot;
    wipe(plain.data(), plain.size());
    if (!ok) return std::nullopt;
    return c;
}

std::string ClientTicket::encode() const {
    ByteWriter w;
    w.u32(kTicketVersion);
    w.str(server); w.u64(claims.matchId); w.str(claims.playerId); w.u8(claims.slot); w.u64(claims.compatHash);
    w.u64(claims.expiresAtMs); putKey(w, claims.c2s); putKey(w, claims.s2c);
    w.u32(uint32_t(sealed.size())); w.buf.insert(w.buf.end(), sealed.begin(), sealed.end());
    std::string s = "aivt1." + toHexString(w.buf.data(), w.buf.size());
    wipe(w.buf.data(), w.buf.size());
    return s;
}

std::optional<ClientTicket> ClientTicket::decode(const std::string& text) {
    if (text.rfind("aivt1.", 0) != 0 || text.size() > 4096) return std::nullopt;
    Bytes b;
    if (!fromHexString(text.substr(6), b)) return std::nullopt;
    ByteReader r(b.data(), b.size());
    ClientTicket t;
    if (r.u32() != kTicketVersion) return std::nullopt;
    t.server = r.str(); t.claims.matchId = r.u64(); t.claims.playerId = r.str(); t.claims.slot = r.u8();
    t.claims.compatHash = r.u64(); t.claims.expiresAtMs = r.u64();
    getKey(r, t.claims.c2s); getKey(r, t.claims.s2c);
    const uint32_t n = r.u32();
    if (!r.ok() || n > 512) return std::nullopt;
    for (uint32_t i = 0; i < n; ++i) t.sealed.push_back(r.u8());
    const bool ok = r.ok() && r.atEnd() && t.server.size() <= 256;
    wipe(b.data(), b.size());
    if (!ok) return std::nullopt;
    return t;
}

std::string compatibilityKey(const std::string& appVersion, uint32_t protocol, const std::string& mapId) {
    return appVersion + "|protocol:" + std::to_string(protocol) + "|map:" + mapId;
}

uint64_t compatibilityHash(const std::string& key) {
    Hasher h;
    h.bytes(key.data(), key.size());
    return h.value();
}

} // namespace aiverse::net
