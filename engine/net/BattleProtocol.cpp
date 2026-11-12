#include "BattleProtocol.h"

namespace aiverse::net {

namespace {
void blob(ByteWriter& w, const Bytes& b) { w.u32(uint32_t(b.size())); w.buf.insert(w.buf.end(), b.begin(), b.end()); }
bool blob(ByteReader& r, Bytes& b, size_t max) {
    const uint32_t n = r.u32();
    if (!r.ok() || n > max) return false;
    b.resize(n);
    for (uint32_t i = 0; i < n; ++i) b[i] = r.u8();
    return r.ok();
}
} // namespace

Bytes encode(const MatchPayloadMsg& m) {
    Writer w(Msg::MatchPayload);
    w.u64(m.matchId); w.str(m.compatKey); w.str(m.mapId); blob(w, m.gameData);
    w.u32(uint32_t(m.players.size()));
    for (auto& p : m.players) { w.u8(p.slot); w.u8(p.team); w.u8(uint8_t(p.kind)); w.str(p.name); blob(w, p.loadout); }
    w.u32(m.joinTimeoutMs); w.u32(m.maxDurationSec);
    return w.buf;
}

bool decode(ByteReader& r, MatchPayloadMsg& m) {
    m.matchId = r.u64(); m.compatKey = r.str(); m.mapId = r.str();
    if (!blob(r, m.gameData, kMaxGameDataBytes)) return false;
    const uint32_t n = r.u32();
    if (!r.ok() || n > 64) return false;
    m.players.resize(n);
    for (auto& p : m.players) {
        p.slot = r.u8(); p.team = r.u8(); p.kind = SlotKind(r.u8() & 3); p.name = r.str();
        if (!blob(r, p.loadout, kMaxLoadoutBytes)) return false;
    }
    m.joinTimeoutMs = r.u32(); m.maxDurationSec = r.u32();
    return r.ok();
}

Bytes encode(const ResultReportMsg& m) { Writer w(Msg::ResultReport); w.u32(m.tick); blob(w, m.blob); return w.buf; }
bool decode(ByteReader& r, ResultReportMsg& m) { m.tick = r.u32(); return blob(r, m.blob, kMaxReportBytes); }

Bytes encodeAway(bool away) { Writer w(Msg::Away); w.boolean(away); return w.buf; }

} // namespace aiverse::net
