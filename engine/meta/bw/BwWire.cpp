#include "BwWire.h"
#include "engine/meta/Encoding.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdlib>

namespace aiverse::meta::bw {

namespace {
// Cut at a UTF-8 boundary so a truncated string is still valid text.
std::string clip(const std::string& s, size_t max) {
    if (s.size() <= max) return s;
    size_t n = max;
    while (n > 0 && (uint8_t(s[n]) & 0xc0) == 0x80) --n;
    return s.substr(0, n);
}
void putString(Bytes& out, const std::string& s) {
    out.push_back(uint8_t(s.size() >> 8));
    out.push_back(uint8_t(s.size()));
    out.insert(out.end(), s.begin(), s.end());
}
// Java Character.getNumericValue on the single character after the comma.
int numericValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'z') return c - 'a' + 10;
    if (c >= 'A' && c <= 'Z') return c - 'A' + 10;
    return -1;
}
} // namespace

Bytes encodeFrame(const Frame& f) {
    const std::string func = f.idx > 0 ? f.func + "," + std::to_string(f.idx) : f.func;
    // Message.WriteToStream throws at >= 32768 bytes (and Java reads the lengths as signed shorts).
    if (func.size() >= kMaxFrameString || f.msg.size() >= kMaxFrameString) return {};
    Bytes out;
    out.reserve(4 + func.size() + f.msg.size());
    putString(out, func);
    putString(out, f.msg);
    return out;
}

bool FrameReader::next(Frame& out) {
    for (;;) {
        const size_t avail = buf_.size() - pos_;
        if (avail < 2) break;
        const size_t l1 = size_t(buf_[pos_]) << 8 | buf_[pos_ + 1];
        if (avail < 2 + l1 + 2) break;
        const size_t l2 = size_t(buf_[pos_ + 2 + l1]) << 8 | buf_[pos_ + 3 + l1];
        if (avail < 4 + l1 + l2) break;
        std::string func(reinterpret_cast<const char*>(&buf_[pos_ + 2]), l1);
        std::string msg(reinterpret_cast<const char*>(&buf_[pos_ + 4 + l1]), l2);
        pos_ += 4 + l1 + l2;
        const size_t comma = func.find(',');
        Frame f;
        if (comma == std::string::npos) {
            f.func = std::move(func);
        } else {
            if (comma + 1 >= func.size()) continue;   // Java dropped this frame
            // The Unity client parses the whole index (int.Parse); the Java server only its first character.
            const std::string tail = func.substr(comma + 1);
            bool digits = !tail.empty() && tail.size() < 9;
            for (char ch : tail) digits &= ch >= '0' && ch <= '9';
            f.idx = digits ? std::atoi(tail.c_str()) : numericValue(func[comma + 1]);
            f.func = func.substr(0, comma);
        }
        f.msg = std::move(msg);
        out = std::move(f);
        if (pos_ > 65536 && pos_ * 2 > buf_.size()) { buf_.erase(buf_.begin(), buf_.begin() + long(pos_)); pos_ = 0; }
        return true;
    }
    if (pos_ && pos_ == buf_.size()) { buf_.clear(); pos_ = 0; }
    return false;
}

// ---------------------------------------------------------------- chat

Bytes encodeChatPacket(uint16_t type, const std::string& json) {
    const std::string c = clip(json, 32767);   // Java reads the length as a signed short
    Bytes out{uint8_t(type >> 8), uint8_t(type), uint8_t(c.size() >> 8), uint8_t(c.size())};
    out.insert(out.end(), c.begin(), c.end());
    return out;
}

Bytes encodeChatAuth(const std::string& token) {
    Json j = Json::object();
    j.set("token", Json::string(token));
    return encodeChatPacket(kChatVersion, j.dump(-1));
}

Bytes encodeChatServerPacket(uint16_t type, uint16_t msgId, const std::string& json, size_t maxChunk) {
    Bytes out;
    auto be = [&](uint64_t v, int n) { for (int i = n - 1; i >= 0; --i) out.push_back(uint8_t(v >> (8 * i))); };
    if (maxChunk == 0) maxChunk = 1;
    size_t off = 0;
    do {
        const size_t chunk = std::min(maxChunk, json.size() - off);
        const bool last = off + chunk >= json.size();
        be(type, 2); be(chunk, 4); be(msgId, 2); out.push_back(last ? 0 : 1); be(json.size(), 4);
        out.insert(out.end(), json.begin() + long(off), json.begin() + long(off + chunk));
        off += chunk;
    } while (off < json.size());
    return out;
}

bool ChatReader::next(ChatPacket& out) {
    for (;;) {
        if (broken_) return false;
        const size_t avail = buf_.size() - pos_;
        if (avail < 13) break;
        auto rd = [&](size_t at, int n) { uint64_t v = 0; for (int i = 0; i < n; ++i) v = v << 8 | buf_[pos_ + at + size_t(i)]; return v; };
        const uint16_t type = uint16_t(rd(0, 2));
        const uint64_t chunk = rd(2, 4);
        const uint16_t msgId = uint16_t(rd(6, 2));
        const bool more = buf_[pos_ + 8] != 0;
        const uint64_t total = rd(9, 4);
        if (chunk > (16u << 20) || total > (64u << 20)) { broken_ = true; return false; }
        if (avail < 13 + chunk) break;
        std::string data(reinterpret_cast<const char*>(&buf_[pos_ + 13]), size_t(chunk));
        pos_ += 13 + size_t(chunk);
        if (pos_ > 65536 && pos_ * 2 > buf_.size()) { buf_.erase(buf_.begin(), buf_.begin() + long(pos_)); pos_ = 0; }
        auto it = partial_.find(msgId);
        if (!more && it == partial_.end()) { out = {type, std::move(data)}; return true; }
        ChatPacket& p = partial_[msgId];
        p.type = type;
        p.json += data;
        if (p.json.size() > total) { partial_.erase(msgId); continue; }   // inconsistent: drop it
        if (!more) { out = std::move(p); partial_.erase(msgId); return true; }
    }
    if (pos_ && pos_ == buf_.size()) { buf_.clear(); pos_ = 0; }
    return false;
}

// ---------------------------------------------------------------- battle packets

const char* rpcMethodName(RpcMethod m) {
    switch (m) {
    case RpcMethod::OnPlayerConnected_RPC: return "OnPlayerConnected_RPC";
    case RpcMethod::OnPlayerDisconnected_RPC: return "OnPlayerDisconnected_RPC";
    case RpcMethod::OnPlayerConnectionPoor_RPC: return "OnPlayerConnectionPoor_RPC";
    case RpcMethod::OnPlayerConnectionGood_RPC: return "OnPlayerConnectionGood_RPC";
    case RpcMethod::OnPlayerDefeatTimeout_RPC: return "OnPlayerDefeatTimeout_RPC";
    }
    return "";
}

bool readPackageHeader(const Bytes& packet, RpcHeader& header, std::string* method, std::string* error) {
    msgpack::Value v;
    std::string err;
    if (!msgpack::decode(packet, v, &err)) { if (error) *error = err; return false; }
    if (!v.isArray() || v.size() < 1 || !v[0].isArray() || v[0].size() < 2 || !v[0][0].isInteger() || !v[0][1].isInteger()) {
        if (error) *error = "NetworkPackage header is missing";
        return false;
    }
    header.tick = v[0][0].asInt();
    header.playerIndex = int(v[0][1].asInt());
    header.playerOrder = v[0].size() >= 3 ? int(v[0][2].asInt()) : 0;
    if (method) *method = v.size() >= 4 && v[3].isStr() ? v[3].asStr() : std::string();
    return true;
}

Bytes packageForPlayer(RpcMethod m, int playerIndex, int64_t tick) {
    if (tick < 0) tick = 0;   // the Java server clamps negative ticks
    msgpack::Writer w({true});
    w.arrayHeader(5);
    w.arrayHeader(3).integer(tick).integer(playerIndex).integer(0);
    w.integer(1);                 // NetworkGroup.Players
    w.integer(playerIndex + 1);   // networkId = playerIndex + 1
    w.str(rpcMethodName(m));
    w.nil();                      // parameters
    return w.take();
}

// ---------------------------------------------------------------- tickets

std::string issueTicket(const BattleTicket& t, const std::string& secret) {
    Json p = Json::object();
    p.set("G", Json::number(t.gameId));
    p.set("U", Json::number(t.userId));
    p.set("Exp", Json::number(t.expiresAt));
    if (t.role != 0) {
        p.set("R", Json::number(int64_t(t.role)));
        p.set("N", Json::string(t.name));
    }
    const std::string body = "v1." + enc::base64(p.dump(-1), true, false);
    const auto sig = enc::hmacSha256(secret, body);
    return body + "." + enc::base64(sig.data(), sig.size(), true, false);
}

std::optional<BattleTicket> validateTicket(const std::string& ticket, const std::string& secret, int64_t nowSec) {
    if (secret.size() < 32 || !looksLikeTicket(ticket)) return std::nullopt;
    const size_t dot = ticket.rfind('.');
    if (dot == std::string::npos || dot <= 3) return std::nullopt;
    const std::string body = ticket.substr(0, dot);
    auto sig = enc::unbase64(std::string_view(ticket).substr(dot + 1));
    auto payload = enc::unbase64(std::string_view(body).substr(3));
    if (!sig || !payload) return std::nullopt;
    const auto expect = enc::hmacSha256(secret, body);
    if (!enc::equalBytes(sig->data(), sig->size(), expect.data(), expect.size())) return std::nullopt;
    BattleTicket t;
    try {
        Json j = Json::parse(std::string(payload->begin(), payload->end()));
        if (!j.isObject() || !j["G"].isNumber() || !j["U"].isNumber() || !j["Exp"].isNumber()) return std::nullopt;
        t.gameId = jsonInt(j["G"]);
        t.userId = jsonInt(j["U"]);
        t.expiresAt = jsonInt(j["Exp"]);
        t.role = int(jsonInt(j["R"]));
        t.name = j.getStr("N");
    } catch (const std::exception&) {
        return std::nullopt;
    }
    if (t.expiresAt < nowSec) return std::nullopt;
    return t;
}

int64_t jsonInt(const Json& j, int64_t def) {
    if (j.isString()) {   // Gson writes longs as numbers, but some DTOs carry ids as strings
        char* end = nullptr;
        errno = 0;
        const long long v = std::strtoll(j.str().c_str(), &end, 10);
        return errno == 0 && end && *end == 0 && !j.str().empty() ? v : def;
    }
    if (!j.isNumber()) return def;
    const std::string& lit = j.numberLiteral();
    char* end = nullptr;
    errno = 0;
    const long long v = std::strtoll(lit.c_str(), &end, 10);
    if (errno == 0 && end && *end == 0) return v;
    const double d = std::strtod(lit.c_str(), nullptr);
    if (!std::isfinite(d) || d > 9.2e18 || d < -9.2e18) return def;
    return int64_t(std::llround(d));
}

} // namespace aiverse::meta::bw
