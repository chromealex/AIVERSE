// Replica server: authoritative objects, per-client delta snapshots, interest, budget, input (docs/replica.md).
//
// Delta model: per client and object the server remembers what it last sent of every snapshot field and in
// which snapshot (seq). Acks (newest seq + 32 previous bits, on input packets) resolve those sends to
// confirmed or lost. A field goes out when its quantized value differs from the last send or that send was
// lost; values are absolute, so the client simply overwrites in seq order (it drops older snapshots).
// An object goes out as a full "create" until a snapshot carrying its create is confirmed; a removal repeats
// until confirmed, and only then the client is told to forget the object's reliable fields and the
// object's id may return to the free list (once no client tracks it any more).
//
// Change versions: every quantized change stamps the object (and the field) with the current broadcast epoch.
// A track remembers the epoch at which the client's copy was last in sync (syncVer) and whether everything it
// was sent is confirmed (settled); a settled track whose object has not changed since costs one compare.
// Numbers of 8+ bits whose last send is confirmed may go as a small difference to that value (delta flag).
#include <algorithm>
#include <cmath>
#include <map>
#include <unordered_map>
#include "engine/net/replica/ReplicaWire.h"

namespace aiverse::net::replica {
using namespace wire;

namespace {
enum St : uint8_t { kPending = 0, kInFlight = 1, kConfirmed = 2, kLost = 3 };
constexpr float kHysteresis = 1.15f;        // an object leaves relevance only past radius x this
constexpr uint32_t kInputWindow = 64;       // input frames ahead of the next expected one
constexpr size_t kMaxPendingInputs = 64;
constexpr int kMaxMessagesPerPoll = 512;
constexpr size_t kMaxRemovalsPerSnapshot = 4096;
constexpr int kMaxMisses = 32;
// A viewer may move this fraction of the smallest radius before relevance is recomputed from the grid (until
// then objects enter / leave late by at most that much; changed objects are always re-tested exactly).
constexpr float kViewSlack = 0.0375f;              // candidates in a row that did not fit: the snapshot is full
const Value kEmpty{};

uint64_t meta(uint32_t seq, uint8_t st) { return uint64_t(seq) | (uint64_t(st) << 32); }
uint32_t metaSeq(uint64_t m) { return uint32_t(m); }
uint8_t metaSt(uint64_t m) { return uint8_t(m >> 32); }
} // namespace

struct Server::Impl {
    struct Obj {
        bool alive = false;
        ClassId cls = 0;
        uint8_t owner = 0;
        uint8_t gen = 0;                         // bumped on every create with this id (kept across destroy)
        uint32_t refs = 0;                       // clients tracking it (id reuse waits for 0)
        std::vector<Value> values;
        // quantized snapshot slots + owner (L.slots), then per snapshot field + owner the epoch of its newest
        // change (L.maskBits): one allocation, read together
        std::vector<uint64_t> q;
        uint64_t* fver(const ClassLayout& L) { return q.data() + L.slots; }
        const uint64_t* fver(const ClassLayout& L) const { return q.data() + L.slots; }
        std::vector<std::pair<uint8_t, bool>> vis;
        uint64_t relDirty = 0;                   // reliable fields changed since the last broadcast
        std::array<float, 3> pos{};
    };
    struct Track {
        uint32_t stamp = 0;                      // relevance pass / change-log dedup stamp of the snapshot being built
        bool on = false;                         // tracked: the client has it or a create is on the way
        bool rel = false;                        // relevant now (on && !rel: removing, in Peer::leaving)
        bool settled = false;                    // every send confirmed: nothing to repair, only changes matter
        bool inWork = false, inLeaving = false;
        uint8_t cst = kPending, rst = kPending;  // create / removal state
        float acc = 0, dist = 0;
        uint64_t syncVer = 0;                    // epoch at which sent == the object's quantized state
        uint32_t cseq = 0, rseq = 0;
        uint32_t cmask = 0;                      // bit i: snapshot cseq - i carried this object's create
        uint32_t metaAt = 0, trackedAt = 0;
        // one allocation: last sent quantized slots, then per snapshot field + owner: seq | state << 32
        std::vector<uint64_t> data;
        uint64_t* sent() { return data.data(); }
        uint64_t* meta() { return data.data() + metaAt; }
    };
    struct Rec { uint32_t seq = 0; uint64_t at = 0; bool acked = false; };
    struct Peer {
        std::unique_ptr<Conn> conn;
        uint8_t slot = 0;
        bool welcomed = false, closing = false;
        uint64_t attachedAt = 0, lastPoll = 0, lastTokenAt = 0;
        double nextSnap = 0, tokens = 0, msgTokens = 0;
        std::vector<Track> tracks;               // by object id
        std::vector<ObjectId> tracked;
        // Incremental relevance: a full grid pass when a viewer moved more than `slack` since the last one,
        // else only the objects in the change log since logPos are re-tested.
        bool full = true;
        std::vector<std::array<float, 3>> fullViewers;
        uint64_t logPos = 0;
        std::vector<ObjectId> work;              // tracks that may have something to send (changed, unconfirmed, starved)
        std::vector<ObjectId> leaving;           // tracked, no longer relevant: removal until confirmed
        size_t lastTaken = 0;                    // entries in the previous snapshot
        std::vector<Rec> ring;                   // power-of-two size >= history (index = seq & mask)
        uint32_t ringMask = 0;
        uint32_t seq = 0, newestAck = 0, stamp = 0, srtt = 0;
        std::vector<std::array<float, 3>> viewers;
        uint32_t nextInput = 1, lastApplied = 0;
        int gapWait = 0;
        std::map<uint32_t, Bytes> inputs;
    };
    struct Cand { float key; ObjectId id; };
    // mask: fields written; dmask: delta-coded ones, widths: their component widths (5 bits per field k < 12)
    struct Pick { ObjectId id; uint64_t mask, dmask, widths; bool create; };
    struct Eval { Cand c; Pick pick; uint32_t bits; };
    struct GridEnt { ObjectId id; float x, y, z, r; bool vis; };
    struct Cell { std::vector<GridEnt> ents; };

    Schema schema;
    ServerOptions opt;
    Layout layout;
    Server* self = nullptr;
    std::vector<Obj> objs{1};                    // index = id; 0 unused
    std::vector<uint64_t> vers{0};               // by id: epoch of the newest quantized change (snapshot field or owner)
    // Change log: ids created / destroyed / moved / changed / re-owned / re-visibility'd, once per epoch each.
    std::vector<ObjectId> changeLog;
    uint64_t logStart = 0;                       // absolute index of changeLog[0]
    std::vector<uint64_t> logged{0};             // by id: epoch it was last logged
    float slack = 0;                             // viewer movement that does not need a full relevance pass
    bool gridBuilt = false;
    std::vector<ObjectId> freeIds;               // FIFO: oldest freed first
    size_t freeHead = 0;
    std::array<std::unique_ptr<Peer>, 256> peers;
    uint32_t tick = 0;
    uint64_t epoch = 1;                          // ++ after every broadcast; set() stamps changes with it
    ServerStats stats;
    std::vector<ObjectId> relDirty;
    bool inPoll = false;
    float maxRadius = 0, cell = 32;
    std::unordered_map<uint64_t, uint32_t> cellIndex;   // cell key -> index into cells
    std::vector<Cell> cells;
    std::vector<std::array<int32_t, 3>> cellPos;
    size_t usedCells = 0;                        // cells with objects this broadcast
    int32_t minCy = 0, maxCy = 0;                // occupied cell layers (a flat world probes one)
    std::vector<ObjectId> always, forced;
    std::array<std::vector<ObjectId>, 256> owned;
    std::vector<ObjectId> rel, removals;
    std::vector<Cand> cands;
    std::vector<Pick> taken;
    std::vector<Eval> evals;

    Impl(const Schema& s, ServerOptions o) : schema(s), opt(o), layout(s) {
        opt.snapshotRate = std::clamp(opt.snapshotRate, 1, 1000);
        opt.tickRate = std::clamp(opt.tickRate, 1, 10000);
        opt.history = std::clamp(opt.history, 2, 1024);
        opt.maxSnapshotBytes = std::clamp(opt.maxSnapshotBytes, 64, 120000);
        opt.maxInputsPerTick = std::max(1, opt.maxInputsPerTick);
        opt.inputGapTicks = std::max(0, opt.inputGapTicks);
        if (!(opt.defaultRadius > 0)) opt.defaultRadius = 64;
        maxRadius = opt.defaultRadius;
        for (size_t c = 0; c < schema.size(); ++c) maxRadius = std::max(maxRadius, schema.cls(ClassId(c)).radius);
        cell = std::max(opt.cellSize > 0 ? opt.cellSize : 32.0f, maxRadius * kHysteresis / 64.0f);
        float minRadius = opt.defaultRadius;
        for (size_t c = 0; c < schema.size(); ++c) if (schema.cls(ClassId(c)).radius > 0) minRadius = std::min(minRadius, schema.cls(ClassId(c)).radius);
        slack = minRadius * kViewSlack;
    }

    void logChange(ObjectId id) {
        if (logged[id] == epoch) return;
        logged[id] = epoch;
        changeLog.push_back(id);
    }
    uint64_t logEnd() const { return logStart + changeLog.size(); }

    Obj* obj(ObjectId id) { return id && id < objs.size() && objs[id].alive ? &objs[id] : nullptr; }
    const Obj* obj(ObjectId id) const { return id && id < objs.size() && objs[id].alive ? &objs[id] : nullptr; }
    float radiusOf(ClassId c) const { float r = schema.cls(c).radius; return r > 0 ? r : opt.defaultRadius; }

    void release(ObjectId id) {
        Obj& o = objs[id];
        if (o.refs) --o.refs;
        if (!o.alive && o.refs == 0) freeIds.push_back(id);
    }

    // ---------------------------------------------------------------- sending helpers
    void sendReliable(Peer& p, const Bytes& b) { p.conn->send(b); stats.reliableBytes += b.size(); }
    void sendField(Peer& p, ObjectId id, const Obj& o, int f) {
        ByteWriter w;
        w.u8(uint8_t(Msg::Field)); w.u32(id); w.u16(o.cls); w.u8(o.gen); w.u8(uint8_t(f));
        writeValue(w, layout.classes[o.cls].fields[size_t(f)], o.values[size_t(f)]);
        sendReliable(p, w.buf);
    }
    void sendReject(Peer& p, RejectReason why) {
        ByteWriter w; w.u8(uint8_t(Msg::Reject)); w.u8(uint8_t(why));
        sendReliable(p, w.buf);
        p.closing = true;
    }

    // ---------------------------------------------------------------- peers
    void drop(uint8_t slot) {
        auto& pp = peers[slot];
        if (!pp) return;
        bool was = pp->welcomed;
        for (ObjectId id : pp->tracked) release(id);
        pp->conn->close();
        pp.reset();
        if (was && self->onLeave) self->onLeave(slot);
    }

    uint8_t resolve(const Peer& p, uint32_t seq) const {
        const Rec& r = p.ring[seq & p.ringMask];
        if (r.seq != seq || p.seq - seq >= uint32_t(opt.history)) return kLost;   // out of the history: resend
        if (r.acked) return kConfirmed;
        return seq < p.newestAck ? kLost : kInFlight;   // the client drops older snapshots once a newer one arrived
    }
    // A create is confirmed when any snapshot that carried it was acked. A changed object whose create is in
    // flight goes as a create again; without this, an object that changes every snapshot on a link whose RTT
    // exceeds the snapshot interval would be re-created forever (each resend moving the create to a newer seq).
    uint8_t resolveCreate(const Peer& p, const Track& t) const {
        bool inFlight = false;
        for (uint32_t i = 0; i < 32; ++i) {
            if (!((t.cmask >> i) & 1u)) continue;
            const uint8_t st = resolve(p, t.cseq - i);
            if (st == kConfirmed) return kConfirmed;
            inFlight |= st == kInFlight;
        }
        return inFlight ? kInFlight : kLost;
    }
    uint8_t fieldState(const Peer& p, Track& t, int k) const {
        uint64_t m = t.meta()[k];
        uint8_t st = metaSt(m);
        if (st == kInFlight) { st = resolve(p, metaSeq(m)); if (st != kInFlight) t.meta()[k] = meta(metaSeq(m), st); }
        return st;
    }

    void handleAck(Peer& p, uint32_t newest, uint32_t bits, uint64_t now) {
        if (newest == 0) return;
        if (newest > p.seq) { ++stats.rejected; return; }
        for (uint32_t i = 0; i <= 32; ++i) {
            if (i > 0 && !(bits & (1u << (i - 1)))) continue;
            if (newest < i + 1 && i > 0) break;
            uint32_t s = newest - i;
            if (s == 0) break;
            Rec& r = p.ring[s & p.ringMask];
            if (r.seq != s || r.acked) continue;
            r.acked = true;
            if (i == 0) {
                uint32_t sample = uint32_t(std::min<uint64_t>(now - std::min(now, r.at), 60000));
                p.srtt = p.srtt ? (p.srtt * 7 + sample) / 8 : sample;
            }
        }
        p.newestAck = std::max(p.newestAck, newest);
    }

    void handleInput(Peer& p, const Bytes& m, uint64_t now) {
        ByteReader r(m.data() + 1, m.size() - 1);
        uint32_t newest = r.u32(), bits = r.u32();
        uint8_t count = r.u8();
        uint32_t first = r.u32();
        if (!r.ok() || count > kMaxInputFramesPerPacket) { ++stats.rejected; return; }
        std::vector<Bytes> frames(count);
        for (auto& f : frames) {
            uint16_t n = r.u16();
            if (!r.ok() || n > opt.maxInputBytes) { ++stats.rejected; return; }
            f.resize(n);
            for (auto& b : f) b = r.u8();
        }
        if (!r.ok() || !r.atEnd()) { ++stats.rejected; return; }
        handleAck(p, newest, bits, now);
        for (uint32_t i = 0; i < count; ++i) {
            uint32_t s = first + i;
            if (s == 0 || s < p.nextInput) continue;                       // already applied or skipped
            if (s - p.nextInput >= kInputWindow) {
                // far ahead: the start of a stream we never saw (lost join packets) resyncs; else hostile
                if (p.inputs.empty() && i == 0) p.nextInput = s;
                else { ++stats.rejected; continue; }
            }
            if (p.inputs.size() >= kMaxPendingInputs) { ++stats.rejected; continue; }
            p.inputs.emplace(s, std::move(frames[i]));
        }
    }

    void handleMessage(Peer& p, const Bytes& m) {
        ByteReader r(m.data() + 1, m.size() - 1);
        uint16_t channel = r.u16();
        Bytes payload;
        if (!readBytes(r, payload, opt.maxMessageBytes) || !r.atEnd()) { ++stats.rejected; return; }
        if (p.msgTokens < 1) { ++stats.rejected; return; }
        p.msgTokens -= 1;
        if (self->onMessage) self->onMessage(p.slot, channel, payload);
    }

    void handleHello(Peer& p, const Bytes& m, uint64_t now) {
        if (p.welcomed) return;
        ByteReader r(m.data() + 1, m.size() - 1);
        uint32_t magic = r.u32(), protocol = r.u32();
        uint64_t hash = r.u64();
        if (!r.ok() || magic != kMagic || protocol != kProtocol) { sendReject(p, RejectReason::Protocol); return; }
        if (hash != schema.hash()) { sendReject(p, RejectReason::Schema); return; }
        p.welcomed = true;
        ByteWriter w;
        w.u8(uint8_t(Msg::Welcome)); w.u32(kProtocol); w.u8(p.slot); w.u16(uint16_t(std::min(opt.tickRate, 65535)));
        w.u16(uint16_t(std::min(opt.snapshotRate, 65535)));
        sendReliable(p, w.buf);
        const double interval = 1000.0 / opt.snapshotRate;
        p.nextSnap = double(now) + std::fmod(p.slot * 7.0, interval);   // spread clients over the interval
        p.lastTokenAt = now;
        p.tokens = double(opt.bytesPerSecond) / opt.snapshotRate;
        p.msgTokens = opt.maxMessagesPerSecond;
        p.full = true;
        p.logPos = logEnd();
        if (self->onJoin) self->onJoin(p.slot);
    }

    void deliverInputs(Peer& p) {
        int n = 0;
        while (n < opt.maxInputsPerTick && !p.closing) {
            auto it = p.inputs.begin();
            if (it == p.inputs.end()) break;
            if (it->first < p.nextInput) { p.inputs.erase(it); continue; }
            if (it->first != p.nextInput) {
                if (p.gapWait >= opt.inputGapTicks) { p.nextInput = it->first; p.gapWait = 0; continue; }
                ++p.gapWait;
                break;
            }
            uint32_t s = it->first;
            Bytes frame = std::move(it->second);
            p.inputs.erase(it);
            p.nextInput = s + 1;
            p.lastApplied = s;
            p.gapWait = 0;
            ++n;
            ++stats.inputs;
            if (self->onInput) self->onInput(p.slot, s, frame);
        }
    }

    // ---------------------------------------------------------------- interest
    static constexpr int32_t kCellLimit = (1 << 20) - 1;   // 21 bits per axis in the cell key
    static uint64_t cellKey(int32_t x, int32_t y, int32_t z) {
        auto u = [](int32_t v) { return uint64_t(uint32_t(v + kCellLimit + 1)) & 0x1FFFFF; };
        return (u(x) << 42) | (u(y) << 21) | u(z);
    }
    int32_t cellOf(float v) const {
        double c = std::floor(double(v) / cell);
        return int32_t(std::clamp(c, -double(kCellLimit), double(kCellLimit)));
    }
    int32_t cellOfY(float v) const { return opt.interest3d ? cellOf(v) : 0; }
    static int visOf(const Obj& o, uint8_t slot) {
        for (auto& [s, on] : o.vis) if (s == slot) return on ? 1 : 0;
        return -1;
    }

    void buildGrid() {
        // cells and their index persist (no per-broadcast allocation); a world that moved far away resets them
        if (cells.size() > 4 * objs.size() + 64) { cellIndex.clear(); cells.clear(); cells.shrink_to_fit(); cellPos.clear(); }
        for (auto& c : cells) c.ents.clear();
        usedCells = 0;
        always.clear(); forced.clear();
        for (auto& o : owned) o.clear();
        minCy = INT32_MAX; maxCy = INT32_MIN;
        for (ObjectId id = 1; id < objs.size(); ++id) {
            const Obj& o = objs[id];
            if (!o.alive) continue;
            const ClassDef& c = schema.cls(o.cls);
            if (c.alwaysRelevant || c.positionField < 0) always.push_back(id);
            else {
                const int32_t cx = cellOf(o.pos[0]), cy = cellOfY(o.pos[1]), cz = cellOf(o.pos[2]);
                minCy = std::min(minCy, cy); maxCy = std::max(maxCy, cy);
                auto [it, fresh] = cellIndex.try_emplace(cellKey(cx, cy, cz), uint32_t(cells.size()));
                if (fresh) { cells.emplace_back(); cellPos.push_back({cx, cy, cz}); }
                auto& ents = cells[it->second].ents;
                if (ents.empty()) ++usedCells;
                ents.push_back({id, o.pos[0], o.pos[1], o.pos[2], radiusOf(o.cls), !o.vis.empty()});
            }
            if (o.owner) owned[o.owner].push_back(id);
            if (!o.vis.empty()) forced.push_back(id);
        }
    }

    void markRelevant(Peer& p, ObjectId id, float d) {
        Track& t = p.tracks[id];
        if (t.stamp != p.stamp) { t.stamp = p.stamp; t.dist = d; rel.push_back(id); }
        else t.dist = std::min(t.dist, d);
    }

    void scanCell(Peer& p, const Cell& c, const std::array<float, 3>& v) {
        stats.interestChecks += c.ents.size();
        for (const GridEnt& e : c.ents) {
            float r = e.r;
            const Track& t = p.tracks[e.id];
            if (t.on && t.rel) r *= kHysteresis;
            const float dx = e.x - v[0], dy = e.y - v[1], dz = e.z - v[2];
            const float d2 = dx * dx + dy * dy + dz * dz;
            if (d2 > r * r) continue;
            if (e.vis && visOf(objs[e.id], p.slot) == 0) continue;
            markRelevant(p, e.id, std::sqrt(d2));
        }
    }

    void collectRelevant(Peer& p) {
        rel.clear();
        for (ObjectId id : always) if (visOf(objs[id], p.slot) != 0) markRelevant(p, id, 0);
        for (ObjectId id : owned[p.slot]) if (visOf(objs[id], p.slot) != 0) markRelevant(p, id, 0);
        for (ObjectId id : forced) if (visOf(objs[id], p.slot) == 1) markRelevant(p, id, 0);
        const float reach = maxRadius * kHysteresis;
        for (auto& v : p.viewers) {
            const int32_t x0 = cellOf(v[0] - reach), x1 = cellOf(v[0] + reach), z0 = cellOf(v[2] - reach), z1 = cellOf(v[2] + reach);
            const int32_t y0 = std::max(cellOfY(v[1] - reach), minCy), y1 = std::min(cellOfY(v[1] + reach), maxCy);
            if (y0 > y1 || usedCells == 0) continue;
            const uint64_t box = uint64_t(x1 - x0 + 1) * uint64_t(y1 - y0 + 1) * uint64_t(z1 - z0 + 1);
            if (box > 2 * usedCells) {
                // a reach of many cells over a sparse world: walk the occupied cells instead of the box
                for (size_t i = 0; i < cells.size(); ++i) {
                    if (cells[i].ents.empty()) continue;
                    const auto& c = cellPos[i];
                    if (c[0] >= x0 && c[0] <= x1 && c[1] >= y0 && c[1] <= y1 && c[2] >= z0 && c[2] <= z1) scanCell(p, cells[i], v);
                }
                continue;
            }
            for (int32_t x = x0; x <= x1; ++x)
                for (int32_t y = y0; y <= y1; ++y)
                    for (int32_t z = z0; z <= z1; ++z) {
                        auto it = cellIndex.find(cellKey(x, y, z));
                        if (it != cellIndex.end()) scanCell(p, cells[it->second], v);
                    }
        }
    }

    // The same test as the grid pass, for one object.
    bool relevantTo(const Peer& p, ObjectId id, float& dist) const {
        dist = 0;
        const Obj* o = obj(id);
        if (!o) return false;
        const int vis = o->vis.empty() ? -1 : visOf(*o, p.slot);
        if (vis >= 0) return vis == 1;
        const ClassDef& c = schema.cls(o->cls);
        if (c.alwaysRelevant || c.positionField < 0 || o->owner == p.slot) return true;
        float r = radiusOf(o->cls);
        const Track& t = p.tracks[id];
        if (t.on && t.rel) r *= kHysteresis;
        float best = -1;
        for (auto& v : p.viewers) {
            const float dx = o->pos[0] - v[0], dy = o->pos[1] - v[1], dz = o->pos[2] - v[2];
            const float d2 = dx * dx + dy * dy + dz * dz;
            if (best < 0 || d2 < best) best = d2;
        }
        if (best < 0 || best > r * r) return false;
        dist = std::sqrt(best);
        return true;
    }

    void addWork(Peer& p, ObjectId id) {
        Track& t = p.tracks[id];
        if (!t.inWork) { t.inWork = true; p.work.push_back(id); }
    }

    void startTrack(Peer& p, ObjectId id) {
        Track& t = p.tracks[id];
        Obj& o = objs[id];
        const ClassLayout& L = layout.classes[o.cls];
        const uint32_t stamp = t.stamp;
        const float d = t.dist;
        const bool inWork = t.inWork, inLeaving = t.inLeaving;
        t = Track{};
        t.stamp = stamp; t.dist = d; t.inWork = inWork; t.inLeaving = inLeaving;
        t.on = true;
        t.data.assign(size_t(L.slots + L.maskBits), 0);
        t.metaAt = uint32_t(L.slots);
        t.trackedAt = uint32_t(p.tracked.size());
        p.tracked.push_back(id);
        ++o.refs;
        for (int f : L.reliableFields) if (!(o.values[size_t(f)] == kEmpty)) sendField(p, id, o, f);
    }

    void untrack(Peer& p, ObjectId id) {
        Track& t = p.tracks[id];
        const uint32_t at = t.trackedAt;
        p.tracked[at] = p.tracked.back();
        p.tracks[p.tracked[at]].trackedAt = at;
        p.tracked.pop_back();
        t.on = false; t.rel = false; t.settled = false;
        t.data.clear();
        release(id);
    }

    void setRel(Peer& p, ObjectId id, bool relevant, float d) {
        Track& t = p.tracks[id];
        if (relevant) {
            t.dist = d;
            if (!t.on) startTrack(p, id);
            else if (!t.rel) { t.settled = false; if (t.rseq) t.cst = kPending; }   // removal cancelled
            t.rel = true;
            if (!(t.settled && vers[id] <= t.syncVer)) addWork(p, id);
        } else if (t.on && t.rel) {
            t.rel = false; t.settled = false; t.rseq = 0; t.rst = kPending;
            if (!t.inLeaving) { t.inLeaving = true; p.leaving.push_back(id); }
        }
    }

    bool viewersMoved(const Peer& p) const {
        if (p.viewers.size() != p.fullViewers.size()) return true;
        for (size_t i = 0; i < p.viewers.size(); ++i) {
            const float dx = p.viewers[i][0] - p.fullViewers[i][0], dy = p.viewers[i][1] - p.fullViewers[i][1],
                        dz = p.viewers[i][2] - p.fullViewers[i][2];
            if (dx * dx + dy * dy + dz * dz > slack * slack) return true;
        }
        return false;
    }

    void updateRelevance(Peer& p) {
        ++p.stamp;
        if (p.tracks.size() < objs.size()) p.tracks.resize(objs.size());
        const uint64_t pending = logEnd() - std::min(p.logPos, logEnd());
        if (p.full || p.logPos < logStart || pending > p.tracked.size() + 64 || viewersMoved(p)) {
            if (!gridBuilt) { buildGrid(); gridBuilt = true; }
            collectRelevant(p);
            for (ObjectId id : p.tracked) if (p.tracks[id].stamp != p.stamp) setRel(p, id, false, 0);
            for (ObjectId id : rel) setRel(p, id, true, p.tracks[id].dist);
            p.fullViewers = p.viewers;
            p.full = false;
        } else {
            for (uint64_t i = p.logPos; i < logEnd(); ++i) {
                const ObjectId id = changeLog[size_t(i - logStart)];
                Track& t = p.tracks[id];
                if (t.stamp == p.stamp) continue;
                t.stamp = p.stamp;
                float d = 0;
                const bool r = relevantTo(p, id, d);
                setRel(p, id, r, d);
                ++stats.interestChecks;
            }
        }
        p.logPos = logEnd();
    }

    // ---------------------------------------------------------------- snapshot
    void writeSlots(BitWriter& bw, const ClassLayout& L, const std::vector<uint64_t>& q, int k) {
        if (k >= int(L.snapFields.size())) { bw.put(q[size_t(L.ownerSlot)], 8); return; }
        const FieldLayout& f = L.fields[size_t(L.snapFields[size_t(k)])];
        for (int a = 0; a < f.comps; ++a) bw.put(q[size_t(f.slot + a)], f.bits);
    }
    static bool slotsDiffer(const ClassLayout& L, const uint64_t* a, const uint64_t* b, int k) {
        if (k >= int(L.snapFields.size())) return a[size_t(L.ownerSlot)] != b[size_t(L.ownerSlot)];
        const FieldLayout& f = L.fields[size_t(L.snapFields[size_t(k)])];
        for (int a2 = 0; a2 < f.comps; ++a2) if (a[size_t(f.slot + a2)] != b[size_t(f.slot + a2)]) return true;
        return false;
    }

    // Bits of masked field k when written as a difference to `base` (the confirmed value); 0 when it does not pay.
    static int deltaWidth(const FieldLayout& f, const uint64_t* q, const uint64_t* base) {
        int w = 0;
        for (int a = 0; a < f.comps; ++a) w = std::max(w, BitWriter::width(zigzag(int64_t(q[a]) - int64_t(base[a]))));
        return w;
    }
    // Bits of a field written as a difference of width w; 0 when the absolute value is not larger.
    static int deltaBits(const FieldLayout& f, int w) {
        if (w >= 32) return 0;
        const int bits = kDeltaWidthBits + w * f.comps;
        return bits < f.bits * f.comps ? bits : 0;
    }
    static void writeDelta(BitWriter& bw, const FieldLayout& f, const uint64_t* q, const uint64_t* base, int w) {
        bw.put(uint64_t(w), kDeltaWidthBits);
        for (int a = 0; a < f.comps; ++a) bw.put(zigzag(int64_t(q[a]) - int64_t(base[a])), w);
    }

    // What object `id` must send to this client now: a create, or a mask of fields (dmask: delta-coded ones).
    // False when nothing: the client's copy equals the current state and nothing needs a repair.
    bool evaluate(Peer& p, ObjectId id, Pick& pk, uint32_t& bits) {
        Track& t = p.tracks[id];
        const Obj& o = objs[id];
        const ClassLayout& L = layout.classes[o.cls];
        ++stats.evaluated;
        uint8_t cst = t.cst;
        if (cst == kInFlight) { cst = resolveCreate(p, t); t.cst = cst; }
        const bool mine = o.owner == p.slot;
        uint64_t mask = 0, dmask = 0, widths = 0;
        bits = 0;
        bool create = cst == kPending || cst == kLost;
        bool settled = cst == kConfirmed;
        if (!create) {
            const int nf = int(L.snapFields.size());
            const uint64_t* fv = o.fver(L);
            for (int k = 0; k < L.maskBits; ++k) {
                const bool changed = fv[k] > t.syncVer && slotsDiffer(L, o.q.data(), t.sent(), k);
                if (cst == kInFlight) { if (changed) { create = true; break; } continue; }
                const uint8_t fs = fieldState(p, t, k);
                if (fs != kConfirmed) settled = false;
                // The owner reconciles its prediction against this snapshot's lastApplied input: every field it
                // has not confirmed yet goes again, or a lost update would pair a stale value with a newer input.
                if (!(changed || fs == kLost || fs == kPending || (mine && fs == kInFlight))) continue;
                mask |= uint64_t(1) << k;
                if (k >= nf) { bits += 8; continue; }
                const FieldLayout& f = L.fields[size_t(L.snapFields[size_t(k)])];
                if (!f.delta) { bits += uint32_t(f.bits * f.comps); continue; }
                // the client holds exactly `sent` when its last send is confirmed (no newer send exists)
                const int w = fs == kConfirmed ? deltaWidth(f, &o.q[size_t(f.slot)], t.sent() + f.slot) : 32;
                const int db = deltaBits(f, w);
                if (db) { dmask |= uint64_t(1) << k; if (k < 12) widths |= uint64_t(w) << (5 * k); }
                bits += 1 + uint32_t(db ? db : f.bits * f.comps);
            }
        }
        if (create) bits = uint32_t(layout.classBits + kGenBits + L.fullBits);
        else if (!mask) { t.syncVer = epoch; t.settled = settled; return false; }   // sent == current state
        else bits += uint32_t(L.maskBits);
        bits += 1 + uint32_t(BitWriter::varBits(id));
        pk = {id, mask, create ? 0 : dmask, widths, create};
        return true;
    }

    void snapshot(Peer& p, uint64_t now) {
        const double perSnap = double(opt.bytesPerSecond) / opt.snapshotRate;
        p.tokens = std::min(p.tokens + double(opt.bytesPerSecond) * double(now - std::min(now, p.lastTokenAt)) / 1000.0,
                            2 * perSnap);
        p.lastTokenAt = now;
        updateRelevance(p);

        // removals of tracked objects that are no longer relevant (repeated until confirmed, then Forget)
        removals.clear();
        for (size_t i = 0; i < p.leaving.size();) {
            const ObjectId id = p.leaving[i];
            Track& t = p.tracks[id];
            auto unlist = [&] { t.inLeaving = false; p.leaving[i] = p.leaving.back(); p.leaving.pop_back(); };
            if (!t.on || t.rel) { unlist(); continue; }
            uint8_t st = t.rseq ? t.rst : uint8_t(kPending);
            if (st == kInFlight) { st = resolve(p, t.rseq); t.rst = st; }
            if (st == kConfirmed) {
                ByteWriter w; w.u8(uint8_t(Msg::Forget)); w.u32(id); w.u8(objs[id].gen);
                sendReliable(p, w.buf);
                unlist();
                untrack(p, id);
                continue;
            }
            if ((st == kPending || st == kLost) && removals.size() < kMaxRemovalsPerSnapshot) removals.push_back(id);
            ++i;
        }

        // candidates: rank first, look at fields only for what the budget reaches (see evaluate)
        cands.clear();
        for (size_t i = 0; i < p.work.size();) {
            const ObjectId id = p.work[i];
            Track& t = p.tracks[id];
            const bool clean = t.settled && vers[id] <= t.syncVer;   // confirmed and unchanged
            if (!t.on || !t.rel || clean) {
                t.inWork = false; p.work[i] = p.work.back(); p.work.pop_back();
                continue;
            }
            ++i;
            const Obj& o = objs[id];
            if (t.cst == kInFlight) t.cst = resolveCreate(p, t);
            // changed or not created: surely something to send; else only lost fields might need a repair
            if (!(vers[id] > t.syncVer || t.cst == kPending || t.cst == kLost)) {
                Pick pk;
                uint32_t bits = 0;
                if (!evaluate(p, id, pk, bits)) continue;
            }
            t.acc += schema.cls(o.cls).priority * (1.0f / (1.0f + t.dist / radiusOf(o.cls))) + 1e-6f;
            cands.push_back({o.owner == p.slot ? 1e30f : t.acc, id});
        }
        stats.relevant += p.tracked.size() - p.leaving.size();

        // budget: best first (accumulated priority, then id). Owned objects go first and may use the whole
        // maxSnapshotBytes. A few candidates (the budget likely fits them all) are evaluated in work order and
        // ranked only if they do not fit; many (budget-bound) go through a heap that evaluates only what it
        // pops, stopping when even the smallest entry seen cannot fit or after kMaxMisses misses in a row.
        const uint64_t hardBits = uint64_t(opt.maxSnapshotBytes) * 8;
        const uint64_t budgetBits = std::min<uint64_t>(uint64_t(std::max(0.0, p.tokens) * 8), hardBits);
        uint64_t used = 8 + 96 + 12;
        { uint32_t prev = 0; for (ObjectId id : removals) { used += uint64_t(BitWriter::varBits(prev ? id - prev - 1 : id)); prev = id; } }
        taken.clear();
        auto worse = [](const Cand& a, const Cand& b) { return a.key != b.key ? a.key < b.key : a.id > b.id; };
        const auto fits = [&](const Cand& c, uint32_t bits) { return used + bits <= (c.key >= 1e29f ? hardBits : budgetBits); };
        if (cands.size() <= 2 * p.lastTaken + 64) {
            uint64_t sum = 0;
            evals.clear();
            for (const Cand& c : cands) {
                Eval e{c, {}, 0};
                if (!evaluate(p, c.id, e.pick, e.bits)) continue;
                sum += e.bits;
                evals.push_back(e);
            }
            if (used + sum <= budgetBits) {
                for (auto& e : evals) taken.push_back(e.pick);
                used += sum;
            } else {
                std::sort(evals.begin(), evals.end(), [&](const Eval& a, const Eval& b) { return worse(b.c, a.c); });
                for (auto& e : evals) {
                    if (fits(e.c, e.bits)) { taken.push_back(e.pick); used += e.bits; }
                    else ++stats.starved;
                }
            }
        } else {
            std::make_heap(cands.begin(), cands.end(), worse);
            auto end = cands.end();
            uint32_t minBits = 0;                        // smallest entry evaluated so far (0: none yet)
            int misses = 0;
            while (end != cands.begin()) {
                const bool mine = cands.front().key >= 1e29f;
                if (!mine && (used + minBits > budgetBits || misses >= kMaxMisses)) break;
                std::pop_heap(cands.begin(), end, worse);
                --end;
                Pick pk;
                uint32_t bits = 0;
                if (!evaluate(p, end->id, pk, bits)) continue;   // changed back to what the client has
                minBits = minBits ? std::min(minBits, bits) : bits;
                if (fits(*end, bits)) { taken.push_back(pk); used += bits; misses = 0; }
                else { ++stats.starved; ++misses; }
            }
            stats.starved += uint64_t(end - cands.begin());
        }
        p.lastTaken = taken.size();
        std::sort(taken.begin(), taken.end(), [](const Pick& a, const Pick& b) { return a.id < b.id; });
        std::sort(removals.begin(), removals.end());

        // write
        const uint32_t seq = ++p.seq;
        BitWriter bw;
        bw.put(seq, 32); bw.put(tick, 32); bw.put(p.lastApplied, 32);
        bw.var(uint32_t(removals.size()));
        uint32_t prev = 0;
        for (ObjectId id : removals) { bw.var(prev ? id - prev - 1 : id); prev = id; }
        bw.var(uint32_t(taken.size()));
        prev = 0;
        for (auto& c : taken) {
            Obj& o = objs[c.id];
            const ClassLayout& L = layout.classes[o.cls];
            Track& t = p.tracks[c.id];
            bw.var(prev ? c.id - prev - 1 : c.id);
            prev = c.id;
            bw.flag(c.create);
            if (c.create) {
                bw.put(o.cls, layout.classBits);
                bw.put(o.gen, kGenBits);
                for (int k = 0; k < L.maskBits; ++k) { writeSlots(bw, L, o.q, k); t.meta()[k] = meta(seq, kInFlight); }
                std::copy(o.q.begin(), o.q.begin() + L.slots, t.sent());
                ++stats.creates;
                t.cmask = t.cst == kInFlight && seq - t.cseq < 32 ? (t.cmask << (seq - t.cseq)) | 1u : 1u;
                t.cst = kInFlight; t.cseq = seq;
            } else {
                bw.put(c.mask, L.maskBits);
                const int nf = int(L.snapFields.size());
                for (int k = 0; k < L.maskBits; ++k) {
                    if (!(c.mask & (uint64_t(1) << k))) continue;
                    t.meta()[k] = meta(seq, kInFlight);
                    if (k >= nf) { writeSlots(bw, L, o.q, k); t.sent()[L.ownerSlot] = o.q[size_t(L.ownerSlot)]; continue; }
                    const FieldLayout& f = L.fields[size_t(L.snapFields[size_t(k)])];
                    uint64_t* sent = t.sent() + f.slot;
                    const uint64_t* q = &o.q[size_t(f.slot)];
                    if (f.delta) {
                        const bool d = (c.dmask >> k) & 1;
                        bw.flag(d);
                        if (d) {
                            writeDelta(bw, f, q, sent, k < 12 ? int((c.widths >> (5 * k)) & 31) : deltaWidth(f, q, sent));
                            ++stats.deltaFields;
                        }
                        else writeSlots(bw, L, o.q, k);
                    } else {
                        writeSlots(bw, L, o.q, k);
                    }
                    for (int a = 0; a < f.comps; ++a) sent[a] = q[a];
                }
            }
            t.acc = 0;
            t.syncVer = epoch;
            t.settled = false;
        }
        stats.entries += taken.size();
        for (ObjectId id : removals) { Track& t = p.tracks[id]; t.rseq = seq; t.rst = kInFlight; }

        Bytes out;
        out.reserve(bw.bits() / 8 + 2);
        out.push_back(uint8_t(Msg::Snapshot));
        bw.finish(out);
        p.ring[seq & p.ringMask] = Rec{seq, now, false};
        p.tokens -= double(out.size());
        p.conn->sendLatest(out);
        ++stats.snapshots;
        stats.snapshotBytes += out.size();
    }

    // Drops the change-log prefix every client has read; a client far behind falls back to a full pass.
    void trimLog() {
        uint64_t minPos = logEnd();
        for (auto& pp : peers) if (pp && pp->welcomed) minPos = std::min(minPos, pp->logPos);
        if (changeLog.size() > 8 * objs.size() + 4096) {
            for (auto& pp : peers) if (pp) { pp->full = true; pp->logPos = logEnd(); }
            minPos = logEnd();
        }
        if (minPos > logStart && (minPos - logStart) * 2 >= changeLog.size()) {
            changeLog.erase(changeLog.begin(), changeLog.begin() + long(minPos - logStart));
            logStart = minPos;
        }
    }

    void flushReliable() {
        for (ObjectId id : relDirty) {
            Obj* o = obj(id);
            if (!o || !o->relDirty) continue;
            for (auto& pp : peers) {
                if (!pp || !pp->welcomed || pp->closing || id >= pp->tracks.size() || !pp->tracks[id].on) continue;
                for (int f = 0; f < 64; ++f) if (o->relDirty & (uint64_t(1) << f)) sendField(*pp, id, *o, f);
            }
            o->relDirty = 0;
        }
        relDirty.clear();
    }
};

// ---------------------------------------------------------------- public API
Server::Server(const Schema& schema, ServerOptions options) : impl_(std::make_unique<Impl>(schema, options)) { impl_->self = this; }
Server::~Server() { for (auto& p : impl_->peers) if (p) p->conn->close(); }

ObjectId Server::create(ClassId cls) {
    auto& I = *impl_;
    if (cls >= I.schema.size()) return kNoObject;
    ObjectId id;
    if (I.freeHead < I.freeIds.size()) {
        id = I.freeIds[I.freeHead++];
        if (I.freeHead > 1024 && I.freeHead * 2 > I.freeIds.size()) {
            I.freeIds.erase(I.freeIds.begin(), I.freeIds.begin() + long(I.freeHead));
            I.freeHead = 0;
        }
    } else {
        if (I.objs.size() >= kMaxObjectId) return kNoObject;
        id = ObjectId(I.objs.size());
        I.objs.emplace_back();
        I.vers.push_back(0);
        I.logged.push_back(0);
    }
    auto& o = I.objs[id];
    const auto& L = I.layout.classes[cls];
    const uint8_t gen = uint8_t(o.gen + 1);
    o = Impl::Obj{};
    o.alive = true;
    o.cls = cls;
    o.gen = gen;
    I.vers[id] = I.epoch;
    o.values.assign(L.fields.size(), Value{});
    o.q.assign(size_t(L.slots + L.maskBits), I.epoch);
    std::fill(o.q.begin(), o.q.begin() + L.slots, 0);
    for (int f : L.snapFields) quantize(L.fields[size_t(f)], o.values[size_t(f)], &o.q[size_t(L.fields[size_t(f)].slot)]);
    int pf = I.schema.cls(cls).positionField;
    if (pf >= 0) { Value n = normalize(L.fields[size_t(pf)], Value{}); o.pos = {n.f[0], n.f[1], n.f[2]}; }
    I.logChange(id);
    return id;
}

void Server::destroy(ObjectId id) {
    auto& I = *impl_;
    auto* o = I.obj(id);
    if (!o) return;
    const uint32_t refs = o->refs;
    const uint8_t gen = o->gen;
    *o = Impl::Obj{};
    o->refs = refs;
    o->gen = gen;
    I.logChange(id);
    if (refs == 0) I.freeIds.push_back(id);
}

bool Server::exists(ObjectId id) const { return impl_->obj(id) != nullptr; }

// Exactly the legacy Server::set semantics, with the object and class
// layout already resolved by the caller. This is also used by batched
// Place body/character updates: one ID lookup instead of 4-5 per tick.
static void setResolvedField(Server::Impl& I, Server::Impl::Obj& o,
                             const ClassLayout& L, ObjectId id,
                             int field, const Value& v) {
    if (field < 0 || size_t(field) >= L.fields.size()) return;
    const FieldLayout& f = L.fields[size_t(field)];
    Value& cur = o.values[size_t(field)];
    if (f.snapshot) {
        uint64_t q[4] = {0, 0, 0, 0};
        quantize(f, v, q);
        uint64_t* cq = &o.q[size_t(f.slot)];
        bool changed = false;
        for (int a = 0; a < f.comps; ++a)
            if (cq[a] != q[a]) { cq[a] = q[a]; changed = true; }
        if (changed) {
            I.vers[id] = I.epoch;
            o.fver(L)[f.snap] = I.epoch;
            I.logChange(id);
        }
        if (changed && field == I.schema.cls(o.cls).positionField) {
            Value n;
            dequantize(f, &o.q[size_t(f.slot)], n);
            o.pos = {n.f[0], n.f[1], n.f[2]};
        }
        cur = v;
        return;
    }
    Value n = normalize(f, v);
    if (normalize(f, cur) == n) { cur = v; return; }
    cur = (f.kind == FieldKind::String || f.kind == FieldKind::Bytes) ? n : v;
    if (!o.relDirty) I.relDirty.push_back(id);
    o.relDirty |= uint64_t(1) << field;
}

void Server::set(ObjectId id, int field, const Value& v) {
    auto& I = *impl_;
    auto* o = I.obj(id);
    if (!o) return;
    setResolvedField(I, *o, I.layout.classes[o->cls], id, field, v);
}

void Server::setFields(ObjectId id, std::span<const FieldUpdate> updates) {
    if (updates.empty()) return;
    auto& I = *impl_;
    auto* o = I.obj(id);
    if (!o) return;
    const ClassLayout& layout = I.layout.classes[o->cls];
    for (const FieldUpdate& update : updates)
        setResolvedField(I, *o, layout, id, update.field, update.value);
}

const Value& Server::get(ObjectId id, int field) const {
    auto* o = impl_->obj(id);
    if (!o || field < 0 || size_t(field) >= o->values.size()) return kEmpty;
    return o->values[size_t(field)];
}

void Server::setOwner(ObjectId id, uint8_t slot) {
    auto* o = impl_->obj(id);
    if (!o || o->owner == slot) return;
    const auto& L = impl_->layout.classes[o->cls];
    o->owner = slot;
    o->q[size_t(L.ownerSlot)] = slot;
    impl_->vers[id] = impl_->epoch;
    o->fver(L)[L.maskBits - 1] = impl_->epoch;
    impl_->logChange(id);
}

uint8_t Server::ownerOf(ObjectId id) const { auto* o = impl_->obj(id); return o ? o->owner : 0; }

void Server::setVisibleTo(ObjectId id, uint8_t slot, bool on) {
    auto* o = impl_->obj(id);
    if (!o || slot == 0) return;
    impl_->logChange(id);
    for (auto& [s, v] : o->vis) if (s == slot) { v = on; return; }
    o->vis.push_back({slot, on});
}

void Server::resetVisibility(ObjectId id, uint8_t slot) {
    auto* o = impl_->obj(id);
    if (!o) return;
    impl_->logChange(id);
    if (slot == 0) { o->vis.clear(); return; }
    o->vis.erase(std::remove_if(o->vis.begin(), o->vis.end(), [&](auto& e) { return e.first == slot; }), o->vis.end());
}

uint8_t Server::attach(std::unique_ptr<Conn> connection) {
    auto& I = *impl_;
    if (!connection) return 0;
    for (int s = 1; s < 256; ++s) {
        if (I.peers[size_t(s)]) continue;
        auto p = std::make_unique<Impl::Peer>();
        p->conn = std::move(connection);
        p->slot = uint8_t(s);
        size_t ring = 1;
        while (ring < size_t(I.opt.history)) ring <<= 1;
        p->ring.resize(ring);
        p->ringMask = uint32_t(ring - 1);
        I.peers[size_t(s)] = std::move(p);
        return uint8_t(s);
    }
    ByteWriter w; w.u8(uint8_t(Msg::Reject)); w.u8(uint8_t(RejectReason::Full));
    connection->send(w.buf);
    connection->update(nowMs());
    return 0;
}

void Server::kick(uint8_t slot) {
    auto& I = *impl_;
    auto& p = I.peers[slot];
    if (!p || p->closing) return;
    I.sendReject(*p, RejectReason::Kicked);
    if (!I.inPoll) { p->conn->update(p->lastPoll); I.drop(slot); }
}

std::vector<uint8_t> Server::clients() const {
    std::vector<uint8_t> out;
    for (auto& p : impl_->peers) if (p && p->welcomed && !p->closing) out.push_back(p->slot);
    return out;
}

void Server::setViewers(uint8_t slot, const std::vector<std::array<float, 3>>& points) {
    auto& p = impl_->peers[slot];
    if (!p) return;
    p->viewers.clear();
    for (auto& v : points)
        if (std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]) && p->viewers.size() < 16) p->viewers.push_back(v);
}

void Server::setViewer(uint8_t slot, std::array<float, 3> point) {
    auto& p = impl_->peers[slot];
    if (!p) return;
    p->viewers.clear();
    if (std::isfinite(point[0]) && std::isfinite(point[1]) &&
        std::isfinite(point[2]))
        p->viewers.push_back(point);
}

uint32_t Server::rttMs(uint8_t slot) const {
    auto& p = impl_->peers[slot];
    if (!p) return 0;
    uint32_t r = p->conn->rttMs();
    return r ? r : p->srtt;
}

void Server::send(uint8_t slot, uint16_t channel, const Bytes& payload) {
    auto& I = *impl_;
    ByteWriter w;
    w.u8(uint8_t(Msg::Remote)); w.u16(channel); writeBytes(w, payload);
    for (auto& p : I.peers)
        if (p && p->welcomed && !p->closing && (slot == 0 || p->slot == slot)) I.sendReliable(*p, w.buf);
}

void Server::poll(uint64_t now) {
    auto& I = *impl_;
    I.inPoll = true;
    for (int s = 1; s < 256; ++s) {
        auto* p = I.peers[size_t(s)].get();
        if (!p) continue;
        if (p->closing) { p->conn->update(now); I.drop(uint8_t(s)); continue; }
        p->conn->update(now);
        if (!p->attachedAt) p->attachedAt = now ? now : 1;
        double dt = double(now - std::min(now, p->lastPoll ? p->lastPoll : now)) / 1000.0;
        p->lastPoll = now;
        p->msgTokens = std::min(p->msgTokens + dt * I.opt.maxMessagesPerSecond, 2.0 * I.opt.maxMessagesPerSecond);
        if (p->conn->state() == ConnState::Closed) { I.drop(uint8_t(s)); continue; }
        Bytes m;
        for (int n = 0; n < kMaxMessagesPerPoll && !p->closing && p->conn->recv(m); ++n) {
            if (m.empty()) { ++I.stats.rejected; continue; }
            switch (Msg(m[0])) {
            case Msg::Hello: I.handleHello(*p, m, now); break;
            case Msg::Input: if (p->welcomed) I.handleInput(*p, m, now); break;
            case Msg::Message: if (p->welcomed) I.handleMessage(*p, m); break;
            default: ++I.stats.rejected; break;
            }
            if (!I.peers[size_t(s)]) break;   // dropped from a callback
        }
        if (!I.peers[size_t(s)] || p->closing) continue;
        if (!p->welcomed && now - p->attachedAt > I.opt.helloTimeoutMs) { I.sendReject(*p, RejectReason::Timeout); continue; }
        if (p->welcomed) I.deliverInputs(*p);
    }
    I.inPoll = false;
}

void Server::tick() { ++impl_->tick; }

void Server::broadcast(uint64_t now) {
    auto& I = *impl_;
    I.flushReliable();
    bool grid = false;
    const double interval = 1000.0 / I.opt.snapshotRate;
    for (auto& pp : I.peers) {
        if (!pp || !pp->welcomed || pp->closing || double(now) < pp->nextSnap) {
            if (pp && !pp->closing) pp->conn->update(now);   // flush reliable traffic
            continue;
        }
        if (!grid) { I.buildGrid(); grid = true; }
        I.snapshot(*pp, now);
        pp->nextSnap += interval;
        if (pp->nextSnap <= double(now)) pp->nextSnap = double(now) + interval;
        pp->conn->update(now);
    }
    I.gridBuilt = false;
    I.trimLog();
    ++I.epoch;
}

uint32_t Server::serverTick() const { return impl_->tick; }
const ServerStats& Server::stats() const { return impl_->stats; }

} // namespace aiverse::net::replica
