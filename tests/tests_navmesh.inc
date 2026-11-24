// ---- navigation mesh (engine/nav, genre-independent) and its RTS use (movement.pathfinding: "navmesh")
#include "engine/nav/NavMesh.h"

namespace navtest {
struct CharGrid final : nav::Source {
    int w = 0, h = 0;
    std::vector<std::string> rows;
    bool open(int x, int z) const { return x >= 0 && z >= 0 && x < w && z < h && rows[size_t(z)][size_t(x)] != '#'; }
    void spans(int32_t x, int32_t z, std::vector<nav::Span>& out) const override { if (open(x, z)) out.push_back({}); }
    bool link(int32_t, int32_t, uint32_t, int32_t x1, int32_t z1, uint32_t) const override { return open(x1, z1); }
};
static Vec3 navAt(int64_t x2, int64_t z2) { return {Fixed::fromRatio(x2, 2), Fixed{}, Fixed::fromRatio(z2, 2)}; }   // half units
// Does the polyline enter a wall cell (points strictly inside it; touching an edge is allowed)?
static bool crossesWall(const CharGrid& g, const std::vector<Vec3>& path) {
    for (size_t i = 1; i < path.size(); ++i)
        for (int s = 0; s <= 128; ++s) {
            const Vec3 p = path[i - 1] + (path[i] - path[i - 1]) * Fixed::fromRatio(s, 128);
            const int64_t fx = p.x.raw & 0xFFFF, fz = p.z.raw & 0xFFFF;
            if (fx < 8 || fx > 0xFFFF - 8 || fz < 8 || fz > 0xFFFF - 8) continue;   // on a cell edge
            if (!g.open(int(p.x.floorInt()), int(p.z.floorInt()))) return true;
        }
    return false;
}
// Same for an RTS grid path that starts at `from` (points strictly inside an impassable cell).
static bool crossesCells(const game::MapGrid& g, Vec3 from, const std::vector<Vec3>& path) {
    Vec3 prev = from;
    for (const Vec3& w : path) {
        for (int s = 1; s < 256; ++s) {
            const Vec3 p = prev + (w - prev) * Fixed::fromRatio(s, 256);
            const int64_t fx = p.x.raw & 0xFFFF, fz = p.z.raw & 0xFFFF;
            if (fx < 8 || fx > 0xFFFF - 8 || fz < 8 || fz > 0xFFFF - 8) continue;
            if (!g.passable(int(p.x.floorInt()), int(p.z.floorInt()), game::kLayerGround)) return true;
        }
        prev = w;
    }
    return false;
}
}

TEST(navmesh_core_paths_rectangles_and_refresh) {
    using namespace navtest;
    CharGrid g;
    g.rows = {"....................", "....................", "..........#.........", "..........#.........", "..........#.........",
              "..........#.........", "..........#.........", "..........#.........", "..........#.........", "..........#........."};
    g.w = 20; g.h = 10;
    nav::Config c; c.width = g.w; c.depth = g.h; c.tile = 8;
    nav::Mesh m;
    CHECK(m.build(g, c));
    CHECK(m.rectCount() < 20);   // 190 open cells merge into a few rectangles
    std::vector<Vec3> path;
    nav::Query q;
    // around the top of the wall: two corners, kept half a cell off the wall
    CHECK(m.findPath(navAt(11, 17), navAt(31, 17), q, path));
    CHECK(path.size() == 4 && !crossesWall(g, path));
    for (auto& p : path) CHECK(!(p.z == Fixed::fromInt(2) && p.x >= Fixed::fromInt(10) && p.x <= Fixed::fromInt(11)));
    // open area: one straight segment (any angle, no grid zigzag)
    CHECK(m.findPath(navAt(1, 1), navAt(39, 3), q, path) && path.size() == 2);
    // closing the gap: no path; reopening restores the exact mesh; refresh == fresh build
    const uint64_t d0 = m.digest();
    g.rows[0][10] = '#'; g.rows[1][10] = '#';
    m.markDirty(10, 0, 10, 1);
    CHECK(m.refresh(g));
    CHECK(!m.findPath(navAt(11, 17), navAt(31, 17), q, path));
    nav::Mesh fresh;
    fresh.build(g, c);
    CHECK(fresh.digest() == m.digest());
    g.rows[0][10] = '.'; g.rows[1][10] = '.';
    m.markDirty(10, 0, 10, 1);
    CHECK(m.refresh(g) && m.digest() == d0);
    // off-mesh endpoints and budgets fail explicitly
    CHECK(!m.findPath(navAt(21, 9), navAt(31, 17), q, path));   // inside the wall
    nav::Query tiny; tiny.maxVisits = 1;
    CHECK(!m.findPath(navAt(11, 17), navAt(31, 17), tiny, path));
}

TEST(navmesh_core_random_mazes_never_cross_walls) {
    using namespace navtest;
    uint32_t seed = 7;
    auto rnd = [&]() { seed = seed * 1664525u + 1013904223u; return seed >> 8; };
    int found = 0;
    Fixed rectLen, bestLen;
    for (int maze = 0; maze < 3; ++maze) {
        CharGrid g;
        g.w = g.h = 64;
        for (int z = 0; z < 64; ++z) { std::string row; for (int x = 0; x < 64; ++x) row += int(rnd() % 100) < 20 + maze * 5 ? '#' : '.'; g.rows.push_back(row); }
        nav::Config c; c.width = c.depth = 64; c.tile = 16;
        nav::Mesh m;
        CHECK(m.build(g, c));
        nav::Query q;
        q.margin = Fixed::fromRatio(3 + maze, 10);
        std::vector<Vec3> path;
        for (int t = 0; t < 120; ++t) {
            const int sx = int(rnd() % 64), sz = int(rnd() % 64), gx = int(rnd() % 64), gz = int(rnd() % 64);
            if (!g.open(sx, sz) || !g.open(gx, gz)) continue;
            if (!m.findPath(navAt(2 * sx + 1, 2 * sz + 1), navAt(2 * gx + 1, 2 * gz + 1), q, path)) continue;
            ++found;
            CHECK(!crossesWall(g, path));
            CHECK(path.front() == navAt(2 * sx + 1, 2 * sz + 1) && path.back() == navAt(2 * gx + 1, 2 * gz + 1));
            // the portal-point search: same reachability, never through walls, not longer on the whole
            nav::Query best = q;
            best.bestCorridor = true;
            std::vector<Vec3> bp;
            CHECK(m.findPath(navAt(2 * sx + 1, 2 * sz + 1), navAt(2 * gx + 1, 2 * gz + 1), best, bp) && !crossesWall(g, bp));
            for (size_t i = 1; i < path.size(); ++i) rectLen += (path[i] - path[i - 1]).xz().length();
            for (size_t i = 1; i < bp.size(); ++i) bestLen += (bp[i] - bp[i - 1]).xz().length();
        }
    }
    CHECK(found > 150);
    CHECK(bestLen <= rectLen);
}

TEST(navmesh_rts_grid_paths) {
    // movement.pathfinding "navmesh" on a MapGrid: any-angle paths, structures update it, cliffs need ramps
    game::MapGrid g;
    g.init(40, 30);
    for (int y = 0; y < 26; ++y) g.flags[size_t(g.index(20, y))] = game::kCellWater;   // water wall with a gap at the bottom
    g.setPathMode(game::MapGrid::kPathNavMesh);
    std::vector<Vec3> path, grid;
    const Vec3 from = g.cellCenter(5, 5), to = g.cellCenter(35, 5);
    CHECK(g.findPath(from, to, game::kLayerGround, path));
    CHECK(g.navStats[0] == 1 && g.navStats[1] == 0);
    CHECK(path.back() == to && path.size() <= 4);
    bool gap = false;
    for (auto& p : path) if (p.z >= Fixed::fromInt(26)) gap = true;
    CHECK(gap);
    g.setPathMode(game::MapGrid::kPathGrid);
    CHECK(g.findPath(from, to, game::kLayerGround, grid));
    g.setPathMode(game::MapGrid::kPathNavMesh);
    // a structure closes the gap: the next query sees it (incremental), the unit's path falls back to the grid search
    g.block(20, 26, 1, 4, +1);
    CHECK(!g.findPath(from, to, game::kLayerGround, path));
    CHECK(g.navStats[1] == 1);
    g.block(20, 26, 1, 4, -1);
    CHECK(g.findPath(from, to, game::kLayerGround, path) && g.navStats[0] == 2);
    // same mesh after an invalidation (save load) as before it
    const uint64_t before = g.navMeshFor(game::kLayerGround)->digest();
    g.invalidatePathing();
    CHECK(g.navMeshFor(game::kLayerGround)->digest() == before);
    // cliff levels: only the ramp connects them
    game::MapGrid c;
    c.init(20, 20);
    for (int y = 0; y < 20; ++y) for (int x = 10; x < 20; ++x) c.level[size_t(c.index(x, y))] = 1;
    c.flags[size_t(c.index(10, 15))] |= game::kCellRamp;
    c.setPathMode(game::MapGrid::kPathNavMesh);
    CHECK(c.findPath(c.cellCenter(3, 3), c.cellCenter(16, 3), game::kLayerGround, path) && c.navStats[0] == 1);
    bool ramp = false;   // a corner next to the ramp cell (10, 15)
    for (auto& p : path) if (p.z >= Fixed::fromInt(14) && p.z <= Fixed::fromInt(17) && p.x >= Fixed::fromInt(9) && p.x <= Fixed::fromInt(12)) ramp = true;
    CHECK(ramp);
}

TEST(navmesh_rts_matches_deterministic) {
    // whole bot matches with navmesh paths: deterministic, and the navmesh answers the searches
    for (auto [game, map] : {std::pair{kSkirmish, std::string("maps/valley.json")}, std::pair{std::string(AIVERSE_TEMPLATES_DIR) + "/frostcrown", std::string("maps/frozen_pass.json")}}) {
      for (const char* mode : {"navmesh", "hybrid"}) {
        auto run = [&](int* answered) {
            SimConfig cfg; cfg.seed = 4;
            auto sim = std::make_unique<Simulation>(cfg, std::make_unique<game::GameCore>());
            Json rules = Json::parseFile(game + "/rules.json");
            rules.member("movement").set("pathfinding", Json::string(mode));
            sim->setup(rules, Json::parseFile(game + "/" + map));
            std::vector<game::ComputerPlayer> bots;
            for (auto& p : sim->players()) bots.emplace_back(p.slot, 4);
            for (uint32_t t = 0; t < 20 * 120 && sim->result() == 0; ++t) { for (auto& b : bots) b.update(*sim); sim->step(); }
            if (answered) *answered = mod(*sim).state(*sim).grid.navStats[0];
            return sim->checksum();
        };
        int answered = 0;
        const uint64_t a = run(&answered), b = run(nullptr);
        CHECK(a == b);
        CHECK(answered > 0);
      }
    }
}

TEST(navmesh_core_tile_classification) {
    // maxRectRatio: maze tiles keep no rectangles (coarse); open tiles merge as usual; classes follow edits
    using namespace navtest;
    CharGrid g;
    g.w = 32; g.h = 16;
    for (int z = 0; z < 16; ++z) {
        std::string row;
        for (int x = 0; x < 32; ++x) row += x >= 16 && (x % 2 == 1 || z % 2 == 1) && (x + z) % 3 ? '#' : '.';   // right tile: lattice
        g.rows.push_back(row);
    }
    nav::Config c; c.width = 32; c.depth = 16; c.tile = 16; c.maxRectRatio = Fixed::fromRatio(1, 4);
    nav::Mesh m;
    CHECK(m.build(g, c));
    CHECK(m.tileCount() == 2 && !m.tileInfo(0).coarse && m.tileInfo(1).coarse);
    CHECK(!m.coarseAt(3, 3) && m.coarseAt(20, 3) && m.coarseAt(40, 3));
    CHECK(m.tileInfo(1).merged * 4 > m.tileInfo(1).spans);
    std::vector<Vec3> path;
    CHECK(!m.locate(navAt(41, 7)) && m.locate(navAt(7, 7)));
    for (auto& r : g.rows) for (int x = 16; x < 32; ++x) r[size_t(x)] = '.';   // the maze is cleared: open again
    m.markDirty(16, 0, 31, 15);
    CHECK(m.refresh(g) && !m.tileInfo(1).coarse && m.findPath(navAt(3, 3), navAt(61, 29), nav::Query{}, path) && path.size() == 2);
}

TEST(navmesh_rts_hybrid_paths) {
    // movement.pathfinding "hybrid": HPA* portal graph over the clusters, navmesh in open clusters, grid A* in mazes
    game::MapGrid g;
    g.init(96, 64);
    uint32_t seed = 11;
    auto rnd = [&]() { seed = seed * 1664525u + 1013904223u; return seed >> 8; };
    for (int y = 0; y < 64; ++y)
        for (int x = 48; x < 96; ++x)
            if (rnd() % 100 < 30) g.flags[size_t(g.index(x, y))] = game::kCellWater;   // east half: maze
    for (int y = 10; y < 54; ++y) g.flags[size_t(g.index(30, y))] = game::kCellWater;   // a wall in the open half
    g.setPathMode(game::MapGrid::kPathHybrid);
    const nav::Mesh* m = g.navMeshFor(game::kLayerGround);
    CHECK(m && m->tileSize() == game::HpaGraph::kC);
    int coarse = 0, open = 0;
    for (size_t i = 0; i < m->tileCount(); ++i) (m->tileInfo(i).coarse ? coarse : open)++;
    CHECK(coarse >= 8 && open >= 8 && m->coarseAt(80, 30) && !m->coarseAt(10, 30));
    game::MapGrid ref = g;   // same cells, grid search
    ref.setPathMode(game::MapGrid::kPathGrid);
    int both = 0, mismatch = 0;
    std::vector<Vec3> p, q;
    for (int t = 0; t < 150; ++t) {
        const int sx = int(rnd() % 96), sy = int(rnd() % 64), gx = int(rnd() % 96), gy = int(rnd() % 64);
        if (!g.passable(sx, sy, game::kLayerGround) || !g.passable(gx, gy, game::kLayerGround)) continue;
        const bool a = g.findPath(g.cellCenter(sx, sy), g.cellCenter(gx, gy), game::kLayerGround, p, 40000);
        const bool b = ref.findPath(ref.cellCenter(sx, sy), ref.cellCenter(gx, gy), game::kLayerGround, q, 40000);
        if (a != b) ++mismatch;
        if (!a) continue;
        ++both;
        // every leg stays out of wall cells (touching a wall edge or corner is allowed: corners of the navmesh legs)
        CHECK(!navtest::crossesCells(g, g.cellCenter(sx, sy), p));
        CHECK(p.back() == g.cellCenter(gx, gy));
    }
    CHECK(both > 60 && mismatch == 0);
    CHECK(g.navStats[0] > 0);
    // a long trip across both halves goes through the portal graph and is deterministic after invalidation
    const Vec3 s = g.cellCenter(2, 2);
    int gx = 93, gy = 60;
    g.nearestPassable(gx, gy, game::kLayerGround);
    CHECK(g.findPath(s, g.cellCenter(gx, gy), game::kLayerGround, p));
    g.invalidatePathing();
    CHECK(g.findPath(s, g.cellCenter(gx, gy), game::kLayerGround, q) && p == q);
    // flow fields use the same cluster graph on this (small) map
    CHECK(g.flowHierFor(g.index(gx, gy)) && !ref.flowHierFor(ref.index(gx, gy)));
}
