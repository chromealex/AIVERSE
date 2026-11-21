// VIVID scalable groom cards, deterministic guide topology and local
// presentation-only secondary motion. Supports mobile card LOD fallback.
#pragma once
#include "HumanDNA.h"
#include "HumanMaterials.h"
#include "engine/render/Model.h"
#include <cstdint>
#include <memory>
#include <utility>
#include <cstddef>
#include <span>
#include <vector>

namespace aiverse::vivid {
struct GroomGuide {
    render::float3 root{}, normal{};
    // Optional triangle/barycentric attachment to a deforming scalp proxy.
    std::uint32_t scalpTriangle=UINT32_MAX;
    render::float3 scalpBary{1,0,0};
    std::vector<render::float3> rest, position, previous;
};
enum class GroomQuality : std::uint8_t { Low, Medium, High };
struct GroomSelfCollisionConfig {
    bool enabled=false;
    float separation=0.014f;
    float response=0.6f;
    std::size_t maxPairChecks=250000;
    bool segmentEnabled=false;  // optional capsule narrowphase
    float friction=0.2f;        // 0..1 tangential velocity attenuation
    float bendPreservation=0.25f; // 0..1 rest-shape bend projection
};
struct GroomSelfCollisionStats {
    std::size_t pairChecks=0;
    std::size_t contacts=0;
    bool budgetHit=false;
};
// Reused broadphase scratch owned by one groom on its render-owner thread.
// Separate from gameplay ECS and from GPU resource lifetime.
// Owner-local scratch for deterministic particle broadphase. No global cache.
struct GroomParticleWorkspace {
    struct Cell {
        int x=0,y=0,z=0;
        bool operator<(const Cell& b) const noexcept {
            if(x!=b.x)return x<b.x;
            if(y!=b.y)return y<b.y;
            return z<b.z;
        }
    };
    struct Particle {
        std::uint32_t guide=0,segment=0;
        render::float3 point{};
    };
    struct Entry {
        Cell cell{};
        std::size_t particle=0;
        bool operator<(const Entry& rhs) const noexcept {
            if(cell<rhs.cell)return true;
            if(rhs.cell<cell)return false;
            return particle<rhs.particle;
        }
    };
    std::vector<Particle> particles;
    std::vector<Cell> cells;
    std::vector<Entry> entries;
    std::vector<render::float3> corrections;
    std::vector<std::uint16_t> counts;
    void reset() noexcept {
        particles.clear();cells.clear();entries.clear();
        corrections.clear();counts.clear();
    }
};
struct GroomSegmentWorkspace {
    struct Cell {
        int x=0,y=0,z=0;
        bool operator<(const Cell& b)const noexcept {
            if(x!=b.x)return x<b.x;
            if(y!=b.y)return y<b.y;
            return z<b.z;
        }
    };
    struct Entry {
        Cell cell{};
        std::size_t segment=0;
        bool operator<(const Entry& b)const noexcept {
            if(cell<b.cell)return true;
            if(b.cell<cell)return false;
            return segment<b.segment;
        }
    };
    // Reusable owner-local segment snapshot, avoiding per-substep allocation.
    struct Segment {
        std::size_t guide=0, first=0;
        render::float3 a{}, b{};
    };
    std::vector<Segment> segments;
    std::vector<Entry> cells;
    std::vector<std::pair<std::size_t,std::size_t>> pairs;
    std::vector<std::size_t> offsets;
    std::vector<render::float3> displacements;
    std::vector<float> counts;
    void reset() noexcept {
        segments.clear();cells.clear();pairs.clear();offsets.clear();
        displacements.clear();counts.clear();
    }
};
struct GroomStats {
    std::size_t guides=0,segments=0,triangles=0;
    float updatedMs=0.0f;
};
class HumanGroomRuntime {
public:
    // Distributes root guides across an ellipsoidal scalp cap using a
    // stable low-discrepancy pattern. Does NOT grow scalp geometry into eyes.
    void generate(const HumanDNA& dna, GroomQuality quality=GroomQuality::High);
    // Fixed-bounded verlet + constraint projection and coarse scalp
    // collision. Steps at most 1/20 second and never mutates simulation ECS.
    bool step(float dt,render::float3 wind={});
    bool setSelfCollision(GroomSelfCollisionConfig config) noexcept;
    GroomSelfCollisionConfig selfCollision() const noexcept {return selfCollision_;}
    GroomSelfCollisionStats selfCollisionStats() const noexcept {
        return selfCollisionStats_;
    }
    // Optional bounded closed triangle collider in the groom's local space.
    // Call after generate(); all triangles are validated before publication.
    // For imported meshes, caller owns model-to-groom coordinate conversion.
    bool setCollisionMesh(std::span<const render::float3> vertices,
                          std::span<const std::uint32_t> triangles);
    // One-shot bind-pose transfer of procedural roots onto the authored proxy.
    // Keeps each guide's rest/previous offsets coherent after relocation.
    bool attachRootsToCollisionMesh();
    // Refit animated proxy vertex positions while preserving stable BVH
    // topology and last-good state on rejection.
    bool refitCollisionMesh(std::span<const render::float3> vertices);
    void clearCollisionMesh() noexcept;
    bool hasCollisionMesh() const noexcept {return !collisionTriangles_.empty();}
    std::unique_ptr<render::Model> makeCardModel(const HumanDNA& dna) const;
    std::unique_ptr<render::Model> makeCardModel(
        const HumanDNA& dna,HumanMaterialQuality quality) const;
    bool updateCardVertices(std::span<render::ModelVertex> output) const;
    std::size_t vertexCount() const noexcept {return guides_.size()*(segments_+1)*2;}
    std::size_t triangleCount() const noexcept {return guides_.size()*segments_*2;}
    const std::vector<GroomGuide>& guides() const noexcept {return guides_;}
    GroomStats stats() const noexcept {return {guides_.size(),segments_,triangleCount(),lastStepMs_};}
private:
    std::vector<GroomGuide> guides_;
    std::size_t segments_=0;
    GroomSelfCollisionConfig selfCollision_{};
    GroomSelfCollisionStats selfCollisionStats_{};
    GroomParticleWorkspace particleWorkspace_{};
    GroomSegmentWorkspace segmentWorkspace_{};
    float stiffness_=0.55f,damping_=0.25f,gravity_=0.65f,windStrength_=0.15f;
    // Scalp ellipsoid matches the DNA-generated guide root geometry.
    // Stored per instance, not inferred from a fixed human head preset.
    render::float3 scalpRadii_{0.72f,0.90f,0.75f};
    struct CollisionTriangle {render::float3 a,b,c;};
    struct CollisionNode {
        render::float3 min{},max{};
        std::uint32_t left=0,right=0,begin=0,count=0;
    };
    std::vector<CollisionTriangle> collisionTriangles_;
    std::vector<std::uint32_t> collisionOrder_;
    std::vector<std::uint32_t> collisionSourceIndices_;
    std::size_t collisionSourceVertexCount_=0;
    std::vector<CollisionNode> collisionNodes_;
    static constexpr std::uint32_t kCollisionLeafSize=8;
    render::float3 resolveCollision(render::float3 p,
                                    const GroomGuide& guide) const;
    float lastStepMs_=0;
};
} // namespace aiverse::vivid
