// NEREID stage 9: opt-in water surface GPU residency for any native scene.
// Presentation only: caller explicitly owns the Device and releases before
// device teardown. Never called from deterministic ECS simulation.
#pragma once
#include "NereidWaterRenderer.h"
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <cstddef>

namespace aiverse::render {

struct NereidSurfaceKey {
    uint64_t sourceRevision=0; // host increments after body/geometry edits or rollback
    lib::nereid::WaterSurfaceLod lod=lib::nereid::WaterSurfaceLod::Medium;
    float uvScale=0, foamWidth=0, oceanPatchRadius=0;
    uint32_t maxVertices=0;
    uint8_t rippleTileGrid=1;
    double patchX=0,patchZ=0; // relevant for infinite ocean only
    double originX=0,originZ=0; // local vertex basis, retained with GPU mesh
    double originY=0;
    lib::nereid::WaterSurfaceStyle style{};
    lib::nereid::WaterRiverGradeComponent grade{};
    bool useGrade=false;
    bool infinite=false;
};

// Center a camera-relative ocean patch on a stable lattice: moving the
// camera within a cell does not re-upload an identical water mesh.
inline float nereidPatchAnchor(float camera,float radius) {
    if(!std::isfinite(camera) || !std::isfinite(radius) || radius<=0)
        return std::numeric_limits<float>::quiet_NaN();
    const double step=double(radius); // patch is 2*radius wide
    const double snapped=std::floor(double(camera)/step)*step;
    if(snapped < -double(std::numeric_limits<float>::max()) ||
       snapped > double(std::numeric_limits<float>::max()))
        return std::numeric_limits<float>::quiet_NaN();
    return float(snapped);
}

inline double nereidPatchAnchor64(double camera,double radius) {
    if(!std::isfinite(camera) || !std::isfinite(radius) || radius<=0)
        return std::numeric_limits<double>::quiet_NaN();
    return std::floor(camera/radius)*radius;
}

inline NereidSurfaceKey makeNereidSurfaceKey(uint64_t revision,bool infinite,
        lib::nereid::WaterSurfaceMeshOptions opts,
        lib::nereid::WaterSurfaceStyle style={}) {
    NereidSurfaceKey key;
    key.sourceRevision=revision;
    key.grade=opts.grade;key.useGrade=opts.useGrade;
    key.originX=opts.originX;
    key.originZ=opts.originZ;
    key.originY=opts.originY;
    key.infinite=infinite;
    key.lod=opts.lod;
    key.uvScale=opts.uvScale;
    key.foamWidth=opts.foamWidth;
    key.maxVertices=opts.maxVertices;
    key.rippleTileGrid=opts.rippleTileGrid;
    key.style=style;
    if(infinite) {
        key.oceanPatchRadius=opts.oceanPatchRadius;
        key.patchX=nereidPatchAnchor64(
            opts.preciseCamera?opts.cameraX64:double(opts.cameraX),
            double(opts.oceanPatchRadius));
        key.patchZ=nereidPatchAnchor64(
            opts.preciseCamera?opts.cameraZ64:double(opts.cameraZ),
            double(opts.oceanPatchRadius));
    }
    return key;
}

inline bool sameNereidSurfaceKey(const NereidSurfaceKey& a,
                                const NereidSurfaceKey& b) {
    const auto& x=a.style;const auto& y=b.style;
    return a.useGrade==b.useGrade && (!a.useGrade||lib::nereid::sameWaterRiverGrade(a.grade,b.grade)) && a.sourceRevision==b.sourceRevision &&
        a.infinite==b.infinite && a.lod==b.lod &&
        a.uvScale==b.uvScale && a.foamWidth==b.foamWidth &&
        a.maxVertices==b.maxVertices &&
        a.rippleTileGrid==b.rippleTileGrid &&
        a.oceanPatchRadius==b.oceanPatchRadius &&
        a.patchX==b.patchX && a.patchZ==b.patchZ &&
        a.originX==b.originX && a.originZ==b.originZ &&
        a.originY==b.originY &&
        x.deepR==y.deepR && x.deepG==y.deepG && x.deepB==y.deepB &&
        x.foamR==y.foamR && x.foamG==y.foamG && x.foamB==y.foamB &&
        x.foamStrength==y.foamStrength &&
        x.refractionStrength==y.refractionStrength &&
        x.absorption==y.absorption &&
        x.shoreFoamDepth==y.shoreFoamDepth &&
        x.depthTint==y.depthTint &&
        x.underwaterDistortion==y.underwaterDistortion &&
        x.underwaterScattering==y.underwaterScattering &&
        x.turbidity==y.turbidity && x.siltR==y.siltR && x.siltG==y.siltG && x.siltB==y.siltB &&
        x.rapidFoamStrength==y.rapidFoamStrength &&
        x.rapidFlowThreshold==y.rapidFlowThreshold &&
        x.rapidNoiseScale==y.rapidNoiseScale &&
         x.rapidFoamR==y.rapidFoamR &&
         x.rapidFoamG==y.rapidFoamG &&
         x.rapidFoamB==y.rapidFoamB &&
         x.rapidDetail==y.rapidDetail &&
        x.opacity==y.opacity &&
        x.specular==y.specular;
}

// These fields are baked into the uploaded per-vertex colour.
inline bool sameNereidWaterVertexStyle(
        const lib::nereid::WaterSurfaceStyle& a,
        const lib::nereid::WaterSurfaceStyle& b) {
    return a.deepR==b.deepR && a.deepG==b.deepG && a.deepB==b.deepB &&
           a.foamR==b.foamR && a.foamG==b.foamG && a.foamB==b.foamB &&
           a.foamStrength==b.foamStrength;
}
// Preserve full-key equality for external users while making upload
// identity ignore draw-only style (optics, whitewater palette and quality).
inline bool sameNereidSurfaceUploadKey(const NereidSurfaceKey& a,
                                      const NereidSurfaceKey& b) {
    if(!sameNereidWaterVertexStyle(a.style,b.style))return false;
    auto meshKey=a;
    meshKey.style=b.style;
    return sameNereidSurfaceKey(meshKey,b);
}

enum class NereidSurfaceUpdate { Unchanged, Uploaded, Failed };

struct NereidPreparedSurface {
    lib::nereid::WaterSurfaceMesh cpu;
    NereidSurfaceKey key{};
    // Exact bytes of the vertex/index arrays uploaded by MeshBuilder.
    // Driver allocation/stride overhead and temporary staging are excluded.
    size_t bufferBytes=0;
};
inline size_t nereidMeshBufferBytes(const lib::nereid::WaterSurfaceMesh& mesh) {
    if(mesh.vertices.size()>SIZE_MAX/sizeof(MeshVertex) ||
       mesh.indices.size()>SIZE_MAX/sizeof(uint32_t))return SIZE_MAX;
    const size_t vb=mesh.vertices.size()*sizeof(MeshVertex);
    const size_t ib=mesh.indices.size()*sizeof(uint32_t);
    return vb>SIZE_MAX-ib?SIZE_MAX:vb+ib;
}

class NereidSurfaceResident final {
public:
    NereidSurfaceResident()=default;
    NereidSurfaceResident(const NereidSurfaceResident&)=delete;
    NereidSurfaceResident& operator=(const NereidSurfaceResident&)=delete;
    NereidSurfaceResident(NereidSurfaceResident&&)=delete;
    NereidSurfaceResident& operator=(NereidSurfaceResident&&)=delete;

    bool needsUpdate(const lib::nereid::WaterBodyComponent& body,
            const lib::nereid::WaterGeometryComponent* geometry,
            uint64_t sourceRevision,
            lib::nereid::WaterSurfaceMeshOptions opts={},
            lib::nereid::WaterSurfaceStyle style={}) const {
        if(!body.enabled)return false;
        const bool infinite=(!geometry || !geometry->enabled) &&
            body.footprint==lib::nereid::WaterFootprint::Infinite;
        const auto key=makeNereidSurfaceKey(sourceRevision,infinite,opts,style);
        return !resident_ || !sameNereidSurfaceUploadKey(key,key_);
    }
    // Pure CPU work, safe outside the render/device update phase as long as
    // the copied body/geometry input remains immutable during preparation.
    static std::optional<NereidPreparedSurface> prepare(
            const lib::nereid::WaterBodyComponent& body,
            const lib::nereid::WaterGeometryComponent* geometry,
            uint64_t sourceRevision,
            lib::nereid::WaterSurfaceMeshOptions opts={},
            lib::nereid::WaterSurfaceStyle style={}) {
        if(!body.enabled || !validNereidWaterStyle(style))return std::nullopt;
        const bool infinite=(!geometry || !geometry->enabled) &&
            body.footprint==lib::nereid::WaterFootprint::Infinite;
        NereidPreparedSurface out;
        out.key=makeNereidSurfaceKey(sourceRevision,infinite,opts,style);
        if(infinite) {
            opts.preciseCamera=true;
            opts.cameraX64=out.key.patchX;
            opts.cameraZ64=out.key.patchZ;
        }
        out.cpu=lib::nereid::buildWaterSurfaceMesh(body,geometry,opts);
        if(out.cpu.empty())return std::nullopt;
        out.bufferBytes=nereidMeshBufferBytes(out.cpu);
        if(out.bufferBytes==SIZE_MAX)return std::nullopt;
        return out;
    }

    NereidSurfaceUpdate uploadPrepared(gfx::Device& device,
                                      const NereidPreparedSurface& prepared) {
        if(prepared.cpu.empty() || !validNereidWaterStyle(prepared.key.style))
            return NereidSurfaceUpdate::Failed;
        if(resident_ && sameNereidSurfaceUploadKey(prepared.key,key_))
            return NereidSurfaceUpdate::Unchanged;
        Mesh replacement=uploadNereidWaterSurface(device,prepared.cpu,
                                                   prepared.key.style);
        if(!replacement.vb.valid() || !replacement.ib.valid() ||
           replacement.indexCount==0) {
            releaseNereidWaterSurface(device,replacement);
            return NereidSurfaceUpdate::Failed;
        }
        releaseNereidWaterSurface(device,mesh_);
        mesh_=replacement;
        tiles_=prepared.cpu.tiles;
        key_=prepared.key;
        resident_=true;
        return NereidSurfaceUpdate::Uploaded;
    }

    // The device must remain valid until release(). Same device required for
    // successive updates; call release(oldDevice) before device recreation.
    NereidSurfaceUpdate update(gfx::Device& device,
            const lib::nereid::WaterBodyComponent& body,
            const lib::nereid::WaterGeometryComponent* geometry,
            uint64_t sourceRevision,
            lib::nereid::WaterSurfaceMeshOptions opts={},
            lib::nereid::WaterSurfaceStyle style={}) {
        auto prepared=prepare(body,geometry,sourceRevision,opts,style);
        return prepared?uploadPrepared(device,*prepared):NereidSurfaceUpdate::Failed;
    }

    // Optics and rapid material changes do not touch the resident VB/IB.
    // Reject baked vertex tint changes (which must still reupload).
    bool setDrawStyle(lib::nereid::WaterSurfaceStyle style) {
        if(!resident_ || !validNereidWaterStyle(style) ||
           !sameNereidWaterVertexStyle(key_.style,style))return false;
        key_.style=style;
        return true;
    }

    const std::vector<lib::nereid::WaterTileRange>& tiles()const{return tiles_;}
    void drawTile(Renderer3D& renderer,const mat4& transform,
                  const lib::nereid::WaterTileRange& tile,
                  const std::array<float4,16>& ripples={},
                  const std::array<float4,16>& wakeShapes={},
                  float4 wind={},float4 hydraulics={},NereidFlowBinding flow={})const {
        if(resident_ && tile.indexCount>0)
            drawNereidWaterSurface(renderer,mesh_,key_.style,transform,
                                   ripples,tile.firstIndex,tile.indexCount,
                                   wakeShapes,wind,hydraulics,std::move(flow));
    }
    void draw(Renderer3D& renderer,const mat4& transform=mat4::identity(),
              const std::array<float4,16>& ripples={},
              const std::array<float4,16>& wakeShapes={},
              float4 wind={},float4 hydraulics={},NereidFlowBinding flow={}) const {
        if(resident_)drawNereidWaterSurface(renderer,mesh_,key_.style,
                                          transform,ripples,0,0,wakeShapes,
                                          wind,hydraulics,std::move(flow));
    }
    void release(gfx::Device& device) {
        releaseNereidWaterSurface(device,mesh_);
        tiles_.clear();
        resident_=false;
        key_={};
    }
    bool resident() const {return resident_;}
    uint32_t indexCount() const {return resident_?mesh_.indexCount:0;}
    const NereidSurfaceKey& key() const {return key_;}

private:
    Mesh mesh_{};
    std::vector<lib::nereid::WaterTileRange> tiles_;
    NereidSurfaceKey key_{};
    bool resident_=false;
};

} // namespace aiverse::render
