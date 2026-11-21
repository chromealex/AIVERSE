// Terrain renderer of the RTS view (RtsView owns one). Static geometry in 32x32-cell chunks,
// frustum-culled, rebuilt only when the cells a chunk depends on change (editor painting: a per-chunk
// hash of the visual inputs). What changes during a match is read by the shader from small per-cell
// textures instead of rebuilding the mesh: resource field amounts and creep (tint), fog of war on
// heightfield maps.
//
// Surfaces (RtsTerrainSurface.h, the same math as RtsView::heightAt):
//   levels       plateaus per cliff level with sharp walls, ramps, soft colour transitions and the
//                apron around the map - the classic look, unchanged
//   heightfield  map terrain.corners: a smooth grid; big maps get CDLOD levels (2^k cell steps per
//                chunk, chosen by camera distance) with geomorphing in the vertex shader
//
// Materials: optional visual `terrain` block (rules.json, game.json wins per key) with up to four
// tiling splat layers per cell kind, a cliff texture and the water look; see docs/game-format.md
// ("Terrain materials"). Quality: Renderer3D::materialQuality() - Low = vertex colours, Medium =
// splat, High = splat + normal maps + triplanar cliffs (AIVERSE_TERRAIN=low|medium|high overrides).
// Without layers, Medium/High add filtered world-space grain; High also adds micro normals.
// Low retains the classic vertex colours (docs/terrain-textures.md).
#pragma once
#include "RtsFog.h"
#include "engine/render/PreparationAllocationBudget.h"
#include <cstdint>
#include <memory>
#include <memory>
#include <string>
#include <vector>
#include "../../../engine/render/Renderer3D.h"
#include "../../../engine/render/TextureCache.h"
#include "../../../engine/render/NativePresentationRetirement.h"
#include "../../../engine/render/RenderDeviceLifetime.h"
#include "../RtsData.h"
#include "../RtsMap.h"
#include "RtsTerrainDetail.h"

namespace aiverse::game {

struct TerrainLook {
    struct Layer { std::string id, texture, normal; float scale = 4; render::float3 tint{1, 1, 1}; };
    std::vector<Layer> layers;                 // <= 4 splat layers
    int cellLayer[8] = {0, 0, 0, 0, 0, 0, 0, 0}; // per cell kind (CellKind) -> layer index
    bool hasColor[8] = {};
    render::float3 color[8];                    // per cell kind: vertex colour override (all tiers)
    std::string cliffTexture, cliffNormal;
    float cliffScale = 3, slopeStart = 0.35f, slopeEnd = 0.6f, splatStrength = 1;
    render::float3 cliffTint{1, 1, 1};
    enum class Water { Auto, Sheet, Bodies } water = Water::Auto;
    render::float4 waterColor{0.22f, 0.45f, 0.62f, 0.78f};
    float waterOffset = -0.12f;                 // below the surrounding ground (bodies) / ground level (sheet)
    int lod = -1;                               // -1 auto (heightfield maps above 256 cells per side), 0 off, 1 on
    float lodRange = 128;                       // world distance of the finest LOD
    // detail surface (RtsTerrainDetail.h): "classic" = the cell-aligned look (default), "smooth" =
    // synthesised sub-cell detail (bevelled organic cliffs, bicubic relief, fine noise)
    bool smooth = false;
    int detailScale = 4;                        // samples per cell wanted (capped by the quality tier)
    float bevel = 0.35f, wobble = 0.3f, roughness = 0.04f;
    float blendSharpness = 0.5f;                // height-based splat blending (0 = linear)
    float macro = 0.25f;                        // macro variation strength (hides tiling)
    float detailRange = 24;                     // world distance of the finest detail LOD (at 4 samples per cell)
    double detailBudgetMB = 96;                 // detail LOD meshes kept on the GPU (LRU beyond)
    render::float3 rockColor{0.45f, 0.41f, 0.36f}; // steep slopes without a cliff texture
    bool hasRockColor = false;
    // Opt-in dielectric PBR. Packed assets use albedo.a=AO, normal.a=roughness.
    bool pbr = false, packedPbr = false;
    float pbrRoughness = 0.85f;
    bool render = true; // explicit authored/external ground disables only this legacy visual surface
    bool materials() const { return !layers.empty(); }
};

// Cell kinds, in the classic colour order.
enum CellKind : uint8_t { kKindGrass, kKindSand, kKindRamp, kKindRock, kKindBlocked, kKindLakebed, kKindShallows, kKindDug };
const char* cellKindName(int k);
// Map key "terrainMaterial" (rows of '0'..'3', other chars = by cell kind) -> per cell layer (255 = by kind).
std::vector<uint8_t> parseMaterialRows(const Json& rows, int w, int h);

class TerrainRenderer {
public:
    void setGpuDeviceLease(render::RenderDeviceLifetime::Lease lease) {gpuDeviceLease_=std::move(lease);}
    void init(gfx::Device& dev, const Rules& rules, const render::FileReader& read, const std::string& contentBase);
    // Explicit safe teardown when the caller has not yet established RHI
    // completion. A failed barrier preserves the published GPU owner.
    bool shutdownAfterGpuIdle(gfx::Device& dev);
    void shutdown(gfx::Device& dev);
    bool ownsGpuResources() const noexcept {return dev_!=nullptr;}
    // Poll only at the host pre-render retirement boundary, never in a draw.
    bool collectRetiredMeshBuffers(gfx::Device& dev) {
        checkGpuDevice(dev);
        return retiredMeshes_.collect(dev);
    }
    size_t pendingMeshGpuRetirements() const noexcept {
        return retiredMeshes_.pending();
    }
    // Optional per-cell material layer painted on the map (0..3, 255 = by cell kind). Next build().
    void setMaterialLayer(std::vector<uint8_t> layer) {
        if (materialLayer_ != layer) {
            materialLayer_ = std::move(layer);
            materialLayerChanged_ = true;
            ++publicationRevision_;
        }
    }
    // (Re)builds the chunks whose inputs changed. Returns true when any geometry changed.
    bool build(gfx::Device& dev, const MapGrid& g, bool flatDig);
    // Published source heights are copied before any fallible GPU upload.
    // Editor MapGrid may mutate in place while the old terrain is displayed.
    // surfaceAt/heightAt must use this immutable published CPU snapshot until
    // the next successful Terrain owner commit.

    // External Stage 47B candidate for full-map/topology replacement.
    // Only private CPU data and new GPU handles are built during prepare.
    // All live terrain ownership remains unchanged until commitMap, which
    // is no-throw and can participate in a multi-owner publication cohort.
    // Shared admission cap across all outstanding topology candidates. Includes
    // geometry, RGBA8 mip payload and camera COW fields; excludes published/
    // retired resources, shader driver storage and physical allocation overhead.
    void setTopologyPreparationBudgetBytes(size_t bytes) {topologyBudget_.setLimit(bytes);}
    size_t topologyPreparationBytes() const noexcept {return topologyBudget_.used();}
    class PreparedMap;
    std::shared_ptr<PreparedMap> prepareMap(
        gfx::Device& dev,const MapGrid& grid,bool flatDig);
    bool commitMap(std::shared_ptr<PreparedMap> candidate) noexcept;
    // Read-only height query against a private staged map. A Road/Shore
    // transaction uses this exact ground before any owner publishes.
    float preparedSurfaceAt(const std::shared_ptr<PreparedMap>& candidate,
                            const MapGrid& grid,float x,float z) const;
    bool mapMatches(int w,int h) const noexcept {
        return built_ && mapW_==w && mapH_==h;
    }
    uint64_t stateRevision() const noexcept {return publicationRevision_;}
    bool canPrepareMap() const noexcept {
        return dev_ && built_ && texturesLoaded_;
    }
    // LENS Stage 47B.2: copy-on-write terrain dynamic + vision RHI
    // textures. Prepare mutates no published bytes/handles; a late owner
    // preparation failure discards all new textures. Returned handles are
    // private to a PreparedFrame until the shared owner cohort commits.
    class PreparedFrame;
    std::shared_ptr<PreparedFrame> prepareFrame(
        const MapGrid& grid,const std::vector<uint8_t>* vision,
        const std::vector<uint8_t>* filteredVisibility=nullptr);
    // Also stage missing Detail LOD buffers of the active primary camera.
    std::shared_ptr<PreparedFrame> prepareFrameVisible(
        const render::Renderer3D& renderer,const MapGrid& grid,
        const std::vector<uint8_t>* vision,
        const std::vector<uint8_t>* filteredVisibility=nullptr);
    void drawCandidate(const std::shared_ptr<PreparedFrame>&,render::Renderer3D&,
        const MapGrid&,const std::vector<uint8_t>*);
    void drawMapCandidate(const std::shared_ptr<PreparedMap>&,render::Renderer3D&,
        const MapGrid&,const std::vector<uint8_t>*);
    void commitFrame(std::shared_ptr<PreparedFrame> candidate) noexcept;
    // Prepare camera resources against unpublished topology. The map owns
    // this frame until commitMap publishes both, or rollback discards both.
    void prepareMapVisible(const std::shared_ptr<PreparedMap>& candidate,
        const render::Renderer3D& renderer,const MapGrid& grid,
        const std::vector<uint8_t>* vision,
        const std::vector<uint8_t>* filteredVisibility=nullptr);
    // Queues the terrain (opaque, main pass) and water. vision: fog of war per cell (heightfield maps
    // darken in the shader; levels maps keep the classic fog sheet drawn by RtsView).
    void draw(render::Renderer3D& r, const MapGrid& g, const std::vector<uint8_t>* vision, bool flatDig);
    // Secondary LENS view: cull prepared terrain LODs per camera without
    // advancing terrain simulation, uploading textures or rebuilding buffers.
    // Must follow draw() on the same presentation frame.
    void drawPrepared(render::Renderer3D& r,const MapGrid& g,
                      const std::vector<uint8_t>* vision);
    void drawWater(render::Renderer3D& r);
    void setMapSurfaceEnabled(bool enabled){mapSurfaceEnabled_=enabled;}
    bool built() const { return built_; }
    bool shaderFog() const { return heightfield_; }
    // Explicit detail heightmap of the map (terrain.detail file, editor detail layer); null = none
    // (synthesised when the look says "smooth"). splat: optional RGBA8 weights per detail sample.
    void setDetail(const terrain::DetailSurface* d, const std::vector<uint8_t>* splat = nullptr);
    bool detailActive() const { return detailOn_; }
    // The drawn surface (detail when active, otherwise RtsTerrainSurface.h) - RtsView::heightAt.
    float surfaceAt(const MapGrid& g, float x, float z) const;
    const terrain::DetailSurface* detail() const { return detailOn_ ? &detail_ : nullptr; }
    // Fog of war look + processed field from RtsFog (fade, blur); null = raw vision, default look.
    void setFog(const std::vector<uint8_t>* field, const FogLook& look, bool changed) { fogField_ = field; fogLook_ = look; fogFieldChanged_ = fogFieldChanged_ || changed; }
    const TerrainLook& look() const { return look_; }
    const render::HeightField& heightField() const { return heightField_; }   // valid after draw() on the trace tier

    struct Stats { int chunks = 0, drawn = 0, lodMax = 0, detailK = 0; uint64_t triangles = 0; double buildMs = 0; int rebuilt = 0; size_t geometryBytes = 0; };
    const Stats& stats() const { return stats_; }
    // Terrain owns chunk LOD buffers, water/apron geometry, map/vision,
    // detail/height-field textures and its local terrain material cache.
    // Called on the graphics owner thread, never GPU readback.
    render::GpuOwnedMemoryStats ownedGpuMemoryStats() const {
        if (!dev_) return {};
        render::GpuOwnedMemoryCounter counter(*dev_);
        for (const auto& chunk : chunks_)
            for (const auto& lod : chunk.lods) {
                counter.buffer(lod.vb);
                counter.buffer(lod.ib);
            }
        counter.buffer(apron_.vb);
        counter.buffer(apron_.ib);
        counter.buffer(water_.vb);
        counter.buffer(water_.ib);
        for (auto texture : {cellTex_,dynTex_,splatTex_,visionTex_,
                white_,flatN_,gray_,heightField_.tex,
                detailNrmTex_,detailSplatTex_,layerNormalAtlas_})
            counter.texture(texture);
        // TextureCache owns layers/cliffs. TextureRef aliases are borrowed.
        const auto cache = textures_.ownedGpuMemoryStats();
        auto stats=counter.stats();
        stats.textureKnownBytes =
            render::GpuOwnedMemoryCounter::saturatingAdd(
                stats.textureKnownBytes,cache.textureKnownBytes);
        stats.bufferKnownBytes =
            render::GpuOwnedMemoryCounter::saturatingAdd(
                stats.bufferKnownBytes,cache.bufferKnownBytes);
        stats.textures+=cache.textures;
        stats.buffers+=cache.buffers;
        stats.unknownTextures+=cache.unknownTextures;
        stats.unknownBuffers+=cache.unknownBuffers;
        stats.multiVersionBuffers+=cache.multiVersionBuffers;
        return stats;
    }

private:
    struct TV { render::float3 pos, nrm; uint32_t rgba; float morph[2]; };
    struct Lod { gfx::BufferHandle vb, ib; uint32_t indexCount = 0, vertexCount = 0; };
    struct Chunk {
        int x0 = 0, y0 = 0, x1 = 0, y1 = 0;          // cell range [x0, x1)
        std::vector<Lod> lods;                       // levels mode: one
        render::float3 bmin, bmax;
        uint64_t hash = 0, geoHash = 0;
        bool valid = false;
        std::vector<uint32_t> lastUse;               // detail mode: frame each LOD was last drawn (LRU)
    };
    void commitMapVisible(PreparedMap& candidate) noexcept;
    bool buildInternal(gfx::Device& dev,const MapGrid& g,bool flatDig);
    void parseLook(const Json& t);
    void loadTextures(gfx::Device& dev);
    uint64_t chunkHash(const MapGrid& g, const Chunk& c, bool flatDig) const;
    void buildLevelsChunk(gfx::Device& dev, const MapGrid& g, Chunk& c, bool flatDig);
    void buildHeightChunk(gfx::Device& dev, const MapGrid& g, Chunk& c);
    // detail mode (RtsTerrainDetailMesh.cpp)
    bool buildDetail(gfx::Device& dev, const MapGrid& g, bool flatDig, bool all);
    // Same-map detail edits: stage only affected coarse chunk LODs, preserve
    // unchanged published meshes, and publish CPU/GPU images atomically.
    bool tryBuildDetailIncremental(gfx::Device& dev,const MapGrid& g,bool flatDig);

    void buildDetailLod(gfx::Device& dev,const MapGrid& g,Chunk& c,
                        int lod,size_t* stagedBytes=nullptr);
    void updateDetailTextures(gfx::Device& dev, int i0, int j0, int i1, int j1);
    uint64_t detailHash(const Chunk& c) const;
    size_t detailSampleCap() const;
    void buildApron(gfx::Device& dev, const MapGrid& g);
    void buildWater(gfx::Device& dev, const MapGrid& g, bool flatDig);
    void freeChunk(gfx::Device& dev, Chunk& c);
    void upload(gfx::Device& dev, const std::vector<TV>& v, const std::vector<uint32_t>& idx, Lod& out);
    int kindOf(const MapGrid& g, size_t i, bool flatDig) const;
    render::float3 cellColor(const MapGrid& g, int x, int y, bool flatDig) const;
    void updateCellTextures(gfx::Device& dev, const MapGrid& g, bool flatDig);
    void updateDynamic(gfx::Device& dev, const MapGrid& g, bool all);
    void updateVision(gfx::Device& dev, const std::vector<uint8_t>& vision);
    // 16-bit height map of the drawn surface for traced terrain shadows (Renderer3D::setHeightField);
    // rebuilt (throttled) after geometry changes, only while the trace tier is active
    void updateHeightField(gfx::Device& dev, const MapGrid& g);
    gfx::PipelineHandle pipeline(gfx::Device& dev, int variant, bool dynamicGi=false, bool compactGi=false); // GI slots 6..11, GLES low slot12
    int tierFor(const render::Renderer3D& r) const;
    void drawInternal(render::Renderer3D& r,const MapGrid& g,
                      const std::vector<uint8_t>* vision,bool preparedOnly);
    int tierFor0() const;                       // device / env tier without a renderer (detail sample count)

    void checkGpuDevice(const gfx::Device& dev) const {
        if(gpuDeviceLease_.device() && !gpuDeviceLease_.validFor(dev))
            throw std::logic_error("TerrainRenderer GPU device generation expired");
    }
    render::RenderDeviceLifetime::Lease gpuDeviceLease_;
    render::PreparationAllocationBudget topologyBudget_{128u*1024u*1024u};
    std::shared_ptr<render::PreparationAllocationBudget::Lease> preparationAllocation_;
    void reservePreparationBytes(size_t count,size_t stride=1) {
        if(preparationAllocation_)preparationAllocation_->reserve(count,stride);
    }
    gfx::TextureHandle createTerrainTexture(gfx::Device&,const gfx::TextureDesc&);
    gfx::Device* dev_ = nullptr;
    render::NativePresentationRetirement retiredMeshes_;
    const Rules* rules_ = nullptr;
    std::string base_;
    render::FileReader read_;
    TerrainLook look_;
    render::TextureCache textures_;
    gfx::ShaderHandle shaders_[13]; // 0..5 legacy, 6..11 Metal/Vulkan GI, 12 GLES compact
    gfx::PipelineHandle pipes_[13];
    bool shaderFailed_[13] = {};
    // per-cell textures
    gfx::TextureHandle cellTex_, dynTex_, splatTex_, visionTex_, white_, flatN_, gray_;
    std::vector<uint8_t> cellPx_, dynPx_, splatPx_, visionPx_;
    const std::vector<uint8_t>* fogField_ = nullptr;
    FogLook fogLook_;
    bool fogFieldChanged_ = true;
    int texW_ = 0, texH_ = 0, dynRow_ = 0;
    bool visionInit_ = false;
    // This primary frame's shader inputs were published by OwnerPrepareCommit.
    // Prevent drawInternal from updating already-submitted GPU textures in place.
    bool frameFieldsPrepared_ = false;
    std::vector<uint8_t> materialLayer_;
    bool materialLayerChanged_ = false; // detail splat depends on painted cell layers
    render::TextureRef layerTex_[4], layerNrm_[4], cliffTex_, cliffNrm_;
    // GL33: four repeatable layer normals packed into one atlas sampler.
    gfx::TextureHandle layerNormalAtlas_;
    int layerNormalAtlasSize_ = 0;
    bool texturesLoaded_ = false;

    // Only the arrays required by terrain::heightAt are copied (no
    // gameplay state/pathfinding/network data). Shared pointer swap is
    // no-throw during commitMap() and never depends on mutable map bytes.
    std::unique_ptr<MapGrid> publishedSurfaceGrid_;
    std::vector<Chunk> chunks_;
    int cw_ = 0, ch_ = 0, mapW_ = 0, mapH_ = 0, lodLevels_ = 1;
    bool heightfield_ = false, built_ = false;
    float lh_ = 1.5f;
    uint64_t globalHash_ = 0;
    Lod apron_;
    render::Mesh water_;
    bool waterSheet_ = false, hasWater_ = false;
    bool mapSurfaceEnabled_=true;
    uint64_t waterHash_ = 0;
    render::HeightField heightField_;
    bool heightDirty_ = true;
    uint64_t publicationRevision_ = 1; // invalidates externally staged candidates
    int heightW_ = 0, heightH_ = 0;
    double heightBuiltAt_ = -1e9;               // seconds (steady clock), throttle while sculpting
    Stats stats_;
    // detail surface
    terrain::DetailSurface detail_, fileDetail_;
    std::vector<uint8_t> fileSplat_;
    bool hasFileDetail_ = false, fileDetailChanged_ = false, detailOn_ = false;
    int detailK_ = 0;
    int dirtyI0_ = 1 << 30, dirtyJ0_ = 1 << 30, dirtyI1_ = -1, dirtyJ1_ = -1;   // edited explicit detail samples
    gfx::TextureHandle detailNrmTex_, detailSplatTex_;
    int nrmW_ = 0, nrmH_ = 0, dsplatW_ = 0, dsplatH_ = 0;
    std::vector<uint8_t> nrmPx_, dsplatPx_;
    std::vector<render::float3> cellCol_;       // classic cell colours (detail vertex colours)
    std::vector<uint8_t> cellLayerOf_;          // splat layer by cell kind
    uint32_t frame_ = 0;
    size_t geoBytes_ = 0;
};

} // namespace aiverse::game
