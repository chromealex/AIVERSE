// Chunked terrain renderer (see RtsTerrainRender.h).
#include "RtsTerrainRender.h"
#include "engine/view/Frustum.h"
#include "engine/view/GridGeometry.h"
#include "../../../engine/data/JsonCompose.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <memory>
#include <limits>
#include <stdexcept>
#include <utility>
#include "../../../engine/platform/Platform.h"
#include "RtsTerrainShaders.h"
#include "RtsTerrainSurface.h"

namespace aiverse::game {

using namespace render;

namespace {
constexpr int kChunk = 32;
constexpr int kApron = 12;
constexpr int kMaxLod = 5;            // steps 1, 2, 4, 8, 16 cells
constexpr float kApronY = -0.05f;

uint32_t hashCell(int x, int y) { uint32_t h = uint32_t(x) * 73856093u ^ uint32_t(y) * 19349663u; h ^= h >> 13; h *= 0x5bd1e995u; return h ^ (h >> 15); }
float noiseOf(int x, int y) { return float(hashCell(x, y) & 255) / 255.0f; }

struct Hasher {
    uint64_t h = 1469598103934665603ull;
    void add(uint64_t v) { for (int i = 0; i < 8; ++i) { h ^= (v >> (i * 8)) & 0xff; h *= 1099511628211ull; } }
};

float3 jsonColor(const Json& j, float3 def) {
    if (!j.isArray() || j.size() < 3) return def;
    return {float(j[0].toDouble()), float(j[1].toDouble()), float(j[2].toDouble())};
}

struct TU0 { float lod[4], mode[4], scale[4], cliff[4], fog[4], tint[4][4], cliffTint[4], fogColor[4], fogGrid[4], detail[4], rock[4], blend[4], pbr[4]; };

const char* kKindNames[] = {"grass", "sand", "ramp", "rock", "blocked", "lakebed", "shallows", "dug"};
} // namespace

const char* cellKindName(int k) { return k >= 0 && k < 8 ? kKindNames[k] : "?"; }

std::vector<uint8_t> parseMaterialRows(const Json& rows, int w, int h) {
    if (!rows.isArray() || w <= 0 || h <= 0) return {};
    std::vector<uint8_t> out(size_t(w * h), 255);
    for (size_t y = 0; y < rows.size() && int(y) < h; ++y) {
        std::string r = rows[y].str();
        for (size_t x = 0; x < r.size() && int(x) < w; ++x)
            if (r[x] >= '0' && r[x] <= '3') out[y * size_t(w) + x] = uint8_t(r[x] - '0');
    }
    return out;
}

// ------------------------------------------------------------------ setup

void TerrainRenderer::init(gfx::Device& dev, const Rules& rules, const FileReader& read, const std::string& contentBase) {
    checkGpuDevice(dev);
    if (dev_ && dev_ != &dev)
        throw std::logic_error("TerrainRenderer: GPU device owner mismatch");

    // Parse and allocate *before* touching an existing published terrain.
    // The three fallback textures are unpublished until the complete kit
    // exists. Missing textures must fail, not leave a half-bound renderer.
    TerrainRenderer parsed;
    parsed.look_ = {};
    if(read) {
        std::string text, rulesPath="rules.json";
        Json game;
        try {
            if(read("game.json",text)) {
                game=Json::parse(text);
                rulesPath=game.getStr("rules","rules.json");
            }
        }catch(...) {}
        try {
            parsed.parseLook(readComposedJson(rulesPath,
                [&](const std::string& p,std::string& out){return read(p,out);})["terrain"]);
        }catch(...) {}
        if(game.isObject())parsed.parseLook(game["terrain"]);
    }
    std::string nextBase=contentBase;
    FileReader nextReader=read;
    gfx::TextureHandle nextWhite{},nextFlatNormal{},nextGray{};
    const auto tex1=[&](uint32_t px,const char* label) {
        gfx::TextureDesc td;
        td.width=td.height=1;
        td.data=&px;
        td.repeat=true;
        td.label=label;
        return createTerrainTexture(dev,td);
    };
    try {
        nextWhite=tex1(0xffffffffu,"terrain white");
        if(!nextWhite.valid())
            throw std::runtime_error("TerrainRenderer: white GPU texture allocation failed");
        nextFlatNormal=tex1(0xffff8080u,"terrain flat normal");
        if(!nextFlatNormal.valid())
            throw std::runtime_error("TerrainRenderer: flat normal GPU texture allocation failed");
        nextGray=tex1(0xff808080u,"terrain gray");
        if(!nextGray.valid())
            throw std::runtime_error("TerrainRenderer: gray GPU texture allocation failed");

        // A previous draw may still refer to the old terrain kit. Never
        // destroy it without an actual full-device idle boundary.
        if(dev_ && !dev.waitForResourceIdle())
            throw std::runtime_error("TerrainRenderer: reload GPU idle barrier failed");
        if(dev_)shutdown(dev);
    }catch(...) {
        // These unpublished textures cannot be referenced by a draw call.
        if(nextGray.valid())dev.destroyTexture(nextGray);
        if(nextFlatNormal.valid())dev.destroyTexture(nextFlatNormal);
        if(nextWhite.valid())dev.destroyTexture(nextWhite);
        throw;
    }

    dev_=&dev;
    rules_=&rules;
    base_.swap(nextBase);
    read_.swap(nextReader);
    using std::swap;
    swap(look_,parsed.look_);
    textures_.init(dev);
    white_=nextWhite;
    flatN_=nextFlatNormal;
    gray_=nextGray;
    texturesLoaded_=false;
    built_=false;
    ++publicationRevision_;
}

void TerrainRenderer::parseLook(const Json& t) {
    if (!t.isObject()) return;
    look_.render=t.getBool("render",look_.render);
    if (t["pbr"].isObject()) {
        const Json& p=t["pbr"];
        look_.pbr=p.getBool("enabled",look_.pbr);
        look_.packedPbr=p.getBool("packedMaps",look_.packedPbr);
        look_.pbrRoughness=std::clamp(float(p["roughness"].toDouble(look_.pbrRoughness)),0.045f,1.0f);
    }
    if (t["layers"].isArray()) {
        look_.layers.clear();
        for (auto& l : t["layers"].items()) {
            if (look_.layers.size() >= 4) break;
            TerrainLook::Layer L;
            L.id = l.getStr("id", "layer" + std::to_string(look_.layers.size()));
            L.texture = l.getStr("texture");
            L.normal = l.getStr("normal");
            L.scale = std::max(0.05f, float(l["scale"].toDouble(4)));
            L.tint = jsonColor(l["tint"], {1, 1, 1});
            look_.layers.push_back(L);
        }
    }
    auto layerIndex = [&](const std::string& id) {
        for (size_t i = 0; i < look_.layers.size(); ++i) if (look_.layers[i].id == id) return int(i);
        return -1;
    };
    if (t["cells"].isObject())
        for (auto& [k, v] : t["cells"].members()) {
            int kind = -1;
            for (int i = 0; i < 8; ++i) if (k == kKindNames[i]) kind = i;
            if (kind < 0) continue;
            const Json& lj = v.isString() ? v : v["layer"];
            if (lj.isString()) { int li = layerIndex(lj.str()); if (li >= 0) look_.cellLayer[kind] = li; }
            else if (lj.isNumber()) look_.cellLayer[kind] = std::clamp(int(lj.integer()), 0, 3);
            if (v.isObject() && v["color"].isArray()) { look_.hasColor[kind] = true; look_.color[kind] = jsonColor(v["color"], {1, 1, 1}); }
        }
    if (const Json& c = t["cliff"]; c.isObject()) {
        look_.cliffTexture = c.getStr("texture", look_.cliffTexture);
        look_.cliffNormal = c.getStr("normal", look_.cliffNormal);
        look_.cliffScale = std::max(0.05f, float(c["scale"].toDouble(look_.cliffScale)));
        look_.cliffTint = jsonColor(c["tint"], look_.cliffTint);
        if (c["slope"].isArray() && c["slope"].size() >= 2) {
            look_.slopeStart = float(c["slope"][0].toDouble()); look_.slopeEnd = std::max(look_.slopeStart + 0.01f, float(c["slope"][1].toDouble()));
        }
    }
    if (t.has("splatStrength")) look_.splatStrength = std::clamp(float(t["splatStrength"].toDouble(1)), 0.0f, 1.0f);
    if (const Json& w = t["water"]; w.isObject()) {
        std::string m = w.getStr("mode", "auto");
        look_.water = m == "sheet" ? TerrainLook::Water::Sheet : m == "bodies" ? TerrainLook::Water::Bodies : TerrainLook::Water::Auto;
        if (w["color"].isArray() && w["color"].size() >= 3) {
            float3 c = jsonColor(w["color"], {0.22f, 0.45f, 0.62f});
            look_.waterColor = {c.x, c.y, c.z, w["color"].size() >= 4 ? float(w["color"][3].toDouble()) : 0.78f};
        }
        look_.waterOffset = float(w["offset"].toDouble(look_.waterOffset));
    } else if (w.isString()) {
        std::string m = w.str();
        look_.water = m == "sheet" ? TerrainLook::Water::Sheet : m == "bodies" ? TerrainLook::Water::Bodies : TerrainLook::Water::Auto;
    }
    if (const Json& l = t["lod"]; l.type() == Json::Type::Bool) look_.lod = l.boolean() ? 1 : 0;
    else if (l.isString()) look_.lod = l.str() == "on" ? 1 : l.str() == "off" ? 0 : -1;
    if (t.has("lodRange")) look_.lodRange = std::max(16.0f, float(t["lodRange"].toDouble(128)));
    // detail surface (docs/terrain.md "Detail surface")
    if (t["style"].isString()) look_.smooth = t["style"].str() == "smooth";
    if (const Json& d = t["detailLook"]; d.isObject()) {
        look_.detailScale = std::clamp(int(d.getInt("scale", look_.detailScale)), 1, 8);
        look_.bevel = std::clamp(float(d["bevel"].toDouble(look_.bevel)), 0.0f, 3.0f);
        look_.wobble = std::clamp(float(d["wobble"].toDouble(look_.wobble)), 0.0f, 2.0f);
        look_.roughness = std::clamp(float(d["roughness"].toDouble(look_.roughness)), 0.0f, 1.0f);
        look_.blendSharpness = std::clamp(float(d["blendSharpness"].toDouble(look_.blendSharpness)), 0.0f, 1.0f);
        look_.macro = std::clamp(float(d["macro"].toDouble(look_.macro)), 0.0f, 1.0f);
        look_.detailRange = std::clamp(float(d["lodRange"].toDouble(look_.detailRange)), 4.0f, 1000.0f);
        if (d["rockColor"].isArray()) { look_.rockColor = jsonColor(d["rockColor"], look_.rockColor); look_.hasRockColor = true; }
    }
}

void TerrainRenderer::loadTextures(gfx::Device&) {
    texturesLoaded_ = true;
    for (int i = 0; i < 4; ++i) { layerTex_[i] = {}; layerNrm_[i] = {}; }
    cliffTex_ = cliffNrm_ = {};
    // tiling textures: repeat + mipmaps, never from an atlas page (TextureCache::resolve(..., tiling))
    auto load = [&](const std::string& p) { return p.empty() ? TextureRef{} : textures_.resolve(p, false, base_, true); };
    for (size_t i = 0; i < look_.layers.size(); ++i) { layerTex_[i] = load(look_.layers[i].texture); layerNrm_[i] = load(look_.layers[i].normal); }
    cliffTex_ = load(look_.cliffTexture);
    cliffNrm_ = load(look_.cliffNormal);
    // GL33 has a 16-active-fragment-texture-unit limit on Apple.
    // Pack all four independently tiled normal maps into one repeatable
    // padded atlas. This leaves the splat, albedo, cliff and baked inputs
    // intact. A failed atlas is never sampled: the shader falls back.
    layerNormalAtlasSize_ = 0;
    if (dev_ && dev_->info().backend == gfx::Backend::GL33) {
        struct Source { int width=1, height=1; std::vector<uint8_t> rgba; };
        Source source[4];
        int tile = 1;
        bool valid = true;
        for (int i=0; i<4; ++i) {
            const auto& name = i < int(look_.layers.size())
                ? look_.layers[size_t(i)].normal : std::string{};
            if (name.empty() || !layerNrm_[i].valid()) continue;
            std::string bytes;
            if (!platform::readContentFile(base_ + name, bytes) ||
                !render::decodeImage(bytes,source[i].width,source[i].height,
                                     source[i].rgba) ||
                source[i].width<=0 || source[i].height<=0) {
                valid=false;
                break;
            }
            tile=std::max(tile,std::max(source[i].width,source[i].height));
        }
        // The atlas is optional. Larger images retain the existing bounded
        // shader-tier fallback instead of silently downsampling source maps.
        if (valid && tile<=2048) {
            constexpr int gutter=16;
            const int stride=tile+2*gutter;
            const int extent=2*stride;
            std::vector<uint8_t> rgba(size_t(extent)*size_t(extent)*4);
            for (int layer=0;layer<4;++layer) {
                const Source& src=source[layer];
                for (int y=-gutter;y<tile+gutter;++y) {
                    const int sy=((y%tile)+tile)%tile;
                    const int iy=std::min(src.height-1,int((int64_t(sy)*src.height)/tile));
                    for (int x=-gutter;x<tile+gutter;++x) {
                        const int sx=((x%tile)+tile)%tile;
                        const int ix=std::min(src.width-1,int((int64_t(sx)*src.width)/tile));
                        const size_t dst=(size_t((layer/2)*stride+y+gutter)*extent+
                                          size_t((layer%2)*stride+x+gutter))*4;
                        if (src.rgba.empty()) {
                            rgba[dst+0]=128;rgba[dst+1]=128;
                            rgba[dst+2]=255;rgba[dst+3]=255;
                        } else {
                            const size_t from=(size_t(iy)*src.width+size_t(ix))*4;
                            for(int channel=0;channel<4;++channel)
                                rgba[dst+channel]=src.rgba[from+size_t(channel)];
                        }
                    }
                }
            }
            gfx::TextureDesc td;
            td.width=extent;td.height=extent;
            td.data=rgba.data();td.filter=gfx::Filter::Linear;
            td.mipmaps=true;td.repeat=false;td.label="terrain GL33 normal atlas";
            layerNormalAtlas_=dev_->createTexture(td);
            if (layerNormalAtlas_.valid()) {
                layerNormalAtlasSize_=tile;
                std::fprintf(stderr,"[terrain] GL33 layer normal atlas %dx%d, four source normals\n",
                             extent,extent);
            }
        }
    }
}

bool TerrainRenderer::shutdownAfterGpuIdle(gfx::Device& dev) {
    checkGpuDevice(dev);
    if(!dev_)return true;
    if(dev_!=&dev)
        throw std::logic_error("TerrainRenderer shutdown: GPU device owner mismatch");
    if(!dev.waitForResourceIdle())return false;
    shutdown(dev);
    return true;
}

void TerrainRenderer::shutdown(gfx::Device& dev) {
    checkGpuDevice(dev);
    if(!dev_)return;
    if(dev_!=&dev)
        throw std::logic_error("TerrainRenderer shutdown: GPU device owner mismatch");
    for (auto& c : chunks_) freeChunk(dev, c);
    chunks_.clear();
    retiredMeshes_.retireBuffer(dev,apron_.vb);
    retiredMeshes_.retireBuffer(dev,apron_.ib);
    apron_={};
    retiredMeshes_.retireBuffer(dev,water_.vb);
    retiredMeshes_.retireBuffer(dev,water_.ib);
    water_={};
    if(!retiredMeshes_.completeForShutdown(dev))
        throw std::runtime_error("TerrainRenderer: pending GPU mesh retirement could not reach idle");
    for (auto t : {cellTex_, dynTex_, splatTex_, visionTex_, detailNrmTex_, detailSplatTex_, white_, flatN_, gray_, heightField_.tex}) if (t.valid()) dev.destroyTexture(t);
    cellTex_ = dynTex_ = splatTex_ = visionTex_ = detailNrmTex_ = detailSplatTex_ = white_ = flatN_ = gray_ = {};
    dev.destroyTexture(layerNormalAtlas_);
    layerNormalAtlas_ = {};
    layerNormalAtlasSize_ = 0;
    heightField_ = {};
    heightDirty_ = true;
    // Base/detail, full GI and compact GI variants.
    for (int i = 0; i < 13; ++i) {
        if (pipes_[i].valid()) dev.destroyPipeline(pipes_[i]);
        if (shaders_[i].valid()) dev.destroyShader(shaders_[i]);
        pipes_[i] = {}; shaders_[i] = {}; shaderFailed_[i] = false;
    }
    textures_.shutdown();
    publishedSurfaceGrid_.reset();
    texW_ = texH_ = 0;
    built_ = false;
    globalHash_ = 0;
    frameFieldsPrepared_=false;
    dev_ = nullptr;
    ++publicationRevision_;
}

void TerrainRenderer::freeChunk(gfx::Device& dev, Chunk& c) {
    for(auto& l:c.lods) {
        retiredMeshes_.retireBuffer(dev,l.vb);
        retiredMeshes_.retireBuffer(dev,l.ib);
    }
    c.lods.clear();
    c.valid=false;
}

void TerrainRenderer::upload(gfx::Device& dev, const std::vector<TV>& v,
                             const std::vector<uint32_t>& idx, Lod& out) {
    checkGpuDevice(dev);
    if(dev_!=&dev)
        throw std::logic_error("TerrainRenderer upload: GPU device owner mismatch");
    Lod next{};
    if(!idx.empty()) {
        try {
            gfx::BufferDesc vb;
            vb.type=gfx::BufferType::Vertex;
            vb.size=v.size()*sizeof(TV);
            vb.data=v.data(); vb.label="terrain";
            reservePreparationBytes(v.size(),sizeof(TV));
            next.vb=dev.createBuffer(vb);
            if(!next.vb.valid())
                throw std::runtime_error("TerrainRenderer: vertex upload failed");
            gfx::BufferDesc ib;
            ib.type=gfx::BufferType::Index;
            ib.size=idx.size()*sizeof(uint32_t);
            ib.data=idx.data(); ib.label="terrain";
            reservePreparationBytes(idx.size(),sizeof(uint32_t));
            next.ib=dev.createBuffer(ib);
            if(!next.ib.valid())
                throw std::runtime_error("TerrainRenderer: index upload failed");
            next.indexCount=uint32_t(idx.size());
            next.vertexCount=uint32_t(v.size());
        }catch(...) {
            // Unpublished handles have not been submitted to any frame.
            if(next.ib.valid())dev.destroyBuffer(next.ib);
            if(next.vb.valid())dev.destroyBuffer(next.vb);
            throw;
        }
    }
    retiredMeshes_.retireBuffer(dev,out.vb);
    retiredMeshes_.retireBuffer(dev,out.ib);
    out=next;
}

// ------------------------------------------------------------------ cell look

int TerrainRenderer::kindOf(const MapGrid& g, size_t i, bool flatDig) const {
    const uint8_t f = g.flags[i];
    if (!(f & kCellGround) && (f & kCellWater)) return kKindLakebed;
    if (flatDig && g.dig[i]) return kKindDug;
    if (!(f & kCellGround)) return kKindBlocked;
    if (f & kCellRock) return kKindRock;
    if (f & kCellRamp) return kKindRamp;
    if (f & kCellWater) return kKindShallows;
    if (f & kCellNoBuild) return kKindSand;
    return kKindGrass;
}

// Classic cell colours without the dynamic part (fields, creep: tinted in the vertex shader).
float3 TerrainRenderer::cellColor(const MapGrid& g, int x, int y, bool flatDig) const {
    const size_t i = terrain::cellIndexClamped(g, x, y);
    const float n = noiseOf(x, y);
    const int kind = kindOf(g, i, flatDig);
    float3 c;
    if (look_.hasColor[kind]) c = look_.color[kind] * (0.96f + n * 0.08f);
    else switch (kind) {
        case kKindLakebed: c = {0.32f, 0.40f, 0.30f}; break;
        case kKindDug: c = {0.46f + n * 0.04f, 0.41f + n * 0.03f, 0.34f}; break;        // under diggable rock: earthen floor once dug
        case kKindBlocked: c = {0.47f + n * 0.05f, 0.45f + n * 0.05f, 0.42f}; break;
        case kKindRock: c = {0.56f + n * 0.04f, 0.50f + n * 0.04f, 0.44f}; break;       // buildable rock plate
        case kKindRamp: c = {0.55f + n * 0.03f, 0.50f, 0.36f}; break;                   // ramp: packed earth
        case kKindShallows: c = {0.42f, 0.55f, 0.50f}; break;
        case kKindSand: c = {0.60f + n * 0.04f, 0.52f, 0.38f}; break;                   // path / sand
        default: c = {0.33f + n * 0.05f, 0.52f + n * 0.06f, 0.24f + n * 0.03f}; break; // grass
    }
    if (!heightfield_) c = c * (1.0f + float(g.level[i]) * 0.05f);   // plateaus a bit brighter
    return c;
}

// ------------------------------------------------------------------ build

uint64_t TerrainRenderer::chunkHash(const MapGrid& g, const Chunk& c, bool flatDig) const {
    Hasher h;
    if (heightfield_) {
        for (int y = std::max(0, c.y0 - 1); y <= std::min(g.h, c.y1 + 1); ++y)
            for (int x = std::max(0, c.x0 - 1); x <= std::min(g.w, c.x1 + 1); ++x) h.add(uint64_t(uint16_t(g.corner[size_t(g.cornerIndex(x, y))])));
    }
    for (int y = std::max(0, c.y0 - 2); y < std::min(g.h, c.y1 + 2); ++y)
        for (int x = std::max(0, c.x0 - 2); x < std::min(g.w, c.x1 + 2); ++x) {
            size_t i = size_t(g.index(x, y));
            // cell colour inputs (vertex colours) + geometry inputs (levels mode)
            h.add(uint64_t(g.flags[i] & (kCellGround | kCellWater | kCellNoBuild | kCellRamp | kCellRock)) |
                  (uint64_t(uint8_t(g.level[i])) << 8) | (uint64_t(uint16_t(g.height[i])) << 16) | (uint64_t(flatDig && g.dig[i] ? 1 : 0) << 32));
        }
    return h.h;
}

// Private full-map terrain candidate. Never invokes TerrainRenderer::shutdown():
// its kit references are borrowed from the published owner, whereas the CPU
// maps and GPU geometry/field textures in `staged_` are newly allocated.
class TerrainRenderer::PreparedMap final {
    friend class TerrainRenderer;
public:
    PreparedMap()=default;
    PreparedMap(const PreparedMap&)=delete;
    PreparedMap& operator=(const PreparedMap&)=delete;
    ~PreparedMap() noexcept {
        if(committed_ || !staged_ || !device_)return;
        auto dropBuffer=[this](gfx::BufferHandle& buffer) noexcept {
            if(!buffer.valid())return;
            try {device_->destroyBuffer(buffer);}catch(...) {}
            buffer={};
        };
        auto dropTexture=[this](gfx::TextureHandle& texture) noexcept {
            if(!texture.valid())return;
            try {device_->destroyTexture(texture);}catch(...) {}
            texture={};
        };
        for(auto& chunk:staged_->chunks_)
            for(auto& lod:chunk.lods) {
                dropBuffer(lod.vb);
                dropBuffer(lod.ib);
            }
        dropBuffer(staged_->apron_.vb);
        dropBuffer(staged_->apron_.ib);
        dropBuffer(staged_->water_.vb);
        dropBuffer(staged_->water_.ib);
        for(auto* texture:{&staged_->cellTex_,&staged_->dynTex_,
                           &staged_->splatTex_,&staged_->visionTex_,
                           &staged_->detailNrmTex_,&staged_->detailSplatTex_,
                           &staged_->heightField_.tex})
            dropTexture(*texture);
        // Shader, layer textures and fallback texture kit belong to the
        // original live TerrainRenderer and are NOT destroyed here.
    }
private:
    TerrainRenderer* owner_=nullptr;
    gfx::Device* device_=nullptr;
    uint64_t revision_=0;
    bool committed_=false;
    std::unique_ptr<TerrainRenderer> staged_;
    std::shared_ptr<PreparedFrame> frame_;
};

gfx::TextureHandle TerrainRenderer::createTerrainTexture(
        gfx::Device& dev,const gfx::TextureDesc& desc) {
    if(preparationAllocation_) {
        // Terrain fields use R8 visibility and RGBA8 material/height data.
        if((desc.format!=gfx::PixelFormat::RGBA8 &&
            desc.format!=gfx::PixelFormat::R8) || desc.levelCount!=0 ||
           desc.width<=0 || desc.height<=0)
            throw std::logic_error("unsupported Terrain preparation texture layout");
        const size_t pixelBytes=desc.format==gfx::PixelFormat::R8?1:4;
        size_t w=size_t(desc.width),h=size_t(desc.height);
        for(;;) {
            if(w>std::numeric_limits<size_t>::max()/h)
                throw std::length_error("Terrain texture payload overflow");
            reservePreparationBytes(w*h,pixelBytes);
            if(!desc.mipmaps || (w==1 && h==1))break;
            w=std::max<size_t>(1,w/2);h=std::max<size_t>(1,h/2);
        }
    }
    return dev.createTexture(desc);
}

std::shared_ptr<TerrainRenderer::PreparedMap> TerrainRenderer::prepareMap(
        gfx::Device& dev,const MapGrid& grid,bool flatDig) {
    checkGpuDevice(dev);
    if(dev_!=&dev)
        throw std::logic_error("TerrainRenderer map prepare: wrong device");
    if(!texturesLoaded_ || !built_)
        throw std::logic_error("TerrainRenderer map prepare: live GPU kit not primed");
    if(grid.w<=0 || grid.h<=0)return {};
    auto candidate=std::make_shared<PreparedMap>();
    candidate->owner_=this;
    candidate->device_=&dev;
    candidate->revision_=publicationRevision_;
    candidate->staged_=std::make_unique<TerrainRenderer>();
    TerrainRenderer& staged=*candidate->staged_;
    staged.preparationAllocation_=topologyBudget_.acquire();
    staged.dev_=&dev;
    staged.gpuDeviceLease_=gpuDeviceLease_;
    staged.rules_=rules_;
    staged.look_=look_;
    staged.mapSurfaceEnabled_=mapSurfaceEnabled_;
    staged.materialLayer_=materialLayer_;
    staged.hasFileDetail_=hasFileDetail_;
    staged.fileDetail_=fileDetail_;
    staged.fileSplat_=fileSplat_;
    staged.fileDetailChanged_=fileDetailChanged_;
    // A fully prepared, *published* material kit stays in its current owner.
    // No material cache, shader or pipeline allocations are made in staging.
    staged.texturesLoaded_=true;
    std::copy(std::begin(layerTex_),std::end(layerTex_),std::begin(staged.layerTex_));
    std::copy(std::begin(layerNrm_),std::end(layerNrm_),std::begin(staged.layerNrm_));
    staged.cliffTex_=cliffTex_;staged.cliffNrm_=cliffNrm_;
    staged.white_=white_;staged.flatN_=flatN_;staged.gray_=gray_;
    staged.fogLook_=fogLook_;
    staged.layerNormalAtlas_=layerNormalAtlas_;
    staged.layerNormalAtlasSize_=layerNormalAtlasSize_;
    // Borrow the immutable live kit; the frame candidate owns only misses.
    std::copy(std::begin(shaders_),std::end(shaders_),std::begin(staged.shaders_));
    std::copy(std::begin(pipes_),std::end(pipes_),std::begin(staged.pipes_));
    std::copy(std::begin(shaderFailed_),std::end(shaderFailed_),
              std::begin(staged.shaderFailed_));
    (void)staged.build(dev,grid,flatDig); // complete private rebuild
    return candidate;
}

float TerrainRenderer::preparedSurfaceAt(
        const std::shared_ptr<PreparedMap>& candidate,
        const MapGrid& grid,float x,float z) const {
    if(!candidate || candidate->owner_!=this ||
       candidate->device_!=dev_ || candidate->committed_ ||
       candidate->revision_!=publicationRevision_ || !candidate->staged_ ||
       !candidate->staged_->mapMatches(grid.w,grid.h))
        throw std::logic_error("TerrainRenderer: stale prepared surface query");
    return candidate->staged_->surfaceAt(grid,x,z);
}

bool TerrainRenderer::commitMap(std::shared_ptr<PreparedMap> candidate) noexcept {
    if(!candidate || candidate->committed_)return false;
    if(candidate->owner_!=this || candidate->device_!=dev_ ||
       candidate->revision_!=publicationRevision_ || !candidate->staged_)
        std::terminate();
    TerrainRenderer& next=*candidate->staged_;
    gfx::Device& dev=*dev_;
    for(auto& chunk:chunks_)freeChunk(dev,chunk);
    retiredMeshes_.retireBuffer(dev,apron_.vb);
    retiredMeshes_.retireBuffer(dev,apron_.ib);
    retiredMeshes_.retireBuffer(dev,water_.vb);
    retiredMeshes_.retireBuffer(dev,water_.ib);
    for(auto* texture:{&cellTex_,&dynTex_,&splatTex_,&visionTex_,
                      &detailNrmTex_,&detailSplatTex_,&heightField_.tex})
        retiredMeshes_.retireTexture(dev,*texture);
    // Swaps/moves of ready handles and standard vector storage only;
    // no CPU allocations, GPU uploads, or waits are allowed in commit.
    chunks_.swap(next.chunks_);
    publishedSurfaceGrid_.swap(next.publishedSurfaceGrid_);
    apron_=std::exchange(next.apron_,{});
    water_=std::exchange(next.water_,{});
    using std::swap;
    swap(detail_,next.detail_);
    swap(cellCol_,next.cellCol_);
    swap(cellLayerOf_,next.cellLayerOf_);
    swap(cellPx_,next.cellPx_);
    swap(dynPx_,next.dynPx_);
    swap(splatPx_,next.splatPx_);
    swap(visionPx_,next.visionPx_);
    swap(nrmPx_,next.nrmPx_);
    swap(dsplatPx_,next.dsplatPx_);
    cellTex_=std::exchange(next.cellTex_,{});
    dynTex_=std::exchange(next.dynTex_,{});
    splatTex_=std::exchange(next.splatTex_,{});
    visionTex_=std::exchange(next.visionTex_,{});
    detailNrmTex_=std::exchange(next.detailNrmTex_,{});
    detailSplatTex_=std::exchange(next.detailSplatTex_,{});
    heightField_={}; // trace texture rebuilt lazily after this publication
    frameFieldsPrepared_=false;
    heightDirty_=true;
    heightW_=heightH_=0;
    heightBuiltAt_=-1e9;
    mapW_=next.mapW_;mapH_=next.mapH_;
    cw_=next.cw_;ch_=next.ch_;
    lh_=next.lh_;
    globalHash_=next.globalHash_;
    lodLevels_=next.lodLevels_;
    heightfield_=next.heightfield_;
    built_=next.built_;
    hasWater_=next.hasWater_;
    waterSheet_=next.waterSheet_;
    waterHash_=next.waterHash_;
    texW_=next.texW_;texH_=next.texH_;dynRow_=next.dynRow_;
    nrmW_=next.nrmW_;nrmH_=next.nrmH_;
    dsplatW_=next.dsplatW_;dsplatH_=next.dsplatH_;
    detailOn_=next.detailOn_;detailK_=next.detailK_;
    geoBytes_=next.geoBytes_;frame_=next.frame_;
    stats_=next.stats_;
    materialLayerChanged_=false;
    fileDetailChanged_=false;
    dirtyI0_=dirtyJ0_=1<<30;dirtyI1_=dirtyJ1_=-1;
    visionInit_=false;
    fogFieldChanged_=true;
    candidate->committed_=true;
    ++publicationRevision_;
    commitMapVisible(*candidate);
    next.preparationAllocation_.reset(); // resources now belong to published/retired owners
    return true;
}

bool TerrainRenderer::build(gfx::Device& dev,const MapGrid& g,
                            bool flatDig) {
    // Stage CPU height inputs before touching GPU resources. A late GPU
    // failure leaves both the old height snapshot and old meshes intact.
    std::unique_ptr<MapGrid> nextSurface;
    if(g.w>0 && g.h>0) {
        nextSurface=std::make_unique<MapGrid>();
        nextSurface->w=g.w;
        nextSurface->h=g.h;
        nextSurface->flags=g.flags;
        nextSurface->level=g.level;
        nextSurface->height=g.height;
        nextSurface->corner=g.corner;
    }
    const bool changed=buildInternal(dev,g,flatDig);
    if(changed)publishedSurfaceGrid_.swap(nextSurface);
    return changed;
}

bool TerrainRenderer::buildInternal(gfx::Device& dev,
                                    const MapGrid& g,bool flatDig) {
    checkGpuDevice(dev);
    ++publicationRevision_;
    auto t0 = std::chrono::steady_clock::now();
    if (g.w <= 0 || g.h <= 0) return false;
    // Resolve the stable material kit before any map transaction is published.
    if (!texturesLoaded_) loadTextures(dev);
    const float previousLh=lh_;
    const bool previousHeightfield=heightfield_;
    const int previousLodLevels=lodLevels_;
    lh_ = rules_ ? float(rules_->levelHeight.toDouble()) : 1.5f;
    heightfield_ = g.hasCorners() && g.corner.size() == size_t((g.w + 1) * (g.h + 1));
    const bool lod = heightfield_ && (look_.lod == 1 || (look_.lod < 0 && std::max(g.w, g.h) > 256));
    const bool detail = look_.smooth || hasFileDetail_;
    lodLevels_ = lod ? kMaxLod : 1;
    Hasher gh;
    gh.add(uint64_t(g.w)); gh.add(uint64_t(g.h)); gh.add(uint64_t(heightfield_)); gh.add(uint64_t(detail ? 99 : lodLevels_));
    uint32_t lhBits; std::memcpy(&lhBits, &lh_, 4); gh.add(lhBits);
    bool all = gh.h != globalHash_ || !built_;
    // Detail rebuilds are staged as one visual publication. The old CPU
    // surface, normal/splat images, coarse LOD meshes and water remain live
    // until every replacement upload has finished successfully.
    if (detail) {
        bool needsDetailRebuild=all || !detailOn_ || fileDetailChanged_ ||
            dirtyI1_>dirtyI0_ || materialLayerChanged_;
        if (!needsDetailRebuild)
            for (const auto& c:chunks_) {
                if (!c.valid || chunkHash(g,c,flatDig)!=c.hash) {
                    needsDetailRebuild=true;
                    break;
                }
            }
        if (!needsDetailRebuild) {
            // Dynamic field/creep data can change without a geometry rebuild.
            updateCellTextures(dev,g,flatDig);
            updateDynamic(dev,g,true);
            stats_.rebuilt=0;
            stats_.chunks=int(chunks_.size());
            stats_.lodMax=lodLevels_;
            stats_.detailK=detailK_;
            stats_.buildMs=std::chrono::duration<double,std::milli>(
                std::chrono::steady_clock::now()-t0).count();
            return false;
        }

        // Small same-topology edits keep clean chunk VBs/IBs and only
        // stage affected coarse LODs + copy-on-write texture generations.
        if(!all && detailOn_) {
            try {
                if(tryBuildDetailIncremental(dev,g,flatDig))return true;
            }catch(...) {
                lh_=previousLh;
                heightfield_=previousHeightfield;
                lodLevels_=previousLodLevels;
                throw;
            }
        }

        TerrainRenderer staged;
        staged.dev_=&dev;
        staged.preparationAllocation_=preparationAllocation_;
        staged.gpuDeviceLease_=gpuDeviceLease_;
        try {
            staged.rules_=rules_;
            staged.look_=look_;
            staged.lh_=lh_;
            staged.heightfield_=heightfield_;
            staged.lodLevels_=lodLevels_;
            staged.materialLayer_=materialLayer_;
            staged.hasFileDetail_=hasFileDetail_;
            staged.fileDetail_=fileDetail_;
            staged.fileSplat_=fileSplat_;
            staged.fileDetailChanged_=fileDetailChanged_;
            staged.mapW_=g.w;
            staged.mapH_=g.h;
            staged.cw_=(g.w+kChunk-1)/kChunk;
            staged.ch_=(g.h+kChunk-1)/kChunk;
            staged.frame_=frame_;
            staged.chunks_.resize(size_t(staged.cw_*staged.ch_));
            for (int cy=0;cy<staged.ch_;++cy)
                for (int cx=0;cx<staged.cw_;++cx) {
                    Chunk& c=staged.chunks_[size_t(cy*staged.cw_+cx)];
                    c.x0=cx*kChunk;
                    c.y0=cy*kChunk;
                    c.x1=std::min(g.w,c.x0+kChunk);
                    c.y1=std::min(g.h,c.y0+kChunk);
                }
            staged.updateCellTextures(dev,g,flatDig);
            // Always rebuild a complete new detail CPU surface; this is
            // intentionally conservative for editor edits and avoids any
            // published-in-place mutation of detail height or textures.
            staged.buildDetail(dev,g,flatDig,true);
            staged.buildApron(dev,g);
            staged.buildWater(dev,g,flatDig);
            staged.updateDynamic(dev,g,true);
            // Prepare a compact fallback LOD for every camera before publish.
            // Fine LODs remain lazy and follow existing primary-view policy.
            const int coarse=staged.lodLevels_-1;
            for(auto& c:staged.chunks_) {
                staged.buildDetailLod(dev,g,c,coarse);
                c.lastUse.assign(size_t(staged.lodLevels_),frame_);
            }
        } catch (...) {
            // All staging handles are private: no camera has submitted them.
            for(auto& c:staged.chunks_)
                for(auto& l:c.lods) {
                    if(l.vb.valid())dev.destroyBuffer(l.vb);
                    if(l.ib.valid())dev.destroyBuffer(l.ib);
                }
            if(staged.apron_.vb.valid())dev.destroyBuffer(staged.apron_.vb);
            if(staged.apron_.ib.valid())dev.destroyBuffer(staged.apron_.ib);
            if(staged.water_.vb.valid())dev.destroyBuffer(staged.water_.vb);
            if(staged.water_.ib.valid())dev.destroyBuffer(staged.water_.ib);
            for(auto* t:{&staged.cellTex_,&staged.dynTex_,&staged.splatTex_,
                         &staged.visionTex_,&staged.detailNrmTex_,&staged.detailSplatTex_})
                if(t->valid())dev.destroyTexture(*t);
            lh_=previousLh;
            heightfield_=previousHeightfield;
            lodLevels_=previousLodLevels;
            throw;
        }
        for(auto& c:chunks_)freeChunk(dev,c);
        retiredMeshes_.retireBuffer(dev,apron_.vb);
        retiredMeshes_.retireBuffer(dev,apron_.ib);
        retiredMeshes_.retireBuffer(dev,water_.vb);
        retiredMeshes_.retireBuffer(dev,water_.ib);
        for(auto* t:{&cellTex_,&dynTex_,&splatTex_,&visionTex_,
                     &detailNrmTex_,&detailSplatTex_})
            retiredMeshes_.retireTexture(dev,*t);
        chunks_=std::move(staged.chunks_);
        apron_=staged.apron_;
        water_=staged.water_;
        hasWater_=staged.hasWater_;
        waterSheet_=staged.waterSheet_;
        mapW_=staged.mapW_;
        mapH_=staged.mapH_;
        cw_=staged.cw_;
        ch_=staged.ch_;
        globalHash_=gh.h;
        detail_=std::move(staged.detail_);
        detailOn_=true;
        detailK_=staged.detailK_;
        geoBytes_=staged.geoBytes_;
        cellCol_=std::move(staged.cellCol_);
        cellLayerOf_=std::move(staged.cellLayerOf_);
        cellTex_=std::exchange(staged.cellTex_,gfx::TextureHandle{});
        dynTex_=std::exchange(staged.dynTex_,gfx::TextureHandle{});
        splatTex_=std::exchange(staged.splatTex_,gfx::TextureHandle{});
        visionTex_=std::exchange(staged.visionTex_,gfx::TextureHandle{});
        detailNrmTex_=std::exchange(staged.detailNrmTex_,gfx::TextureHandle{});
        detailSplatTex_=std::exchange(staged.detailSplatTex_,gfx::TextureHandle{});
        cellPx_=std::move(staged.cellPx_);
        dynPx_=std::move(staged.dynPx_);
        splatPx_=std::move(staged.splatPx_);
        visionPx_=std::move(staged.visionPx_);
        nrmPx_=std::move(staged.nrmPx_);
        dsplatPx_=std::move(staged.dsplatPx_);
        texW_=staged.texW_;
        texH_=staged.texH_;
        nrmW_=staged.nrmW_;
        nrmH_=staged.nrmH_;
        dsplatW_=staged.dsplatW_;
        dsplatH_=staged.dsplatH_;
        dynRow_=staged.dynRow_;
        visionInit_=false;
        fogFieldChanged_=true;
        fileDetailChanged_=false;
        dirtyI0_=dirtyJ0_=1<<30;
        dirtyI1_=dirtyJ1_=-1;
        materialLayerChanged_=false;
        heightDirty_=true;
        built_=true;
        stats_.rebuilt=staged.stats_.rebuilt;
        stats_.chunks=int(chunks_.size());
        stats_.lodMax=lodLevels_;
        stats_.detailK=detailK_;
        stats_.geometryBytes=geoBytes_;
        stats_.buildMs=std::chrono::duration<double,std::milli>(
            std::chrono::steady_clock::now()-t0).count();
        return true;
    }
    // Whole-map non-detail rebuild is transactional across every chunk, the
    // apron and water. Never retire any published GPU handle until the final
    // replacement upload has succeeded. The detail path is handled separately.
    if (all && !detail) {
        TerrainRenderer staged;
        staged.dev_=&dev;
        staged.preparationAllocation_=preparationAllocation_;
        staged.gpuDeviceLease_=gpuDeviceLease_;
        try {
            staged.rules_=rules_;
            staged.look_=look_;
            staged.materialLayer_=materialLayer_;
            staged.lh_=lh_;
            staged.heightfield_=heightfield_;
            staged.lodLevels_=lodLevels_;
            staged.detailOn_=false;
            staged.mapW_=g.w;
            staged.mapH_=g.h;
            staged.cw_=(g.w+kChunk-1)/kChunk;
            staged.ch_=(g.h+kChunk-1)/kChunk;
            staged.chunks_.resize(size_t(staged.cw_*staged.ch_));
            for (int cy=0; cy<staged.ch_; ++cy)
                for (int cx=0; cx<staged.cw_; ++cx) {
                    Chunk& c=staged.chunks_[size_t(cy*staged.cw_+cx)];
                    c.x0=cx*kChunk;
                    c.y0=cy*kChunk;
                    c.x1=std::min(g.w,c.x0+kChunk);
                    c.y1=std::min(g.h,c.y0+kChunk);
                    c.hash=staged.chunkHash(g,c,flatDig);
                    if (staged.heightfield_) staged.buildHeightChunk(dev,g,c);
                    else staged.buildLevelsChunk(dev,g,c,flatDig);
                    c.valid=true;
                }
            staged.buildApron(dev,g);
            staged.buildWater(dev,g,flatDig);
            // Fully populate map textures before publishing any geometry.
            staged.updateCellTextures(dev,g,flatDig);
            staged.updateDynamic(dev,g,true);
        } catch (...) {
            // These buffers belong exclusively to a not-yet-submitted map.
            // Unlike old published buffers they can be destroyed immediately.
            for (auto& c:staged.chunks_)
                for (auto& l:c.lods) {
                    if (l.vb.valid()) dev.destroyBuffer(l.vb);
                    if (l.ib.valid()) dev.destroyBuffer(l.ib);
                }
            if (staged.apron_.vb.valid()) dev.destroyBuffer(staged.apron_.vb);
            if (staged.apron_.ib.valid()) dev.destroyBuffer(staged.apron_.ib);
            if (staged.water_.vb.valid()) dev.destroyBuffer(staged.water_.vb);
            if (staged.water_.ib.valid()) dev.destroyBuffer(staged.water_.ib);
            for(auto* t:{&staged.cellTex_,&staged.dynTex_,&staged.splatTex_,&staged.visionTex_})
                if(t->valid())dev.destroyTexture(*t);
            lh_=previousLh;
            heightfield_=previousHeightfield;
            lodLevels_=previousLodLevels;
            throw;
        }
        // Publish only after the last map buffer (including water) is ready.
        for (auto& c:chunks_) freeChunk(dev,c);
        retiredMeshes_.retireBuffer(dev,apron_.vb);
        retiredMeshes_.retireBuffer(dev,apron_.ib);
        retiredMeshes_.retireBuffer(dev,water_.vb);
        retiredMeshes_.retireBuffer(dev,water_.ib);
        for(auto* t:{&cellTex_,&dynTex_,&splatTex_,&visionTex_})
            retiredMeshes_.retireTexture(dev,*t);
        chunks_=std::move(staged.chunks_);
        apron_=staged.apron_;
        water_=staged.water_;
        hasWater_=staged.hasWater_;
        waterSheet_=staged.waterSheet_;
        mapW_=g.w;
        mapH_=g.h;
        cw_=staged.cw_;
        ch_=staged.ch_;
        globalHash_=gh.h;
        // Switching from detail to ordinary terrain retires all old normal
        // and splat textures too, not just the shared per-cell textures.
        retiredMeshes_.retireTexture(dev,detailNrmTex_);
        retiredMeshes_.retireTexture(dev,detailSplatTex_);
        detail_={};
        detailOn_=false;
        detailK_=0;
        stats_.detailK=0;
        nrmPx_.clear();
        dsplatPx_.clear();
        nrmW_=nrmH_=dsplatW_=dsplatH_=0;
        geoBytes_=0;
        cellTex_=std::exchange(staged.cellTex_,gfx::TextureHandle{});
        dynTex_=std::exchange(staged.dynTex_,gfx::TextureHandle{});
        splatTex_=std::exchange(staged.splatTex_,gfx::TextureHandle{});
        visionTex_=std::exchange(staged.visionTex_,gfx::TextureHandle{});
        cellPx_=std::move(staged.cellPx_);
        dynPx_=std::move(staged.dynPx_);
        splatPx_=std::move(staged.splatPx_);
        visionPx_=std::move(staged.visionPx_);
        cellLayerOf_=std::move(staged.cellLayerOf_);
        texW_=staged.texW_;
        texH_=staged.texH_;
        dynRow_=staged.dynRow_;
        visionInit_=false;
        fogFieldChanged_=true;
        heightDirty_=true;
        built_=true;
        stats_.rebuilt=int(chunks_.size());
        stats_.chunks=int(chunks_.size());
        stats_.lodMax=lodLevels_;
        stats_.buildMs=std::chrono::duration<double,std::milli>(
            std::chrono::steady_clock::now()-t0).count();
        return true;
    }
    if (all) {
        for (auto& c : chunks_) freeChunk(dev, c);
        mapW_ = g.w; mapH_ = g.h;
        cw_ = (g.w + kChunk - 1) / kChunk; ch_ = (g.h + kChunk - 1) / kChunk;
        chunks_.assign(size_t(cw_ * ch_), Chunk{});
        for (int cy = 0; cy < ch_; ++cy)
            for (int cx = 0; cx < cw_; ++cx) {
                Chunk& c = chunks_[size_t(cy * cw_ + cx)];
                c.x0 = cx * kChunk; c.y0 = cy * kChunk; c.x1 = std::min(g.w, c.x0 + kChunk); c.y1 = std::min(g.h, c.y0 + kChunk);
            }
        globalHash_ = gh.h;
        buildApron(dev, g);
    }
    stats_.rebuilt = 0;
    detailOn_=false;
    for(auto& c:chunks_) {
        const uint64_t h=chunkHash(g,c,flatDig);
        if(c.valid && h==c.hash)continue;
        // Build every LOD into private buffers. A late LOD failure must not
        // replace an earlier LOD of the published chunk or its bounds/hash.
        Chunk next;
        next.x0=c.x0; next.y0=c.y0;
        next.x1=c.x1; next.y1=c.y1;
        next.hash=h;
        try {
            if(heightfield_)buildHeightChunk(dev,g,next);
            else buildLevelsChunk(dev,g,next,flatDig);
        }catch(...) {
            // No renderer has ever submitted a staged chunk buffer.
            for(auto& l:next.lods) {
                if(l.vb.valid())dev.destroyBuffer(l.vb);
                if(l.ib.valid())dev.destroyBuffer(l.ib);
            }
            throw;
        }
        next.valid=true;
        for(auto& old:c.lods) {
            retiredMeshes_.retireBuffer(dev,old.vb);
            retiredMeshes_.retireBuffer(dev,old.ib);
        }
        c=std::move(next);
        ++stats_.rebuilt;
    }
    const bool changed = all || stats_.rebuilt > 0;
    updateCellTextures(dev, g, flatDig);
    updateDynamic(dev, g, true);
    if (changed) { buildWater(dev, g, flatDig); heightDirty_ = true; }
    built_ = true;
    stats_.chunks = int(chunks_.size());
    stats_.lodMax = lodLevels_;
    stats_.buildMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    return changed;
}

// Levels mode: exactly the classic mesh - per-cell top quads (separate vertices: sharp cliff edges),
// corner heights averaged over same-level cells, cliff walls where neighbouring edges don't meet,
// skirts down the map border.
void TerrainRenderer::buildLevelsChunk(gfx::Device& dev, const MapGrid& g, Chunk& c, bool flatDig) {
    const int W = g.w, H = g.h;
    auto level = [&](int x, int y) { return int(g.level[terrain::cellIndexClamped(g, x, y)]); };
    auto corners = [&](int x, int y, float* o) {
        o[0] = terrain::cellCorner(g, lh_, x, y, 0, 0, flatDig); o[1] = terrain::cellCorner(g, lh_, x, y, 1, 0, flatDig);
        o[2] = terrain::cellCorner(g, lh_, x, y, 0, 1, flatDig); o[3] = terrain::cellCorner(g, lh_, x, y, 1, 1, flatDig);
    };
    // corner heights for the chunk plus one cell east/south (walls)
    const int ex = std::min(W, c.x1 + 1), ey = std::min(H, c.y1 + 1);
    const int cwid = ex - c.x0;
    std::vector<float> ch(size_t(cwid * (ey - c.y0)) * 4);
    for (int y = c.y0; y < ey; ++y) for (int x = c.x0; x < ex; ++x) corners(x, y, &ch[size_t((y - c.y0) * cwid + (x - c.x0)) * 4]);
    auto chAt = [&](int x, int y) { return &ch[size_t((y - c.y0) * cwid + (x - c.x0)) * 4]; };
    // cell colours for the chunk plus a one cell border (corner colour averages)
    const int bx0 = std::max(0, c.x0 - 1), by0 = std::max(0, c.y0 - 1), bx1 = std::min(W, c.x1 + 1), by1 = std::min(H, c.y1 + 1);
    const int bw = bx1 - bx0;
    std::vector<float3> col(size_t(bw * (by1 - by0)));
    for (int y = by0; y < by1; ++y) for (int x = bx0; x < bx1; ++x) col[size_t((y - by0) * bw + (x - bx0))] = cellColor(g, x, y, flatDig);
    auto colAt = [&](int x, int y) { return col[size_t((y - by0) * bw + (x - bx0))]; };

    std::vector<TV> v;
    std::vector<uint32_t> idx;
    v.reserve(size_t((c.x1 - c.x0) * (c.y1 - c.y0)) * 5);
    float3 bmin{1e9f, 1e9f, 1e9f}, bmax{-1e9f, -1e9f, -1e9f};
    auto vert = [&](float3 p, float3 n, uint32_t rgba) {
        bmin = min3(bmin, p); bmax = max3(bmax, p);
        v.push_back({p, n, rgba, {p.y, 0}});
        return uint32_t(v.size() - 1);
    };
    auto quad = [&](float3 p0, float3 p1, float3 p2, float3 p3, float3 n, uint32_t c0, uint32_t c1, uint32_t c2, uint32_t c3) {
        uint32_t base = vert(p0, n, c0); vert(p1, n, c1); vert(p2, n, c2); vert(p3, n, c3);
        // front faces satisfy cross(b - a, c - a) . n > 0; fix the winding per triangle
        auto tri = [&](uint32_t i, uint32_t j, uint32_t k, float3 a, float3 bp, float3 cp) {
            if (dot(cross(bp - a, cp - a), n) >= 0) { idx.push_back(i); idx.push_back(j); idx.push_back(k); }
            else { idx.push_back(i); idx.push_back(k); idx.push_back(j); }
        };
        tri(base, base + 2, base + 1, p0, p2, p1);
        tri(base + 1, base + 2, base + 3, p1, p2, p3);
    };
    auto solid = [](float3 cc) { return rgbaf({cc.x, cc.y, cc.z, 1}); };
    auto wallColor = [&](int x, int y) {
        float n = noiseOf(x * 3, y * 7);
        return solid({0.42f + n * 0.06f, 0.38f + n * 0.05f, 0.33f + n * 0.04f});
    };
    // corner colour: average over the cells around the corner on the same level (soft ground, sharp
    // cliffs). Alpha = bit mask of those cells: the vertex shader adds their dynamic tint.
    auto cornerCol = [&](int x, int y, int cx, int cy) {
        float3 sum{};
        float cnt = 0;
        int mask = 0;
        const int lv = level(x, y);
        for (int dy = cy - 1; dy <= cy; ++dy)
            for (int dx = cx - 1; dx <= cx; ++dx) {
                int nx = x + dx, ny = y + dy;
                if (!g.inside(nx, ny) || level(nx, ny) != lv) continue;
                sum += colAt(nx, ny); cnt += 1;
                mask |= 1 << ((dx - cx + 1) + 2 * (dy - cy + 1));
            }
        float3 cc = cnt > 0 ? sum * (1.0f / cnt) : colAt(x, y);
        uint32_t rgba = rgbaf({cc.x, cc.y, cc.z, 1});
        return (rgba & 0x00ffffffu) | (uint32_t(mask) << 24);
    };
    for (int y = c.y0; y < c.y1; ++y)
        for (int x = c.x0; x < c.x1; ++x) {
            const float* k = chAt(x, y);
            float3 n = normalize({(k[0] + k[2]) - (k[1] + k[3]), 2.0f, (k[0] + k[1]) - (k[2] + k[3])});
            quad({float(x), k[0], float(y)}, {float(x + 1), k[1], float(y)}, {float(x), k[2], float(y + 1)}, {float(x + 1), k[3], float(y + 1)}, n,
                 cornerCol(x, y, 0, 0), cornerCol(x, y, 1, 0), cornerCol(x, y, 0, 1), cornerCol(x, y, 1, 1));
        }
    for (int y = c.y0; y < c.y1; ++y)
        for (int x = c.x0; x < c.x1; ++x) {
            const float* a = chAt(x, y);
            if (x + 1 < W) { // east edge: a(1,0),a(1,1) vs b(0,0),b(0,1)
                const float* bb = chAt(x + 1, y);
                float ta = a[1], tb = a[3], ua = bb[0], ub = bb[2];
                if (std::fabs(ta - ua) > 0.05f || std::fabs(tb - ub) > 0.05f) {
                    bool aHigh = ta + tb > ua + ub;
                    float3 n = aHigh ? float3{1, 0, 0} : float3{-1, 0, 0};
                    float X = float(x + 1);
                    if (aHigh) { uint32_t wc = wallColor(x, y); quad({X, ta, float(y)}, {X, tb, float(y + 1)}, {X, ua, float(y)}, {X, ub, float(y + 1)}, n, wc, wc, wc, wc); }
                    else { uint32_t wc = wallColor(x + 1, y); quad({X, ub, float(y + 1)}, {X, ua, float(y)}, {X, tb, float(y + 1)}, {X, ta, float(y)}, n, wc, wc, wc, wc); }
                }
            }
            if (y + 1 < H) { // south edge: a(0,1),a(1,1) vs b(0,0),b(1,0)
                const float* bb = chAt(x, y + 1);
                float ta = a[2], tb = a[3], ua = bb[0], ub = bb[1];
                if (std::fabs(ta - ua) > 0.05f || std::fabs(tb - ub) > 0.05f) {
                    bool aHigh = ta + tb > ua + ub;
                    float3 n = aHigh ? float3{0, 0, 1} : float3{0, 0, -1};
                    float Z = float(y + 1);
                    if (aHigh) { uint32_t wc = wallColor(x, y); quad({float(x + 1), tb, Z}, {float(x), ta, Z}, {float(x + 1), ub, Z}, {float(x), ua, Z}, n, wc, wc, wc, wc); }
                    else { uint32_t wc = wallColor(x, y + 1); quad({float(x), ua, Z}, {float(x + 1), ub, Z}, {float(x), ta, Z}, {float(x + 1), tb, Z}, n, wc, wc, wc, wc); }
                }
            }
        }
    // skirts down the map border
    for (int x = c.x0; x < c.x1; ++x) {
        if (c.y0 == 0) {
            const float* top = chAt(x, 0);
            uint32_t wc = wallColor(x, 0);
            if (top[0] > 0.0f || top[1] > 0.0f) quad({float(x + 1), top[1], 0}, {float(x), top[0], 0}, {float(x + 1), kApronY, 0}, {float(x), kApronY, 0}, {0, 0, -1}, wc, wc, wc, wc);
        }
        if (c.y1 == H) {
            const float* bot = chAt(x, H - 1);
            uint32_t wc = wallColor(x, H - 1);
            if (bot[2] > 0.0f || bot[3] > 0.0f) quad({float(x), bot[2], float(H)}, {float(x + 1), bot[3], float(H)}, {float(x), kApronY, float(H)}, {float(x + 1), kApronY, float(H)}, {0, 0, 1}, wc, wc, wc, wc);
        }
    }
    for (int y = c.y0; y < c.y1; ++y) {
        if (c.x0 == 0) {
            const float* l = chAt(0, y);
            uint32_t wc = wallColor(0, y);
            if (l[0] > 0.0f || l[2] > 0.0f) quad({0, l[0], float(y)}, {0, l[2], float(y + 1)}, {0, kApronY, float(y)}, {0, kApronY, float(y + 1)}, {-1, 0, 0}, wc, wc, wc, wc);
        }
        if (c.x1 == W) {
            const float* r = chAt(W - 1, y);
            uint32_t wc = wallColor(W - 1, y);
            if (r[1] > 0.0f || r[3] > 0.0f) quad({float(W), r[3], float(y + 1)}, {float(W), r[1], float(y)}, {float(W), kApronY, float(y + 1)}, {float(W), kApronY, float(y)}, {1, 0, 0}, wc, wc, wc, wc);
        }
    }
    c.lods.resize(1);
    upload(dev, v, idx, c.lods[0]);
    c.bmin = bmin; c.bmax = bmax;
}

// Heightfield: a smooth grid on the corners (diagonal (0,0)-(1,1), as terrain::cornerSurface). With
// LOD, level k samples every 2^k-th corner; `morph` is the height on level k+1 at the vertex, so the
// vertex shader can blend into the coarser level before the chunk switches (CDLOD geomorphing).
// Chunk edges get skirts when LOD is on (neighbours may be one level apart).
void TerrainRenderer::buildHeightChunk(gfx::Device& dev, const MapGrid& g, Chunk& c) {
    const int W = g.w, H = g.h;
    auto hAt = [&](int X, int Y) { return terrain::cornerHeight(g, X, Y); };
    auto normalAt = [&](int X, int Y) { return normalize({hAt(X - 1, Y) - hAt(X + 1, Y), 2.0f, hAt(X, Y - 1) - hAt(X, Y + 1)}); };
    auto colorAt = [&](int X, int Y) {
        float3 sum{};
        float cnt = 0;
        int mask = 0;
        for (int i = 0; i < 4; ++i) {
            int x = X - 1 + (i & 1), y = Y - 1 + (i >> 1);
            if (!g.inside(x, y)) continue;
            sum += cellColor(g, x, y, false); cnt += 1; mask |= 1 << i;
        }
        float3 cc = cnt > 0 ? sum * (1.0f / cnt) : float3{0.3f, 0.45f, 0.24f};
        return (rgbaf({cc.x, cc.y, cc.z, 1}) & 0x00ffffffu) | (uint32_t(mask) << 24);
    };
    auto wallColor = [&](int x, int y) {
        float n = noiseOf(x * 3, y * 7);
        return rgbaf({0.42f + n * 0.06f, 0.38f + n * 0.05f, 0.33f + n * 0.04f, 1});
    };
    c.lods.resize(size_t(lodLevels_));
    float3 bmin{1e9f, 1e9f, 1e9f}, bmax{-1e9f, -1e9f, -1e9f};
    for (int k = 0; k < lodLevels_; ++k) {
        const int s = 1 << k;
        std::vector<TV> v;
        std::vector<uint32_t> idx;
        // vertex grid: multiples of s inside the chunk, plus the chunk's far edge (may not be a multiple at the map edge)
        std::vector<int> xs, ys;
        for (int x = c.x0; x < c.x1; x += s) xs.push_back(x);
        xs.push_back(c.x1);
        for (int y = c.y0; y < c.y1; y += s) ys.push_back(y);
        ys.push_back(c.y1);
        const int nx = int(xs.size()), ny = int(ys.size());
        auto morphAt = [&](int X, int Y) {
            if (k + 1 >= lodLevels_) return hAt(X, Y);
            const int S = 2 * s;
            int X0 = (X / S) * S, Y0 = (Y / S) * S;
            if (X0 == X && Y0 == Y) return hAt(X, Y);
            int X1 = std::min(X0 + S, W), Y1 = std::min(Y0 + S, H);
            float fx = X1 > X0 ? float(X - X0) / float(X1 - X0) : 0.0f, fz = Y1 > Y0 ? float(Y - Y0) / float(Y1 - Y0) : 0.0f;
            return terrain::quadSurface(hAt(X0, Y0), hAt(X1, Y0), hAt(X0, Y1), hAt(X1, Y1), fx, fz);
        };
        v.reserve(size_t(nx * ny) + size_t(nx + ny) * 4);
        for (int j = 0; j < ny; ++j)
            for (int i = 0; i < nx; ++i) {
                int X = xs[size_t(i)], Y = ys[size_t(j)];
                float3 p{float(X), hAt(X, Y), float(Y)};
                if (k == 0) { bmin = min3(bmin, p); bmax = max3(bmax, p); }
                v.push_back({p, normalAt(X, Y), colorAt(X, Y), {morphAt(X, Y), 0}});
            }
        // Shared quad diagonal/winding; terrain LOD skirts are still
        // authored by this host immediately after these indices.
        ::aiverse::view::appendQuadGridIndices(idx,nx,ny);
        auto vi = [&](int i, int j) { return uint32_t(j * nx + i); };
        // skirts: hide cracks between chunks on different LOD levels
        if (lodLevels_ > 1) {
            const float depth = 0.3f + 0.25f * float(s);
            auto skirt = [&](uint32_t ia, uint32_t ib, float3 out) {
                TV a = v[ia], b = v[ib];
                TV a2 = a, b2 = b;
                a2.pos.y -= depth; b2.pos.y -= depth; a2.morph[0] -= depth; b2.morph[0] -= depth;
                uint32_t base = uint32_t(v.size());
                v.push_back(a); v.push_back(b); v.push_back(a2); v.push_back(b2);
                float3 e = b.pos - a.pos, dn{0, -1, 0};
                if (dot(cross(e, dn), out) >= 0) idx.insert(idx.end(), {base, base + 2, base + 1, base + 1, base + 2, base + 3});
                else idx.insert(idx.end(), {base, base + 1, base + 2, base + 1, base + 3, base + 2});
            };
            for (int i = 0; i + 1 < nx; ++i) { skirt(vi(i, 0), vi(i + 1, 0), {0, 0, -1}); skirt(vi(i, ny - 1), vi(i + 1, ny - 1), {0, 0, 1}); }
            for (int j = 0; j + 1 < ny; ++j) { skirt(vi(0, j), vi(0, j + 1), {-1, 0, 0}); skirt(vi(nx - 1, j), vi(nx - 1, j + 1), {1, 0, 0}); }
        }
        // walls down the map border to the apron
        auto border = [&](int X0, int Y0, int X1, int Y1, float3 n, int cx, int cy) {
            float h0 = hAt(X0, Y0), h1 = hAt(X1, Y1);
            if (h0 <= 0.0f && h1 <= 0.0f) return;
            uint32_t wc = wallColor(cx, cy);
            uint32_t base = uint32_t(v.size());
            v.push_back({{float(X0), h0, float(Y0)}, n, wc, {h0, 0}});
            v.push_back({{float(X1), h1, float(Y1)}, n, wc, {h1, 0}});
            v.push_back({{float(X0), kApronY, float(Y0)}, n, wc, {kApronY, 0}});
            v.push_back({{float(X1), kApronY, float(Y1)}, n, wc, {kApronY, 0}});
            float3 e = float3{float(X1 - X0), 0, float(Y1 - Y0)};
            if (dot(cross(e, float3{0, -1, 0}), n) >= 0) idx.insert(idx.end(), {base, base + 2, base + 1, base + 1, base + 2, base + 3});
            else idx.insert(idx.end(), {base, base + 1, base + 2, base + 1, base + 3, base + 2});
        };
        if (k == 0) {
            for (int x = c.x0; x < c.x1; ++x) {
                if (c.y0 == 0) border(x, 0, x + 1, 0, {0, 0, -1}, x, 0);
                if (c.y1 == H) border(x, H, x + 1, H, {0, 0, 1}, x, H - 1);
            }
            for (int y = c.y0; y < c.y1; ++y) {
                if (c.x0 == 0) border(0, y, 0, y + 1, {-1, 0, 0}, 0, y);
                if (c.x1 == W) border(W, y, W, y + 1, {1, 0, 0}, W - 1, y);
            }
        } else {
            for (int i = 0; i + 1 < nx; ++i) {
                if (c.y0 == 0) border(xs[size_t(i)], 0, xs[size_t(i + 1)], 0, {0, 0, -1}, xs[size_t(i)], 0);
                if (c.y1 == H) border(xs[size_t(i)], H, xs[size_t(i + 1)], H, {0, 0, 1}, xs[size_t(i)], H - 1);
            }
            for (int j = 0; j + 1 < ny; ++j) {
                if (c.x0 == 0) border(0, ys[size_t(j)], 0, ys[size_t(j + 1)], {-1, 0, 0}, 0, ys[size_t(j)]);
                if (c.x1 == W) border(W, ys[size_t(j)], W, ys[size_t(j + 1)], {1, 0, 0}, W - 1, ys[size_t(j)]);
            }
        }
        upload(dev, v, idx, c.lods[size_t(k)]);
    }
    bmin.y = std::min(bmin.y, kApronY) - 1.0f;   // skirts
    c.bmin = bmin; c.bmax = bmax;
}

// Ground around the map at the apron level, so the edge does not look cut off.
void TerrainRenderer::buildApron(gfx::Device& dev, const MapGrid& g) {
    const float W = float(g.w), H = float(g.h), A = float(kApron);
    std::vector<TV> v;
    std::vector<uint32_t> idx;
    const uint32_t col = rgbaf({0.30f, 0.45f, 0.24f, 1}); // flat: per-cell noise would show as a checkerboard
    auto rect = [&](float x0, float z0, float x1, float z1) {
        uint32_t b = uint32_t(v.size());
        for (float3 p : {float3{x0, kApronY, z0}, float3{x1, kApronY, z0}, float3{x0, kApronY, z1}, float3{x1, kApronY, z1}})
            v.push_back({p, {0, 1, 0}, col, {kApronY, 0}});
        idx.insert(idx.end(), {b, b + 2, b + 1, b + 1, b + 2, b + 3});
    };
    rect(-A, -A, W + A, 0);
    rect(-A, H, W + A, H + A);
    rect(-A, 0, 0, H);
    rect(W, 0, W + A, H);
    upload(dev, v, idx, apron_);
}

// Water. Sheet (classic): one translucent plane slightly below ground level, visible where the
// terrain dips. Bodies: every connected water area gets its own surface just below its lowest shore
// corner, so lakes on plateaus and heightfield valleys hold water at their own level.
void TerrainRenderer::buildWater(gfx::Device& dev, const MapGrid& g, bool flatDig) {
    // The existing water mesh can still be referenced by submitted cameras.
    // Publish replacement only after the new mesh has uploaded successfully.
    auto publish=[&](Mesh replacement,bool has,bool sheet) {
        retiredMeshes_.retireBuffer(dev,water_.vb);
        retiredMeshes_.retireBuffer(dev,water_.ib);
        water_=replacement;
        hasWater_=has;
        waterSheet_=sheet;
    };
    bool nextHasWater=false;
    const int W = g.w, H = g.h;
    for (auto f : g.flags) if ((f & kCellWater) && !(f & kCellGround)) { nextHasWater = true; break; }
    const bool nextWaterSheet=look_.water==TerrainLook::Water::Sheet ||
        (look_.water==TerrainLook::Water::Auto && !heightfield_);
    const auto uploadWater=[&](const MeshBuilder& builder) {
        reservePreparationBytes(builder.vertices().size(),sizeof(MeshVertex));
        // Conservative upper bound: MeshBuilder may choose 16-bit indices.
        reservePreparationBytes(builder.indices().size(),sizeof(uint32_t));
        return builder.upload(dev);
    };
    MeshBuilder wb;
    wb.color(rgba(255, 255, 255));
    if (nextWaterSheet) {
        if (!nextHasWater) { publish({},false,nextWaterSheet); return; }
        const float a = float(kApron), y = look_.waterOffset;
        wb.quad({-a, y, -a}, {-a, y, float(H) + a}, {float(W) + a, y, float(H) + a}, {float(W) + a, y, -a});
        publish(uploadWater(wb),true,nextWaterSheet);
        return;
    }
    std::vector<int> body(size_t(W * H), -1);
    std::vector<int> stack;
    int bodies = 0;
    auto isWater = [&](int x, int y) { return (g.flags[size_t(g.index(x, y))] & kCellWater) != 0; };
    for (int y0 = 0; y0 < H; ++y0)
        for (int x0 = 0; x0 < W; ++x0) {
            if (!isWater(x0, y0) || body[size_t(g.index(x0, y0))] >= 0) continue;
            const int id = bodies++;
            std::vector<int> cells;
            bool deep = false;
            float rim = 1e9f, top = -1e9f;
            stack.assign(1, g.index(x0, y0));
            body[size_t(g.index(x0, y0))] = id;
            while (!stack.empty()) {
                int i = stack.back(); stack.pop_back();
                cells.push_back(i);
                int x = i % W, y = i / W;
                if (!(g.flags[size_t(i)] & kCellGround)) deep = true;
                for (auto [dx, dy] : {std::pair{1, 0}, std::pair{-1, 0}, std::pair{0, 1}, std::pair{0, -1}}) {
                    int nx = x + dx, ny = y + dy;
                    if (!g.inside(nx, ny)) continue;
                    size_t ni = size_t(g.index(nx, ny));
                    if (isWater(nx, ny)) { if (body[ni] < 0) { body[ni] = id; stack.push_back(int(ni)); } continue; }
                    // shore: the ground of the land cell next to the water (its centre; corners dip into the lake)
                    rim = std::min(rim, detailOn_ ? detail_.sample(float(nx) + 0.5f, float(ny) + 0.5f) : terrain::heightAt(g, lh_, float(nx) + 0.5f, float(ny) + 0.5f));
                }
                for (int cy = 0; cy <= 1; ++cy) for (int cx = 0; cx <= 1; ++cx) top = std::max(top, terrain::gridCorner(g, lh_, x + cx, y + cy, flatDig));
            }
            if (!deep && look_.water == TerrainLook::Water::Auto && !heightfield_) continue; // shallows alone: no surface
            const float y = (rim < 1e8f ? rim : top) + look_.waterOffset;
            // detail surface: the water reaches one cell under the shore, the smooth bank cuts its outline
            if (detailOn_) {
                std::vector<int> grown = cells;
                std::vector<uint8_t> in(size_t(W * H), 0);
                for (int c : cells) in[size_t(c)] = 1;
                for (int c : cells)
                    for (auto [dx, dy] : {std::pair{1, 0}, std::pair{-1, 0}, std::pair{0, 1}, std::pair{0, -1}, std::pair{1, 1}, std::pair{-1, -1}, std::pair{1, -1}, std::pair{-1, 1}}) {
                        const int nx = c % W + dx, ny = c / W + dy;
                        if (!g.inside(nx, ny) || in[size_t(ny * W + nx)]) continue;
                        in[size_t(ny * W + nx)] = 1; grown.push_back(ny * W + nx);
                    }
                cells.swap(grown);
            }
            // rows of cells merged into runs
            std::sort(cells.begin(), cells.end());
            for (size_t i = 0; i < cells.size();) {
                size_t j = i + 1;
                while (j < cells.size() && cells[j] == cells[j - 1] + 1 && cells[j] / W == cells[i] / W) ++j;
                float x0f = float(cells[i] % W), x1f = float(cells[j - 1] % W + 1), z0 = float(cells[i] / W), z1 = z0 + 1;
                // Kingston shoreline depth tint v1
                auto vertex = [&](float x, float z) {
                    float distance = 3.0f;
                    for (int dz = -3; dz <= 2; ++dz) for (int dx = -3; dx <= 2; ++dx) {
                        int nx = int(x) + dx, nz = int(z) + dz;
                        if (!g.inside(nx,nz) || isWater(nx,nz)) continue;
                        float vx = float(nx)+.5f-x, vz = float(nz)+.5f-z;
                        distance = std::min(distance,std::sqrt(vx*vx+vz*vz));
                    }
                    float depth = std::clamp((distance-.7f)/2.3f,0.f,1.f);
                    return wb.addVertex({x,y,z},{0,1,0},rgba(
                        uint8_t(255-85*depth),uint8_t(255-40*depth),uint8_t(255-24*depth),255));
                };
                for (float x=x0f; x<x1f; x+=1.f) {
                    auto a=vertex(x,z0), b=vertex(x,z1), c=vertex(x+1,z1), d=vertex(x+1,z0);
                    wb.addTriangle(a,b,c); wb.addTriangle(a,c,d);
                }
                i = j;
            }
        }
    nextHasWater = !wb.empty();
    if(nextHasWater)publish(uploadWater(wb),true,nextWaterSheet);
    else publish({},false,nextWaterSheet);
}

// ------------------------------------------------------------------ per-cell textures

void TerrainRenderer::updateCellTextures(gfx::Device& dev, const MapGrid& g, bool flatDig) {
    const int W = g.w, H = g.h;
    if (texW_ != W || texH_ != H) {
        // Allocate all replacements and their CPU storage first. A failed
        // second/third GPU allocation must not destroy the published set.
        std::vector<uint8_t> nextCell(size_t(W)*size_t(H)*4,0);
        std::vector<uint8_t> nextDyn(size_t(W)*size_t(H)*4,0);
        std::vector<uint8_t> nextSplat(size_t(W)*size_t(H)*4,0);
        std::vector<uint8_t> nextVision(size_t(W)*size_t(H),255);
        gfx::TextureHandle nextCellTex{},nextDynTex{},nextSplatTex{},nextVisionTex{};
        try {
            gfx::TextureDesc td;
            td.width=W; td.height=H; td.filter=gfx::Filter::Nearest;
            td.data=nextCell.data(); td.label="terrain cells";
            nextCellTex=createTerrainTexture(dev,td);
            if(!nextCellTex.valid())throw std::runtime_error("terrain cell texture allocation failed");
            td.data=nextDyn.data(); td.label="terrain dynamic";
            nextDynTex=createTerrainTexture(dev,td);
            if(!nextDynTex.valid())throw std::runtime_error("terrain dynamic texture allocation failed");
            td.filter=gfx::Filter::Linear;
            td.data=nextSplat.data(); td.label="terrain splat";
            nextSplatTex=createTerrainTexture(dev,td);
            if(!nextSplatTex.valid())throw std::runtime_error("terrain splat texture allocation failed");
            td.format=gfx::PixelFormat::R8;
            td.data=nextVision.data(); td.label="terrain vision";
            nextVisionTex=createTerrainTexture(dev,td);
            if(!nextVisionTex.valid())throw std::runtime_error("terrain vision texture allocation failed");
        }catch(...) {
            if(nextCellTex.valid())dev.destroyTexture(nextCellTex);
            if(nextDynTex.valid())dev.destroyTexture(nextDynTex);
            if(nextSplatTex.valid())dev.destroyTexture(nextSplatTex);
            if(nextVisionTex.valid())dev.destroyTexture(nextVisionTex);
            throw;
        }
        retiredMeshes_.retireTexture(dev,cellTex_);
        retiredMeshes_.retireTexture(dev,dynTex_);
        retiredMeshes_.retireTexture(dev,splatTex_);
        retiredMeshes_.retireTexture(dev,visionTex_);
        cellTex_=nextCellTex;
        dynTex_=nextDynTex;
        splatTex_=nextSplatTex;
        visionTex_=nextVisionTex;
        cellPx_=std::move(nextCell);
        dynPx_=std::move(nextDyn);
        splatPx_=std::move(nextSplat);
        visionPx_=std::move(nextVision);
        texW_=W; texH_=H;
        visionInit_=false;
    }
    cellLayerOf_.resize(size_t(W * H));
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            size_t i = size_t(y * W + x);
            float3 c = cellColor(g, x, y, flatDig);
            uint32_t px = rgbaf({c.x, c.y, c.z, 1});
            std::memcpy(&cellPx_[i * 4], &px, 4);
            int layer = i < materialLayer_.size() && materialLayer_[i] < 4 ? materialLayer_[i] : look_.cellLayer[kindOf(g, i, flatDig)];
            cellLayerOf_[i] = uint8_t(look_.cellLayer[kindOf(g, i, flatDig)]);
            layer = std::clamp(layer, 0, std::max(0, int(look_.layers.size()) - 1));
            for (int k = 0; k < 4; ++k) splatPx_[i * 4 + size_t(k)] = k == layer ? 255 : 0;
        }
    dev.updateTexture(cellTex_, 0, 0, W, H, cellPx_.data(), W * 4);
    dev.updateTexture(splatTex_, 0, 0, W, H, splatPx_.data(), W * 4);
}

// Resource fields and creep: compared against the last upload, a band of rows per call (the whole
// map when `all`), only changed rows are uploaded.
void TerrainRenderer::updateDynamic(gfx::Device& dev, const MapGrid& g, bool all) {
    const int W = g.w, H = g.h;
    if (!dynTex_.valid() || texW_ != W || texH_ != H) return;
    const int rows = all ? H : std::clamp(65536 / std::max(W, 1), 1, H);
    int lo = H, hi = -1;
    for (int r = 0; r < rows; ++r) {
        int y = all ? r : (dynRow_ + r) % H;
        bool changed = false;
        for (int x = 0; x < W; ++x) {
            size_t i = size_t(y * W + x);
            uint8_t px[4] = {0, 0, 0, 255};
            if (g.fieldRes[i] >= 0 && g.fieldAmt[i].raw > 0) {
                float k = std::min(1.0f, float(g.fieldAmt[i].toDouble()) / 60.0f) * (0.55f + noiseOf(x, y) * 0.25f);
                px[0] = uint8_t(std::clamp(k * 255.0f + 0.5f, 1.0f, 255.0f));
                px[1] = uint8_t((g.fieldRes[i] % 3) * 127);
            }
            if (g.creep[i]) px[2] = 255;
            if (std::memcmp(&dynPx_[i * 4], px, 4) != 0) { std::memcpy(&dynPx_[i * 4], px, 4); changed = true; }
        }
        if (changed) { lo = std::min(lo, y); hi = std::max(hi, y); }
    }
    if (!all) dynRow_ = (dynRow_ + rows) % H;
    if (hi >= lo) dev.updateTexture(dynTex_, 0, lo, W, hi - lo + 1, &dynPx_[size_t(lo * W) * 4], W * 4);
}

void TerrainRenderer::updateVision(gfx::Device& dev, const std::vector<uint8_t>& vision) {
    if (!visionTex_.valid() || vision.size() != visionPx_.size()) return;
    const int W = texW_, H = texH_;
    int lo = H, hi = -1;
    for (int y = 0; y < H; ++y) {
        uint8_t* dst = &visionPx_[size_t(y * W)];
        const uint8_t* src = &vision[size_t(y * W)];
        bool changed = !visionInit_;
        for (int x = 0; x < W; ++x) {
            uint8_t v = src[x] >= 2 ? 255 : src[x] == 1 ? 128 : 0;
            if (dst[x] != v) { dst[x] = v; changed = true; }
        }
        if (changed) { lo = std::min(lo, y); hi = std::max(hi, y); }
    }
    visionInit_ = true;
    if (hi >= lo) dev.updateTexture(visionTex_, 0, lo, W, hi - lo + 1, &visionPx_[size_t(lo * W)], W);
}

// ------------------------------------------------------------------ draw

int TerrainRenderer::tierFor0() const {
    if (const char* e = std::getenv("AIVERSE_TERRAIN")) { std::string v = e; return v == "low" ? 0 : v == "medium" ? 1 : 2; }
    if (!dev_) return 2;
    const MaterialQuality q = Renderer3D::defaultMaterialQualityFor(*dev_, platform::isTouchDevice());
    return q == MaterialQuality::Low ? 0 : q == MaterialQuality::Medium ? 1 : 2;
}

int TerrainRenderer::tierFor(const Renderer3D& r) const {
    if (const char* e = std::getenv("AIVERSE_TERRAIN")) {
        std::string v = e;
        return v == "low" ? 0 : v == "medium" ? 1 : 2;
    }
    // the renderer's model material tier, capped by the device default (weak Mali / Adreno / PowerVR
    // get vertex colours, other phones splat without normal maps) - one quality switch for both
    auto tier = [](MaterialQuality q) { return q == MaterialQuality::Low ? 0 : q == MaterialQuality::Medium ? 1 : 2; };
    static const int deviceTier = dev_ ? tier(Renderer3D::defaultMaterialQualityFor(*dev_, platform::isTouchDevice())) : 2;
    return std::min(tier(r.materialQuality()), deviceTier);
}

gfx::PipelineHandle TerrainRenderer::pipeline(gfx::Device& dev, int variant, bool dynamicGi, bool compactGi) {
    if(compactGi && (!dynamicGi || variant!=0 ||
         dev.info().backend!=gfx::Backend::GLES3))return {};
    const int dbase = variant >= 3 ? 3 : 0;
    for (int tier = variant - dbase; tier >= 0; --tier) {
        const int slot = compactGi ? 12 : (dynamicGi ? 6 : 0) + dbase + tier;
        if (pipes_[slot].valid()) return pipes_[slot];
        if (shaderFailed_[slot]) continue;
        const std::string def = "#define TERRAIN_TIER " + std::to_string(tier) +
            "\n#define TERRAIN_DETAIL " + std::to_string(dbase ? 1 : 0) +
            (dynamicGi ? "\n#define DYNAMIC_GI 1\n#define DYNAMIC_GI_TERRAIN 1" : "") +
            (compactGi ? "\n#define DYNAMIC_GI_TERRAIN_COMPACT 1" : "") +
            "\n#define TERRAIN_GL_NORMAL_ATLAS " +
            std::to_string(layerNormalAtlas_.valid() && dev.info().backend ==
                           gfx::Backend::GL33 ? 1 : 0) + "\n";
        const std::string vs = def + shaders::kTerrainVS, fs = def + shaders::kTerrainFS, msl = def + shaders::kTerrainMSL;
        gfx::ShaderDesc sd;
        sd.glslVertex = vs.c_str(); sd.glslFragment = fs.c_str(); sd.msl = msl.c_str();
        sd.uniformSlots = dynamicGi ? 7 : 4;
        sd.textureSlots = compactGi ? 16 : dynamicGi ? 22 : 21;
        sd.label = dynamicGi ? "terrain + dynamic GI" : "terrain";   // tex18: height field (traced shadows); U3 + tex19/20: baked lightmap
        shaders_[slot] = dev.createShader(sd);
        if (!shaders_[slot].valid()) {
            std::fprintf(stderr, "[terrain] tier %d%s shader: %s\n", tier, dbase ? " detail" : "", dev.lastError().c_str());
            shaderFailed_[slot] = true;
            continue;
        }
        gfx::PipelineDesc p;
        p.shader = shaders_[slot];
        p.attrs[0] = {0, gfx::VertexFormat::Float3, 0};
        p.attrs[1] = {1, gfx::VertexFormat::Float3, 12};
        p.attrs[2] = {2, gfx::VertexFormat::UByte4N, 24};
        p.attrs[3] = {3, gfx::VertexFormat::Float2, 28};
        p.attrCount = 4; p.stride = sizeof(TV);
        p.indexType = gfx::IndexType::UInt32;
        p.label = "terrain";
        pipes_[slot] = dev.createPipeline(p);
        if (pipes_[slot].valid()) return pipes_[slot];
        shaderFailed_[slot] = true;
    }
    // GL3.3 may exceed its active fragment sampler budget when detail
    // textures are present. Preserve terrain using the non-detail shader.
    // A Metal backend which compiled the detailed shader retains it.
    if (dbase) {
        std::fprintf(stderr, "[terrain] detail shader unavailable; falling back to non-detail tier %d\n", variant - dbase);
        return pipeline(dev, variant - dbase, dynamicGi, compactGi);
    }
    // A failed GI shader must fall back to baseline terrain, not a black
    // surface. On GLES avoid sampling a phantom tex8/tex18/tex19 source.
    return dynamicGi ? pipeline(dev, variant, false, false) : gfx::PipelineHandle{};
}

// Dynamic fields and visibility are published with all other LENS owners.
// A staged frame owns at most two private textures and the matching CPU image
// generations. It never references the live handle for GPU writes.
class TerrainRenderer::PreparedFrame final {
    friend class TerrainRenderer;
public:
    PreparedFrame()=default;
    PreparedFrame(const PreparedFrame&)=delete;
    PreparedFrame& operator=(const PreparedFrame&)=delete;
    ~PreparedFrame() noexcept {
        if(committed_ || !device_)return;
        if(vision_.valid())try {device_->destroyTexture(vision_);}catch(...) {}
        if(dynamic_.valid())try {device_->destroyTexture(dynamic_);}catch(...) {}
        for(auto& item:lods_) {
            if(item.mesh.vb.valid())try {device_->destroyBuffer(item.mesh.vb);}catch(...) {}
            if(item.mesh.ib.valid())try {device_->destroyBuffer(item.mesh.ib);}catch(...) {}
        }
        if(traceHeight_.tex.valid())
            try {device_->destroyTexture(traceHeight_.tex);}catch(...) {}
        if(pipeline_.valid())
            try {device_->destroyPipeline(pipeline_);}catch(...) {}
        if(shader_.valid())
            try {device_->destroyShader(shader_);}catch(...) {}
    }
private:
    TerrainRenderer* owner_=nullptr;
    gfx::Device* device_=nullptr;
    uint64_t revision_=0;
    bool committed_=false;
    gfx::TextureHandle dynamic_{},vision_{};
    std::vector<uint8_t> dynamicPx_,visionPx_;
    struct PendingLod {
        size_t chunk=0,level=0,bytes=0;
        Lod mesh{};
    };
    std::vector<PendingLod> lods_;
    gfx::ShaderHandle shader_{};
    gfx::PipelineHandle pipeline_{};
    int pipelineSlot_=-1;
    render::HeightField traceHeight_{};
    double traceBuiltAt_=-1e9;
    int traceWidth_=0,traceHeightPixels_=0;
    int nextDynRow_=0;
    bool nextVisionInit_=false;
};

std::shared_ptr<TerrainRenderer::PreparedFrame> TerrainRenderer::prepareFrame(
        const MapGrid& g,const std::vector<uint8_t>* vision,
        const std::vector<uint8_t>* filteredVisibility) {
    if(!dev_ || !built_ || mapW_!=g.w || mapH_!=g.h ||
       texW_!=g.w || texH_!=g.h || g.w<=0 || g.h<=0)
        return {};
    checkGpuDevice(*dev_);
    const size_t count=size_t(g.w)*size_t(g.h);
    if(dynPx_.size()!=count*4 || visionPx_.size()!=count ||
       g.fieldRes.size()<count || g.fieldAmt.size()<count ||
       g.creep.size()<count)
        throw std::logic_error("TerrainRenderer: malformed dynamic map fields");
    auto next=std::make_shared<PreparedFrame>();
    next->owner_=this;
    next->device_=dev_;
    next->revision_=publicationRevision_;
    next->dynamicPx_=dynPx_;
    next->nextDynRow_=dynRow_;
    next->nextVisionInit_=visionInit_;
    const int W=g.w,H=g.h;
    const int rows=std::clamp(65536/std::max(W,1),1,H);
    bool changedDynamic=false;
    for(int row=0;row<rows;++row) {
        const int y=(dynRow_+row)%H;
        for(int x=0;x<W;++x) {
            const size_t i=size_t(y)*size_t(W)+size_t(x);
            uint8_t px[4]={0,0,0,255};
            if(g.fieldRes[i]>=0 && g.fieldAmt[i].raw>0) {
                const float k=std::min(1.0f,float(g.fieldAmt[i].toDouble())/60.0f)*
                    (0.55f+noiseOf(x,y)*0.25f);
                px[0]=uint8_t(std::clamp(k*255.0f+0.5f,1.0f,255.0f));
                px[1]=uint8_t((g.fieldRes[i]%3)*127);
            }
            if(g.creep[i])px[2]=255;
            uint8_t* dst=&next->dynamicPx_[i*4];
            if(std::memcmp(dst,px,4)!=0) {
                std::memcpy(dst,px,4);
                changedDynamic=true;
            }
        }
    }
    next->nextDynRow_=(dynRow_+rows)%H;
    if(changedDynamic) {
        gfx::TextureDesc td;
        td.width=W;td.height=H;td.filter=gfx::Filter::Nearest;
        td.data=next->dynamicPx_.data();
        td.label="terrain dynamic COW";
        next->dynamic_=createTerrainTexture(*dev_,td);
        if(!next->dynamic_.valid())
            throw std::runtime_error("TerrainRenderer: private dynamic texture allocation failed");
    }
    // Heightfield terrain samples the processed visibility field; classic
    // terrain continues to use the separate Fog Sheet owner. A resized or
    // changed Fog candidate may supply its unpublished processed pixel field.
    if(heightfield_ && vision && vision->size()==count) {
        const bool filtered=filteredVisibility && filteredVisibility->size()==count;
        next->visionPx_=visionPx_;
        bool changed=!visionInit_;
        for(size_t i=0;i<count;++i) {
            const uint8_t v=filtered?(*filteredVisibility)[i]:
                ((*vision)[i]>=2?uint8_t(255):
                 ((*vision)[i]==1?uint8_t(128):uint8_t(0)));
            if(next->visionPx_[i]!=v) {
                next->visionPx_[i]=v;
                changed=true;
            }
        }
        next->nextVisionInit_=true;
        if(changed) {
            gfx::TextureDesc td;
            td.width=W;td.height=H;td.filter=gfx::Filter::Linear;
            td.format=gfx::PixelFormat::R8;
            td.data=next->visionPx_.data();
            td.label="terrain visibility COW";
            next->vision_=createTerrainTexture(*dev_,td);
            if(!next->vision_.valid())
                throw std::runtime_error("TerrainRenderer: private visibility texture allocation failed");
        }
    }
    return next;
}

// Prepare only visible missing fine LODs. No published chunk, GPU
// allocation or byte accounting is modified until commitFrame.
std::shared_ptr<TerrainRenderer::PreparedFrame>
TerrainRenderer::prepareFrameVisible(
        const Renderer3D& camera,const MapGrid& g,
        const std::vector<uint8_t>* vision,
        const std::vector<uint8_t>* filteredVisibility) {
    auto pending=prepareFrame(g,vision,filteredVisibility);
    if(!pending || !dev_)return pending;
    if(detailOn_) {
    constexpr size_t kMaxNewLods=16;
    pending->lods_.reserve(kMaxNewLods);
    const auto frustum=::aiverse::view::SideFrustum::fromViewProjection(
        camera.viewProj());
    const float3 eye=camera.camera().eye();
    for(size_t i=0;i<chunks_.size() &&
                   pending->lods_.size()<kMaxNewLods;++i) {
        const Chunk& published=chunks_[i];
        if(!published.valid || published.lods.empty() ||
           !frustum.visibleAABB(published.bmin,published.bmax))
            continue;
        float3 closest=max3(published.bmin,min3(eye,published.bmax));
        const float distance=length(closest-eye);
        float range=look_.detailRange*4.0f/float(std::max(detailK_,1));
        int lod=0;
        while(lod+1<lodLevels_ && distance>=range) {
            ++lod;range*=2.0f;
        }
        if(size_t(lod)>=published.lods.size() ||
           published.lods[size_t(lod)].indexCount)
            continue;
        // The shallow chunk copy borrows existing LOD handles. Clear the
        // target slot so buildDetailLod only creates a new private pair.
        Chunk staging=published;
        staging.lods[size_t(lod)]={};
        size_t bytes=0;
        buildDetailLod(*dev_,g,staging,lod,&bytes);
        Lod& newLod=staging.lods[size_t(lod)];
        pending->lods_.push_back({i,size_t(lod),bytes,
                                 std::exchange(newLod,Lod{})});
    }
    }

    // Prepare shader/pipeline variants before any multi-owner publication.
    // Shader and pipeline handles are unpublished and owned by this candidate
    // until commitFrame transfers them. A lower quality tier is attempted on
    // backend compile failures, preserving the existing fallback policy.
    const bool compactGi=camera.dynamicGiOverlayEnabled() && dev_->info().backend==gfx::Backend::GLES3 &&
        !detailOn_ && !camera.bakedActive() && !camera.traceActive();
    const bool dynamicGi=compactGi || (camera.dynamicGiOverlayEnabled() &&
        (dev_->info().backend==gfx::Backend::Metal || dev_->info().backend==gfx::Backend::Vulkan));
    const int first=compactGi?0:tierFor(camera);
    const int dbase=compactGi?12:(detailOn_?3:0)+(dynamicGi?6:0);
    for(int tier=first;tier>=0;--tier) {
        const int slot=dbase+tier;
        if(pipes_[slot].valid())break;
        if(shaderFailed_[slot])continue;
        const std::string def="#define TERRAIN_TIER "+
            std::to_string(tier)+"\n#define TERRAIN_DETAIL "+
            std::to_string(detailOn_?1:0)+"\n"+
            (dynamicGi?"#define DYNAMIC_GI 1\n#define DYNAMIC_GI_TERRAIN 1\n":"")+
            (compactGi?"#define DYNAMIC_GI_TERRAIN_COMPACT 1\n":"");
        const std::string vs=def+shaders::kTerrainVS;
        const std::string fs=def+shaders::kTerrainFS;
        const std::string msl=def+shaders::kTerrainMSL;
        const bool borrowedShader=shaders_[slot].valid();
        if(!borrowedShader) {
            gfx::ShaderDesc sd;
            sd.glslVertex=vs.c_str();
            sd.glslFragment=fs.c_str();
            sd.msl=msl.c_str();
            sd.uniformSlots=dynamicGi?7:4;
            sd.textureSlots=compactGi?16:dynamicGi?22:21;
            sd.label="terrain staged pipeline";
            pending->shader_=dev_->createShader(sd);
            if(!pending->shader_.valid())continue;
        }
        gfx::PipelineDesc p;
        p.shader=borrowedShader?shaders_[slot]:pending->shader_;
        p.attrs[0]={0,gfx::VertexFormat::Float3,0};
        p.attrs[1]={1,gfx::VertexFormat::Float3,12};
        p.attrs[2]={2,gfx::VertexFormat::UByte4N,24};
        p.attrs[3]={3,gfx::VertexFormat::Float2,28};
        p.attrCount=4;
        p.stride=sizeof(TV);
        p.indexType=gfx::IndexType::UInt32;
        p.label="terrain staged pipeline";
        pending->pipeline_=dev_->createPipeline(p);
        if(pending->pipeline_.valid()) {
            pending->pipelineSlot_=slot;
            break;
        }
        // Failed private shaders were never submitted and need no idle proof.
        if(pending->shader_.valid()) {
            dev_->destroyShader(pending->shader_);
            pending->shader_={};
        }
    }

    // Never publish a frame with missing terrain material pipeline. A
    // lower-tier fallback is fine, but a fully unusable backend must fail
    // the owner transaction instead of silently publishing a blank surface.
    bool usablePipeline=pending->pipeline_.valid();
    for(int tier=first;tier>=0 && !usablePipeline;--tier)
        usablePipeline=pipes_[dbase+tier].valid();
    if(!usablePipeline)
        throw std::runtime_error("TerrainRenderer: no usable staged pipeline");

    // Traced shadows require a GPU height texture. Stage the replacement
    // privately so an exception cannot replace a prior-camera generation.
    if(camera.traceActive() && heightDirty_) {
        using clock=std::chrono::steady_clock;
        const double now=std::chrono::duration<double>(
            clock::now().time_since_epoch()).count();
        if(!heightField_.tex.valid() || now-heightBuiltAt_>=0.3) {
            const int tw=std::max(2,std::min(1024,g.w*2));
            const int th=std::max(2,std::min(1024,g.h*2));
            std::vector<float> heights(size_t(tw)*size_t(th));
            float mn=1e9f,mx=-1e9f;
            for(int j=0;j<th;++j) {
                const float z=(float(j)+0.5f)*float(g.h)/float(th);
                for(int i=0;i<tw;++i) {
                    const float x=(float(i)+0.5f)*float(g.w)/float(tw);
                    const float value=surfaceAt(g,x,z);
                    heights[size_t(j)*size_t(tw)+size_t(i)]=value;
                    mn=std::min(mn,value);
                    mx=std::max(mx,value);
                }
            }
            const float range=std::max(mx-mn,1e-3f);
            std::vector<uint8_t> pixels(heights.size()*4);
            for(size_t i=0;i<heights.size();++i) {
                const int q=std::clamp(int((heights[i]-mn)/range*
                                           65535.0f+0.5f),0,65535);
                pixels[i*4]=uint8_t(q>>8);
                pixels[i*4+1]=uint8_t(q&255);
                pixels[i*4+2]=0;pixels[i*4+3]=255;
            }
            gfx::TextureDesc desc;
            desc.width=tw;desc.height=th;
            desc.filter=gfx::Filter::Linear;
            desc.data=pixels.data();
            desc.label="terrain trace height staged";
            pending->traceHeight_.tex=createTerrainTexture(*dev_,desc);
            if(!pending->traceHeight_.tex.valid())
                throw std::runtime_error(
                    "TerrainRenderer: private trace height upload failed");
            pending->traceHeight_.originX=0;
            pending->traceHeight_.originZ=0;
            pending->traceHeight_.extentX=float(g.w);
            pending->traceHeight_.extentZ=float(g.h);
            pending->traceHeight_.heightMin=mn;
            pending->traceHeight_.heightRange=
                mx-mn>1e-3f?mx-mn:0.0f;
            pending->traceBuiltAt_=now;
            pending->traceWidth_=tw;
            pending->traceHeightPixels_=th;
        }
    }
    return pending;
}

void TerrainRenderer::prepareMapVisible(
        const std::shared_ptr<PreparedMap>& candidate,
        const Renderer3D& renderer,const MapGrid& grid,
        const std::vector<uint8_t>* vision,
        const std::vector<uint8_t>* filteredVisibility) {
    if(!candidate || candidate->owner_!=this || candidate->device_!=dev_ ||
       candidate->committed_ || candidate->revision_!=publicationRevision_ ||
       !candidate->staged_ || candidate->frame_ ||
       !candidate->staged_->mapMatches(grid.w,grid.h))
        throw std::logic_error("TerrainRenderer: stale topology frame prepare");
    candidate->frame_=candidate->staged_->prepareFrameVisible(
        renderer,grid,vision,filteredVisibility);
    if(!candidate->frame_)
        throw std::runtime_error("TerrainRenderer: topology frame prepare failed");
}

void TerrainRenderer::commitMapVisible(PreparedMap& candidate) noexcept {
    if(!candidate.frame_)return;
    // commitMap has just transferred the exact staged chunk/texture layout.
    // No intervening owner mutation or external frame rebinding is allowed.
    auto frame=std::move(candidate.frame_);
    if(frame->owner_!=candidate.staged_.get() || frame->device_!=dev_ ||
       frame->revision_!=candidate.staged_->publicationRevision_)
        std::terminate();
    frame->owner_=this;
    frame->revision_=publicationRevision_;
    commitFrame(std::move(frame));
}

void TerrainRenderer::drawCandidate(const std::shared_ptr<PreparedFrame>& next,
        Renderer3D& r,const MapGrid& g,const std::vector<uint8_t>* vision) {
    if(!next){drawPrepared(r,g,vision);drawWater(r);return;}
    if(next->owner_!=this || next->revision_!=publicationRevision_ || next->committed_)
        throw std::logic_error("Terrain: stale candidate draw");
    const bool dynamic=next->dynamic_.valid(),visible=next->vision_.valid();
    const bool trace=next->traceHeight_.tex.valid(),pipeline=next->pipeline_.valid();
    const auto exchange=[&]() noexcept {
        using std::swap;
        if(dynamic)swap(dynTex_,next->dynamic_);
        if(visible)swap(visionTex_,next->vision_);
        if(trace)swap(heightField_,next->traceHeight_);
        if(pipeline)swap(pipes_[next->pipelineSlot_],next->pipeline_);
        for(auto& item:next->lods_)swap(chunks_[item.chunk].lods[item.level],item.mesh);
    };
    exchange();try {drawPrepared(r,g,vision);drawWater(r);}catch(...) {exchange();throw;}exchange();
}
void TerrainRenderer::drawMapCandidate(const std::shared_ptr<PreparedMap>& c,
        Renderer3D& r,const MapGrid& g,const std::vector<uint8_t>* vision) {
    if(!c || c->owner_!=this || c->revision_!=publicationRevision_ || c->committed_)
        throw std::logic_error("Terrain: stale map candidate draw");
    c->staged_->drawCandidate(c->frame_,r,g,vision);
}

void TerrainRenderer::commitFrame(std::shared_ptr<PreparedFrame> next) noexcept {
    if(!next || next->committed_)return;
    if(next->owner_!=this || next->device_!=dev_ ||
       next->revision_!=publicationRevision_)
        std::terminate();
    if(next->dynamic_.valid()) {
        retiredMeshes_.retireTexture(*dev_,dynTex_);
        dynTex_=std::exchange(next->dynamic_,{});
        dynPx_.swap(next->dynamicPx_);
    }
    if(next->vision_.valid()) {
        retiredMeshes_.retireTexture(*dev_,visionTex_);
        visionTex_=std::exchange(next->vision_,{});
        visionPx_.swap(next->visionPx_);
    }
    dynRow_=next->nextDynRow_;
    visionInit_=next->nextVisionInit_;
    for(auto& item:next->lods_) {
        if(item.chunk>=chunks_.size() ||
           item.level>=chunks_[item.chunk].lods.size() ||
           chunks_[item.chunk].lods[item.level].indexCount)
            std::terminate();
        chunks_[item.chunk].lods[item.level]=
            std::exchange(item.mesh,Lod{});
        geoBytes_+=item.bytes;
    }
    if(next->pipeline_.valid()) {
        const int slot=next->pipelineSlot_;
        if(slot<0 || slot>=13 || pipes_[slot].valid() ||
           (next->shader_.valid() && shaders_[slot].valid()))
            std::terminate();
        if(next->shader_.valid())
            shaders_[slot]=std::exchange(next->shader_,{});
        pipes_[slot]=std::exchange(next->pipeline_,{});
        shaderFailed_[slot]=false;
    }
    if(next->traceHeight_.tex.valid()) {
        retiredMeshes_.retireTexture(*dev_,heightField_.tex);
        heightField_=std::exchange(next->traceHeight_,{});
        heightW_=next->traceWidth_;
        heightH_=next->traceHeightPixels_;
        heightBuiltAt_=next->traceBuiltAt_;
        heightDirty_=false;
    }
    frameFieldsPrepared_=true;
    fogFieldChanged_=false;
    next->committed_=true;
    ++publicationRevision_;
}

void TerrainRenderer::draw(Renderer3D& r, const MapGrid& g,
                           const std::vector<uint8_t>* vision, bool) {
    drawInternal(r,g,vision,false);
}
void TerrainRenderer::drawPrepared(Renderer3D& r,const MapGrid& g,
                                   const std::vector<uint8_t>* vision) {
    drawInternal(r,g,vision,true);
}
void TerrainRenderer::drawInternal(Renderer3D& r,const MapGrid& g,
                                   const std::vector<uint8_t>* vision,
                                   bool preparedOnly) {
    // terrain.render=false or entity-authored terrain suppresses the map surface.
    if(!look_.render||!mapSurfaceEnabled_){r.setHeightField({});return;}
    if (!built_ || !dev_ || g.w != mapW_ || g.h != mapH_) return;
    gfx::Device& dev = *dev_;
    // A LENS primary owner transaction has already installed the immutable
    // copy-on-write texture generations. Standalone legacy draws retain
    // the old path until their callers use prepareFrame explicitly.
    const bool stagedFields=!preparedOnly && frameFieldsPrepared_;
    if(!preparedOnly && !stagedFields)updateDynamic(dev, g, false);
    const bool fog = heightfield_ && vision && !vision->empty();
    if (fog && !preparedOnly && !stagedFields) {
        if (fogField_ && fogField_->size() == visionPx_.size()) { if (fogFieldChanged_) { visionPx_ = *fogField_; dev.updateTexture(visionTex_, 0, 0, texW_, texH_, visionPx_.data(), texW_); visionInit_ = true; } }
        else updateVision(dev, *vision);
        fogFieldChanged_ = false;
    }
    // Shared, unnormalized four side-plane AABB test (clip-depth independent).
    const auto frustum=::aiverse::view::SideFrustum::fromViewProjection(r.viewProj());
    const float3 eye = r.camera().eye();
    struct Item { Lod lod; float morphStart, morphEnd; };
    auto items = std::make_shared<std::vector<Item>>();
    if(!preparedOnly) {
        stats_.drawn = 0;
        stats_.triangles = apron_.indexCount / 3;
    }
    for (auto& c : chunks_) {
        if (!c.valid || c.lods.empty() || !frustum.visibleAABB(c.bmin,c.bmax)) continue;
        int k = 0;
        float start = 1e9f, end = 2e9f;
        if (lodLevels_ > 1) {
            float3 q = max3(c.bmin, min3(eye, c.bmax));
            float d = length(q - eye);
            // detail mode: level 0 = one quad per detail sample up to detailRange (scaled with the sample size)
            float range = detailOn_ ? look_.detailRange * 4.0f / float(std::max(detailK_, 1)) : look_.lodRange;
            while (k + 1 < lodLevels_ && d >= range) { ++k; range *= 2; }
            if (k + 1 < lodLevels_) { start = range * 0.75f; end = range; }
        }
        // A secondary camera may use a coarser prebuilt level, but it may
        // not allocate/upload a new mesh while source GPU work is in flight.
        if(detailOn_ && !c.lods[size_t(k)].indexCount &&
           !preparedOnly && !stagedFields)
            buildDetailLod(dev,g,c,k); // standalone legacy path only
        // Published primary and secondary camera frames are read-only.
        if(preparedOnly || stagedFields) {
            while(k+1<lodLevels_ && !c.lods[size_t(k)].indexCount)++k;
            if(!c.lods[size_t(k)].indexCount)
                while(k>0 && !c.lods[size_t(k)].indexCount)--k;
        }
        if(detailOn_ && !preparedOnly)c.lastUse[size_t(k)]=frame_;
        const Lod& l = c.lods[size_t(k)];
        if (!l.indexCount) continue;
        items->push_back({l, start, end});
        if(!preparedOnly) {
            ++stats_.drawn;
            stats_.triangles += l.indexCount / 3;
        }
    }
    if(!preparedOnly)++frame_;
    if (!preparedOnly && !stagedFields && detailOn_ &&
        geoBytes_ > look_.detailBudgetMB * 1048576.0) { // legacy LRU only; LENS GPU lifetime is frame-fenced
        for (auto& c : chunks_)
            for (size_t L = 0; L < c.lods.size(); ++L) {
                Lod& l = c.lods[L];
                if (!l.indexCount || c.lastUse[L] + 120 > frame_) continue;
                geoBytes_ -= std::min<size_t>(geoBytes_, size_t(l.indexCount) * 4 + size_t(l.vertexCount) * sizeof(TV));
                retiredMeshes_.retireBuffer(dev,l.vb);
                retiredMeshes_.retireBuffer(dev,l.ib);
                l={};
            }
    }
    if(!preparedOnly)stats_.geometryBytes = geoBytes_;
    if(!preparedOnly && stagedFields) {
        // Consume one prepared primary frame, never reuse a stale skip flag
        // for standalone draws on later frames.
        frameFieldsPrepared_=false;
    }
    const int tier = tierFor(r);
    const bool mobileGi = r.dynamicGiOverlayEnabled() &&
        dev.info().backend==gfx::Backend::GLES3 &&
        !detailOn_ && !r.bakedActive() && !r.traceActive();
    const bool fullGi = r.dynamicGiOverlayEnabled() &&
        (dev.info().backend==gfx::Backend::Metal ||
         dev.info().backend==gfx::Backend::Vulkan);
    const bool giRequest=mobileGi || fullGi;
    const int desiredVariant=mobileGi ? 0 : tier+(detailOn_ ? 3 : 0);
    int effectiveTier=mobileGi ? 0 : tier;
    gfx::PipelineHandle pipe{};
    if(preparedOnly || stagedFields) {
        // Secondary cameras and staged primary frames never compile shaders.
        const int first=mobileGi ? 12 : desiredVariant+(fullGi?6:0);
        const int minimum=mobileGi ? 12 : (detailOn_?3:0)+(fullGi?6:0);
        for(int i=first;i>=minimum;--i)
            if(pipes_[i].valid()) {
                pipe=pipes_[i];
                effectiveTier=i-minimum; // shader fallback must match uniforms
                break;
            }
    }else pipe=pipeline(dev,desiredVariant,giRequest,mobileGi);
    if (!pipe.valid()) return;
    bool giPipeline=false;
    for (int i=6;i<13;++i) if (pipes_[i].valid() && pipes_[i]==pipe)
        giPipeline=true;
    const bool compactGi=giPipeline && pipes_[12].valid() && pipe==pipes_[12];
    TU0 u{};
    u.pbr[0]=look_.pbr ? 1.0f:0.0f;
    u.pbr[1]=look_.pbrRoughness;
    u.pbr[3]=look_.packedPbr ? 1.0f:0.0f;
    u.lod[2] = float(mapW_); u.lod[3] = float(mapH_);
    u.mode[0] = 0.05f;
    u.mode[1] = effectiveTier >= 1 && look_.materials() ? 1.0f : 0.0f;
    u.mode[2] = effectiveTier >= 2 ? 1.0f : 0.0f;
    u.mode[3] = fog ? 1.0f : 0.0f;
    for (int i = 0; i < 4; ++i) {
        const TerrainLook::Layer* L = i < int(look_.layers.size()) ? &look_.layers[size_t(i)] : nullptr;
        u.scale[i] = L ? L->scale : 4.0f;
        u.tint[i][0] = L ? L->tint.x : 1; u.tint[i][1] = L ? L->tint.y : 1; u.tint[i][2] = L ? L->tint.z : 1;
        u.tint[i][3] = layerNrm_[i].valid() ? 1.0f : 0.0f;
    }
    u.cliff[0] = look_.cliffScale; u.cliff[1] = look_.splatStrength;
    u.cliff[2] = cliffTex_.valid() ? look_.slopeStart : 2.0f; u.cliff[3] = cliffTex_.valid() ? look_.slopeEnd : 3.0f; // no cliff texture: never
    u.cliffTint[0] = look_.cliffTint.x; u.cliffTint[1] = look_.cliffTint.y; u.cliffTint[2] = look_.cliffTint.z;
    u.cliffTint[3] = cliffNrm_.valid() ? 1.0f : 0.0f;
    u.fog[0] = 120.0f / 255.0f; u.fog[1] = 235.0f / 255.0f;
    u.fogColor[0] = 8.0f / 255.0f; u.fogColor[1] = 10.0f / 255.0f; u.fogColor[2] = 14.0f / 255.0f;
    if (fogField_) { // look from RtsFog (rules / game.json "fogOfWar")
        u.fog[0] = fogLook_.explored; u.fog[1] = fogLook_.unexplored; u.fog[2] = fogLook_.gridEverywhere ? 1.0f : 0.0f;
        for (int i = 0; i < 3; ++i) { u.fogColor[i] = fogLook_.color[i]; u.fogGrid[i] = fogLook_.gridColor[i]; }
        u.fogColor[3] = fogLook_.style == FogLook::Cells || fogLook_.style == FogLook::Grid ? 1.0f : 0.0f;
        u.fogGrid[3] = fogLook_.gridAlpha + (fogLook_.style == FogLook::Grid ? 0.18f : 0.0f);
        if (!fogLook_.show) u.mode[3] = 0.0f;
    }
    if (detailOn_ && nrmW_ > 1) {
        // world -> detail texture uv: sample = x * k, texel = sample * (T - 1) / (S - 1)
        const float tx = float(nrmW_ - 1) / float(detail_.sw() - 1), tz = float(nrmH_ - 1) / float(detail_.sh() - 1);
        u.detail[0] = float(detail_.k) * tx / float(nrmW_); u.detail[1] = float(detail_.k) * tz / float(nrmH_);
        u.detail[2] = 0.5f / float(nrmW_); u.detail[3] = 0.5f / float(nrmH_);
        // slope rock: a cliff texture handles steep slopes on the splat tiers; otherwise a rock colour
        const bool cliffTex = cliffTex_.valid() && u.mode[1] > 0.5f;
        u.rock[0] = look_.rockColor.x; u.rock[1] = look_.rockColor.y; u.rock[2] = look_.rockColor.z; u.rock[3] = cliffTex ? 0.0f : 1.0f;
        if (!cliffTex) { u.cliff[2] = look_.slopeStart; u.cliff[3] = look_.slopeEnd; }
        u.blend[0] = look_.blendSharpness; u.blend[1] = look_.macro; u.blend[2] = 1.0f;
    }
    if (layerNormalAtlasSize_ > 0) {
        constexpr int gutter=16;
        u.blend[3]=float(layerNormalAtlasSize_)/
                   float(2*(layerNormalAtlasSize_+2*gutter));
    }
    // Capture the per-camera GPU bindings by value. A subsequent terrain
    // rebuild can replace the live maps while this draw command is queued.
    struct GpuBindings {
        gfx::TextureHandle cell,dynamic,splat,vision,detailNormal,detailSplat;
        gfx::TextureHandle cliff,cliffNormal,layerNormalAtlas;
        std::array<gfx::TextureHandle,4> layer{},layerNormal{};
        bool detail=false;
    };
    GpuBindings bindings{};
    bindings.layerNormalAtlas=layerNormalAtlas_;
    bindings.cell=cellTex_;
    bindings.dynamic=dynTex_;
    bindings.splat=splatTex_;
    bindings.vision=visionTex_;
    bindings.detailNormal=detailNrmTex_;
    bindings.detailSplat=detailSplatTex_;
    bindings.cliff=cliffTex_.valid()?cliffTex_.tex:gray_;
    bindings.cliffNormal=cliffNrm_.valid()?cliffNrm_.tex:flatN_;
    bindings.detail=detailOn_;
    for(int i=0;i<4;++i) {
        bindings.layer[size_t(i)]=layerTex_[i].valid()?layerTex_[i].tex:white_;
        bindings.layerNormal[size_t(i)]=layerNrm_[i].valid()?layerNrm_[i].tex:flatN_;
    }
    const Lod apron=apron_;
    // traced terrain shadows (Renderer3D trace tier): the height map of the drawn surface
    if (r.traceActive()) {
        if(!preparedOnly && !stagedFields && heightDirty_)
            updateHeightField(dev,g); // standalone compatibility only
        r.setHeightField(heightField_);
    } else r.setHeightField({});
    r.drawCustomOpaque([&r, items, u, pipe, apron, bindings, giPipeline, compactGi](gfx::Device& d) {
        d.setPipeline(pipe);
        r.bindLitFrame();
        if(!compactGi) {
            r.bindTrace(18);
            r.bindBakedTerrain(3, 19, 20);
        }
        d.setTexture(2, bindings.cell);
        d.setTexture(3, bindings.dynamic);
        d.setTexture(4, bindings.splat);
        for (int i = 0; i < 4; ++i) {
            d.setTexture(5 + i, bindings.layer[size_t(i)]);
            d.setTexture(9 + i, bindings.layerNormal[size_t(i)]);
        }
        d.setTexture(13, bindings.cliff);
        d.setTexture(14, bindings.cliffNormal);
        d.setTexture(15, bindings.vision);
        if (bindings.detail) {
            d.setTexture(16, bindings.detailNormal);
            d.setTexture(17, bindings.detailSplat);
        }
        if (bindings.layerNormalAtlas.valid()) d.setTexture(9,bindings.layerNormalAtlas);
        if (giPipeline)
            (void)r.bindDynamicGiForCustomAt(compactGi ? 8 : 21);
        TU0 uu = u;
        auto drawLod = [&](const Lod& l, float s, float e) {
            uu.lod[0] = s; uu.lod[1] = e;
            d.setUniforms(0, &uu, sizeof uu);
            d.setVertexBuffer(l.vb);
            d.setIndexBuffer(l.ib);
            d.drawIndexed(l.indexCount);
        };
        if (apron.indexCount) drawLod(apron, 1e9f, 2e9f);
        for (const auto& it : *items) drawLod(it.lod, it.morphStart, it.morphEnd);
    });
    // Water was already captured as a model/mesh packet in the primary
    // view; avoid duplicating it when the secondary terrain path runs.
    if (look_.render && mapSurfaceEnabled_ && !preparedOnly && hasWater_ && water_.indexCount)
        r.drawMeshSnapshot(water_,mat4{},look_.waterColor,kDrawNoCastShadow,0.9f);
    // AIVERSE_TERRAIN_STATS=1: chunks / triangles / frame time every 60 frames (stderr; perf checks on big maps)
    static const bool printStats = std::getenv("AIVERSE_TERRAIN_STATS") != nullptr;
    if (printStats && !preparedOnly) {
        static auto last = std::chrono::steady_clock::now();
        static int frames = 0;
        if (++frames == 60) {
            auto now = std::chrono::steady_clock::now();
            double ms = std::chrono::duration<double, std::milli>(now - last).count() / frames;
            std::fprintf(stderr, "[terrain] map %dx%d chunks %d drawn %d lods %d tris %llu frame %.2f ms build %.1f ms detail k=%d tex %dx%d geo %.1f MB\n", mapW_, mapH_,
                         stats_.chunks, stats_.drawn, lodLevels_, (unsigned long long)stats_.triangles, ms, stats_.buildMs,
                         detailOn_ ? detailK_ : 0, nrmW_, nrmH_, double(geoBytes_) / 1048576.0);
            last = now; frames = 0;
        }
    }
}

void TerrainRenderer::updateHeightField(gfx::Device& dev, const MapGrid& g) {
    using clock = std::chrono::steady_clock;
    const double now = std::chrono::duration<double>(clock::now().time_since_epoch()).count();
    if (heightField_.tex.valid() && now - heightBuiltAt_ < 0.3) return;   // sculpting: at most ~3 rebuilds per second
    // Only mark the traced surface clean after the replacement GPU image
    // has been allocated and published successfully.
    // two samples per cell, at most 1024 per side (2048-cell maps: one sample per two cells - the
    // long shadows this map serves are a coarse feature anyway); 16-bit heights in R (high) and G (low)
    const int tw = std::max(2, std::min(1024, g.w * 2)), th = std::max(2, std::min(1024, g.h * 2));
    std::vector<float> h(size_t(tw) * size_t(th));
    float mn = 1e9f, mx = -1e9f;
    for (int j = 0; j < th; ++j) {
        const float z = (float(j) + 0.5f) * float(g.h) / float(th);
        for (int i = 0; i < tw; ++i) {
            const float x = (float(i) + 0.5f) * float(g.w) / float(tw);
            const float v = surfaceAt(g, x, z);
            h[size_t(j) * size_t(tw) + size_t(i)] = v;
            mn = std::min(mn, v); mx = std::max(mx, v);
        }
    }
    const float range = std::max(mx - mn, 1e-3f);
    std::vector<uint8_t> px(h.size() * 4);
    for (size_t n = 0; n < h.size(); ++n) {
        const int q = std::clamp(int((h[n] - mn) / range * 65535.0f + 0.5f), 0, 65535);
        px[n * 4 + 0] = uint8_t(q >> 8); px[n * 4 + 1] = uint8_t(q & 255); px[n * 4 + 2] = 0; px[n * 4 + 3] = 255;
    }
    // A queued multi-camera trace may still sample the previous image.
    // Never mutate or destroy it in place while its frame can be in flight.
    gfx::TextureDesc td;
    td.width=tw; td.height=th; td.filter=gfx::Filter::Linear;
    td.data=px.data(); td.label="terrain height field";
    const gfx::TextureHandle newHeight=createTerrainTexture(dev,td);
    if (!newHeight.valid())
        throw std::runtime_error("TerrainRenderer: trace height texture upload failed");
    retiredMeshes_.retireTexture(dev,heightField_.tex);
    heightField_.tex=newHeight;
    heightW_=tw; heightH_=th;
    heightBuiltAt_=now;
    heightDirty_=false;
    heightField_.originX = 0; heightField_.originZ = 0;
    heightField_.extentX = float(g.w); heightField_.extentZ = float(g.h);
    heightField_.heightMin = mn; heightField_.heightRange = mx - mn > 1e-3f ? mx - mn : 0.0f;   // flat map: nothing to shadow
}

void TerrainRenderer::drawWater(render::Renderer3D& r) {
    if (look_.render && mapSurfaceEnabled_ && hasWater_ && water_.indexCount)
        r.drawMeshSnapshot(water_, render::mat4{}, look_.waterColor, render::kDrawNoCastShadow, .9f);
}

} // namespace aiverse::game
