// TerraWeave CPU foliage exclusion geometry shared by every AIVERSE genre.
// The authoring/collision host converts roads, buildings and water outlines
// into immutable masks; no RTS/FPS-specific dependencies or GPU work.
#pragma once

#include "engine/world/TerraWeave.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <utility>
#include <stdexcept>
#include <vector>

namespace aiverse::terraweave {

struct MaskPointCm {
    int64_t x = 0, z = 0;
};

struct BufferedRoadCm {
    MaskPointCm a{}, b{};
    int32_t halfWidthCm = 150;
};

struct ExclusionPolygonCm {
    // Simple closed polygon. First/last point need not be repeated.
    // Also covers rotated building footprints and irregular water bodies.
    std::vector<MaskPointCm> vertices;
};

class FoliageExclusionMask {
public:
    void addBuilding(RectCm rect) {
        validateRect(rect);
        const size_t index = buildings_.size();
        buildings_.push_back(rect);
        // Most collision boxes cover a handful of 64m buckets. Index
        // those locally instead of scanning every building in a streamed
        // city for each candidate tree. Very large shapes stay in one
        // overflow list to bound indexing allocations.
        const int64_t firstX = detail::floorDiv(rect.minX, kBucketCm);
        const int64_t lastX = detail::floorDiv(rect.maxX - 1, kBucketCm);
        const int64_t firstZ = detail::floorDiv(rect.minZ, kBucketCm);
        const int64_t lastZ = detail::floorDiv(rect.maxZ - 1, kBucketCm);
        const int64_t cells = (lastX - firstX + 1) * (lastZ - firstZ + 1);
        if (cells > 256) {
            broadBuildings_.push_back(index);
            return;
        }
        for (int64_t z = firstZ; z <= lastZ; ++z)
            for (int64_t x = firstX; x <= lastX; ++x)
                buildingBuckets_[{x, z}].push_back(index);
    }
    void addRoad(BufferedRoadCm road) {
        validate(road.a); validate(road.b);
        if (road.halfWidthCm < 0 || road.halfWidthCm > 100000)
            throw std::invalid_argument("TerraWeave road clearance");
        roads_.push_back(road);
    }
    void addWater(ExclusionPolygonCm water) {
        addPolygon(std::move(water), water_);
    }
    void addFootprint(ExclusionPolygonCm building) {
        addPolygon(std::move(building), footprints_);
    }

    bool excludes(int64_t x, int64_t z) const {
        validate({x, z});
        for (size_t i : broadBuildings_)
            if (buildings_[i].contains(x, z)) return true;
        const auto bucket = buildingBuckets_.find({
            detail::floorDiv(x, kBucketCm),
            detail::floorDiv(z, kBucketCm)
        });
        if (bucket != buildingBuckets_.end())
            for (size_t i : bucket->second)
                if (buildings_[i].contains(x, z)) return true;
        for (const auto& p : water_) if (insidePolygon(p.vertices, x, z)) return true;
        for (const auto& p : footprints_) if (insidePolygon(p.vertices, x, z)) return true;
        for (const BufferedRoadCm& r : roads_)
            if (insideRoad(r, x, z)) return true;
        return false;
    }
    size_t shapeCount() const {
        return buildings_.size() + roads_.size() +
               water_.size() + footprints_.size();
    }
private:
    static constexpr int64_t kBucketCm = 6400;
    static void validate(MaskPointCm p) {
        if (p.x < -kWorldLimitCm || p.x > kWorldLimitCm ||
            p.z < -kWorldLimitCm || p.z > kWorldLimitCm)
            throw std::out_of_range("TerraWeave exclusion coordinates");
    }
    static void validateRect(RectCm rect) {
        validate({rect.minX, rect.minZ});
        validate({rect.maxX, rect.maxZ});
        if (rect.maxX <= rect.minX || rect.maxZ <= rect.minZ)
            throw std::invalid_argument("TerraWeave empty building exclusion");
    }
    static void addPolygon(ExclusionPolygonCm p, std::vector<ExclusionPolygonCm>& out) {
        if (p.vertices.size() < 3 || p.vertices.size() > 128)
            throw std::invalid_argument("TerraWeave polygon vertex count");
        for (const auto& v : p.vertices) validate(v);
        out.push_back(std::move(p));
    }
    static bool insideRoad(const BufferedRoadCm& road,int64_t x,int64_t z) {
        // Long-double squared distance is presentation-only: authoritative
        // physics/harvesting use their own deterministic collision policies.
        const long double dx=static_cast<long double>(road.b.x)-road.a.x;
        const long double dz=static_cast<long double>(road.b.z)-road.a.z;
        const long double px=static_cast<long double>(x)-road.a.x;
        const long double pz=static_cast<long double>(z)-road.a.z;
        const long double length2=dx*dx+dz*dz;
        const long double t=length2>0 ?
            std::clamp((px*dx+pz*dz)/length2,0.0L,1.0L) : 0.0L;
        const long double ax=px-t*dx, az=pz-t*dz;
        return ax*ax+az*az<=
            static_cast<long double>(road.halfWidthCm)*road.halfWidthCm;
    }
    static bool insidePolygon(const std::vector<MaskPointCm>& p,int64_t x,int64_t z) {
        // Even-odd test. Include points on the boundary, avoiding narrow
        // water/shoreline slivers of trees from floating-point rounding.
        bool inside=false;
        for(size_t i=0,j=p.size()-1;i<p.size();j=i++) {
            const auto& a=p[j];const auto& b=p[i];
            const long double dx=static_cast<long double>(b.x)-a.x;
            const long double dz=static_cast<long double>(b.z)-a.z;
            const long double px=static_cast<long double>(x)-a.x;
            const long double pz=static_cast<long double>(z)-a.z;
            const long double cross=dx*pz-dz*px;
            if(cross==0 && px*dx+pz*dz>=0 &&
                px*dx+pz*dz<=dx*dx+dz*dz)return true;
            if((a.z>z)!=(b.z>z)) {
                const long double intersection =
                    a.x + (static_cast<long double>(z)-a.z) *
                    (static_cast<long double>(b.x)-a.x) /
                    (static_cast<long double>(b.z)-a.z);
                if(static_cast<long double>(x)<intersection)inside=!inside;
            }
        }
        return inside;
    }
    std::vector<RectCm> buildings_;
    std::map<std::pair<int64_t, int64_t>, std::vector<size_t>> buildingBuckets_;
    std::vector<size_t> broadBuildings_;
    std::vector<BufferedRoadCm> roads_;
    std::vector<ExclusionPolygonCm> water_, footprints_;
};

} // namespace aiverse::terraweave
