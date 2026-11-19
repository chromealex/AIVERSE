#include "engine/world/TerraWeave.h"
#include <algorithm>
#include <cassert>
#include <future>
#include <stdexcept>
#include <vector>

using namespace aiverse::terraweave;

namespace {
void checkPartitionStable() {
    TerrainSettings terrain;
    terrain.seed = 0x1234abcd;
    BiomeSettings biome;
    ScatterSettings scatterCfg;
    scatterCfg.seed = 0x34578;
    scatterCfg.spacingCm = 275;
    scatterCfg.densityPermille = 1000;
    FoliageSpecies tree;
    tree.id = 8;
    tree.biomeMask = kLandBiomes | biomeBit(Biome::Ocean);
    tree.maxSlopePermille = 100000;
    FoliageSpecies shrub = tree;
    shrub.id = 3;
    shrub.weight = 3;
    const std::vector<FoliageSpecies> species{tree, shrub};
    const RectCm full{-7300, -4100, 6100, 9800};
    auto single = scatter(terrain, biome, scatterCfg, full, species);
    assert(!single.empty());
    const auto left = scatter(terrain, biome, scatterCfg,
                              {full.minX, full.minZ, 0, full.maxZ}, species);
    const auto right = scatter(terrain, biome, scatterCfg,
                               {0, full.minZ, full.maxX, full.maxZ}, species);
    std::vector<FoliageInstance> joined = left;
    joined.insert(joined.end(), right.begin(), right.end());
    auto byKey = [](const FoliageInstance& a, const FoliageInstance& b) {
        return a.key < b.key;
    };
    std::sort(single.begin(), single.end(), byKey);
    std::sort(joined.begin(), joined.end(), byKey);
    assert(single == joined);

    std::vector<FoliageSpecies> reversed{shrub, tree};
    auto shuffled = scatter(terrain, biome, scatterCfg, full, reversed);
    std::sort(shuffled.begin(), shuffled.end(), byKey);
    assert(single == shuffled);

    const RectCm excluded{-2000, -2000, 2000, 2000};
    auto withoutCenter = scatter(terrain, biome, scatterCfg, full, species, {excluded});
    for (const auto& instance : withoutCenter)
        assert(!excluded.contains(instance.xCm, instance.zCm));
    assert(withoutCenter.size() < single.size());
    assert(scatter(terrain, biome, ScatterSettings{scatterCfg.seed, 275, 0},
                   full, species).empty());
}

void checkTerrainSeams() {
    TerrainSettings terrain;
    terrain.seed = 98765;
    BiomeSettings biome;
    const auto center = generateChunk(terrain, biome, {0, 0}, 32, 100);
    const auto east = generateChunk(terrain, biome, {1, 0}, 32, 100);
    const auto west = generateChunk(terrain, biome, {-1, 0}, 32, 100);
    const auto south = generateChunk(terrain, biome, {0, 1}, 32, 100);
    for (uint32_t i = 0; i <= 32; ++i) {
        assert(center.height(32, i) == east.height(0, i));
        assert(west.height(32, i) == center.height(0, i));
        assert(center.height(i, 32) == south.height(i, 0));
        assert(center.biomes[size_t(i) * 33 + 32] == east.biomes[size_t(i) * 33]);
    }
    const auto again = generateChunk(terrain, biome, {0, 0}, 32, 100);
    assert(center.heightsCm == again.heightsCm);
    assert(center.biomes == again.biomes);
    terrain.seed++;
    const auto changed = generateChunk(terrain, biome, {0, 0}, 32, 100);
    assert(center.heightsCm != changed.heightsCm);
}

void checkParallelPureGeneration() {
    TerrainSettings terrain;
    BiomeSettings biome;
    const std::vector<ChunkKey> keys{{-2, -3}, {2, 5}, {6, -8}, {0, 0}};
    std::vector<std::future<TerrainChunk>> jobs;
    for (const auto& key : keys)
        jobs.push_back(std::async(std::launch::async, [=] {
            return generateChunk(terrain, biome, key, 16, 200);
        }));
    for (size_t i = 0; i < keys.size(); ++i) {
        auto threaded = jobs[i].get();
        auto sequential = generateChunk(terrain, biome, keys[i], 16, 200);
        assert(threaded.heightsCm == sequential.heightsCm);
        assert(threaded.biomes == sequential.biomes);
    }
}

void checkInputValidation() {
    TerrainSettings t;
    BiomeSettings b;
    bool threw = false;
    try { t.basePeriodCm = 0; (void)heightAtCm(t, 0, 0); }
    catch (const std::invalid_argument&) { threw = true; }
    assert(threw);
    t.basePeriodCm = 6400;
    threw = false;
    try { (void)generateChunk(t, b, {0, 0}, 0, 100); }
    catch (const std::invalid_argument&) { threw = true; }
    assert(threw);
    threw = false;
    try {
        FoliageSpecies a; FoliageSpecies duplicate;
        (void)scatter(t, b, {}, {0, 0, 100, 100}, {a, duplicate});
    } catch (const std::invalid_argument&) { threw = true; }
    assert(threw);

    threw = false;
    try { (void)heightAtCm(t, std::numeric_limits<int64_t>::max(), 0); }
    catch (const std::out_of_range&) { threw = true; }
    assert(threw);

    threw = false;
    try { (void)generateChunk(t, b, {std::numeric_limits<int64_t>::max(), 0}, 32, 100); }
    catch (const std::out_of_range&) { threw = true; }
    assert(threw);

    threw = false;
    try { (void)slopePermilleAt(t, kWorldLimitCm, 0); }
    catch (const std::out_of_range&) { threw = true; }
    assert(threw);

    threw = false;
    try { (void)scatter(t, b, {}, {-kWorldLimitCm, 0, 1000, 1000}, {FoliageSpecies{}}); }
    catch (const std::out_of_range&) { threw = true; }
    assert(threw);
}
}

int main() {
    checkTerrainSeams();
    checkPartitionStable();
    checkParallelPureGeneration();
    checkInputValidation();
}
