// Authored erosion zones: physical areas where the ground erodes with a given
// coefficient (x erodibility) and optional soil class. Pure deterministic
// integer data, shared by the editor (authoring + gizmos) and the native
// NEREID -> TerraWeave bank bridge (NereidBankProbe::soil / ratePermille).
//
// JSON (map "erosionZones", metres):
//   [{"id":"bank-1","shape":"circle","x":260,"z":205,"radius":12,
//     "soil":"sand","coefficient":2.5},
//    {"id":"rock-ledge","shape":"polygon","points":[[x,z],...],
//     "soil":"rock","coefficient":0}]
// coefficient 0..10 (1 = default rate), soil sand|silt|loam|clay|gravel|rock
// (omitted = keep the probe's soil). Later zones override earlier ones.
#pragma once
#include "engine/data/Json.h"
#include "engine/world/nereid/NereidErosion.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace aiverse::terraweave {

struct ErosionZone {
    std::string id;
    bool polygon = false;
    int64_t xCm = 0, zCm = 0, radiusCm = 0;               // circle
    std::vector<std::pair<int64_t, int64_t>> pointsCm;    // polygon, >= 3
    std::optional<lib::nereid::ShoreSoil> soil;
    uint32_t ratePermille = 1000;                          // coefficient x 1000, 0..10000
};

struct ErosionZoneHit {
    bool inside = false;
    std::optional<lib::nereid::ShoreSoil> soil;
    uint32_t ratePermille = 1000;
};

inline const char* erosionSoilName(lib::nereid::ShoreSoil soil) {
    using lib::nereid::ShoreSoil;
    switch (soil) {
        case ShoreSoil::Sand: return "sand";
        case ShoreSoil::Silt: return "silt";
        case ShoreSoil::Loam: return "loam";
        case ShoreSoil::Clay: return "clay";
        case ShoreSoil::Gravel: return "gravel";
        case ShoreSoil::Rock: return "rock";
    }
    return "loam";
}
inline std::optional<lib::nereid::ShoreSoil> erosionSoilFromName(const std::string& name) {
    using lib::nereid::ShoreSoil;
    if (name == "sand") return ShoreSoil::Sand;
    if (name == "silt") return ShoreSoil::Silt;
    if (name == "loam") return ShoreSoil::Loam;
    if (name == "clay") return ShoreSoil::Clay;
    if (name == "gravel") return ShoreSoil::Gravel;
    if (name == "rock") return ShoreSoil::Rock;
    return std::nullopt;
}

class ErosionZoneSet {
public:
    static constexpr size_t kMaxZones = 1024;
    static constexpr size_t kMaxPoints = 256;

    static ErosionZoneSet parse(const Json& zones) {
        ErosionZoneSet out;
        if (zones.isNull()) return out;
        if (!zones.isArray() || zones.size() > kMaxZones)
            throw JsonError("erosionZones: expected an array of at most 1024 zones");
        const auto cm = [](const Json& v, const char* what) {
            if (!v.isNumber()) throw JsonError(std::string("erosionZones: ") + what + " must be a number");
            const double m = v.toDouble();
            if (!std::isfinite(m) || std::fabs(m) > 1.0e7) throw JsonError(std::string("erosionZones: ") + what + " out of range");
            return int64_t(std::llround(m * 100.0));
        };
        for (const auto& z : zones.items()) {
            if (!z.isObject()) throw JsonError("erosionZones: zone must be an object");
            ErosionZone zone;
            zone.id = z.getStr("id", "");
            const std::string shape = z.getStr("shape", z.has("points") ? "polygon" : "circle");
            if (shape == "circle") {
                zone.xCm = cm(z["x"], "x"); zone.zCm = cm(z["z"], "z");
                zone.radiusCm = cm(z["radius"], "radius");
                if (zone.radiusCm <= 0) throw JsonError("erosionZones: radius must be positive");
            } else if (shape == "polygon") {
                zone.polygon = true;
                const Json& pts = z["points"];
                if (!pts.isArray() || pts.size() < 3 || pts.size() > kMaxPoints)
                    throw JsonError("erosionZones: polygon needs 3..256 [x,z] points");
                for (const auto& p : pts.items()) {
                    if (!p.isArray() || p.size() != 2) throw JsonError("erosionZones: point must be [x,z]");
                    zone.pointsCm.push_back({cm(p[0], "point.x"), cm(p[1], "point.z")});
                }
            } else throw JsonError("erosionZones: shape must be circle or polygon");
            if (z.has("soil")) {
                zone.soil = erosionSoilFromName(z.getStr("soil"));
                if (!zone.soil) throw JsonError("erosionZones: soil must be sand|silt|loam|clay|gravel|rock");
            }
            const double k = z["coefficient"].isNumber() ? z["coefficient"].toDouble() : 1.0;
            if (!std::isfinite(k) || k < 0 || k > 10) throw JsonError("erosionZones: coefficient must be 0..10");
            zone.ratePermille = uint32_t(std::llround(k * 1000.0));
            out.zones_.push_back(std::move(zone));
        }
        return out;
    }

    static bool contains(const ErosionZone& z, int64_t x, int64_t zz) {
        if (!z.polygon) {
            const int64_t dx = x - z.xCm, dz = zz - z.zCm;
            // int64 is safe: coordinates are bounded to 1e9 cm.
            return dx * dx + dz * dz <= z.radiusCm * z.radiusCm;
        }
        bool in = false;
        const auto& p = z.pointsCm;
        for (size_t i = 0, j = p.size() - 1; i < p.size(); j = i++) {
            const auto [xi, zi] = p[i];
            const auto [xj, zj] = p[j];
            if ((zi > zz) != (zj > zz)) {
                // x < xi + (zz-zi)*(xj-xi)/(zj-zi), exact: |coords| <= 1e9 cm so products <= 4e18
                const int64_t lhs = (x - xi) * (zj - zi);
                const int64_t rhs = (zz - zi) * (xj - xi);
                if (zj > zi ? lhs < rhs : lhs > rhs) in = !in;
            }
        }
        return in;
    }

    ErosionZoneHit at(int64_t xCm, int64_t zCm) const {
        ErosionZoneHit hit;
        for (const auto& z : zones_) {
            if (!contains(z, xCm, zCm)) continue;
            hit.inside = true;
            hit.ratePermille = z.ratePermille;
            if (z.soil) hit.soil = z.soil;
        }
        return hit;
    }

    const std::vector<ErosionZone>& zones() const { return zones_; }
    bool empty() const { return zones_.empty(); }

private:
    std::vector<ErosionZone> zones_;
};

} // namespace aiverse::terraweave
