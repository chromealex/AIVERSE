// TerraWeave: genre-independent, deterministic world generation primitives.
// CPU authoritative data only. No rendering API, global RNG, floats, or platform threads.
// The caller owns job scheduling and ECS commit order.
#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

namespace aiverse::terraweave {

// Explicit supported extent avoids undefined signed overflow on arbitrary input.
// World position is always signed integer centimetres (+/-10,000 km).
inline constexpr int64_t kWorldLimitCm = 1000000000LL;

enum class Biome : uint8_t { Ocean = 0, Tundra, Desert, Grassland, Forest, Wetland, Alpine };
// Grass is stored in aggregated patches, never scattered as one ECS plant.
enum class PlantHabit : uint8_t { Tree=0, Shrub=1, Grass=2 };
constexpr uint32_t biomeBit(Biome biome) { return 1u << static_cast<unsigned>(biome); }
constexpr uint32_t kLandBiomes = biomeBit(Biome::Tundra) | biomeBit(Biome::Desert) |
    biomeBit(Biome::Grassland) | biomeBit(Biome::Forest) |
    biomeBit(Biome::Wetland) | biomeBit(Biome::Alpine);

struct TerrainSettings {
    uint64_t seed = 1;
    int32_t baseHeightCm = 0;
    int32_t reliefCm = 6000;
    int32_t basePeriodCm = 6400;
    uint32_t octaves = 5;  // 1..12
};

struct BiomeSettings {
    uint64_t seed = 2;
    int32_t seaLevelCm = -2500;
    int32_t baseTemperature = 600;  // 0..1000
    int32_t baseHumidity = 550;     // 0..1000
    int32_t climatePeriodCm = 32000;
    int32_t variation = 320;        // 0..1000
    int32_t lapsePer100m = 45;      // temperature drop / 100 m
};

struct ClimateSample {
    int32_t temperature = 0;
    int32_t humidity = 0;
    Biome biome = Biome::Grassland;
};

struct ChunkKey {
    int64_t x = 0, z = 0;
    constexpr bool operator==(const ChunkKey&) const = default;
};

struct TerrainChunk {
    ChunkKey key{};
    uint32_t resolution = 0;    // quads per side
    int32_t spacingCm = 0;
    std::vector<int32_t> heightsCm; // (resolution + 1)^2, row-major
    std::vector<Biome> biomes;
    int32_t height(uint32_t x, uint32_t z) const {
        if (x > resolution || z > resolution) throw std::out_of_range("TerraWeave chunk sample");
        return heightsCm[size_t(z) * (resolution + 1u) + x];
    }
};

struct RectCm {
    int64_t minX = 0, minZ = 0, maxX = 0, maxZ = 0; // half-open
    constexpr bool contains(int64_t x, int64_t z) const {
        return x >= minX && x < maxX && z >= minZ && z < maxZ;
    }
};

struct FoliageSpecies {
    uint32_t id = 0;                  // stable prototype id, not an ECS entity
    uint32_t biomeMask = kLandBiomes;
    uint32_t weight = 1;
    int32_t minHeightCm = std::numeric_limits<int32_t>::min();
    int32_t maxHeightCm = std::numeric_limits<int32_t>::max();
    int32_t maxSlopePermille = 800;   // rise / run * 1000
    uint32_t scaleMinPermille = 850;
    uint32_t scaleMaxPermille = 1150;
    PlantHabit habit = PlantHabit::Tree;
    // 0..1e12 sim ticks, deterministic per-placement age selection.
    uint64_t minAgeTicks = 0;
    uint64_t maxAgeTicks = 0;
    uint64_t matureTicks = 0;      // 0: already mature (old behavior)
    uint64_t lifespanTicks = 0;    // 0: indefinite
    uint32_t lightPermille = 1000;
    uint32_t waterPermille = 1000;
    uint32_t nutrientsPermille = 1000;
};

struct ScatterSettings {
    uint64_t seed = 3;
    int32_t spacingCm = 450;          // at most one instance per global lattice cell
    uint32_t densityPermille = 850;   // 0..1000
};

struct FoliageInstance {
    uint64_t key = 0;                 // stable across chunking, useful for persistence
    uint32_t speciesId = 0;
    int64_t xCm = 0, zCm = 0;
    int32_t heightCm = 0;
    uint32_t yawMilliDegrees = 0;     // 0..359999
    uint32_t scalePermille = 1000;
    // Seeds/ages are independent of chunk boundaries, draw LOD and order.
    // Individual grass blades are instead derived from a GrassPatch.
    uint64_t ageTicks = 0;
    uint64_t matureTicks = 0;
    uint64_t lifespanTicks = 0;
    uint32_t lightPermille = 1000;
    uint32_t waterPermille = 1000;
    uint32_t nutrientsPermille = 1000;
    PlantHabit habit = PlantHabit::Tree;
    uint32_t foliagePermille = 1000; // derived presentation canopy density
    constexpr bool operator==(const FoliageInstance&) const = default;
};

// ECS-ready patch descriptors: thousands of cosmetic blades/leaves remain in GPU buffers.
struct TerrainChunkComponent {
    ChunkKey key{};
    uint64_t revision = 0;
};
struct VegetationPatchComponent {
    ChunkKey key{};
    uint64_t generation = 0;
    uint32_t instanceCount = 0;
};

namespace detail {
inline uint64_t mix64(uint64_t x) {
    x ^= x >> 30; x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27; x *= 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}
inline uint64_t hash2(int64_t x, int64_t z, uint64_t seed) {
    return mix64(seed ^ mix64(static_cast<uint64_t>(x) + 0x9e3779b97f4a7c15ULL) ^
                 mix64(static_cast<uint64_t>(z) + 0xd1b54a32d192ed03ULL));
}
inline int64_t floorDiv(int64_t a, int64_t b) {
    const int64_t q = a / b, r = a % b;
    return q - (r < 0 ? 1 : 0); // b must be positive
}
inline int32_t clamp1000(int64_t x) {
    return static_cast<int32_t>(std::clamp<int64_t>(x, 0, 1000));
}
inline int64_t qmul(int64_t a, int64_t b) { return (a * b) / 65536; }
inline int64_t smooth(int64_t t) {
    return qmul(qmul(t, t), 3 * 65536 - 2 * t);
}
inline int64_t lerp(int64_t a, int64_t b, int64_t t) {
    return a + qmul(b - a, t);
}
// Signed Q16 value noise, sampled in world centimetres. No compiler-specific signed shifts.
inline int64_t noise(int64_t x, int64_t z, int32_t period, uint64_t seed) {
    const int64_t gx = floorDiv(x, period), gz = floorDiv(z, period);
    const int64_t u = smooth((x - gx * period) * 65536 / period);
    const int64_t v = smooth((z - gz * period) * 65536 / period);
    const auto at = [seed](int64_t a, int64_t b) -> int64_t {
        return static_cast<int64_t>(hash2(a, b, seed) & 65535ULL) * 2 - 65535;
    };
    return lerp(lerp(at(gx, gz), at(gx + 1, gz), u),
                lerp(at(gx, gz + 1), at(gx + 1, gz + 1), u), v);
}
inline int32_t clamp32(int64_t n) {
    return static_cast<int32_t>(std::clamp<int64_t>(n,
        std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::max()));
}
} // namespace detail

// Independent world-coordinate evaluation means chunk borders agree exactly.
inline int32_t heightAtCm(const TerrainSettings& s, int64_t xCm, int64_t zCm) {
    if (s.basePeriodCm <= 0 || s.octaves == 0 || s.octaves > 12 || s.reliefCm < 0)
        throw std::invalid_argument("TerraWeave terrain settings");
    if (xCm < -kWorldLimitCm || xCm > kWorldLimitCm ||
        zCm < -kWorldLimitCm || zCm > kWorldLimitCm)
        throw std::out_of_range("TerraWeave terrain world extent");
    int64_t sum = 0, totalWeight = 0, weight = 65536;
    int32_t period = s.basePeriodCm;
    for (uint32_t i = 0; i < s.octaves; ++i) {
        sum += detail::qmul(detail::noise(xCm, zCm, period, s.seed + i * 7919ULL), weight);
        totalWeight += weight;
        weight = std::max<int64_t>(1, weight / 2);
        period = std::max<int32_t>(1, period / 2);
    }
    return detail::clamp32(static_cast<int64_t>(s.baseHeightCm) +
                           static_cast<int64_t>(s.reliefCm) * sum / totalWeight);
}

inline ClimateSample climateAt(const TerrainSettings& t, const BiomeSettings& b,
                               int64_t xCm, int64_t zCm) {
    if (b.climatePeriodCm <= 0 || b.variation < 0)
        throw std::invalid_argument("TerraWeave biome settings");
    const int32_t h = heightAtCm(t, xCm, zCm);
    const int64_t temperature = static_cast<int64_t>(b.baseTemperature) +
        detail::noise(xCm, zCm, b.climatePeriodCm, b.seed) * b.variation / 65536 -
        (std::max<int64_t>(0, static_cast<int64_t>(h) - b.seaLevelCm) * b.lapsePer100m / 10000);
    const int64_t humidity = static_cast<int64_t>(b.baseHumidity) +
        detail::noise(xCm, zCm, b.climatePeriodCm, b.seed ^ 0xf19c54d1ULL) * b.variation / 65536;
    ClimateSample out{detail::clamp1000(temperature), detail::clamp1000(humidity), Biome::Grassland};
    if (h < b.seaLevelCm) out.biome = Biome::Ocean;
    else if (h - static_cast<int64_t>(b.seaLevelCm) > 300000) out.biome = Biome::Alpine;
    else if (out.temperature < 250) out.biome = Biome::Tundra;
    else if (out.humidity > 750) out.biome = Biome::Wetland;
    else if (out.temperature > 650 && out.humidity < 320) out.biome = Biome::Desert;
    else if (out.humidity < 440) out.biome = Biome::Grassland;
    else out.biome = Biome::Forest;
    return out;
}

// Pure function; safe to schedule per chunk on independent worker threads.
inline TerrainChunk generateChunk(const TerrainSettings& t, const BiomeSettings& b,
                                  ChunkKey key, uint32_t resolution = 32,
                                  int32_t spacingCm = 100) {
    if (resolution == 0 || resolution > 1024 || spacingCm <= 0)
        throw std::invalid_argument("TerraWeave chunk settings");
    const int64_t spanCm = int64_t(resolution) * spacingCm;
    if (spanCm > kWorldLimitCm)
        throw std::out_of_range("TerraWeave chunk span");
    const int64_t maxIndex = (kWorldLimitCm - spanCm) / spanCm;
    if (key.x < -maxIndex || key.x > maxIndex ||
        key.z < -maxIndex || key.z > maxIndex)
        throw std::out_of_range("TerraWeave chunk world extent");
    TerrainChunk out;
    out.key = key; out.resolution = resolution; out.spacingCm = spacingCm;
    const size_t side = size_t(resolution) + 1;
    out.heightsCm.resize(side * side);
    out.biomes.resize(side * side);
    for (uint32_t z = 0; z <= resolution; ++z)
        for (uint32_t x = 0; x <= resolution; ++x) {
            const int64_t wx = (key.x * resolution + x) * spacingCm;
            const int64_t wz = (key.z * resolution + z) * spacingCm;
            const size_t index = size_t(z) * side + x;
            out.heightsCm[index] = heightAtCm(t, wx, wz);
            out.biomes[index] = climateAt(t, b, wx, wz).biome;
        }
    return out;
}

inline int32_t slopePermilleAt(const TerrainSettings& t, int64_t x, int64_t z,
                              int32_t stepCm = 100) {
    if (stepCm <= 0) throw std::invalid_argument("TerraWeave slope step");
    if (stepCm > kWorldLimitCm ||
        x < -kWorldLimitCm + stepCm || x > kWorldLimitCm - stepCm ||
        z < -kWorldLimitCm + stepCm || z > kWorldLimitCm - stepCm)
        throw std::out_of_range("TerraWeave slope world extent");
    const int64_t dx = static_cast<int64_t>(heightAtCm(t, x + stepCm, z)) -
                       heightAtCm(t, x - stepCm, z);
    const int64_t dz = static_cast<int64_t>(heightAtCm(t, x, z + stepCm)) -
                       heightAtCm(t, x, z - stepCm);
    const int64_t absX = dx < 0 ? -dx : dx, absZ = dz < 0 ? -dz : dz;
    return detail::clamp32(std::max(absX, absZ) * 1000 / (2 * stepCm));
}

// Globally keyed jitter lattice. The same world is generated for ANY partition of bounds.
// FoliageSpecies order is irrelevant; ids must be unique. Placement exclusions are world-space.
inline std::vector<FoliageInstance> scatter(const TerrainSettings& t, const BiomeSettings& b,
        const ScatterSettings& cfg, RectCm bounds,
        const std::vector<FoliageSpecies>& species,
        const std::vector<RectCm>& exclusions = {}) {
    if (cfg.spacingCm <= 0 || cfg.densityPermille > 1000 ||
        bounds.maxX < bounds.minX || bounds.maxZ < bounds.minZ)
        throw std::invalid_argument("TerraWeave scatter settings");
    // Slopes sample +/-100 cm around each point: leave this border clear.
    if (bounds.minX < -kWorldLimitCm + 100 ||
        bounds.minZ < -kWorldLimitCm + 100 ||
        bounds.maxX > kWorldLimitCm - 100 ||
        bounds.maxZ > kWorldLimitCm - 100)
        throw std::out_of_range("TerraWeave scatter world extent");
    if (bounds.maxX == bounds.minX || bounds.maxZ == bounds.minZ ||
        cfg.densityPermille == 0 || species.empty()) return {};

    std::vector<const FoliageSpecies*> sorted;
    sorted.reserve(species.size());
    for (const auto& s : species) {
        if (!s.weight || s.scaleMinPermille > s.scaleMaxPermille ||
            s.habit>PlantHabit::Grass || s.habit==PlantHabit::Grass ||
            s.maxAgeTicks<s.minAgeTicks ||
            s.maxAgeTicks>1000000000000ULL ||
            s.matureTicks>1000000000000ULL ||
            s.lifespanTicks>1000000000000ULL ||
            s.lightPermille>1000u || s.waterPermille>1000u ||
            s.nutrientsPermille>1000u)
            throw std::invalid_argument("TerraWeave species settings (grass uses patches)");
        sorted.push_back(&s);
    }
    std::sort(sorted.begin(), sorted.end(), [](const auto* a, const auto* b) {
        return a->id < b->id;
    });
    for (size_t i = 1; i < sorted.size(); ++i)
        if (sorted[i - 1]->id == sorted[i]->id)
            throw std::invalid_argument("TerraWeave duplicate species id");

    const int64_t minCX = detail::floorDiv(bounds.minX, cfg.spacingCm);
    const int64_t maxCX = detail::floorDiv(bounds.maxX - 1, cfg.spacingCm);
    const int64_t minCZ = detail::floorDiv(bounds.minZ, cfg.spacingCm);
    const int64_t maxCZ = detail::floorDiv(bounds.maxZ - 1, cfg.spacingCm);
    if (maxCX - minCX > 1000000 || maxCZ - minCZ > 1000000 ||
        (maxCX - minCX + 1) * (maxCZ - minCZ + 1) > 1000000)
        throw std::invalid_argument("TerraWeave scatter region too large");

    std::vector<FoliageInstance> out;
    std::vector<const FoliageSpecies*> eligible;
    for (int64_t cz = minCZ; cz <= maxCZ; ++cz)
        for (int64_t cx = minCX; cx <= maxCX; ++cx) {
            const uint64_t h = detail::hash2(cx, cz, cfg.seed);
            const int64_t x = cx * cfg.spacingCm + cfg.spacingCm *
                (250 + static_cast<int64_t>(h & 0xffffULL) * 500 / 65535) / 1000;
            const int64_t z = cz * cfg.spacingCm + cfg.spacingCm *
                (250 + static_cast<int64_t>((h >> 16) & 0xffffULL) * 500 / 65535) / 1000;
            if (!bounds.contains(x, z) || (detail::mix64(h) % 1000) >= cfg.densityPermille)
                continue;
            bool excluded = false;
            for (const RectCm& rect : exclusions)
                if (rect.contains(x, z)) { excluded = true; break; }
            if (excluded) continue;
            const int32_t height = heightAtCm(t, x, z);
            const Biome biome = climateAt(t, b, x, z).biome;
            const int32_t slope = slopePermilleAt(t, x, z);
            uint64_t weightSum = 0;
            eligible.clear();
            for (const FoliageSpecies* s : sorted) {
                if (!(s->biomeMask & biomeBit(biome)) ||
                    height < s->minHeightCm || height > s->maxHeightCm ||
                    slope > s->maxSlopePermille) continue;
                eligible.push_back(s);
                weightSum += s->weight;
            }
            if (!weightSum) continue;
            uint64_t pick = detail::mix64(h ^ 0x55ad31dbd6a80fefULL) % weightSum;
            const FoliageSpecies* chosen = nullptr;
            for (const auto* s : eligible) {
                if (pick < s->weight) { chosen = s; break; }
                pick -= s->weight;
            }
            if (!chosen) throw std::logic_error("TerraWeave scatter selection");
            const uint64_t variation = detail::mix64(h ^ 0x96325a3b789abd12ULL);
            const uint32_t span = chosen->scaleMaxPermille - chosen->scaleMinPermille;
            const uint64_t ageSpan=chosen->maxAgeTicks-chosen->minAgeTicks;
            const uint64_t age=chosen->minAgeTicks+
                (ageSpan?detail::mix64(variation^0x39edce154c88aa71ULL)%
                (ageSpan+1):0ULL);
            out.push_back({h, chosen->id, x, z, height,
                static_cast<uint32_t>(variation % 360000ULL),
                chosen->scaleMinPermille + static_cast<uint32_t>((variation >> 20) % (static_cast<uint64_t>(span) + 1)),
                age,chosen->matureTicks,chosen->lifespanTicks,
                chosen->lightPermille,chosen->waterPermille,
                chosen->nutrientsPermille,chosen->habit});
        }
    return out;
}

} // namespace aiverse::terraweave
