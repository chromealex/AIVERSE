// Spline roads in the view (docs/roads.md § Look): the map's "roadNetwork" turned into static models — carriageways
// with lane markings, curbs, shoulders, sidewalks, medians, junction surfaces with fillets and zebra crossings,
// barriers, bridge decks with pillars, tunnel portals and the road props (built-in shapes or the game's models).
// Geometry comes from RtsRoadNet (the simulation's numbers); meshes are cut into square chunks so the renderer culls
// them. Textures: the game's (road type "look.textures") or procedural tiles generated at init. No shaders of its own:
// everything is drawn through Renderer3D::drawModel (PBR, shadows, lightmaps as static scenery).
#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "../../../engine/render/Model.h"
#include "../../../engine/render/Renderer3D.h"
#include "../../../engine/render/SpriteBatch.h"
#include "../../../engine/render/TextureCache.h"
#include "../RtsRoadNet.h"

namespace aiverse::game {

struct MapGrid;

class RtsRoadView {
public:
    // rules "roadTypes" (composed rules.json); reader for the game's textures / models
    void init(gfx::Device& dev, const Json& roadTypes, render::FileReader reader);
    void shutdown(gfx::Device& dev);
    // the map (its "roadNetwork"); rebuilt on the next draw. Errors go to warnings().
    void setMap(const Json& map);
    void setNetwork(const RoadNetwork& n) { net_ = n; dirty_ = true; hasNet_ = true; }
    void invalidateGround() { dirty_ = true; }   // terrain changed under the roads
    // heightAt(x, z) in world units (the view's terrain); cellMeters of the road data turns it into metres
    void draw(render::Renderer3D& r, const std::function<float(float, float)>& heightAt);
    bool active() const { return hasNet_ && !net_.empty(); }
    bool needsRebuild() const { return dirty_; }
    // roadNetwork.replaces: tags or type ids (prefixes) of the objects the spline roads stand in for (a game's
    // baked road surfaces): the view does not draw those while the network is shown
    const std::vector<std::string>& replaces() const { return replaces_; }
    const std::vector<RoadTypeDef>& types() const { return types_; }
    const RoadNetwork& network() const { return net_; }
    const RoadGeometry& geometry() const { return geom_; }
    float cellMeters() const { return cellMeters_; }
    const std::vector<std::string>& warnings() const { return warnings_; }
    // Asset/build-time breakdown, not an estimate of actual GPU draw calls.
    // One road chunk has a separate model with potentially many materials.
    // Decals (zebras/paint/wet film) use a different custom draw path.
    struct BatchStats {
        struct MaterialUse {
            std::string name;
            uint32_t textureId = 0;
            size_t primitives = 0, triangles = 0, chunks = 0;
        };
        size_t sourceChunks = 0, uploadedChunks = 0;
        size_t primitives = 0, triangles = 0;
        int mergeGroup = 1;
        std::vector<MaterialUse> materials;
    };
    const BatchStats& batchStats() const { return batchStats_; }
    // Rain on the roads (docs/roads.md § Wet roads and puddles): wet 0..1 darkens every road surface (carriageways,
    // junctions, driveways; not in tunnels), puddles 0..1 fills the puddle spots: along the kerbs, in the wheel
    // tracks, in the ruts of dirt roads and in the corners of junctions; each spot has its own level it appears at
    // and grows from there, so a shower fills them one by one and drying shrinks them back. The game's weather
    // sets both every frame; a map can fix them with roadNetwork "weather": {"wet": .., "puddles": ..}.
    void setWeather(float wet, float puddles);
    float wet() const { return wet_; }
    float puddles() const { return puddles_; }
    struct PuddleSpot {
        render::float3 c;                        // centre on the road surface (world units)
        render::float2 dir{1, 0};                // long axis (unit, xz)
        float len = 1, width = 1;                // full size (world units)
        float from = 0;                          // puddles level it starts to show at
        bool dirt = false;                       // on a dirt / gravel road (muddy water)
    };
    // every spot of the network, for a game that draws its own (reflective) water in them
    const std::vector<PuddleSpot>& puddleSpots() const { return spots_; }
    // a spot's size at a puddles level: 0 below its `from`, 1 at puddles 1
    static float puddleScale(const PuddleSpot& p, float puddles);
    // The drawn top of the road surfaces at (x, z), world units: carriageways (which lie a little over the ground),
    // kerbs and raised sidewalks, junctions, medians, driveways, verges, low embankments. Barriers, poles, signals and
    // props don't count, nor do decks higher than a bridge clearance over the ground (the simulation's decks carry
    // those). Sampled every half cell / 8 and interpolated, so a kerb is a short ramp a wheel climbs; `terrain` (the
    // ground there) is used where no road surface lies. Presentation only (docs/roads.md § Surface for wheels).
    float surfaceAt(float x, float z, float terrain) const;
    bool hasSurface() const { return !surface_.empty(); }
    uint64_t surfaceGeneration() const { return surfaceGeneration_; }
    // editor preview: one edge drawn as a translucent ribbon (not part of the network)
    struct Ghost { bool on = false; RoadNetwork net; bool ok = true; };
    void setGhost(const Ghost& g) { ghost_ = g; ghostDirty_ = true; }

    // materials of the road kit (index into the chunk models' material lists)
    static constexpr int kDrivewaySurface = -2;   // paintedMaterial: a driveway apron (variant: width, 1/1000 cell)
    enum Mat : int { kAsphalt = 0, kConcrete, kCobble, kPaving, kGravel, kDirt, kGrass, kCurb, kWhite, kYellow, kMetal,
                     kWood, kLeaves, kBark, kGlow, kSoil, kBridge, kHedge, kDark, kSignalRed, kSignalAmber, kSignalGreen, kSign, kGlass, kAsphaltWorn, kMatCount };

private:
    struct Chunk { std::unique_ptr<render::Model> model; };
    struct PropModel { std::string path; std::unique_ptr<render::Model> model; };
    struct PropInstance { int model = -1; render::mat4 xf; };
    void rebuild(gfx::Device& dev, const std::function<float(float, float)>& heightAt);
    void buildGhost(gfx::Device& dev, const std::function<float(float, float)>& heightAt);
    void release(gfx::Device& dev);
    int propModel(gfx::Device& dev, const std::string& path);
    int paintedMaterial(gfx::Device& dev, int type, int variant);   // carriageway texture with its lines, made on demand
    std::map<std::pair<int, int>, int> painted_;

    gfx::Device* dev_ = nullptr;
    render::FileReader read_;
    std::vector<RoadTypeDef> types_;
    float cellMeters_ = 4;
    RoadNetwork net_;
    Json netJson_;                               // the map's roadNetwork (parsed again when the types change)
    std::vector<std::string> replaces_;
    RoadGeometry geom_;
    bool hasNet_ = false, dirty_ = false;
    std::vector<Chunk> chunks_;
    BatchStats batchStats_;
    uint64_t surfaceGeneration_ = 1; // increments whenever road geometry/surface is rebuilt
    // standable road surface: tiles of 8x8 cells, kSurfRes samples per cell, height * 2048 (INT16_MIN: none)
    static constexpr int kSurfTile = 8, kSurfRes = 8, kSurfSide = kSurfTile * kSurfRes;
    struct SurfTile { int16_t h[kSurfSide * kSurfSide]; };
    std::map<int64_t, SurfTile> surface_;
    void rasterSurface(const std::vector<render::float3>& tris, const std::function<float(float, float)>& heightAt);
    float surfaceSample(int sx, int sz, bool& ok) const;
    std::vector<PropModel> propModels_;
    std::vector<PropInstance> props_;
    std::vector<render::Image> images_;          // the kit's textures (copied into every chunk model, shared GPU handles)
    std::vector<render::Material> materials_;
    std::vector<std::string> warnings_;
    Ghost ghost_;
    bool ghostDirty_ = false;
    std::unique_ptr<render::Model> ghostModel_;
    render::SpriteBatch decalBatch_;             // road paint as decals
    render::TextureCache paintTex_;
    render::TextureRef paint_, verge_;
    render::SpriteMesh decalMesh_, vergeMesh_;
    // rain: the road surfaces as one film (alpha set by wet) and the puddle spots; meshes remade when the levels
    // change by a step (1/32), not every frame
    void buildWeather(gfx::Device& dev);
    std::vector<render::SpriteVertex> film_;
    std::vector<PuddleSpot> spots_;
    render::SpriteMesh filmMesh_, puddleMesh_;
    float wet_ = 0, puddles_ = 0, mapWet_ = 0, mapPuddles_ = 0;
    bool weatherSet_ = false;
    int wetStep_ = -1, puddleStep_ = -1;
};

} // namespace aiverse::game
