// Authored erosion zones -> NEREID bank erosion through the TerraWeave
// Earthworks bridge: a zone with coefficient 3 erodes ~3x faster than the
// default bank, a rock zone does not erode, deterministic replay.
#include "engine/world/TerraWeaveNereidErosionBridge.h"
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

using namespace aiverse;
using namespace aiverse::terraweave;

namespace {
std::vector<NereidBankProbe> probes() {
    std::vector<NereidBankProbe> out;
    for (int64_t x : {0, 5, 10}) {
        NereidBankProbe p;
        p.vertex = {x, 0};
        p.soil = lib::nereid::ShoreSoil::Sand;
        p.water.found = true;
        p.water.flow = {Fixed::fromRatio(1, 2), Fixed{}, Fixed{}};   // 0.5 m/s: below the 8 cm/tick cut cap even at x3
        p.wetContactPermille = 1000;
        p.slopePermille = 500;
        out.push_back(p);
    }
    return out;
}

std::vector<int32_t> run(const ErosionZoneSet& zones, int ticks) {
    EarthworksLayer earth(100, [](int64_t, int64_t) { return 0; }, -500);
    NereidEarthworksBridge bridge(earth);
    for (int t = 1; t <= ticks; ++t) {
        auto input = probes();
        applyErosionZones(input, zones, earth.spacingCm());
        bridge.step(input, uint64_t(t), 1000);
    }
    return {earth.heightAt({0, 0}), earth.heightAt({5, 0}), earth.heightAt({10, 0})};
}
} // namespace

int main() {
    const auto zones = ErosionZoneSet::parse(Json::parse(R"([
        {"id":"soft-bank","shape":"circle","x":5,"z":0,"radius":1.5,"soil":"sand","coefficient":3},
        {"id":"ledge","shape":"polygon","points":[[9,-1],[11,-1],[11,1],[9,1]],"soil":"rock","coefficient":1}
    ])"));
    assert(zones.zones().size() == 2);
    assert(zones.at(500, 0).inside && zones.at(500, 0).ratePermille == 3000);
    assert(zones.at(1000, 0).inside && *zones.at(1000, 0).soil == lib::nereid::ShoreSoil::Rock);
    assert(!zones.at(0, 0).inside && zones.at(0, 0).ratePermille == 1000);
    assert(!zones.at(1200, 0).inside);   // outside the polygon

    const auto plain = run(ErosionZoneSet{}, 40);
    const auto zoned = run(zones, 40);
    std::printf("erosion cm after 40 s: default %d, zone x3 %d, rock %d (no zones: %d %d %d)\n",
                zoned[0], zoned[1], zoned[2], plain[0], plain[1], plain[2]);
    // Default bank erodes; the zone is 3x deeper (+-1 cm of integer carry);
    // rock stays put; without zones all three behave the same.
    assert(plain[0] < 0 && plain[0] == plain[1] && plain[1] == plain[2]);
    assert(zoned[0] == plain[0]);
    assert(zoned[1] < zoned[0]);
    assert(std::abs(zoned[1] - 3 * zoned[0]) <= 2 || zoned[1] == -500);
    assert(zoned[2] == 0);
    // Deterministic replay.
    assert(run(zones, 40) == zoned);

    bool bad = false;
    try { (void)ErosionZoneSet::parse(Json::parse(R"([{"shape":"circle","x":0,"z":0,"radius":1,"coefficient":11}])")); }
    catch (const std::exception&) { bad = true; }
    assert(bad);
    bad = false;
    try { (void)ErosionZoneSet::parse(Json::parse(R"([{"shape":"polygon","points":[[0,0],[1,1]]}])")); }
    catch (const std::exception&) { bad = true; }
    assert(bad);
    std::printf("terraweave erosion zones: PASS\n");
    return 0;
}
