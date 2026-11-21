// Read-only coverage of the map's authored NEREID water (entity components
// NereidWater: river splines [x,z,halfWidth], boxes, discs) for editor
// presentation hosts: the PHYSIS nature preview keeps grass and fuel out of
// rivers and wets their banks. Not a water simulation.
#pragma once
#include "apps/editor/core/MapDoc.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace aiverse::editor {

class AuthoredWaterQuery {
public:
    static AuthoredWaterQuery fromDoc(const MapDoc& doc) {
        AuthoredWaterQuery q;
        for (const auto& e : doc.data.entities) {
            const Json& w = e.extra["components"]["NereidWater"];
            if (!w.isObject()) continue;
            Body b;
            b.x = e.x; b.z = e.z;
            const std::string shape = w.getStr("shape", w.getStr("kind", "river"));
            const Json& pts = w["points"];
            if (pts.isArray() && pts.size() >= 2) {
                b.kind = Body::River;
                for (const auto& p : pts.items())
                    if (p.isArray() && p.size() >= 2)
                        b.points.push_back({e.x + float(p[0].toDouble(0)), e.z + float(p[1].toDouble(0)),
                                            p.size() > 2 ? float(p[2].toDouble(2)) : 2.f});
            } else if (w["halfExtents"].isArray() && w["halfExtents"].size() == 2) {
                b.kind = Body::Box;
                b.hx = float(w["halfExtents"][0].toDouble(0)); b.hz = float(w["halfExtents"][1].toDouble(0));
            } else if (w.has("radius")) {
                b.kind = Body::Disc;
                b.hx = float(w["radius"].toDouble(0));
            } else continue;
            for (const auto& p : b.points) { q.minX_ = std::min(q.minX_, p.x - p.h); q.maxX_ = std::max(q.maxX_, p.x + p.h);
                                             q.minZ_ = std::min(q.minZ_, p.z - p.h); q.maxZ_ = std::max(q.maxZ_, p.z + p.h); }
            if (b.kind != Body::River) { q.minX_ = std::min(q.minX_, b.x - std::max(b.hx, b.hz)); q.maxX_ = std::max(q.maxX_, b.x + std::max(b.hx, b.hz));
                                         q.minZ_ = std::min(q.minZ_, b.z - std::max(b.hx, b.hz)); q.maxZ_ = std::max(q.maxZ_, b.z + std::max(b.hx, b.hz)); }
            q.bodies_.push_back(std::move(b));
            q.revision_ = q.revision_ * 1099511628211ull ^ std::hash<std::string>{}(w.dump(-1)) ^ std::uint64_t(e.x * 100) ^ (std::uint64_t(e.z * 100) << 20);
        }
        return q;
    }
    bool empty() const { return bodies_.empty(); }
    std::uint64_t revision() const { return revision_; }
    // Signed distance outside the nearest water edge in metres (<= 0 inside).
    float edgeDistance(float x, float z) const {
        if (bodies_.empty() || x < minX_ - 64 || x > maxX_ + 64 || z < minZ_ - 64 || z > maxZ_ + 64) return 1e9f;
        float best = 1e9f;
        for (const auto& b : bodies_) {
            if (b.kind == Body::River) {
                for (size_t i = 0; i + 1 < b.points.size(); ++i) {
                    const auto& a = b.points[i]; const auto& c = b.points[i + 1];
                    const float dx = c.x - a.x, dz = c.z - a.z, l2 = dx * dx + dz * dz;
                    const float t = l2 > 0 ? std::clamp(((x - a.x) * dx + (z - a.z) * dz) / l2, 0.f, 1.f) : 0.f;
                    const float px = a.x + dx * t, pz = a.z + dz * t;
                    const float half = a.h + (c.h - a.h) * t;
                    best = std::min(best, std::hypot(x - px, z - pz) - half);
                }
            } else if (b.kind == Body::Box) {
                const float qx = std::fabs(x - b.x) - b.hx, qz = std::fabs(z - b.z) - b.hz;
                best = std::min(best, std::hypot(std::max(qx, 0.f), std::max(qz, 0.f)) + std::min(std::max(qx, qz), 0.f));
            } else {
                best = std::min(best, std::hypot(x - b.x, z - b.z) - b.hx);
            }
        }
        return best;
    }
    bool inside(float x, float z) const { return edgeDistance(x, z) <= 0.f; }

private:
    struct Point { float x, z, h; };
    struct Body { enum Kind { River, Box, Disc } kind = River; float x = 0, z = 0, hx = 0, hz = 0; std::vector<Point> points; };
    std::vector<Body> bodies_;
    float minX_ = 1e9f, maxX_ = -1e9f, minZ_ = 1e9f, maxZ_ = -1e9f;
    std::uint64_t revision_ = 1469598103934665603ull;
};

} // namespace aiverse::editor
