// Hosted battles: the dedicated battle server mode. An orchestrator allocates a match with its
// players, loadouts, map, seed and compatibility key; players connect with tickets over the secure
// transport and are seated by the ticket's identity. The match itself is the same input relay as
// lobby matches (ServerMatch.cpp); this file adds allocation, seating, the start payload, the
// join timeout / time limit, command filtering and the results report for meta.
#include "ServerInternal.h"
#include <algorithm>
#include <set>
#include "../../core/Hash.h"

namespace aiverse::net {

RoomSettings battleSettings(uint32_t tickRate) {
    RoomSettings s;
    s.tickRate = tickRate ? tickRate : 20;
    s.inputDelay = 1;                                    // local hero reacts next tick; prediction hides the rest
    // Run ahead of confirmation for RTT + jitter + one resend: 150 ms RTT + 60 ms jitter + 50 ms
    // at 20 Hz is ~6 ticks; 12 leaves room for loss bursts without stalling the local player.
    s.rollbackWindow = std::max<uint32_t>(8, s.tickRate * 600 / 1000);
    s.lagToleranceMs = 250;                              // a lossy player delays others' confirmation by at most this
    s.checksumInterval = std::max<uint32_t>(1, s.tickRate);   // desync check every second
    s.spectatorDelayTicks = s.tickRate * 3;
    s.allowSpectators = false;
    return s;
}

Room* Server::Impl::hostedRoom(uint64_t matchId) {
    auto it = hostedRooms.find(matchId);
    if (it == hostedRooms.end()) return nullptr;
    Room* r = room(it->second);
    return r && r->hosted ? r : nullptr;
}

static const MatchAllocation::Player* allocPlayer(const MatchAllocation& a, const std::string& playerId) {
    for (auto& p : a.players) if (!p.playerId.empty() && p.playerId == playerId) return &p;
    return nullptr;
}

bool Server::allocateMatch(const MatchAllocation& a, std::string* error) {
    auto& i = *impl_;
    auto fail = [&](const std::string& e) { if (error) *error = e; return false; };
    if (!a.matchId) return fail("matchId is 0");
    if (i.hostedRoom(a.matchId)) return fail("match " + std::to_string(a.matchId) + " already allocated");
    if (a.map.players() == 0 || a.map.file.empty()) return fail("map has no player slots");
    if (a.compatKey.empty()) return fail("compatKey is empty");
    std::set<uint8_t> slots;
    std::set<std::string> ids;
    size_t humans = 0;
    for (auto& p : a.players) {
        if (p.info.slot < 1 || p.info.slot > a.map.players()) return fail("player slot " + std::to_string(p.info.slot) + " is not on the map");
        if (!slots.insert(p.info.slot).second) return fail("slot " + std::to_string(p.info.slot) + " used twice");
        if (p.info.kind == SlotKind::Human) {
            if (p.playerId.empty() || p.playerId.size() > 64) return fail("human in slot " + std::to_string(p.info.slot) + " needs a playerId (1..64 bytes)");
            if (!ids.insert(p.playerId).second) return fail("player " + p.playerId + " listed twice");
            ++humans;
        }
        if (p.info.loadout.size() > kMaxLoadoutBytes) return fail("loadout of slot " + std::to_string(p.info.slot) + " is too large");
    }
    if (!humans) return fail("no human players");
    if (a.gameData.size() > kMaxGameDataBytes) return fail("gameData is too large");
    Room& r = i.createRoom("match " + std::to_string(a.matchId), a.gameId, a.map, a.settings);
    r.matchmade = true;
    r.queueId = kHostedQueue;
    r.hostId = 0;
    r.hosted = std::make_shared<Hosted>();
    r.hosted->alloc = a;
    r.hosted->createdMs = i.now;
    for (auto& s : r.slots) s.kind = SlotKind::Closed;
    for (auto& p : a.players) {
        SlotEntry& s = r.slots[p.info.slot - 1];
        s.kind = p.info.kind == SlotKind::Bot ? SlotKind::Bot : SlotKind::Human;
        s.name = p.info.kind == SlotKind::Bot && p.info.name.empty() ? "@net.bot" : p.info.name;
        s.ready = true;
    }
    i.hostedRooms[a.matchId] = r.id;
    i.log("hosted match %llu: room #%u map=%s, %zu humans", (unsigned long long)a.matchId, r.id, a.map.file.c_str(), humans);
    return true;
}

bool Server::admits(const TicketClaims& c) const {
    auto& i = *impl_;
    auto it = i.hostedRooms.find(c.matchId);
    if (it == i.hostedRooms.end()) return false;
    auto rit = i.rooms.find(it->second);
    if (rit == i.rooms.end() || !rit->second->hosted || rit->second->hosted->done) return false;
    const MatchAllocation& a = rit->second->hosted->alloc;
    const auto* p = allocPlayer(a, c.playerId);
    return p && p->info.slot == c.slot && c.compatHash == compatibilityHash(a.compatKey);
}

void Server::addSecureConnection(std::unique_ptr<Conn> conn, const TicketClaims& claims) {
    auto& i = *impl_;
    const uint32_t id = i.nextClientId;
    addConnection(std::move(conn));
    if (Client* c = i.client(id)) { c->trusted = true; c->playerId = claims.playerId; c->hostedMatch = claims.matchId; }
}

std::vector<HostedMatchInfo> Server::hostedMatches() const {
    std::vector<HostedMatchInfo> out;
    auto& i = *impl_;
    for (auto& [id, roomId] : i.hostedRooms) {
        auto rit = i.rooms.find(roomId);
        if (rit == i.rooms.end() || !rit->second->hosted) continue;
        const Room& r = *rit->second;
        HostedMatchInfo h;
        h.matchId = id;
        h.phase = r.hosted->done ? 2 : r.match ? 1 : 0;
        for (auto& p : r.hosted->alloc.players) if (p.info.kind == SlotKind::Human) ++h.expected;
        for (auto& s : r.slots) if (s.kind == SlotKind::Human && s.clientId) ++h.joined;
        h.tick = r.match ? r.match->confirmedNext : 0;
        h.ageMs = i.now - r.hosted->createdMs;
        out.push_back(h);
    }
    return out;
}

std::vector<MatchReport> Server::takeReports() { auto out = std::move(impl_->reports); impl_->reports.clear(); return out; }

bool Server::releaseMatch(uint64_t matchId) {
    auto& i = *impl_;
    Room* r = i.hostedRoom(matchId);
    if (!r || r->hosted->done) return false;
    r->hosted->aborted = true;
    if (r->match) {
        Match& M = *r->match;
        i.endMatch(*r, 0, NetText("net.end.released"), M.confirmedNext, 0, false);
    } else {
        i.hostedReport(*r, 0, NetText("net.end.released"), 0, 0, false, true);
        std::vector<uint32_t> ids;
        for (auto& [id, c] : i.clients) if (c->roomId == r->id) ids.push_back(id);
        for (uint32_t id : ids) if (Client* c = i.client(id)) i.leaveRoom(*c);
    }
    return true;
}

bool Server::Impl::onHostedHello(Client& c, const HelloMsg& h) {
    if (!c.trusted) {
        if (!cfg.hostedOnly) return false;
        sendError(c, ErrorCode::BadRequest, NetText("net.error.ticket_required"));
        c.conn->update(now);
        c.conn->close();
        return true;
    }
    Room* r = hostedRoom(c.hostedMatch);
    const MatchAllocation::Player* ap = r && !r->hosted->done ? allocPlayer(r->hosted->alloc, c.playerId) : nullptr;
    if (!ap) {
        sendError(c, ErrorCode::NoSuchRoom, NetText("net.error.no_such_room"));
        c.conn->update(now);
        c.conn->close();
        return true;
    }
    auto welcome = [&](Client& to, bool resumed) {
        WelcomeMsg w;
        w.clientId = to.id; w.reconnectToken = to.token; w.serverName = cfg.name; w.resumed = resumed;
        send(to, encode(w));
    };
    // The same player again (new handshake after a drop, app restart, network change): the old
    // record takes over the new connection and the match resumes (delta or full history).
    for (auto& [id, old] : clients) {
        if (old.get() == &c || !old->trusted || !old->hello || old->hostedMatch != c.hostedMatch || old->playerId != c.playerId) continue;
        if (old->conn) { old->conn->close(); old->conn.reset(); }
        old->conn = std::move(c.conn);
        old->droppedAt = 0;
        log("hosted %llu: player %s back as #%u", (unsigned long long)c.hostedMatch, old->playerId.c_str(), old->id);
        welcome(*old, true);
        if (Room* rm = room(old->roomId)) {
            send(*old, encode(roomState(*rm)));
            if (rm->match) memberResumed(*rm, *old, h.resumeSeed, h.resumeTick);
        } else if (r->match) {
            old->roomId = r->id;
            if (Member* m = member(*r, old->id)) { m->connected = true; m->synced = false; sendMatchStart(*r, *m, true); }
        }
        const uint32_t dead = c.id;
        clients.erase(dead);   // `c` is gone after this line
        return true;
    }
    c.hello = true;
    c.name = ap->info.name;                       // names come from the backend, not from the client
    c.gameId = r->hosted->alloc.gameId;
    Hasher hk; hk.bytes(c.playerId.data(), c.playerId.size());
    c.key = hk.value();
    do c.token = rng(); while (!c.token);
    c.roomId = r->id;
    SlotEntry& s = r->slots[ap->info.slot - 1];
    s.clientId = c.id;
    r->hosted->joined[ap->info.slot] = true;
    welcome(c, false);
    log("hosted %llu: player %s joined slot %u as #%u", (unsigned long long)c.hostedMatch, c.playerId.c_str(), ap->info.slot, c.id);
    if (r->match) {   // joins after the start: the history follows
        for (auto& m : r->match->members) {
            if (m.slot != ap->info.slot || m.left) continue;
            m.clientId = c.id; m.connected = true; m.synced = false;
            if (r->match->keyBySlot.find(m.slot) == r->match->keyBySlot.end()) r->match->keyBySlot[m.slot] = c.key;
            sendMatchStart(*r, m, true);
            PlayerStatusMsg ps; ps.slot = m.slot; ps.status = 0;
            Bytes b = encode(ps);
            for (auto& o : r->match->members) if (o.clientId != c.id) if (Client* oc = client(o.clientId)) send(*oc, b);
        }
    }
    broadcastRoom(*r);
    return true;
}

void Server::Impl::sendPayload(Room& r, Client& c) {
    if (!r.hosted) return;
    const MatchAllocation& a = r.hosted->alloc;
    MatchPayloadMsg p;
    p.matchId = a.matchId; p.compatKey = a.compatKey; p.mapId = a.mapId; p.gameData = a.gameData;
    for (auto& ap : a.players) p.players.push_back(ap.info);
    std::sort(p.players.begin(), p.players.end(), [](const BattlePlayer& x, const BattlePlayer& y) { return x.slot < y.slot; });
    p.joinTimeoutMs = a.joinTimeoutMs; p.maxDurationSec = a.maxDurationSec;
    send(c, encode(p));
}

bool Server::Impl::filterHostedCommand(Room& r, Member& m, const Command& c, size_t accepted) {
    const MatchAllocation& a = r.hosted->alloc;
    const bool typeOk = a.allowedCommands.empty() || std::find(a.allowedCommands.begin(), a.allowedCommands.end(), c.type) != a.allowedCommands.end();
    if (typeOk && (!a.maxCommandsPerTick || accepted < a.maxCommandsPerTick)) return true;
    ++m.droppedCommands;
    return false;
}

void Server::Impl::hostedUpdate() {
    for (auto it = hostedRooms.begin(); it != hostedRooms.end();) {
        Room* r = room(it->second);
        if (!r || !r->hosted) { it = hostedRooms.erase(it); continue; }
        Hosted& H = *r->hosted;
        ++it;
        if (H.done) continue;
        if (!r->match) {
            uint32_t expected = 0, here = 0;
            for (auto& p : H.alloc.players) if (p.info.kind == SlotKind::Human) ++expected;
            for (auto& s : r->slots) if (s.kind == SlotKind::Human && s.clientId) if (Client* c = client(s.clientId); c && c->conn && c->hello) ++here;
            const bool timeUp = now - H.createdMs >= H.alloc.joinTimeoutMs;
            if (here == expected || (timeUp && here)) { H.startedMs = now; startMatch(*r); }
            else if (timeUp) hostedReport(*r, 0, NetText("net.end.no_show"), 0, 0, false, true);
            continue;
        }
        Match& M = *r->match;
        if (M.concluding) continue;
        // Allocated players who never showed up count as gone once the reconnect grace is over.
        if (now - H.startedMs > uint64_t(cfg.reconnectGraceSec) * 1000) {
            bool changed = false, anyHuman = false;
            for (auto& m : M.members) {
                if (m.slot && !m.clientId && !m.left) { m.left = true; changed = true; }
                anyHuman |= m.slot && !m.left;
            }
            if (changed && !anyHuman) { endMatch(*r, 0, NetText("net.end.everyone_left"), M.confirmedNext, 0, false); continue; }
        }
        if (H.alloc.maxDurationSec && now - H.startedMs > uint64_t(H.alloc.maxDurationSec) * 1000) {
            log("hosted %llu: time limit", (unsigned long long)H.alloc.matchId);
            requestVerdict(*r, NetText("net.end.time_limit"));
        }
    }
    // Players of a finished battle get a moment to receive MatchEnd, then the link is closed.
    std::vector<uint32_t> idle;
    for (auto& [id, c] : clients) {
        if (!c->trusted || !c->hello || c->roomId) continue;
        if (!c->endedAt) c->endedAt = now ? now : 1;
        else if (now - c->endedAt > 5000) idle.push_back(id);
    }
    for (uint32_t id : idle) if (Client* c = client(id)) { if (c->conn) c->conn->close(); dropClient(id); }
}

void Server::Impl::hostedReport(Room& r, int32_t result, const NetText& reason, uint32_t endTick, uint64_t endHash, bool verified, bool aborted) {
    Hosted& H = *r.hosted;
    if (H.done) return;
    H.done = true;
    MatchReport rep;
    rep.matchId = H.alloc.matchId; rep.result = result; rep.reason = reason; rep.endTick = endTick; rep.endHash = endHash;
    rep.verified = verified; rep.aborted = aborted || H.aborted;
    rep.desync = r.match && r.match->desync;
    rep.durationMs = H.startedMs ? now - H.startedMs : 0;
    std::set<Bytes> distinct;
    size_t reported = 0;
    for (auto& ap : H.alloc.players) {
        if (ap.info.kind != SlotKind::Human) continue;
        MatchReport::Player p;
        p.playerId = ap.playerId; p.slot = ap.info.slot; p.team = ap.info.team;
        p.joined = H.joined.count(p.slot) > 0;
        if (r.match) for (auto& m : r.match->members) {
            if (m.slot != p.slot) continue;
            p.left = m.left;
            Client* c = client(m.clientId);
            p.connectedAtEnd = m.connected && !m.left && c && c->conn;
            p.report = m.report;
            p.droppedCommands = m.droppedCommands;
        }
        if (!p.report.empty()) { distinct.insert(p.report); ++reported; }
        rep.players.push_back(std::move(p));
    }
    rep.consensus = reported > 0 && distinct.size() == 1;
    if (rep.consensus) rep.report = *distinct.begin();
    rep.replayPath = lastReplayPath;
    lastReplayPath.clear();
    log("hosted %llu: report result %d%s%s, %zu player reports%s", (unsigned long long)rep.matchId, result, rep.aborted ? " (aborted)" : "",
        rep.desync ? " (desync)" : "", reported, rep.consensus ? " agree" : distinct.size() > 1 ? " DISAGREE" : "");
    reports.push_back(rep);
    if (cfg.onMatchReport) cfg.onMatchReport(rep);
}

#ifndef __EMSCRIPTEN__
static SecureServerConfig secureConfig(Server& srv, Server::Impl& i, const Key& key, SecureLinkConfig link) {
    SecureServerConfig sc;
    sc.serverKey = key;
    sc.admit = [&srv](const TicketClaims& c) { return srv.admits(c); };
    sc.clock = [&i] { return i.ticketClock(); };
    sc.link = link;
    sc.maxSessions = i.cfg.maxClients;
    return sc;
}

bool Server::listenSecure(uint16_t port, const Key& serverKey, bool loopbackOnly, std::string* error) {
    auto l = std::make_unique<SecureListener>(secureConfig(*this, *impl_, serverKey, {}));
    if (!l->open(port, loopbackOnly)) { if (error) *error = "cannot bind secure UDP port " + std::to_string(port); return false; }
    impl_->secure = std::move(l);
    return true;
}

void Server::attachSecure(std::unique_ptr<DatagramPort> port, const Key& serverKey, SecureLinkConfig link) {
    auto l = std::make_unique<SecureListener>(secureConfig(*this, *impl_, serverKey, link));
    l->attach(std::move(port));
    impl_->secure = std::move(l);
}

uint16_t Server::securePort() const { return impl_->secure ? impl_->secure->port() : 0; }
SecureStats Server::secureStats() const { return impl_->secure ? impl_->secure->stats() : SecureStats{}; }
void Server::setTicketClock(std::function<uint64_t()> clock) { impl_->ticketClock = std::move(clock); }
#endif

} // namespace aiverse::net
