// Matches formed by an external matchmaker (a meta backend): Server::reserve() registers the seats,
// players claim them with a ticket ("ticket:<ticket>" as the queue id), the match starts when all
// human seats are taken, and ServerConfig::onExternalMatchEnd reports the outcome (or the cancel).
#include "ServerInternal.h"

namespace aiverse::net {

bool Server::reserve(Reservation r, std::string* error) {
    auto& i = *impl_;
    auto fail = [&](const char* e) { if (error) *error = e; return false; };
    if (r.externalId.empty() || r.gameId.empty() || r.mapFile.empty()) return fail("externalId, gameId and mapFile are required");
    if (r.seats.size() < 2 || r.seats.size() > 32) return fail("2..32 seats");
    if (i.reserved.count(r.externalId)) return fail("already reserved");
    for (auto& [id, room] : i.rooms) if (room->externalId == r.externalId) return fail("already hosted");
    bool human = false;
    for (auto& s : r.seats) human |= !s.bot && s.userId;
    if (!human) return fail("no human seat");
    ReservedMatch m;
    m.createdAt = i.now;
    m.spec = std::move(r);
    const std::string id = m.spec.externalId;
    i.log("reserved external match %s: %zu seats on %s", id.c_str(), m.spec.seats.size(), m.spec.mapFile.c_str());
    i.reserved.emplace(id, std::move(m));
    return true;
}

size_t Server::reservations() const { return impl_->reserved.size(); }

void Server::Impl::onTicketJoin(Client& c, const std::string& ticket, const std::vector<MapInfo>& maps) {
    std::string externalId;
    uint64_t userId = 0;
    if (!cfg.ticketCheck || !cfg.ticketCheck(ticket, externalId, userId)) {
        sendError(c, ErrorCode::NoSuchRoom, NetText("net.error.no_such_room"));
        return;
    }
    auto it = reserved.find(externalId);
    if (it == reserved.end()) {
        // Already running: a player whose ticket names a live match comes back through Hello resume.
        sendError(c, ErrorCode::NoSuchRoom, NetText("net.error.no_such_room"));
        return;
    }
    ReservedMatch& R = it->second;
    if (c.gameId != R.spec.gameId) { sendError(c, ErrorCode::DataMismatch, NetText("net.error.data_mismatch")); return; }
    const MapInfo* mine = nullptr;
    for (auto& m : maps) if (m.file == R.spec.mapFile) { mine = &m; break; }
    if (!mine || mine->players() != R.spec.seats.size() || (!R.map.file.empty() && R.map.dataHash != mine->dataHash)) {
        sendError(c, ErrorCode::DataMismatch, NetText("net.error.data_mismatch"));
        return;
    }
    size_t seat = R.spec.seats.size();
    for (size_t k = 0; k < R.spec.seats.size(); ++k)
        if (!R.spec.seats[k].bot && R.spec.seats[k].userId == userId) { seat = k; break; }
    if (seat == R.spec.seats.size()) { sendError(c, ErrorCode::NoSuchRoom, NetText("net.error.no_such_room")); return; }
    if (auto s = R.seated.find(seat); s != R.seated.end() && s->second != c.id) {
        // The same user again (a new connection after a crash): the newest connection wins.
        if (Client* old = client(s->second); old && old->conn) old->conn->close();
    }
    if (R.map.file.empty()) R.map = *mine;
    if (c.roomId) leaveRoom(c);
    leaveQueue(c);
    R.seated[seat] = c.id;
    log("client #%u '%s' took seat %zu of external match %s", c.id, c.name.c_str(), seat + 1, externalId.c_str());
    QueueStatusMsg s;
    s.state = 1; s.queueId = "ticket"; s.waitedSec = uint32_t((now - R.createdAt) / 1000); s.inQueue = uint32_t(R.seated.size());
    send(c, encode(s));
    size_t humans = 0;
    for (auto& st : R.spec.seats) humans += !st.bot;
    if (R.seated.size() == humans) startReserved(externalId);
}

void Server::Impl::releaseSeat(uint32_t clientId) {
    for (auto& [id, R] : reserved)
        for (auto it = R.seated.begin(); it != R.seated.end();)
            it = it->second == clientId ? R.seated.erase(it) : std::next(it);
}

void Server::Impl::startReserved(const std::string& externalId) {
    auto it = reserved.find(externalId);
    if (it == reserved.end()) return;
    ReservedMatch R = std::move(it->second);
    reserved.erase(it);
    Room& r = createRoom("@net.queue.default", R.spec.gameId, R.map, R.spec.settings);
    r.matchmade = true;
    r.queueId = "external";
    r.externalId = externalId;
    std::map<uint8_t, uint64_t> userBySlot;
    for (size_t k = 0; k < R.spec.seats.size() && k < r.slots.size(); ++k) {
        const ReservedSeat& seat = R.spec.seats[k];
        SlotEntry& s = r.slots[k];
        auto st = R.seated.find(k);
        Client* c = st != R.seated.end() ? client(st->second) : nullptr;
        if (!seat.bot && c && c->conn) {
            s.kind = SlotKind::Human; s.clientId = c->id; s.name = seat.name.empty() ? c->name : seat.name; s.ready = true;
            c->roomId = r.id;
            if (!r.hostId) r.hostId = c->id;
            userBySlot[uint8_t(k + 1)] = seat.userId;
            sendQueueStatus(*c, 2);
        } else {
            s.kind = SlotKind::Bot; s.name = seat.name.empty() ? "@net.bot" : seat.name; s.ready = true;
        }
    }
    log("external match %s starts in room #%u", externalId.c_str(), r.id);
    startMatch(r);
    // The backend identifies players by its user ids: results and resumes use them, not client keys.
    if (r.match) for (auto& [slot, user] : userBySlot) r.match->keyBySlot[slot] = user;
    if (cfg.onExternalMatchStarted) cfg.onExternalMatchStarted(externalId);
}

void Server::Impl::cancelReserved(const std::string& externalId, const std::string& reason) {
    auto it = reserved.find(externalId);
    if (it == reserved.end()) return;
    ReservedMatch R = std::move(it->second);
    reserved.erase(it);
    log("external match %s cancelled: %s", externalId.c_str(), reason.c_str());
    for (auto& [seat, id] : R.seated)
        if (Client* c = client(id)) sendError(*c, ErrorCode::NoSuchRoom, NetText("net.error.no_such_room"));
    if (!cfg.onExternalMatchEnd) return;
    ExternalMatchEnd e;
    e.externalId = externalId;
    e.reason = reason;
    e.endMs = now;
    for (size_t k = 0; k < R.spec.seats.size(); ++k) {
        ExternalSeatResult s;
        s.slot = uint8_t(k + 1); s.team = R.spec.seats[k].team; s.userId = R.spec.seats[k].userId; s.bot = R.spec.seats[k].bot;
        s.joined = R.seated.count(k) != 0;
        e.seats.push_back(s);
    }
    cfg.onExternalMatchEnd(e);
}

void Server::Impl::updateReservations() {
    std::vector<std::string> due;
    for (auto& [id, R] : reserved)
        if (now - R.createdAt >= uint64_t(R.spec.joinTimeoutSec) * 1000) due.push_back(id);
    for (auto& id : due) {
        ReservedMatch& R = reserved[id];
        if (R.spec.fillMissingWithBots && !R.seated.empty()) {
            for (size_t k = 0; k < R.spec.seats.size(); ++k) if (!R.seated.count(k)) R.spec.seats[k].bot = true;
            startReserved(id);
        } else {
            cancelReserved(id, R.seated.empty() ? "nobody joined" : "players missing");
        }
    }
}

} // namespace aiverse::net
