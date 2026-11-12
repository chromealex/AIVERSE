// City planner (preset shape "city", agent-gta2): a street network with a hierarchy, a river with bridges,
// roundabouts, blocks zoned by distance from the centre, buildings fitted to the lots, parks, car parks, street
// trees, lamps and furniture, parked cars, people and the hero. Integer maths and its own splitmix generator only:
// the same request gives the same map everywhere. Format: docs/city-planner.md.
//
// Everything stands on one level: bridges cross water, junctions are at grade (crossroads, T-junctions,
// roundabouts). The "city" block of the result carries the road graph for traffic and for tools.
#include "GenInternal.h"
#include <algorithm>

namespace aiverse::gen {
namespace {

struct Rng {
    uint64_t s;
    uint32_t next() { s += 0x9e3779b97f4a7c15ull; uint64_t z = s; z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull; z = (z ^ (z >> 27)) * 0x94d049bb133111ebull; return uint32_t((z ^ (z >> 31)) >> 16); }
    int range(int a, int b) { return b <= a ? a : a + int(next() % uint32_t(b - a + 1)); }
    bool chance(int permille) { return int(next() % 1000) < permille; }
};

enum Kind : uint8_t { KLot, KRoad, KStreet, KPave, KWater, KPark, KParking, KIsland, KBuilt };
struct Road { int pos, a, b, w; bool vert; int cls; };     // vert: x in [pos, pos + w), z in [a, b); cls 0 arterial, 1 street
struct Rect { int x, z, w, h; };
struct Kit {
    std::vector<int> buildings[4];                        // any, residential, commercial, industrial
    std::vector<int> trees, bushes, lamps, props, cars, peds;
    int hero = -1;
};

uint64_t tagBit(const game::Rules& R, const char* name) {
    for (size_t i = 0; i < R.tagNames.size(); ++i) if (R.tagNames[i] == name) return uint64_t(1) << i;
    return 0;
}

Kit findKit(const Ctx& c) {
    Kit k;
    const game::Rules& R = c.R;
    const uint64_t bBuild = tagBit(R, "building"), bZone[3] = {tagBit(R, "residential"), tagBit(R, "commercial"), tagBit(R, "industrial")};
    const uint64_t bTree = tagBit(R, "tree"), bBush = tagBit(R, "bush"), bLamp = tagBit(R, "lamp"), bProp = tagBit(R, "prop");
    const uint64_t bPed = tagBit(R, "pedestrian") | tagBit(R, "ambient_pedestrian"), bCar = tagBit(R, "vehicle");
    for (size_t i = 0; i < R.types.size(); ++i) {
        const game::UnitType& t = R.types[i];
        if (t.structure && (t.tags & bBuild)) {
            k.buildings[0].push_back(int(i));
            for (int z = 0; z < 3; ++z) if (t.tags & bZone[z]) k.buildings[z + 1].push_back(int(i));
        }
        if (t.tags & bTree) k.trees.push_back(int(i));
        if (t.tags & bBush) k.bushes.push_back(int(i));
        if (t.tags & bLamp) k.lamps.push_back(int(i));
        else if (t.tags & bProp) k.props.push_back(int(i));
        if (t.tags & bPed) k.peds.push_back(int(i));
        if (t.tags & bCar) k.cars.push_back(int(i));
    }
    for (auto& [id, v] : c.rulesJson["driving"]["vehicles"].members()) {   // vehicles of the driving block need no tag
        (void)v;
        int t = R.findType(id);
        if (t >= 0 && std::find(k.cars.begin(), k.cars.end(), t) == k.cars.end()) k.cars.push_back(t);
    }
    std::sort(k.cars.begin(), k.cars.end());
    const Json& heroes = c.rulesJson["topDown"]["heroes"];
    const std::string hero = heroes.isString() ? heroes.str() : heroes.isArray() && heroes.size() ? heroes[size_t(0)].str() : c.pstr("hero", "");
    if (!hero.empty()) k.hero = R.findType(hero);
    return k;
}

int permille(const Ctx& c, const char* key, int def) {
    const Json& v = c.P[key];
    if (v.type() == Json::Type::Bool) return v.boolean() ? 1000 : 0;
    if (!v.isNumber()) return def;
    return int(std::clamp<int64_t>((v.fx().raw * 1000) >> 16, 0, 1000));
}

// evenly spread lines of width `w` over [0, L), one flush with each edge, the inner ones jittered
std::vector<int> spread(Rng& rng, int L, int w, int spacing, int jitter) {
    const int n = std::max(2, (L - w + spacing / 2) / spacing + 1), gap = w + 6;
    std::vector<int> p(size_t(n), 0);
    for (int i = 0; i < n; ++i) p[size_t(i)] = int(int64_t(i) * (L - w) / (n - 1)) + (i > 0 && i < n - 1 ? rng.range(-jitter, jitter) : 0);
    for (int i = 1; i < n - 1; ++i) p[size_t(i)] = std::clamp(p[size_t(i)], p[size_t(i - 1)] + gap, std::max(p[size_t(i - 1)] + gap, p[size_t(n - 1)] - (n - 1 - i) * gap));
    return p;
}

struct Plan {
    int W = 0, H = 0;
    std::vector<uint8_t> kind, bridge;
    std::vector<int16_t> vr, hr;                          // index of the vertical / horizontal road over the cell, -1
    std::vector<Road> roads;
    std::vector<int> xs, zs;                              // arterial positions
    std::vector<Rect> leaves;
    std::vector<std::pair<int, int>> roundabouts;         // (i, j) arterial indices
    bool river = false;
    int rmin = 0, rmax = 0;
    int ci(int x, int z) const { return z * W + x; }
    bool in(int x, int z) const { return x >= 0 && z >= 0 && x < W && z < H; }
    uint8_t at(int x, int z) const { return in(x, z) ? kind[size_t(ci(x, z))] : uint8_t(KBuilt); }
    bool road(int x, int z) const { const uint8_t k = at(x, z); return in(x, z) && (k == KRoad || k == KStreet); }
};

void splitBlock(Plan& p, Rng& rng, Rect r, int maxW, int maxH, int minB, int sw) {
    const bool wide = r.w > maxW, tall = r.h > maxH;
    if (wide || tall) {
        bool vert = wide && (!tall || int64_t(r.w) * maxH >= int64_t(r.h) * maxW);
        for (int attempt = 0; attempt < 2; ++attempt, vert = !vert) {
            const int len = vert ? r.w : r.h, org = vert ? r.x : r.z;
            const int lo = org + minB, hi = org + len - minB - sw;
            if (lo > hi) continue;
            int at = rng.range(lo + (hi - lo) / 4, hi - (hi - lo) / 4);
            if (vert && p.river && r.x < p.rmax + 2 && r.x + r.w > p.rmin - 2 && at + sw > p.rmin - 2 && at < p.rmax + 2) {
                if (p.rmin - 2 - sw >= lo) at = p.rmin - 2 - sw;     // never a street along the river bed
                else if (p.rmax + 2 <= hi) at = p.rmax + 2;
                else continue;
            }
            if (vert) {
                p.roads.push_back({at, r.z, r.z + r.h, sw, true, 1});
                splitBlock(p, rng, {r.x, r.z, at - r.x, r.h}, maxW, maxH, minB, sw);
                splitBlock(p, rng, {at + sw, r.z, r.x + r.w - at - sw, r.h}, maxW, maxH, minB, sw);
            } else {
                p.roads.push_back({at, r.x, r.x + r.w, sw, false, 1});
                splitBlock(p, rng, {r.x, r.z, r.w, at - r.z}, maxW, maxH, minB, sw);
                splitBlock(p, rng, {r.x, at + sw, r.w, r.z + r.h - at - sw}, maxW, maxH, minB, sw);
            }
            return;
        }
    }
    p.leaves.push_back(r);
}

Json num(int v) { return Json::number(int64_t(v)); }
Json quarter(int q) { return (q % 4) == 0 ? Json::number(int64_t(q / 4)) : Json::number(Fixed::fromRatio(q, 4)); }

} // namespace

bool generateCity(Ctx& c, const GenRequest& req, Json& map, std::string* err) {
    Plan p;
    const Json& sz = c.P["size"];
    const bool mapSize = req.params["size"].isNull() && map["size"].isArray();
    p.W = int(mapSize ? map["size"][size_t(0)].integer(96) : sz.isArray() ? sz[size_t(0)].integer(96) : 96);
    p.H = int(mapSize ? map["size"][size_t(1)].integer(72) : sz.isArray() ? sz[size_t(1)].integer(72) : 72);
    if (p.W < 32 || p.H < 32 || p.W > 512 || p.H > 512) { if (err) *err = "generate (city): size must be 32..512 cells per side"; return false; }
    const int W = p.W, H = p.H, wA = 4, wS = 2;
    Rng rng{c.seed64 ^ 0x63697479706c616eull};
    const Kit kit = findKit(c);
    const size_t n = size_t(W) * size_t(H);
    p.kind.assign(n, KLot); p.bridge.assign(n, 0); p.vr.assign(n, -1); p.hr.assign(n, -1);

    // 1. arterial grid: four cells wide, a ring along the edges
    const int spacing = std::clamp(c.pint("arterialSpacing", 26), 14, 80);
    p.xs = spread(rng, W, wA, spacing, spacing / 6);
    p.zs = spread(rng, H, wA, spacing, spacing / 6);
    for (int x : p.xs) p.roads.push_back({x, 0, H, wA, true, 0});
    for (int z : p.zs) p.roads.push_back({z, 0, W, wA, false, 0});
    const size_t arterials = p.roads.size();

    // 2. river: a meandering band inside one column of superblocks, so only east-west roads cross it
    if (permille(c, "river", 1000) > 0 && p.xs.size() >= 3 && rng.chance(permille(c, "river", 1000))) {
        const int col = std::clamp(int(p.xs.size() - 1) / 2 + rng.range(-1, 0), 0, int(p.xs.size()) - 2);
        const int x0 = p.xs[size_t(col)] + wA, cw = p.xs[size_t(col + 1)] - x0;
        const int rw = std::clamp(c.pint("riverWidth", 6), 3, 12);
        if (cw - rw >= 6) {
            p.river = true;
            int off = (cw - rw) / 2;
            p.rmin = W; p.rmax = 0;
            for (int z = 0; z < H; ++z) {
                if (z % 2 == 0) off = std::clamp(off + rng.range(-1, 1), 2, cw - rw - 2);
                for (int x = x0 + off; x < x0 + off + rw; ++x) p.kind[size_t(p.ci(x, z))] = KWater;
                p.rmin = std::min(p.rmin, x0 + off); p.rmax = std::max(p.rmax, x0 + off + rw);
            }
        }
    }

    // 3. local streets: every superblock is split until its blocks are small enough
    const int maxW = std::clamp(c.pint("blockWidth", 14), 8, 40), maxH = std::clamp(c.pint("blockDepth", 12), 8, 40);
    for (size_t j = 0; j + 1 < p.zs.size(); ++j)
        for (size_t i = 0; i + 1 < p.xs.size(); ++i)
            splitBlock(p, rng, {p.xs[i] + wA, p.zs[j] + wA, p.xs[i + 1] - p.xs[i] - wA, p.zs[j + 1] - p.zs[j] - wA}, maxW, maxH, 6, wS);

    // 4. paint: streets first, arterials over them; a road over water is a bridge
    for (int pass = 1; pass >= 0; --pass)
        for (size_t r = 0; r < p.roads.size(); ++r) {
            const Road& rd = p.roads[r];
            if (rd.cls != pass) continue;
            for (int t = rd.a; t < rd.b; ++t)
                for (int k = rd.pos; k < rd.pos + rd.w; ++k) {
                    const int x = rd.vert ? k : t, z = rd.vert ? t : k;
                    if (!p.in(x, z)) continue;
                    const size_t i = size_t(p.ci(x, z));
                    if (p.kind[i] == KWater) p.bridge[i] = 1;
                    p.kind[i] = rd.cls == 0 ? KRoad : KStreet;
                    (rd.vert ? p.vr : p.hr)[i] = int16_t(r);
                }
        }
    // 5. roundabouts at some inner arterial crossings: a 2 x 2 island in the middle of the junction
    const int roundP = permille(c, "roundabouts", 250);
    for (size_t j = 1; j + 1 < p.zs.size(); ++j)
        for (size_t i = 1; i + 1 < p.xs.size(); ++i) {
            if (!rng.chance(roundP)) continue;
            bool wet = false;
            for (int z = p.zs[j] - 2; z < p.zs[j] + wA + 2; ++z)
                for (int x = p.xs[i] - 2; x < p.xs[i] + wA + 2; ++x) wet = wet || (p.in(x, z) && (p.bridge[size_t(p.ci(x, z))] || p.kind[size_t(p.ci(x, z))] == KWater));
            if (wet) continue;
            for (int z = p.zs[j] + 1; z < p.zs[j] + 3; ++z)
                for (int x = p.xs[i] + 1; x < p.xs[i] + 3; ++x) p.kind[size_t(p.ci(x, z))] = KIsland;
            p.roundabouts.push_back({int(i), int(j)});
        }
    // 6. pavements: lot cells next to a road (corners too) and the embankment along the water
    for (int z = 0; z < H; ++z)
        for (int x = 0; x < W; ++x) {
            if (p.kind[size_t(p.ci(x, z))] != KLot) continue;
            bool near = false;
            for (int dz = -1; dz <= 1; ++dz)
                for (int dx = -1; dx <= 1; ++dx) near = near || p.road(x + dx, z + dz);
            near = near || p.at(x - 1, z) == KWater || p.at(x + 1, z) == KWater || p.at(x, z - 1) == KWater || p.at(x, z + 1) == KWater;
            if (near) p.bridge[size_t(p.ci(x, z))] = 2;      // marked first, so a new pavement does not spread
        }
    for (size_t i = 0; i < n; ++i) if (p.kind[i] == KLot && p.bridge[i] == 2) { p.kind[i] = KPave; p.bridge[i] = 0; }

    // 7. blocks: use and zone, then buildings packed in rows
    Json entities = Json::array(), blocks = Json::array();
    auto place = [&](int type, int owner, int x4, int z4, int facing) {
        Json e = Json::object();
        e.set("type", Json::string(c.R.types[size_t(type)].id));
        e.set("owner", num(owner));
        Json pos = Json::array(); pos.push(quarter(x4)); pos.push(quarter(z4));
        e.set("pos", pos);
        if (facing >= 0) e.set("facing", num(facing));
        entities.push(e);
    };
    auto pick = [&](const std::vector<int>& v) { return v[size_t(rng.range(0, int(v.size()) - 1))]; };
    const int parkP = permille(c, "parks", 150), parkingP = permille(c, "parking", 80);
    int nBuildings = 0, nParks = 0;
    static const char* kUse[3] = {"buildings", "park", "parking"};
    static const char* kZone[3] = {"residential", "commercial", "industrial"};
    for (const Rect& r : p.leaves) {
        bool wet = false;
        for (int z = r.z; z < r.z + r.h; ++z) for (int x = r.x; x < r.x + r.w; ++x) wet = wet || p.kind[size_t(p.ci(x, z))] == KWater;
        const int dx = std::abs((r.x * 2 + r.w) - W) * 100 / W, dz = std::abs((r.z * 2 + r.h) - H) * 100 / H, d = std::max(dx, dz);
        const int zone = d < 34 ? 1 : (d > 72 || wet) ? 2 : 0;
        int use = rng.chance(wet ? parkP * 3 : parkP) ? 1 : rng.chance(parkingP) ? 2 : 0;
        if (use == 0 && kit.buildings[0].empty()) use = 1;
        for (int z = r.z; z < r.z + r.h; ++z)
            for (int x = r.x; x < r.x + r.w; ++x) {
                uint8_t& k = p.kind[size_t(p.ci(x, z))];
                if (k == KLot && use) k = use == 1 ? KPark : KParking;
            }
        if (use == 1) ++nParks;
        for (int z = r.z; use == 0 && z < r.z + r.h;) {
            int rowH = 0;
            for (int x = r.x; x < r.x + r.w;) {
                std::vector<int> fit;
                for (int pass = 0; pass < 2 && fit.empty(); ++pass)
                    for (int t : kit.buildings[pass == 0 ? zone + 1 : 0]) {
                        const game::UnitType& bt = c.R.types[size_t(t)];
                        if (x + bt.footprintW > r.x + r.w || z + bt.footprintH > r.z + r.h) continue;
                        bool free = true;
                        for (int zz = z; free && zz < z + bt.footprintH; ++zz)
                            for (int xx = x; free && xx < x + bt.footprintW; ++xx) free = p.kind[size_t(p.ci(xx, zz))] == KLot;
                        if (free) fit.push_back(t);
                    }
                if (fit.empty()) { ++x; continue; }
                const int t = pick(fit);
                const game::UnitType& bt = c.R.types[size_t(t)];
                for (int zz = z; zz < z + bt.footprintH; ++zz)
                    for (int xx = x; xx < x + bt.footprintW; ++xx) p.kind[size_t(p.ci(xx, zz))] = KBuilt;
                place(t, 0, x * 4 + bt.footprintW * 2, z * 4 + bt.footprintH * 2, -1);
                ++nBuildings;
                rowH = std::max(rowH, int(bt.footprintH));
                x += bt.footprintW + (rng.chance(500) ? 1 : 0);
            }
            z += rowH > 0 ? rowH + 1 : 1;
        }
        Json b = Json::object();
        b.set("x", num(r.x)); b.set("z", num(r.z)); b.set("w", num(r.w)); b.set("h", num(r.h));
        b.set("use", Json::string(kUse[use])); b.set("zone", Json::string(kZone[zone]));
        blocks.push(b);
    }

    // 8. pavements: lamps by the kerb (arm over the road), trees, furniture, people; parks and islands get greenery
    const int treeP = permille(c, "trees", 180), propP = permille(c, "props", 60), pedP = permille(c, "people", 40);
    const int cityOwner = c.np >= 2 ? 2 : 0;
    int nPeople = 0;
    for (int z = 0; z < H; ++z)
        for (int x = 0; x < W; ++x) {
            const uint8_t k = p.kind[size_t(p.ci(x, z))];
            if (k == KPark) {
                if (!kit.trees.empty() && rng.chance(300)) place(pick(kit.trees), 0, x * 4 + rng.range(1, 3), z * 4 + rng.range(1, 3), rng.range(0, 3) * 90);
                else if (!kit.bushes.empty() && rng.chance(150)) place(pick(kit.bushes), 0, x * 4 + rng.range(1, 3), z * 4 + rng.range(1, 3), rng.range(0, 3) * 90);
                continue;
            }
            if (k != KPave) continue;
            const bool rn = p.road(x, z - 1), re = p.road(x + 1, z), rs = p.road(x, z + 1), rw = p.road(x - 1, z);
            if (rn || re || rs || rw) {
                const int facing = rn ? 90 : re ? 180 : rs ? 270 : 0;        // the model's +x (lamp arm) points at the road
                const int ox = re ? 1 : rw ? -1 : 0, oz = rs ? 1 : rn ? -1 : 0;
                if (!kit.lamps.empty() && (x + z * 3) % 5 == 0) { place(pick(kit.lamps), 0, x * 4 + 2 + ox, z * 4 + 2 + oz, facing); continue; }
                if (!kit.trees.empty() && rng.chance(treeP)) { place(pick(kit.trees), 0, x * 4 + 2 + ox, z * 4 + 2 + oz, rng.range(0, 3) * 90); continue; }
                if (!kit.props.empty() && rng.chance(propP)) { place(pick(kit.props), 0, x * 4 + 2 - ox, z * 4 + 2 - oz, (facing + 270) % 360); continue; }
            }
            if (!kit.peds.empty() && rng.chance(pedP)) { place(pick(kit.peds), cityOwner, x * 4 + 2, z * 4 + 2, rng.range(0, 3) * 90); ++nPeople; }
        }
    for (auto& [i, j] : p.roundabouts)
        if (!kit.trees.empty()) place(pick(kit.trees), 0, (p.xs[size_t(i)] + 2) * 4, (p.zs[size_t(j)] + 2) * 4, 0);

    // 9. parked cars: kerb lanes of the arterials (facing the way traffic goes, right-hand), rows in car parks
    const int carP = permille(c, "cars", 220);
    int nCars = 0;
    for (size_t r = 0; r < arterials && !kit.cars.empty(); ++r) {
        const Road& rd = p.roads[r];
        for (int side = 0; side < 2; ++side) {
            const int lane = side ? rd.pos + rd.w - 1 : rd.pos, out = side ? rd.pos + rd.w : rd.pos - 1;
            int last = -10;
            for (int t = rd.a + 1; t < rd.b - 1; ++t) {
                const int x = rd.vert ? lane : t, z = rd.vert ? t : lane, ox = rd.vert ? out : t, oz = rd.vert ? t : out;
                const size_t i = size_t(p.ci(x, z));
                if (p.bridge[i] || p.kind[i] != KRoad || (p.vr[i] >= 0 && p.hr[i] >= 0) || p.at(ox, oz) != KPave) continue;
                if (t - last < 2 || !rng.chance(carP)) continue;
                last = t;
                // east lane of a north-south road runs north, the south lane of an east-west road runs east
                place(pick(kit.cars), 0, x * 4 + 2, z * 4 + 2, rd.vert ? (side ? 0 : 180) : (side ? 90 : 270));
                ++nCars;
            }
        }
    }
    for (int z = 0; z < H && !kit.cars.empty(); ++z)
        for (int x = 0; x < W; ++x)
            if (p.kind[size_t(p.ci(x, z))] == KParking && x % 2 == 0 && rng.chance(550)) { place(pick(kit.cars), 0, x * 4 + 2, z * 4 + 2, z % 2 ? 90 : 270); ++nCars; }   // along x: two cells each (nose to tail down the column they overlap, cars are longer than a cell)

    // 10. the hero: the pavement cell nearest to the centre
    if (kit.hero >= 0) {
        int best = -1, bestD = 0;
        for (int z = 0; z < H; ++z)
            for (int x = 0; x < W; ++x) {
                if (p.kind[size_t(p.ci(x, z))] != KPave) continue;
                const int d = std::abs(x * 2 + 1 - W) + std::abs(z * 2 + 1 - H);
                if (best < 0 || d < bestD) { best = p.ci(x, z); bestD = d; }
            }
        if (best >= 0) {
            Json e = Json::object();
            e.set("type", Json::string(c.R.types[size_t(kit.hero)].id));
            e.set("owner", num(1));
            Json pos = Json::array(); pos.push(quarter((best % W) * 4 + 2)); pos.push(quarter((best / W) * 4 + 2));
            e.set("pos", pos);
            entities.itemsMut().insert(entities.itemsMut().begin(), e);
        }
    }

    // ---- the map
    Json out = Json::object();
    for (auto& [k, v] : map.members()) {
        if (k == "generate" || k == "terrain" || k == "entities" || k == "size" || k == "tiles" || k == "city" || k == "generated" || k == "name") continue;
        out.set(k, v);
    }
    if (map.has("name")) out.set("name", map["name"]);
    Json size = Json::array(); size.push(num(W)); size.push(num(H));
    out.set("size", size);
    if (!map["players"].isArray() || map["players"].size() == 0) {
        Json players = Json::array();
        const char* names[2] = {"Player", "City"};
        for (int i = 0; i < std::max(1, std::min(c.np, 2)); ++i) { Json pl = Json::object(); pl.set("name", Json::string(names[i])); pl.set("team", num(i + 1)); players.push(pl); }
        out.set("players", players);
    }
    Json rows = Json::array();
    for (int z = 0; z < H; ++z) {
        std::string s(size_t(W), '.');
        for (int x = 0; x < W; ++x) {
            const size_t i = size_t(p.ci(x, z));
            if (p.kind[i] == KWater) s[size_t(x)] = '~';
            else if (p.kind[i] == KIsland) s[size_t(x)] = '#';
        }
        rows.push(Json::string(s));
    }
    Json terrain = Json::object();
    terrain.set("rows", rows);
    out.set("terrain", terrain);
    out.set("entities", entities);

    // tiles: one character per kind (params "chars"), exact road tiles when the tileset says where they are ("tileBases")
    if (c.P["tileset"].isString()) {
        const Json& ch = c.P["chars"];
        auto chr = [&](const char* key, char def) { const std::string s = ch[key].isString() ? ch[key].str() : ""; return s.empty() ? def : s[0]; };
        const char cRoad = chr("road", 'a'), cStreet = chr("street", 's'), cPave = chr("pavement", 'p'), cLot = chr("lot", 'l'),
                   cPark = chr("park", 'g'), cWater = chr("water", 'w'), cParking = chr("parking", 'k');
        const Json& tb = c.P["tileBases"];
        auto base = [&](const std::string& key) { return tb[key].isNumber() ? int(tb[key].integer(-1)) : -1; };
        Json trows = Json::array(), fixed = Json::array();
        auto fix = [&](int x, int z, int t) { Json f = Json::array(); f.push(num(x)); f.push(num(z)); f.push(num(t)); fixed.push(f); };
        for (int z = 0; z < H; ++z) {
            std::string s(size_t(W), cLot);
            for (int x = 0; x < W; ++x) {
                const size_t i = size_t(p.ci(x, z));
                const uint8_t k = p.kind[i];
                s[size_t(x)] = k == KRoad ? cRoad : k == KStreet ? cStreet : k == KPave ? cPave : k == KWater ? cWater : k == KPark || k == KIsland ? cPark :
                               k == KParking ? cParking : cLot;
                if (!tb.isObject()) continue;
                const Road* v = p.vr[i] >= 0 ? &p.roads[size_t(p.vr[i])] : nullptr;
                const Road* h = p.hr[i] >= 0 ? &p.roads[size_t(p.hr[i])] : nullptr;
                int t = -1;
                if (k == KRoad && v && h && v->cls == 0 && h->cls == 0) {          // arterial crossing: arms that exist
                    const int mask = (h->pos > 0 ? 1 : 0) | (v->pos + v->w < W ? 2 : 0) | (h->pos + h->w < H ? 4 : 0) | (v->pos > 0 ? 8 : 0);
                    if (int b = base("x" + std::to_string(mask)); b >= 0) t = b + (z - h->pos) * 4 + (x - v->pos);
                } else if (k == KRoad && v && v->cls == 0) { if (int b = base("ns"); b >= 0) t = b + (z % 4) * 4 + (x - v->pos); }
                else if (k == KRoad && h && h->cls == 0) { if (int b = base("ew"); b >= 0) t = b + (z - h->pos) * 4 + (x % 4); }
                else if (k == KStreet && v && !h) { if (int b = base("ns2"); b >= 0) t = b + (z % 2) * 2 + (x - v->pos); }
                else if (k == KStreet && h && !v) { if (int b = base("ew2"); b >= 0) t = b + (z - h->pos) * 2 + (x % 2); }
                else {
                    const char* key = k == KStreet ? "street" : k == KPave ? "walk" : k == KWater ? "water" : k == KPark || k == KIsland ? "grass" :
                                      k == KParking ? "parking" : "lot";
                    if (int b = base(key); b >= 0) t = b + (z % 2) * 2 + (x % 2);
                }
                if (t >= 0) fix(x, z, t);
            }
            trows.push(Json::string(s));
        }
        Json tiles = Json::object();
        tiles.set("tileset", c.P["tileset"]);
        tiles.set("rows", trows);
        if (fixed.size()) tiles.set("fixed", fixed);
        tiles.set("seed", Json::number(int64_t(c.seed & 0x7fffffff)));
        out.set("tiles", tiles);
    }

    // the plan for traffic and tools: roads (cell rectangles), junctions, bridges, blocks
    Json city = Json::object(), jroads = Json::array(), junctions = Json::array(), bridges = Json::array();
    for (size_t r = 0; r < p.roads.size(); ++r) {
        const Road& rd = p.roads[r];
        Json j = Json::object();
        j.set("id", num(int(r)));
        j.set("class", Json::string(rd.cls == 0 ? "arterial" : "street"));
        j.set("axis", Json::string(rd.vert ? "ns" : "ew"));
        j.set("pos", num(rd.pos)); j.set("from", num(rd.a)); j.set("to", num(rd.b)); j.set("width", num(rd.w));
        j.set("lanes", num(rd.w));                          // one lane per cell, half each way
        jroads.push(j);
        int start = -1;                                     // bridge spans along the road
        for (int t = rd.a; t <= rd.b; ++t) {
            const bool on = t < rd.b && p.bridge[size_t(rd.vert ? p.ci(rd.pos, t) : p.ci(t, rd.pos))] == 1;
            if (on && start < 0) start = t;
            if (!on && start >= 0) {
                Json b = Json::object();
                b.set("road", num(int(r))); b.set("from", num(start)); b.set("to", num(t));
                bridges.push(b);
                start = -1;
            }
        }
    }
    auto junction = [&](int x, int z, int w, int h, int a, int b, const char* type) {
        Json j = Json::object();
        j.set("x", num(x)); j.set("z", num(z)); j.set("w", num(w)); j.set("h", num(h));
        Json rs = Json::array(); rs.push(num(a)); rs.push(num(b));
        j.set("roads", rs);
        j.set("type", Json::string(type));
        junctions.push(j);
    };
    for (size_t a = 0; a < p.roads.size(); ++a)
        for (size_t b = 0; b < p.roads.size(); ++b) {
            const Road& v = p.roads[a];
            const Road& h = p.roads[b];
            if (!v.vert || h.vert) continue;
            const bool crossV = v.a < h.pos + h.w && v.b > h.pos, crossH = h.a < v.pos + v.w && h.b > v.pos;
            if (crossV && crossH) {                         // the rectangles overlap: a crossing of two arterials
                bool round = false;
                for (auto& [i, j] : p.roundabouts) round = round || (p.xs[size_t(i)] == v.pos && p.zs[size_t(j)] == h.pos);
                const int arms = (h.pos > 0) + (h.pos + h.w < H) + (v.pos > 0) + (v.pos + v.w < W);
                junction(v.pos, h.pos, v.w, h.w, int(a), int(b), round ? "roundabout" : arms == 4 ? "cross" : arms == 3 ? "t" : "corner");
            } else if (crossH && (v.a == h.pos + h.w || v.b == h.pos)) junction(v.pos, h.pos, v.w, h.w, int(a), int(b), "t");   // v ends at h
            else if (crossV && (h.a == v.pos + v.w || h.b == v.pos)) junction(v.pos, h.pos, v.w, h.w, int(a), int(b), "t");     // h ends at v
        }
    city.set("cellMeters", c.rulesJson["driving"]["metersPerCell"].isNumber() ? c.rulesJson["driving"]["metersPerCell"] : num(4));
    city.set("traffic", Json::string("right"));
    city.set("roads", jroads);
    city.set("junctions", junctions);
    city.set("bridges", bridges);
    city.set("blocks", blocks);
    if (p.river) { Json rv = Json::object(); rv.set("from", num(p.rmin)); rv.set("to", num(p.rmax)); city.set("river", rv); }
    out.set("city", city);

    Json g = Json::object();
    g.set("preset", Json::string(req.preset));
    g.set("seed", Json::number(int64_t(req.seed & 0x7fffffffffffffffull)));
    g.set("players", num(c.np));
    g.set("roads", num(int(p.roads.size()))); g.set("junctions", num(int(junctions.size()))); g.set("bridges", num(int(bridges.size())));
    g.set("roundabouts", num(int(p.roundabouts.size()))); g.set("blocks", num(int(p.leaves.size()))); g.set("parks", num(nParks));
    g.set("buildings", num(nBuildings)); g.set("cars", num(nCars)); g.set("people", num(nPeople));
    Json warn = Json::array();
    if (kit.buildings[0].empty()) warn.push(Json::string("no structure type has the tag 'building': blocks without buildings became parks"));
    if (kit.hero < 0) warn.push(Json::string("no hero type (rules topDown.heroes or params.hero): no player start"));
    if (kit.cars.empty()) warn.push(Json::string("no vehicle types (rules driving.vehicles or the tag 'vehicle')"));
    if (warn.size()) g.set("warnings", warn);
    out.set("generated", g);
    map = std::move(out);
    return true;
}

} // namespace aiverse::gen
