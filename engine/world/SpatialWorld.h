#pragma once
#include "engine/core/Fixed.h"
#include <map>
#include <set>
#include <vector>
#include <functional>

namespace aiverse::world {
struct Cell {
    int32_t x=0, y=0, z=0;
    auto operator<=>(const Cell&) const = default;
};
struct Triangle { Vec3 a,b,c; };
struct Bounds { Vec3 min{}, max{}; };
bool overlaps(const Bounds& a, const Bounds& b);
// Segment slab query, including starting inside; t is a fraction in [0,1].
bool intersect(Vec3 start, Vec3 delta, const Bounds& box, Fixed& t, Vec3& normal);
struct Collider { uint32_t id=0; Bounds bounds; };
struct Hit { bool hit=false; uint32_t id=0; Fixed fraction=1_fx; Vec3 normal{}; };
// Sparse cells; very large colliders use a bounded overflow list instead of filling millions of cells.
class SpatialWorld {
public:
    explicit SpatialWorld(int cellSize=32): cellSize_(cellSize>0?cellSize:32) {}
    bool add(Collider collider);
    bool addTriangle(uint32_t id,Triangle triangle);
    Hit castCollider(uint32_t id,Vec3 start,Vec3 delta,Vec3 halfExtent={}) const;
    void remove(uint32_t id);
    void clear();
    std::vector<uint32_t> query(Bounds bounds) const;
    bool blocked(Bounds bounds)const;
    Hit cast(Vec3 start, Vec3 delta, Vec3 halfExtent={}) const;
    const std::map<uint32_t,Collider>& colliders() const { return colliders_; }
    Cell cell(Vec3 point) const;
private:
    std::vector<Cell> cells(Bounds bounds) const;
    int cellSize_;
    std::map<uint32_t,Collider> colliders_;
    std::map<Cell,std::set<uint32_t>> index_;
    std::set<uint32_t> oversized_;
    std::map<uint32_t,Triangle> triangles_;
};
// Presentation origin only: subtract in Fixed before converting to float. Simulation never rebases.
Vec3 relativeTo(Vec3 absolute, Cell origin, int cellSize);

// Host-side streaming scheduler. A failed load remains missing; required collision must be resident
// before simulation is allowed to enter a cell. Budget is per pump, all observers share residency.
class Streamer {
public:
    using Load = std::function<bool(Cell)>;
    using Unload = std::function<void(Cell)>;
    void desire(std::set<Cell> cells) { desired_=std::move(cells); }
    void pump(size_t budget, const Load& load, const Unload& unload);
    bool resident(Cell c) const { return resident_.contains(c); }
    const std::set<Cell>& loaded() const { return resident_; }
    bool ready() const;
private:
    std::set<Cell> desired_, resident_;
};
}
