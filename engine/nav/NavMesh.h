// Genre-independent navigation mesh (docs/navmesh.md).
//
// A module describes walkability as a raster of columns on the XZ plane; every column holds zero or more walkable
// spans (surfaces at a height) and the module answers whether an agent can cross between spans of orthogonal
// neighbour columns (nav::Source). The mesh merges linked spans into rectangles (never across tile borders, so a
// change rebuilds only its tiles), links rectangles through portals (with wall corners marked), searches A* over
// rectangles and pulls the corridor into an any-angle path (funnel). RTS feeds it map cells, FPS feeds it spans
// from AABB collision; nothing here knows a genre.
//
// Deterministic: Fixed / integer math only, fixed iteration order, ties broken by ids. Pure function of the source:
// an incrementally refreshed mesh equals a mesh built from scratch (rectangles and portals are tile-local).
#pragma once
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
#include "engine/core/Fixed.h"
#include "engine/core/Hash.h"

namespace aiverse::nav {

struct Span {
    Fixed y;               // surface height
    uint16_t area = 0;     // area class: rectangles merge only equal areas; Query::areaCost scales travel cost
};

class Source {
public:
    virtual ~Source() = default;
    // Walkable spans of column (x, z), ascending height. Append to `out` (it arrives empty).
    virtual void spans(int32_t x, int32_t z, std::vector<Span>& out) const = 0;
    // Can an agent cross between span i of column (x0, z0) and span j of the orthogonal neighbour (x1, z1)?
    // Must be symmetric and depend only on the source's own state.
    virtual bool link(int32_t x0, int32_t z0, uint32_t i, int32_t x1, int32_t z1, uint32_t j) const = 0;
};

struct Config {
    int32_t width = 0, depth = 0;      // columns along x and z
    Fixed originX, originZ;            // world position of the corner of column (0, 0)
    Fixed cell = Fixed::fromInt(1);    // column size in world units
    int32_t tile = 32;                 // rectangles never cross tile borders (local rebuilds)
    bool flat = true;                  // merge only spans of equal height (false: any linked spans)
    uint32_t maxSpans = 1u << 24;      // build fails beyond this (explicit, never truncated)
    // Tile classification (0: off): a tile whose merge gives more than maxRectRatio rectangles per span is "coarse" -
    // a maze where rectangles shrink to cells and buy nothing. It keeps no rectangles; callers route through it with
    // their own solver (RTS hybrid pathing: grid A*). Pure function of the tile, so classes refresh with the tile.
    Fixed maxRectRatio;
};

struct TileInfo { int32_t x0 = 0, z0 = 0, w = 0, d = 0; uint32_t spans = 0, merged = 0; bool coarse = false; };

struct Rect {
    int32_t x0 = 0, z0 = 0, x1 = 0, z1 = 0;   // inclusive column bounds
    Fixed y;                                  // height of its first span
    uint16_t area = 0;
};

struct Portal {
    uint32_t tile = 0, rect = 0;   // the rectangle on the other side (tile, index in tile)
    uint8_t side = 0;              // 0: -x, 1: +x, 2: -z, 3: +z side of the owning rectangle
    uint8_t solid = 0;             // bit 0: wall corner at lo, bit 1: wall corner at hi
    int8_t pushLo = 0, pushHi = 0; // wall beyond that end on one side only: keep off it across the portal too
                                   // (+1: toward the far side, -1: toward the near side)
    Fixed fixed, lo, hi;           // the portal line: x (sides 0/1) or z (sides 2/3) = fixed, the other axis lo..hi
    Fixed y;                       // surface height on the far side
};

struct Query {
    Fixed margin = Fixed::fromRatio(1, 2);   // distance kept from wall corners (portals shrink, at most to their middle)
    Fixed yTolerance = Fixed::fromInt(1);    // locating points among stacked spans
    size_t maxVisits = 8192;                 // rectangles expanded by A* before giving up
    const std::vector<Fixed>* areaCost = nullptr;   // per area id cost multiplier (missing: 1)
    // Corridor search. false (default): A* over rectangles, entering each at the point where the straight line to the
    // goal meets the portal - fast (open areas: ~3x the grid), but now and then a longer corridor (a few % on open
    // maps). true: A* over portal points (both shrunk ends + middle) - the best corridor of those points, paths a few
    // % shorter than string-pulled grid paths at about grid speed in open areas; slow in mazes (many tiny portals).
    bool bestCorridor = false;
};

struct RefreshTelemetry {
    uint64_t revision=0,elapsedNs=0;
    size_t rebuiltTiles=0,portalTiles=0,rectangles=0;
    std::vector<uint32_t> tileIds; // bounded sample, not stable polygon identities
};
class Mesh {
public:
    static constexpr uint32_t kNone = 0xFFFFFFFFu;

    bool build(const Source& src, const Config& cfg, std::string* err = nullptr) {
        if (cfg.width <= 0 || cfg.depth <= 0 || cfg.tile <= 0 || cfg.tile > 1024 || cfg.cell.raw <= 0) {
            if (err) *err = "navmesh: invalid size, tile or cell";
            return false;
        }
        lastRefresh_.reset();
        cfg_ = cfg;
        tw_ = (cfg.width + cfg.tile - 1) / cfg.tile;
        td_ = (cfg.depth + cfg.tile - 1) / cfg.tile;
        tiles_.assign(size_t(tw_) * size_t(td_), Tile{});
        for (int32_t tz = 0; tz < td_; ++tz)
            for (int32_t tx = 0; tx < tw_; ++tx) {
                Tile& t = tiles_[size_t(tz * tw_ + tx)];
                t.x0 = tx * cfg.tile; t.z0 = tz * cfg.tile;
                t.w = std::min(cfg.tile, cfg.width - t.x0); t.d = std::min(cfg.tile, cfg.depth - t.z0);
            }
        built_ = true;
        markAllDirty();
        return refresh(src, err);
    }
    bool built() const { return built_; }
    const Config& config() const { return cfg_; }

    // Columns whose spans or links may have changed (inclusive, clamped). Applied by the next refresh().
    void markDirty(int32_t x0, int32_t z0, int32_t x1, int32_t z1) {
        if (!built_) return;
        x0 = std::max(0, x0); z0 = std::max(0, z0); x1 = std::min(cfg_.width - 1, x1); z1 = std::min(cfg_.depth - 1, z1);
        if (x0 > x1 || z0 > z1) return;
        for (int32_t tz = z0 / cfg_.tile; tz <= z1 / cfg_.tile; ++tz)
            for (int32_t tx = x0 / cfg_.tile; tx <= x1 / cfg_.tile; ++tx) tiles_[size_t(tz * tw_ + tx)].dirty = true;
        anyDirty_ = true;
    }
    void markAllDirty() { for (auto& t : tiles_) t.dirty = true; anyDirty_ = !tiles_.empty(); }
    bool dirty() const { return anyDirty_; }
    void setRefreshTelemetryEnabled(bool enabled){telemetryEnabled_=enabled;if(!enabled)lastRefresh_.reset();}
    const std::optional<RefreshTelemetry>& lastRefreshTelemetry()const noexcept{return lastRefresh_;}

    // Rebuilds dirty tiles (spans, rectangles) and the portals of every tile that can see them.
    bool refresh(const Source& src, std::string* err = nullptr) {
        if (!anyDirty_) return true;
        const auto started=telemetryEnabled_?std::chrono::steady_clock::now():std::chrono::steady_clock::time_point{};
        RefreshTelemetry measured;
        std::vector<uint8_t> edges(tiles_.size(), 0);
        std::vector<Span> tmp;
        for (size_t i = 0; i < tiles_.size(); ++i) {
            if (!tiles_[i].dirty) continue;
            rebuildTile(src, tiles_[i], tmp);
            if(telemetryEnabled_){++measured.rebuiltTiles;if(measured.tileIds.size()<128)measured.tileIds.push_back(uint32_t(i));}
            const int32_t tx = int32_t(i) % tw_, tz = int32_t(i) / tw_;
            // a tile's portals look one column into its 8 neighbours (corner checks): refresh all of them
            for (int32_t dz = -1; dz <= 1; ++dz)
                for (int32_t dx = -1; dx <= 1; ++dx)
                    if (tx + dx >= 0 && tz + dz >= 0 && tx + dx < tw_ && tz + dz < td_) edges[size_t((tz + dz) * tw_ + tx + dx)] = 1;
        }
        size_t spans = 0;
        for (auto& t : tiles_) spans += t.spans.size();
        if (spans > cfg_.maxSpans) {
            if (err) *err = "navmesh: span budget exceeded; use a smaller region or larger cells";
            for (auto& t : tiles_) t.dirty = true;
            return false;
        }
        for (size_t i = 0; i < tiles_.size(); ++i) {
            tiles_[i].dirty = false;
            if (edges[i]) {rebuildPortals(src, uint32_t(i));if(telemetryEnabled_)++measured.portalTiles;}
        }
        base_.assign(tiles_.size() + 1, 0);
        for (size_t i = 0; i < tiles_.size(); ++i) base_[i + 1] = base_[i] + uint32_t(tiles_[i].rects.size());
        // portal slots (dense, rectangle order) for the portal-point search
        slotBase_.assign(rectCount() + 1, 0);
        slotOwner_.clear(); slotIndex_.clear(); slotPortal_.clear(); slotTarget_.clear(); rectArea_.clear();
        for (uint32_t id = 0; id < rectCount(); ++id) {
            const auto& ps = portals(id);
            rectArea_.push_back(rect(id).area);
            slotBase_[id + 1] = slotBase_[id] + uint32_t(ps.size());
            for (uint32_t i = 0; i < ps.size(); ++i) {
                slotOwner_.push_back(id); slotIndex_.push_back(i); slotPortal_.push_back(&ps[i]); slotTarget_.push_back(idOf(ps[i].tile, ps[i].rect));
            }
        }
        anyDirty_ = false;
        ++revision_;
        if(telemetryEnabled_){
            measured.revision=revision_;measured.rectangles=rectCount();
            measured.elapsedNs=uint64_t(std::max<int64_t>(0,std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-started).count()));
            lastRefresh_=std::move(measured);
        }
        return true;
    }
    uint64_t revision() const { return revision_; }
    // Only after rebuilding the exact source at a deterministic snapshot
    // barrier; retain the saved revision used by persisted path cursors.
    bool restoreRebuiltRevision(uint64_t revision){
        if(!built_||anyDirty_||!revision)return false;
        revision_=revision;lastRefresh_.reset();return true;
    }
    size_t lastVisits() const { return lastVisits_; }   // rectangles the last rectangle search expanded (diagnostics)

    // Tiles (classification, statistics). merged: rectangles the merge produced (also for coarse tiles).
    size_t tileCount() const { return tiles_.size(); }
    int32_t tileSize() const { return cfg_.tile; }
    TileInfo tileInfo(size_t i) const {
        const Tile& t = tiles_[i];
        return {t.x0, t.z0, t.w, t.d, uint32_t(t.spans.size()), t.merged, t.coarse};
    }
    // Column (x, z) lies in a coarse tile (or outside the mesh).
    bool coarseAt(int32_t x, int32_t z) const { return !inside(x, z) || tiles_[tileIndex(x, z)].coarse; }

    // Rectangles: dense ids in tile order, stable until a refresh rebuilds tiles.
    size_t rectCount() const { return base_.empty() ? 0 : base_.back(); }
    uint32_t idOf(uint32_t tile, uint32_t local) const { return base_[tile] + local; }
    const Rect& rect(uint32_t id) const { auto [t, l] = split(id); return tiles_[t].rects[l]; }
    const std::vector<Portal>& portals(uint32_t id) const { auto [t, l] = split(id); return tiles_[t].portals[l]; }
    std::vector<uint32_t> neighbors(uint32_t id) const {
        std::vector<uint32_t> out;
        for (const auto& p : portals(id)) out.push_back(idOf(p.tile, p.rect));
        std::sort(out.begin(), out.end());
        out.erase(std::unique(out.begin(), out.end()), out.end());
        return out;
    }
    // World corners of a rectangle (x0, z0) - (x1, z1).
    Fixed worldX(int32_t col) const { return cfg_.originX + cfg_.cell * int64_t(col); }
    Fixed worldZ(int32_t row) const { return cfg_.originZ + cfg_.cell * int64_t(row); }

    // Rectangle under a point: its column, the span nearest in height within the tolerance.
    std::optional<uint32_t> locate(Vec3 p, Fixed yTolerance = Fixed::fromInt(1)) const {
        if (!built_ || anyDirty_ || base_.empty()) return {};
        if (p.x < cfg_.originX || p.z < cfg_.originZ) return {};
        const int64_t cx = (p.x - cfg_.originX).raw / cfg_.cell.raw, cz = (p.z - cfg_.originZ).raw / cfg_.cell.raw;
        if (cx >= cfg_.width || cz >= cfg_.depth) return {};
        const uint32_t ti = tileIndex(int32_t(cx), int32_t(cz));
        const Tile& t = tiles_[ti];
        const uint32_t col = uint32_t((int32_t(cz) - t.z0) * t.w + (int32_t(cx) - t.x0));
        uint32_t best = kNone;
        Fixed bestD = yTolerance;
        for (uint32_t s = t.colFirst[col]; s < t.colFirst[col + 1]; ++s) {
            const Fixed d = fxAbs(t.spans[s].y - p.y);
            if (d <= bestD && (best == kNone || d < bestD)) { best = s; bestD = d; }
        }
        if (best == kNone || t.spanRect[best] == kNone) return {};
        return idOf(ti, t.spanRect[best]);
    }

    // Any-angle path from -> to: out = from, corners..., to. corridor (optional): the rectangles crossed.
    // False: a point off the mesh, no connection, or the visit budget ran out.
    bool findPath(Vec3 from, Vec3 to, const Query& q, std::vector<Vec3>& out, std::vector<uint32_t>* corridor = nullptr) const {
        out.clear();
        if (corridor) corridor->clear();
        auto s = locate(from, q.yTolerance), g = locate(to, q.yTolerance);
        if (!s || !g) return false;
        if (*s == *g) {
            out = {from, to};
            if (corridor) corridor->push_back(*s);
            return true;
        }
        if (q.bestCorridor) return portalSearch(from, to, *s, *g, q, out, corridor);
        const size_t n = rectCount();
        if (gCost_.size() != n) { gCost_.assign(n, Fixed{}); stamp_.assign(n, 0); parent_.assign(n, kNone); via_.assign(n, kNone); entry_.assign(n, Vec2{}); closed_.assign(n, 0); }
        if (++search_ == 0) { std::fill(stamp_.begin(), stamp_.end(), 0u); search_ = 1; }
        struct Node { Fixed f; uint32_t id; };
        auto worse = [](const Node& a, const Node& b) { return a.f != b.f ? a.f > b.f : a.id > b.id; };
        std::vector<Node> heap;
        const Vec2 goal = to.xz();
        auto open = [&](uint32_t id, Fixed gc, Vec2 at, uint32_t par, uint32_t via) {
            stamp_[id] = search_; closed_[id] = 0; gCost_[id] = gc; entry_[id] = at; parent_[id] = par; via_[id] = via;
            heap.push_back({gc + (goal - at).length(), id});
            std::push_heap(heap.begin(), heap.end(), worse);
        };
        open(*s, Fixed{}, from.xz(), kNone, kNone);
        size_t visits = q.maxVisits;
        bool found = false;
        while (!heap.empty()) {
            std::pop_heap(heap.begin(), heap.end(), worse);
            const Node cur = heap.back();
            heap.pop_back();
            if (closed_[cur.id]) continue;
            closed_[cur.id] = 1;
            if (cur.id == *g) { found = true; break; }
            if (visits == 0) break;
            --visits;
            const Rect& r = rect(cur.id);
            const Fixed mul = q.areaCost && r.area < q.areaCost->size() ? (*q.areaCost)[r.area] : Fixed::fromInt(1);
            const auto& ps = portals(cur.id);
            for (uint32_t pi = 0; pi < ps.size(); ++pi) {
                const Portal& p = ps[pi];
                const uint32_t nb = idOf(p.tile, p.rect);
                if (stamp_[nb] == search_ && closed_[nb]) continue;
                const Vec2 at = crossing(p, entry_[cur.id], goal, q.margin);
                const Fixed gc = gCost_[cur.id] + (at - entry_[cur.id]).length() * mul;
                if (stamp_[nb] == search_ && gc >= gCost_[nb]) continue;
                open(nb, gc, at, cur.id, pi);
            }
        }
        lastVisits_ = q.maxVisits - visits;
        if (!found) return false;
        std::vector<const Portal*> chain;
        for (uint32_t id = *g; parent_[id] != kNone; id = parent_[id]) {
            chain.push_back(&portals(parent_[id])[via_[id]]);
            if (corridor) corridor->push_back(id);
        }
        if (corridor) { corridor->push_back(*s); std::reverse(corridor->begin(), corridor->end()); }
        std::reverse(chain.begin(), chain.end());
        std::vector<uint32_t> ids;
        for (uint32_t id = *g;; id = parent_[id]) { ids.push_back(id); if (parent_[id] == kNone) break; }
        funnel(from, to, chain, ids, q.margin, out);
        return true;
    }

    // Content digest (tests: incremental refresh == fresh build, determinism).
    uint64_t digest() const {
        Hasher h;
        for (const Tile& t : tiles_) {
            h.u32(uint32_t(t.rects.size())); h.u32(t.coarse);
            for (size_t i = 0; i < t.rects.size(); ++i) {
                const Rect& r = t.rects[i];
                h.i32(r.x0); h.i32(r.z0); h.i32(r.x1); h.i32(r.z1); h.fx(r.y); h.u32(r.area);
                for (const Portal& p : t.portals[i]) { h.u32(p.tile); h.u32(p.rect); h.u32(p.side); h.u32(p.solid); h.i32(p.pushLo); h.i32(p.pushHi); h.fx(p.fixed); h.fx(p.lo); h.fx(p.hi); h.fx(p.y); }
            }
        }
        return h.value();
    }

private:
    struct Tile {
        int32_t x0 = 0, z0 = 0, w = 0, d = 0;
        bool dirty = true, coarse = false;
        uint32_t merged = 0;
        std::vector<uint32_t> colFirst;    // w * d + 1, local columns row-major
        std::vector<Span> spans;
        std::vector<uint32_t> spanRect;    // per span: rectangle index in the tile (kNone: none)
        std::vector<Rect> rects;
        std::vector<std::vector<Portal>> portals;
    };
    struct ColRef { const Tile* t = nullptr; uint32_t first = 0, count = 0, tile = 0; };

    Config cfg_;
    int32_t tw_ = 0, td_ = 0;
    bool built_ = false, anyDirty_ = false;
    uint64_t revision_ = 0;
    bool telemetryEnabled_=false;
    std::optional<RefreshTelemetry> lastRefresh_;
    std::vector<Tile> tiles_;
    std::vector<uint32_t> base_;
    std::vector<uint32_t> slotBase_, slotOwner_, slotIndex_, slotTarget_;
    std::vector<const Portal*> slotPortal_;   // valid until the next refresh
    std::vector<uint16_t> rectArea_;
    mutable std::vector<Fixed> pg_;
    mutable std::vector<uint32_t> pstamp_, pparent_;
    mutable std::vector<uint8_t> pclosed_;
    mutable uint32_t psearch_ = 0;

    // A* over portal points: three points per portal (both shrunk ends and the middle), costs = straight distances
    // between points of portals of one rectangle (scaled by its area cost). The corridor of the best point route then
    // goes through the funnel. Ties by node id.
    bool portalSearch(Vec3 from, Vec3 to, uint32_t s, uint32_t g, const Query& q, std::vector<Vec3>& out, std::vector<uint32_t>* corridor) const {
        const size_t nodes = size_t(slotOwner_.size()) * 3, T = nodes;
        if (pg_.size() != nodes + 1) { pg_.assign(nodes + 1, Fixed{}); pstamp_.assign(nodes + 1, 0); pparent_.assign(nodes + 1, kNone); pclosed_.assign(nodes + 1, 0); }
        if (++psearch_ == 0) { std::fill(pstamp_.begin(), pstamp_.end(), 0u); psearch_ = 1; }
        const Vec2 goal = to.xz();
        auto point = [&](uint32_t slot, int k) {
            const Portal& p = *slotPortal_[slot];
            Fixed lo, hi;
            interval(p, q.margin, lo, hi);
            const Fixed v = k == 0 ? lo : k == 1 ? (lo + hi) / 2 : hi;
            return p.side < 2 ? Vec2{p.fixed, v} : Vec2{v, p.fixed};
        };
        auto mulOf = [&](uint32_t rid) { const uint16_t a = rectArea_[rid]; return q.areaCost && a < q.areaCost->size() ? (*q.areaCost)[a] : Fixed::fromInt(1); };
        struct Node { Fixed f; uint32_t id; };
        auto worse = [](const Node& a, const Node& b) { return a.f != b.f ? a.f > b.f : a.id > b.id; };
        std::vector<Node> heap;
        auto relax = [&](uint32_t node, Fixed gc, Fixed h, uint32_t par) {
            if (pstamp_[node] == psearch_ && (pclosed_[node] || gc >= pg_[node])) return;
            pstamp_[node] = psearch_; pclosed_[node] = 0; pg_[node] = gc; pparent_[node] = par;
            heap.push_back({gc + h, uint32_t(node)});
            std::push_heap(heap.begin(), heap.end(), worse);
        };
        uint32_t prevRect = kNone;
        auto expand = [&](uint32_t rid, Vec2 at, Fixed gc, uint32_t par) {   // from a point inside rectangle rid
            const Fixed mul = mulOf(rid);
            for (uint32_t slot = slotBase_[rid]; slot < slotBase_[rid + 1]; ++slot) {
                if (slotTarget_[slot] == prevRect) continue;   // never straight back through the portal just crossed
                const Portal& p = *slotPortal_[slot];
                const int kinds = p.hi - p.lo > q.margin * 4 ? 3 : 1;   // short portals: their middle only
                for (int k = 0; k < kinds; ++k) {
                    const int kk = kinds == 1 ? 1 : k;
                    const Vec2 pt = point(slot, kk);
                    relax(slot * 3 + uint32_t(kk), gc + (pt - at).length() * mul, (goal - pt).length(), par);
                }
            }
            if (rid == g) relax(uint32_t(T), gc + (goal - at).length() * mul, Fixed{}, par);
        };
        expand(s, from.xz(), Fixed{}, kNone);
        size_t visits = q.maxVisits * 3;
        bool found = false;
        while (!heap.empty()) {
            std::pop_heap(heap.begin(), heap.end(), worse);
            const Node cur = heap.back();
            heap.pop_back();
            if (pclosed_[cur.id]) continue;
            pclosed_[cur.id] = 1;
            if (cur.id == T) { found = true; break; }
            if (visits == 0) break;
            --visits;
            const uint32_t slot = cur.id / 3;
            prevRect = slotOwner_[slot];
            expand(slotTarget_[slot], point(slot, int(cur.id % 3)), pg_[cur.id], cur.id);
        }
        if (!found) return false;
        std::vector<const Portal*> chain;
        std::vector<uint32_t> ids{g};
        for (uint32_t node = pparent_[T]; node != kNone; node = pparent_[node]) {
            const uint32_t slot = node / 3;
            chain.push_back(slotPortal_[slot]);
            ids.push_back(slotOwner_[slot]);
        }
        std::reverse(chain.begin(), chain.end());
        if (corridor) { *corridor = ids; std::reverse(corridor->begin(), corridor->end()); }
        funnel(from, to, chain, ids, q.margin, out);
        return true;
    }
    // search scratch (derived, never part of the result)
    mutable std::vector<Fixed> gCost_;
    mutable std::vector<uint32_t> stamp_, parent_, via_;
    mutable std::vector<Vec2> entry_;
    mutable std::vector<uint8_t> closed_;
    mutable uint32_t search_ = 0;
    mutable size_t lastVisits_ = 0;

    std::pair<uint32_t, uint32_t> split(uint32_t id) const {
        const uint32_t t = uint32_t(std::upper_bound(base_.begin(), base_.end(), id) - base_.begin()) - 1;
        return {t, id - base_[t]};
    }
    uint32_t tileIndex(int32_t x, int32_t z) const { return uint32_t((z / cfg_.tile) * tw_ + x / cfg_.tile); }
    bool inside(int32_t x, int32_t z) const { return x >= 0 && z >= 0 && x < cfg_.width && z < cfg_.depth; }
    ColRef column(int32_t x, int32_t z) const {
        ColRef c;
        if (!inside(x, z)) return c;
        c.tile = tileIndex(x, z);
        c.t = &tiles_[c.tile];
        const uint32_t col = uint32_t((z - c.t->z0) * c.t->w + (x - c.t->x0));
        c.first = c.t->colFirst[col];
        c.count = c.t->colFirst[col + 1] - c.first;
        return c;
    }
    bool mergeable(const Span& a, const Span& b) const { return a.area == b.area && (!cfg_.flat || a.y == b.y); }

    void rebuildTile(const Source& src, Tile& t, std::vector<Span>& tmp) {
        t.colFirst.assign(size_t(t.w) * size_t(t.d) + 1, 0);
        t.spans.clear();
        for (int32_t lz = 0; lz < t.d; ++lz)
            for (int32_t lx = 0; lx < t.w; ++lx) {
                tmp.clear();
                src.spans(t.x0 + lx, t.z0 + lz, tmp);
                t.colFirst[size_t(lz * t.w + lx)] = uint32_t(t.spans.size());
                t.spans.insert(t.spans.end(), tmp.begin(), tmp.end());
            }
        t.colFirst.back() = uint32_t(t.spans.size());
        t.spanRect.assign(t.spans.size(), kNone);
        t.rects.clear();
        auto col = [&](int32_t lx, int32_t lz) { return uint32_t(lz * t.w + lx); };
        auto local = [&](uint32_t span, uint32_t c) { return span - t.colFirst[c]; };
        std::vector<uint32_t> row, next;
        for (int32_t lz = 0; lz < t.d; ++lz)
            for (int32_t lx = 0; lx < t.w; ++lx) {
                const uint32_t c0 = col(lx, lz);
                for (uint32_t s = t.colFirst[c0]; s < t.colFirst[c0 + 1]; ++s) {
                    if (t.spanRect[s] != kNone) continue;
                    // grow along +x
                    row.assign(1, s);
                    for (int32_t cx = lx + 1; cx < t.w; ++cx) {
                        const uint32_t pc = col(cx - 1, lz), cc = col(cx, lz);
                        uint32_t pick = kNone;
                        for (uint32_t u = t.colFirst[cc]; u < t.colFirst[cc + 1] && pick == kNone; ++u)
                            if (t.spanRect[u] == kNone && mergeable(t.spans[s], t.spans[u]) &&
                                src.link(t.x0 + cx - 1, t.z0 + lz, local(row.back(), pc), t.x0 + cx, t.z0 + lz, local(u, cc))) pick = u;
                        if (pick == kNone) break;
                        row.push_back(pick);
                    }
                    const uint32_t id = uint32_t(t.rects.size());
                    for (uint32_t u : row) t.spanRect[u] = id;
                    int32_t lz1 = lz;
                    // grow along +z while a whole linked row exists
                    for (int32_t nz = lz + 1; nz < t.d; ++nz) {
                        next.clear();
                        bool ok = true;
                        for (size_t k = 0; k < row.size() && ok; ++k) {
                            const int32_t cx = lx + int32_t(k);
                            const uint32_t above = col(cx, nz - 1), cc = col(cx, nz);
                            uint32_t pick = kNone;
                            for (uint32_t u = t.colFirst[cc]; u < t.colFirst[cc + 1] && pick == kNone; ++u) {
                                if (t.spanRect[u] != kNone || !mergeable(t.spans[s], t.spans[u])) continue;
                                if (!src.link(t.x0 + cx, t.z0 + nz - 1, local(row[k], above), t.x0 + cx, t.z0 + nz, local(u, cc))) continue;
                                if (k > 0 && !src.link(t.x0 + cx - 1, t.z0 + nz, local(next.back(), col(cx - 1, nz)), t.x0 + cx, t.z0 + nz, local(u, cc))) continue;
                                pick = u;
                            }
                            if (pick == kNone) ok = false;
                            else next.push_back(pick);
                        }
                        if (!ok) break;
                        for (uint32_t u : next) t.spanRect[u] = id;
                        row.swap(next);
                        lz1 = nz;
                    }
                    t.rects.push_back({t.x0 + lx, t.z0 + lz, t.x0 + lx + int32_t(row.size()) - 1, t.z0 + lz1, t.spans[s].y, t.spans[s].area});
                }
            }
        t.merged = uint32_t(t.rects.size());
        t.coarse = cfg_.maxRectRatio.raw > 0 && !t.spans.empty() &&
                   int64_t(t.rects.size()) * Fixed::kOne > cfg_.maxRectRatio.raw * int64_t(t.spans.size());
        if (t.coarse) { t.rects.clear(); std::fill(t.spanRect.begin(), t.spanRect.end(), kNone); }
        t.portals.assign(t.rects.size(), {});
    }

    // Span of rectangle `rect` (tile-local index) in column (x, z) of that tile, as a column-local index.
    uint32_t spanOf(const ColRef& c, uint32_t rect) const {
        for (uint32_t i = 0; i < c.count; ++i) if (c.t->spanRect[c.first + i] == rect) return i;
        return kNone;
    }
    // The corner between (near, far) and their neighbours one step along (dx, dz): open (no wall touches it), or
    // solid with the side(s) that continue: push +1 = only the far side continues, -1 = only the near side.
    bool openCorner(const Source& src, int32_t nx, int32_t nz, uint32_t ni, int32_t fx, int32_t fz, uint32_t fi, int32_t dx, int32_t dz, int8_t& push) const {
        push = 0;
        const ColRef a = column(nx + dx, nz + dz), b = column(fx + dx, fz + dz);
        bool nearOpen = false, farOpen = false;
        for (uint32_t i = 0; a.t && i < a.count; ++i) {
            if (!src.link(nx, nz, ni, nx + dx, nz + dz, i)) continue;
            nearOpen = true;
            for (uint32_t j = 0; b.t && j < b.count; ++j)
                if (src.link(fx, fz, fi, fx + dx, fz + dz, j) && src.link(nx + dx, nz + dz, i, fx + dx, fz + dz, j)) return true;
        }
        for (uint32_t j = 0; b.t && j < b.count && !farOpen; ++j) farOpen = src.link(fx, fz, fi, fx + dx, fz + dz, j);
        push = int8_t(nearOpen == farOpen ? 0 : farOpen ? 1 : -1);
        return false;
    }

    void rebuildPortals(const Source& src, uint32_t ti) {
        Tile& t = tiles_[ti];
        for (uint32_t ri = 0; ri < t.rects.size(); ++ri) {
            const Rect r = t.rects[ri];
            auto& out = t.portals[ri];
            out.clear();
            for (uint8_t side = 0; side < 4; ++side) {
                const bool alongZ = side < 2;
                const int32_t k0 = alongZ ? r.z0 : r.x0, k1 = alongZ ? r.z1 : r.x1;
                const int32_t ax = alongZ ? 0 : 1, az = alongZ ? 1 : 0;   // step along the side
                auto nearXZ = [&](int32_t k, int32_t& x, int32_t& z) {
                    x = alongZ ? (side == 0 ? r.x0 : r.x1) : k;
                    z = alongZ ? k : (side == 2 ? r.z0 : r.z1);
                };
                const int32_t ox = side == 0 ? -1 : side == 1 ? 1 : 0, oz = side == 2 ? -1 : side == 3 ? 1 : 0;
                struct Hit { uint32_t tile = kNone, rect = kNone, ni = kNone, fi = kNone; Fixed y; };
                std::vector<Hit> hits(size_t(k1 - k0 + 1));
                for (int32_t k = k0; k <= k1; ++k) {
                    int32_t x, z;
                    nearXZ(k, x, z);
                    const ColRef nc = column(x, z), fc = column(x + ox, z + oz);
                    if (!fc.t) continue;
                    const uint32_t ni = spanOf(nc, ri);
                    if (ni == kNone) continue;
                    const Fixed ny = nc.t->spans[nc.first + ni].y;
                    Hit best;
                    Fixed bestD;
                    for (uint32_t j = 0; j < fc.count; ++j) {
                        const uint32_t fr = fc.t->spanRect[fc.first + j];
                        if (fr == kNone || !src.link(x, z, ni, x + ox, z + oz, j)) continue;
                        const Fixed d = fxAbs(fc.t->spans[fc.first + j].y - ny);
                        if (best.rect == kNone || d < bestD) { best = {fc.tile, fr, ni, j, fc.t->spans[fc.first + j].y}; bestD = d; }
                    }
                    hits[size_t(k - k0)] = best;
                }
                for (int32_t k = k0; k <= k1;) {
                    const Hit h = hits[size_t(k - k0)];
                    if (h.rect == kNone) { ++k; continue; }
                    int32_t e = k;
                    while (e + 1 <= k1 && hits[size_t(e + 1 - k0)].tile == h.tile && hits[size_t(e + 1 - k0)].rect == h.rect) ++e;
                    Portal p;
                    p.tile = h.tile; p.rect = h.rect; p.side = side; p.y = h.y;
                    if (alongZ) { p.fixed = worldX(side == 0 ? r.x0 : r.x1 + 1); p.lo = worldZ(k); p.hi = worldZ(e + 1); }
                    else { p.fixed = worldZ(side == 2 ? r.z0 : r.z1 + 1); p.lo = worldX(k); p.hi = worldX(e + 1); }
                    int32_t x, z;
                    nearXZ(k, x, z);
                    if (!openCorner(src, x, z, h.ni, x + ox, z + oz, h.fi, -ax, -az, p.pushLo)) p.solid |= 1;
                    const Hit he = hits[size_t(e - k0)];
                    nearXZ(e, x, z);
                    if (!openCorner(src, x, z, he.ni, x + ox, z + oz, he.fi, ax, az, p.pushHi)) p.solid |= 2;
                    out.push_back(p);
                    k = e + 1;
                }
            }
        }
    }

    // Shrunk portal interval (wall corners keep `margin`, never past the middle).
    static void interval(const Portal& p, Fixed margin, Fixed& lo, Fixed& hi) {
        const Fixed m = fxMin(margin, (p.hi - p.lo) / 2);
        lo = p.lo + ((p.solid & 1) ? m : Fixed{});
        hi = p.hi - ((p.solid & 2) ? m : Fixed{});
        if (lo > hi) lo = hi = (lo + hi) / 2;
    }
    // Where to cross the portal coming from `at` toward `goal`: the straight line's crossing, clamped.
    static Vec2 crossing(const Portal& p, Vec2 at, Vec2 goal, Fixed margin) {
        Fixed lo, hi;
        interval(p, margin, lo, hi);
        const bool alongZ = p.side < 2;
        const Fixed a = alongZ ? at.x : at.y, ga = alongZ ? goal.x : goal.y;   // across the portal
        const Fixed b = alongZ ? at.y : at.x, gb = alongZ ? goal.y : goal.x;   // along it
        Fixed v = b;
        const Fixed da = ga - a;
        if (da.raw != 0 && ((a <= p.fixed && ga >= p.fixed) || (a >= p.fixed && ga <= p.fixed)))
            v = b + (gb - b) * ((p.fixed - a) / da);
        v = fxClamp(v, lo, hi);
        return alongZ ? Vec2{p.fixed, v} : Vec2{v, p.fixed};
    }
    static Fixed cross(Vec2 a, Vec2 b) { return a.x * b.y - a.y * b.x; }

    // Simple stupid funnel over the portal chain (left/right as seen walking through each portal). The funnel runs on
    // the portal lines. Two consecutive corners on the same portal line would run along it, over a wall stretch that
    // continues on one side of a portal end: such corners move off that wall across the portal (at most half a cell,
    // into the cell next to the portal), so paths do not scrape walls.
    // Is the segment a-b inside the union of the rectangles (closed)? Exact clipping, a few raw units of slack.
    bool covered(Vec2 a, Vec2 b, const std::vector<uint32_t>& ids) const {
        std::vector<std::pair<int64_t, int64_t>> spans;
        const Vec2 d = b - a;
        const int64_t one = Fixed::kOne;
        for (uint32_t id : ids) {
            const Rect& r = rect(id);
            const Fixed lo[2] = {worldX(r.x0), worldZ(r.z0)}, hi[2] = {worldX(r.x1 + 1), worldZ(r.z1 + 1)};
            const Fixed p[2] = {a.x, a.y}, v[2] = {d.x, d.y};
            Fixed t0{}, t1 = Fixed::fromInt(1);
            bool ok = true;
            for (int k = 0; k < 2 && ok; ++k) {
                if (v[k].raw == 0) { ok = p[k] >= lo[k] && p[k] <= hi[k]; continue; }
                Fixed ta = (lo[k] - p[k]) / v[k], tb = (hi[k] - p[k]) / v[k];
                if (ta > tb) std::swap(ta, tb);
                t0 = fxMax(t0, ta); t1 = fxMin(t1, tb);
                ok = t0 <= t1;
            }
            if (ok) spans.push_back({t0.raw, t1.raw});
        }
        std::sort(spans.begin(), spans.end());
        int64_t reach = 0;
        for (auto [s0, s1] : spans) { if (s0 > reach + 4) return false; reach = std::max(reach, s1); }
        return reach >= one - 4;
    }

    void funnel(Vec3 from, Vec3 to, const std::vector<const Portal*>& chain, const std::vector<uint32_t>& ids, Fixed margin, std::vector<Vec3>& out) const {
        const Fixed across = fxMin(margin, cfg_.cell / 2);
        struct Gate { Vec2 l, r, lOff, rOff; Fixed y; };
        std::vector<Gate> gates;
        gates.reserve(chain.size() + 2);
        gates.push_back({from.xz(), from.xz(), {}, {}, from.y});
        for (const Portal* p : chain) {
            Fixed lo, hi;
            interval(*p, margin, lo, hi);
            const bool alongZ = p->side < 2;
            const Fixed farSign = (p->side & 1) ? across : -across;   // offset toward the far side
            const Vec2 a = alongZ ? Vec2{p->fixed, lo} : Vec2{lo, p->fixed};
            const Vec2 b = alongZ ? Vec2{p->fixed, hi} : Vec2{hi, p->fixed};
            const Fixed pa = farSign * int64_t(p->pushLo), pb = farSign * int64_t(p->pushHi);
            const Vec2 oa = alongZ ? Vec2{pa, {}} : Vec2{{}, pa}, ob = alongZ ? Vec2{pb, {}} : Vec2{{}, pb};
            // walking -x or +z: lo is on the left; walking +x or -z: hi is
            const bool loLeft = p->side == 0 || p->side == 3;
            gates.push_back(loLeft ? Gate{a, b, oa, ob, p->y} : Gate{b, a, ob, oa, p->y});
        }
        gates.push_back({to.xz(), to.xz(), {}, {}, to.y});
        out.clear();
        out.push_back(from);
        Vec2 apex = from.xz(), L = apex, R = apex;
        size_t ai = 0, li = 0, ri = 0;
        std::vector<Vec2> offs{Vec2{}};
        auto corner = [&](Vec2 p, Vec2 off, Fixed y) { out.push_back({p.x, y, p.y}); offs.push_back(off); };
        for (size_t i = 1; i < gates.size(); ++i) {
            const Gate& gt = gates[i];
            // right edge
            if (cross(R - apex, gt.r - apex) >= Fixed{}) {
                if (apex == R || cross(L - apex, gt.r - apex) <= Fixed{}) { R = gt.r; ri = i; }
                else {
                    corner(L, gates[li].lOff, gates[li].y);
                    apex = L; ai = li; R = L; ri = li;
                    i = ai;
                    continue;
                }
            }
            // left edge
            if (cross(L - apex, gt.l - apex) <= Fixed{}) {
                if (apex == L || cross(R - apex, gt.l - apex) >= Fixed{}) { L = gt.l; li = i; }
                else {
                    corner(R, gates[ri].rOff, gates[ri].y);
                    apex = R; ai = ri; L = R; li = ri;
                    i = ai;
                    continue;
                }
            }
        }
        (void)ai;
        if (!(out.back().x == to.x && out.back().z == to.z)) { out.push_back(to); offs.push_back({}); }
        else out.back() = to;
        // corners that run along their portal line with a neighbour: move them off the wall when both segments stay in
        // the corridor, else step off it with an extra point (a short move into the cell next to the portal)
        std::vector<Vec3> res;
        res.reserve(out.size() + 4);
        res.push_back(out[0]);
        for (size_t k = 1; k < out.size(); ++k) {
            const Vec2 o = offs[k];
            const bool onX = o.x.raw != 0, onZ = o.y.raw != 0;   // portal line x = const / z = const
            const bool prevAlong = (onX && out[k - 1].x == out[k].x) || (onZ && out[k - 1].z == out[k].z);
            const bool nextAlong = k + 1 < out.size() && ((onX && out[k + 1].x == out[k].x) || (onZ && out[k + 1].z == out[k].z));
            if (k + 1 >= out.size() || !(prevAlong || nextAlong)) { res.push_back(out[k]); continue; }
            Vec3 m = out[k];
            m.x += o.x; m.z += o.y;
            // every new segment is checked against the corridor (the next corner may still move: checked then)
            const bool inOk = covered(res.back().xz(), m.xz(), ids), outOk = covered(m.xz(), out[k + 1].xz(), ids);
            if (inOk && outOk) res.push_back(m);
            else if (prevAlong && inOk) { res.push_back(m); res.push_back(out[k]); }   // came along the line on the moved side
            else if (nextAlong && outOk) { res.push_back(out[k]); res.push_back(m); }  // leaves along it on the moved side
            else res.push_back(out[k]);
        }
        out.swap(res);
    }
};

} // namespace aiverse::nav
