#include "RtsGroundFx.h"
#include "../../../engine/render/NativeMeshBatch.h"
#include "../../../engine/data/JsonCompose.h"
#include "RtsGroundFxShaders.h"
#include "RtsView.h"
#include "../RtsModule.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <utility>

namespace aiverse::game {

using namespace render;

namespace {
constexpr int kChunk = 32;
constexpr float kTerrY = 0.045f, kRoadY = 0.06f, kLaneY = 0.075f;
enum { kDirtyT = 1, kDirtyD = 2, kDirtyM = 4, kDirtyR = 8, kDirtyC = 16 };

struct GU0 { float color[4]; float params[4]; float params2[4]; float rect[4]; };

uint32_t hash2(int x, int y, uint32_t s = 0) {
    uint32_t h = uint32_t(x) * 73856093u ^ uint32_t(y) * 19349663u ^ s * 83492791u;
    h ^= h >> 13; h *= 0x5bd1e995u; return h ^ (h >> 15);
}
float rnd(int x, int y, uint32_t s) { return float(hash2(x, y, s) & 0xffff) / 65535.0f; }
float num(const Json& j, const char* k, float def) { const Json& v = j[k]; return v.isNumber() ? float(v.fx().toDouble()) : def; }
bool col(const Json& c, float3& out) {
    if (c.isArray() && c.size() >= 3) { out = {float(c[0].fx().toDouble()), float(c[1].fx().toDouble()), float(c[2].fx().toDouble())}; return true; }
    if (c.isString()) {
        std::string s = c.str();
        if (s.size() == 7 && s[0] == '#') {
            unsigned v = unsigned(std::strtoul(s.c_str() + 1, nullptr, 16));
            out = {float((v >> 16) & 255) / 255.0f, float((v >> 8) & 255) / 255.0f, float(v & 255) / 255.0f};
            return true;
        }
    }
    return false;
}
void colKey(const Json& j, const char* k, float3& out) { col(j[k], out); }
uint32_t c4(float3 c, float a) { return rgbaf({c.x, c.y, c.z, a}); }

// triangle with the winding fixed so that its front face points along n
void tri(MeshBuilder& b, float3 p0, float3 p1, float3 p2, float3 n, uint32_t c0, uint32_t c1, uint32_t c2) {
    uint32_t i = b.addVertex(p0, n, c0), j = b.addVertex(p1, n, c1), k = b.addVertex(p2, n, c2);
    if (dot(cross(p1 - p0, p2 - p0), n) >= 0) b.addTriangle(i, j, k); else b.addTriangle(i, k, j);
}
// quad p0 p1 p2 p3 in order around the rim
void quad(MeshBuilder& b, float3 p0, float3 p1, float3 p2, float3 p3, float3 n, uint32_t c0, uint32_t c1, uint32_t c2, uint32_t c3) {
    uint32_t i0 = b.addVertex(p0, n, c0), i1 = b.addVertex(p1, n, c1), i2 = b.addVertex(p2, n, c2), i3 = b.addVertex(p3, n, c3);
    if (dot(cross(p1 - p0, p2 - p0), n) >= 0) { b.addTriangle(i0, i1, i2); b.addTriangle(i0, i2, i3); }
    else { b.addTriangle(i0, i2, i1); b.addTriangle(i0, i3, i2); }
}
void quad(MeshBuilder& b, float3 p0, float3 p1, float3 p2, float3 p3, float3 n, uint32_t c) { quad(b, p0, p1, p2, p3, n, c, c, c, c); }

float3 roomColor(int idx) {
    static const float3 pal[] = {{0.62f, 0.32f, 0.22f}, {0.80f, 0.66f, 0.30f}, {0.30f, 0.42f, 0.70f}, {0.55f, 0.30f, 0.55f},
                                 {0.30f, 0.58f, 0.42f}, {0.70f, 0.42f, 0.30f}, {0.42f, 0.42f, 0.48f}, {0.66f, 0.30f, 0.36f}};
    return pal[size_t(idx) % 8];
}

// small tileable noise textures (default look; games may set their own through TextureCache paths)
std::vector<uint32_t> makeNoise(int n, bool cobble) {
    std::vector<uint32_t> px(size_t(n * n));
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x) {
            float v;
            if (cobble) {
                // cells of a jittered 4x4 grid: stones with darker joints
                float best = 9, second = 9;
                for (int gy = -1; gy <= 4; ++gy)
                    for (int gx = -1; gx <= 4; ++gx) {
                        int wx = (gx + 4) % 4, wy = (gy + 4) % 4;
                        float px_ = (float(gx) + 0.2f + 0.6f * rnd(wx, wy, 7)) * float(n) / 4.0f;
                        float py_ = (float(gy) + 0.2f + 0.6f * rnd(wx, wy, 9)) * float(n) / 4.0f;
                        float d = std::hypot(float(x) - px_, float(y) - py_);
                        if (d < best) { second = best; best = d; } else if (d < second) second = d;
                    }
                float edge = std::clamp((second - best) / (float(n) * 0.05f), 0.0f, 1.0f);
                v = 0.62f + 0.38f * edge - 0.08f * rnd(x, y, 3);
            } else {
                v = 0.86f + 0.14f * rnd(x / 2, y / 2, 5) - 0.06f * rnd(x, y, 11);
            }
            uint8_t q = uint8_t(std::clamp(v, 0.0f, 1.0f) * 255.0f);
            px[size_t(y * n + x)] = rgba(q, q, q, 255);
        }
    return px;
}

bool boxVisible(const mat4& vp, float3 mn, float3 mx) {
    int out[6] = {};
    for (int i = 0; i < 8; ++i) {
        float4 p = vp * float4{(i & 1) ? mx.x : mn.x, (i & 2) ? mx.y : mn.y, (i & 4) ? mx.z : mn.z, 1};
        out[0] += p.x < -p.w; out[1] += p.x > p.w; out[2] += p.y < -p.w; out[3] += p.y > p.w; out[4] += p.w < 0.0f;
    }
    for (int k = 0; k < 5; ++k) if (out[k] == 8) return false;
    return true;
}
} // namespace

// ------------------------------------------------------------------ setup

void RtsGroundFx::init(gfx::Device& dev, const Rules& rules, const FileReader& read, const std::string& contentBase) {
    checkGpuDevice(dev);
    if(dev_ && dev_!=&dev)
        throw std::logic_error("RtsGroundFx: GPU device owner mismatch");
    // Build a complete replacement on a private, never-submitted owner.
    // This preserves the live overlay/terrain state if shader, pipeline,
    // white fallback or optional texture decoding/allocation throws.
    auto staged=std::make_unique<RtsGroundFx>();
    try {
        staged->initUnpublished(dev,rules,read,contentBase);
        if(dev_ && !dev.waitForResourceIdle())
            throw std::runtime_error("RtsGroundFx: reload GPU idle barrier failed");
        if(dev_)shutdown(dev);
    } catch(...) {
        // The staging GPU kit was NEVER published or submitted. Its handles
        // must be destroyed directly, not through shutdown(), which requires
        // an idle proof intended for resources used by prior render passes.
        // In particular, failed reload when the OLD owner is still busy must
        // not leak the newly allocated procedural material textures.
        const auto original=std::current_exception();
        if(staged->dev_) {
            if(staged->pipe_.valid())
                try {dev.destroyPipeline(staged->pipe_);}catch(...) {}
            if(staged->shader_.valid())
                try {dev.destroyShader(staged->shader_);}catch(...) {}
            if(staged->white_.valid())
                try {dev.destroyTexture(staged->white_);}catch(...) {}
            try {staged->textures_.shutdown();}catch(...) {}
            staged->pipe_={};staged->shader_={};staged->white_={};
            staged->cobbleTexture_={};staged->dirtTexture_={};
            staged->dev_=nullptr;
        }
        std::rethrow_exception(original);
    }
    // No GPU allocation after this point. Swap the prepared kit, look and
    // refs into the live owner; all previous mesh state has been shut down.
    dev_=&dev;
    rules_=&rules;
    ++publicationRevision_;
    base_.swap(staged->base_);
    textures_.swap(staged->textures_);
    using std::swap;
    swap(terr_,staged->terr_);
    swap(dig_,staged->dig_);
    swap(lane_,staged->lane_);
    swap(road_,staged->road_);
    swap(conc_,staged->conc_);
    shader_=std::exchange(staged->shader_,{});
    pipe_=std::exchange(staged->pipe_,{});
    white_=std::exchange(staged->white_,{});
    cobbleTexture_=staged->cobbleTexture_;
    dirtTexture_=staged->dirtTexture_;
    useT_=staged->useT_; useD_=staged->useD_;
    useR_=staged->useR_; useC_=staged->useC_;
    invalid_=true;
    staged->dev_=nullptr;
}

void RtsGroundFx::initUnpublished(gfx::Device& dev, const Rules& rules, const FileReader& read, const std::string& contentBase) {
    dev_ = &dev;
    rules_ = &rules;
    base_ = contentBase;
    textures_.init(dev);
    terr_ = {}; dig_ = {}; lane_ = {}; road_ = {};
    // defaults per diggable kind: no yield = plain rock, a yield = gold vein, infinite yield = gem seam
    for (const DiggableDef& d : rules.diggables) {
        DigKind k;
        k.rock = {0.40f, 0.35f, 0.30f};
        if (d.yields.empty()) k.style = 0;
        else if (d.infinite) { k.style = 2; k.vein = {0.35f, 0.85f, 0.95f}; k.glow = 0.55f; }
        else { k.style = 1; k.vein = {0.98f, 0.76f, 0.22f}; k.glow = 0.25f; }
        dig_.kinds.push_back(k);
    }
    // the visual `terrain` block: rules file first, game.json overrides per key
    if (read) {
        std::string text, rulesPath = "rules.json";
        Json game;
        try { if (read("game.json", text)) { game = Json::parse(text); rulesPath = game.getStr("rules", "rules.json"); } } catch (...) {}
        try { parseLook(readComposedJson(rulesPath, [&](const std::string& p, std::string& o) { return read(p, o); })["terrain"]); } catch (...) {}
        if (game.isObject()) parseLook(game["terrain"]);
    }
    useT_ = rules.territory.enabled || !rules.rooms.empty();
    useD_ = !rules.diggables.empty();
    useR_ = rules.roadNetwork.enabled; // or a map with '+' cells (resetGrid)
    useC_ = rules.foundation.enabled;
    loadTex(terr_.tex); loadTex(lane_.tex); loadTex(road_.tex); loadTex(conc_.tex);

    gfx::ShaderDesc sd;
    sd.glslVertex = shaders::kGroundFxVS; sd.glslFragment = shaders::kGroundFxFS; sd.msl = shaders::kGroundFxMSL;
    sd.uniformSlots = 3; sd.textureSlots = 3; sd.label = "groundfx";
    shader_ = dev.createShader(sd);
    if (shader_.valid()) {
        gfx::PipelineDesc p;
        p.shader = shader_;
        p.attrs[0] = {0, gfx::VertexFormat::Float3, 0};
        p.attrs[1] = {1, gfx::VertexFormat::Float3, 12};
        p.attrs[2] = {2, gfx::VertexFormat::UByte4N, 24};
        p.attrCount = 3; p.stride = sizeof(MeshVertex);
        p.indexType = gfx::IndexType::UInt32;
        p.cull = gfx::CullMode::None;
        p.blend = gfx::BlendMode::Alpha;
        p.depthWrite = false;
        p.depthBiasConstant = -1.0f; p.depthBiasSlope = -1.0f;   // decals win the depth test against the ground
        p.label = "groundfx";
        pipe_ = dev.createPipeline(p);
    }
    if (!pipe_.valid())
        throw std::runtime_error("RtsGroundFx: pipeline creation failed");
    const uint32_t w = 0xffffffffu;
    gfx::TextureDesc td; td.width = 1; td.height = 1; td.data = &w; td.label = "groundfx white";
    white_ = dev.createTexture(td);
    if(!white_.valid())
        throw std::runtime_error("RtsGroundFx: white texture allocation failed");
    // Once-per-kit procedural textures. drawPrepared() must never create a
    // new GPU texture after the multi-owner presentation transaction commits.
    cobbleTexture_=textures_.addGenerated(
        "builtin:groundfx-cobble",64,64,makeNoise(64,true),false);
    dirtTexture_=textures_.addGenerated(
        "builtin:groundfx-dirt",32,32,makeNoise(32,false),false);
    if(!cobbleTexture_.valid() || !dirtTexture_.valid())
        throw std::runtime_error("RtsGroundFx: generated material texture allocation failed");
    invalid_ = true;
}

void RtsGroundFx::loadTex(Tex& t) {
    t.ref = {};
    if (!t.path.empty()) t.ref = textures_.resolve(t.path, false, base_);
}

void RtsGroundFx::parseLook(const Json& t) {
    if (!t.isObject()) return;
    if (const Json& j = t["territory"]; !j.isNull()) {
        if (!j.isObject()) terr_.on = j.boolean(true);
        else {
            terr_.on = j.getBool("enabled", true);
            terr_.fill = num(j, "fill", terr_.fill);
            terr_.border = num(j, "border", terr_.border);
            terr_.glow = num(j, "glow", terr_.glow);
            terr_.pulse = j.getBool("pulse", terr_.pulse);
            terr_.smooth = j.getBool("smooth", terr_.smooth);
            terr_.transition = std::clamp(num(j, "transition", terr_.transition), 0.0f, 2.0f);
            terr_.lift = std::clamp(num(j, "lift", terr_.lift), 0.0f, 2.0f);
            terr_.roomFill = num(j, "roomFill", terr_.roomFill);
            if (const Json& c = j["colors"]; c.isArray()) {
                terr_.colors.clear();
                for (auto& e : c.items()) { float3 v{-1, -1, -1}; col(e, v); terr_.colors.push_back(v); }
            }
            for (auto& [id, c] : j["rooms"].members()) { float3 v; if (col(c, v)) terr_.rooms[id] = v; }
            terr_.tex.path = j.getStr("texture", terr_.tex.path);
            terr_.tex.scale = num(j, "textureScale", terr_.tex.scale);
            terr_.tex.strength = num(j, "textureStrength", terr_.tex.strength);
        }
    }
    if (const Json& j = t["dig"]; !j.isNull()) {
        if (!j.isObject()) dig_.on = j.boolean(true);
        else {
            dig_.on = j.getBool("enabled", true);
            dig_.height = std::clamp(num(j, "height", dig_.height), 0.2f, 1.25f);
            dig_.progress = j.getBool("progress", dig_.progress);
            colKey(j, "mark", dig_.mark);
            for (auto& [key, k] : j["kinds"].members()) {
                int i = key.size() == 1 && rules_ ? rules_->findDiggable(key[0]) : -1;
                if (i < 0 || size_t(i) >= dig_.kinds.size() || !k.isObject()) continue;
                DigKind& d = dig_.kinds[size_t(i)];
                colKey(k, "rock", d.rock);
                colKey(k, "vein", d.vein);
                std::string st = k.getStr("style");
                if (st == "plain") d.style = 0; else if (st == "vein") d.style = 1; else if (st == "gem") d.style = 2;
                d.glow = num(k, "glow", d.glow);
            }
        }
    }
    auto texKeys = [](const Json& j, Tex& x) {
        x.path = j.getStr("texture", x.path);
        x.scale = std::max(0.05f, num(j, "textureScale", x.scale));
        x.strength = num(j, "textureStrength", x.strength);
    };
    if (const Json& j = t["lanes"]; !j.isNull()) {
        if (!j.isObject()) lane_.on = j.boolean(true);
        else {
            lane_.on = j.getBool("enabled", true);
            colKey(j, "color", lane_.color); colKey(j, "edge", lane_.edge);
            lane_.alpha = num(j, "alpha", lane_.alpha);
            lane_.width = num(j, "width", lane_.width);
            lane_.sideLanes = j.getBool("sideLanes", lane_.sideLanes);
            lane_.all = j.getBool("all", lane_.all);
            texKeys(j, lane_.tex);
        }
    }
    if (const Json& j = t["roads"]; !j.isNull()) {
        if (!j.isObject()) road_.on = j.boolean(true);
        else {
            road_.on = j.getBool("enabled", true);
            colKey(j, "color", road_.color); colKey(j, "edge", road_.edge);
            road_.width = std::clamp(num(j, "width", road_.width), 0.2f, 1.0f);
            road_.alpha = num(j, "alpha", road_.alpha);
            road_.cobbles = j.getBool("cobbles", road_.cobbles);
            texKeys(j, road_.tex);
        }
    }
    if (const Json& j = t["concrete"]; !j.isNull()) {
        if (!j.isObject()) conc_.on = j.boolean(true);
        else {
            conc_.on = j.getBool("enabled", true);
            colKey(j, "color", conc_.color); colKey(j, "seam", conc_.seam);
            conc_.alpha = num(j, "alpha", conc_.alpha);
            texKeys(j, conc_.tex);
        }
    }
}

void RtsGroundFx::freeMesh(Mesh& m) {
    if(!dev_)return;
    // A staging clone holds shallow BORROWED references to published meshes.
    // Clearing its slots must never retire those live GPU allocations.
    if(stagingJournal_) {m=Mesh{};return;}
    retiredMeshes_.retireBuffer(*dev_,m.vb);
    retiredMeshes_.retireBuffer(*dev_,m.ib);
    m=Mesh{};
}

void RtsGroundFx::setMesh(Mesh& m, const MeshBuilder& b) {
    if(!dev_)throw std::logic_error("RtsGroundFx mesh: unbound GPU device");
    // A multi-layer/chunk draw transaction prepares every replacement before
    // publishing any of them. Outside a batch retain the legacy single-mesh
    // transactional path for compatibility with other GroundFX owners.
    if(activeMeshBatch_) {
        if(stagingJournal_) {
            // The clone owns NO published buffers. The GPU batch writes new
            // resources into a private empty slot; commit later retires the
            // matching live slot using its explicit changed-slot journal.
            if(std::find(stagingJournal_->begin(),stagingJournal_->end(),&m)==
               stagingJournal_->end())
                stagingJournal_->push_back(&m);
            m=Mesh{};
        }
        activeMeshBatch_->stage(m,b);
        return;
    }
    Mesh replacement{};
    if(!b.empty())replacement=b.upload(*dev_);
    freeMesh(m);
    m=replacement;
}

bool RtsGroundFx::shutdownAfterGpuIdle(gfx::Device& dev) {
    checkGpuDevice(dev);
    if(!dev_)return true;
    if(dev_!=&dev)
        throw std::logic_error("RtsGroundFx shutdown: GPU device owner mismatch");
    if(!dev.waitForResourceIdle())return false;
    shutdown(dev);
    return true;
}

void RtsGroundFx::shutdown(gfx::Device& dev) {
    checkGpuDevice(dev);
    if(!dev_)return;
    if(dev_!=&dev)
        throw std::logic_error("RtsGroundFx shutdown: GPU device owner mismatch");
    for (Chunk& c : chunks_)
        for (Mesh* m : {&c.fill, &c.border, &c.rock, &c.veins, &c.gems, &c.marks, &c.road, &c.concrete}) freeMesh(*m);
    chunks_.clear();
    freeMesh(lanes_);
    // Mesh retirement is ordered by actual RHI completion tokens, not frame
    // age. Shutdown requires explicit full-device completion proof before
    // destruction of any GPU pipeline, shader or texture.
    if(!retiredMeshes_.completeForShutdown(dev))
        throw std::runtime_error("RtsGroundFx shutdown: GPU mesh retirement could not reach idle");
    if (pipe_.valid()) dev.destroyPipeline(pipe_);
    if (shader_.valid()) dev.destroyShader(shader_);
    if (white_.valid()) dev.destroyTexture(white_);
    pipe_ = {}; shader_ = {}; white_ = {};
    cobbleTexture_={}; dirtTexture_={};
    textures_.shutdown();
    dev_ = nullptr;
    invalid_ = true;
    ++publicationRevision_;
}

float3 RtsGroundFx::ownerColor(int slot) const {
    if (slot >= 1 && size_t(slot - 1) < terr_.colors.size() && terr_.colors[size_t(slot - 1)].x >= 0) return terr_.colors[size_t(slot - 1)];
    float4 c = RtsView::playerColor(slot);
    return {c.x, c.y, c.z};
}

// ------------------------------------------------------------------ change tracking

void RtsGroundFx::dirty(int x, int y, bool neighbours, int what) {
    auto mark = [&](int cx, int cy) {
        if (cx < 0 || cy < 0 || cx >= gridW_ || cy >= gridH_) return;
        Chunk& c = chunks_[size_t((cy / kChunk) * chunksX_ + cx / kChunk)];
        if (what & kDirtyT) c.dT = true;
        if (what & kDirtyD) c.dD = true;
        if (what & kDirtyM) c.dM = true;
        if (what & kDirtyR) c.dR = true;
        if (what & kDirtyC) c.dC = true;
    };
    mark(x, y);
    if (neighbours) { mark(x - 1, y); mark(x + 1, y); mark(x, y - 1); mark(x, y + 1); }
}

void RtsGroundFx::resetGrid(const MapGrid& g) {
    for (Chunk& c : chunks_)
        for (Mesh* m : {&c.fill, &c.border, &c.rock, &c.veins, &c.gems, &c.marks, &c.road, &c.concrete}) freeMesh(*m);
    gridW_ = g.w; gridH_ = g.h;
    chunksX_ = (g.w + kChunk - 1) / kChunk; chunksY_ = (g.h + kChunk - 1) / kChunk;
    chunks_.assign(size_t(chunksX_ * chunksY_), Chunk{});
    const size_t n = size_t(g.w * g.h);
    // terrain (rock, roads) is known from the start like the map itself; territory and rooms are learned by seeing them
    memT_.assign(n, 0); memRoom_.assign(n, -1); memM_.assign(n, 0);
    territoryBlend_.clear(); territoryTime_ = -1;
    memD_.assign(n, 0); memR_.assign(n, 0); memC_.assign(n, 0); memW_.assign(n, 0);
    bool anyRoad = false;
    for (size_t i = 0; i < n; ++i) {
        memD_[i] = i < g.dig.size() ? g.dig[i] : 0;
        memR_[i] = (g.flags[i] & kCellRoad) ? 1 : 0;
        anyRoad |= memR_[i] != 0;
    }
    useR_ = (rules_ && rules_->roadNetwork.enabled) || anyRoad;
    const float lh = rules_ ? float(rules_->levelHeight.toDouble()) : 1.5f;
    int maxLevel = 0;
    for (auto l : g.level) maxLevel = std::max<int>(maxLevel, l);
    for (Chunk& c : chunks_) { c.yMin = -1.0f; c.yMax = float(maxLevel) * lh + 3.0f; }
    marked_.clear();
    laneCount_ = ~size_t(0);
    invalid_ = false;
    nextScan_ = -1;
}

void RtsGroundFx::scan(const MapGrid& g, const std::vector<uint8_t>* vision, uint8_t viewer) {
    const bool fog = vision && vision->size() == g.flags.size();
    const uint8_t bit = viewer >= 1 && viewer <= 8 ? uint8_t(1u << (viewer - 1)) : 0xff;
    const bool hasDig = g.dig.size() == g.flags.size(), hasTerr = g.territory.size() == g.flags.size() && g.room.size() == g.flags.size();
    const bool hasMark = g.digMark.size() == g.flags.size();
    marked_.clear();
    for (int y = 0; y < g.h; ++y)
        for (int x = 0; x < g.w; ++x) {
            const size_t i = size_t(y * g.w + x);
            const uint8_t m = hasMark && hasDig && g.dig[i] ? uint8_t(g.digMark[i] & bit) : 0;
            if (m != memM_[i]) { memM_[i] = m; dirty(x, y, false, kDirtyM); }
            if (m) marked_.push_back(uint32_t(i));
            if (fog && (*vision)[i] != 2) continue;
            if (useT_ && hasTerr) {
                if (g.territory[i] != memT_[i]) { memT_[i] = g.territory[i]; dirty(x, y, true, kDirtyT); }
                if (g.room[i] != memRoom_[i]) { memRoom_[i] = g.room[i]; dirty(x, y, false, kDirtyT); }
            }
            const uint8_t d = hasDig ? g.dig[i] : 0;
            if (d != memD_[i]) { memD_[i] = d; dirty(x, y, true, kDirtyD | kDirtyT | kDirtyM); }
            const uint8_t wl = d && i < g.wall.size() ? g.wall[i] : 0;
            if (wl != memW_[i]) { memW_[i] = wl; dirty(x, y, true, kDirtyD); }
            const uint8_t r = (g.flags[i] & kCellRoad) ? 1 : 0;
            if (r != memR_[i]) { memR_[i] = r; dirty(x, y, true, kDirtyR); }
            if (useC_) {
                const uint8_t cc = i < g.foundation.size() ? g.foundation[i] : 0;
                if (cc != memC_[i]) { memC_[i] = cc; dirty(x, y, false, kDirtyC); }
            }
        }
}

// ------------------------------------------------------------------ territory

void RtsGroundFx::animateTerritory(const MapGrid& g, double time) {
    const bool initial = territoryTime_ < 0;
    const float dt = initial ? 0.0f : float(std::clamp(time - territoryTime_, 0.0, 0.1));
    territoryTime_ = time;
    for (uint8_t owner : memT_)
        if (owner && !territoryBlend_.count(owner)) territoryBlend_[owner].assign(memT_.size(), 0.0f);
    const float step = terr_.transition <= 0 ? 1.0f : dt / terr_.transition;
    for (auto& [owner, values] : territoryBlend_) {
        for (size_t i = 0; i < values.size(); ++i) {
            const float target = memT_[i] == owner && !memD_[i] ? 1.0f : 0.0f;
            const float old = values[i];
            values[i] = initial ? target : old + std::clamp(target - old, -step, step);
            if (old != values[i]) {
                const int x = int(i % g.w), y = int(i / g.w);
                // Bilinear reconstruction affects diagonal neighbours across chunk corners too.
                for (int dy = -2; dy <= 2; ++dy) for (int dx = -2; dx <= 2; ++dx)
                    dirty(x + dx, y + dy, false, kDirtyT);
            }
        }
    }
}

void RtsGroundFx::buildSmoothTerritory(Chunk& c, int cx, int cy, const MapGrid& g, const RtsView& v) {
    MeshBuilder fill, border;
    const float3 up{0,1,0};
    const int subdivisions = hi_ ? 4 : 2;
    auto smooth = [](float a, float b, float x) {
        const float t = std::clamp((x-a)/(b-a), 0.0f, 1.0f); return t*t*(3-2*t);
    };
    for (const auto& [owner, values] : territoryBlend_) {
        auto sample = [&](float x, float z) {
            x -= 0.5f; z -= 0.5f;
            const int ix = int(std::floor(x)), iz = int(std::floor(z));
            const float tx=x-ix, tz=z-iz;
            auto raw = [&](int a, int b) { return g.inside(a,b) ? values[size_t(g.index(a,b))] : 0.0f; };
            auto at = [&](int a, int b) {
                // Compact binomial reconstruction rounds grid corners without hiding a lone cell.
                float sum=0;
                for(int dz=-1;dz<=1;++dz) for(int dx=-1;dx<=1;++dx)
                    sum+=raw(a+dx,b+dz)*float((dx==0?2:1)*(dz==0?2:1));
                return raw(a,b)*0.5f+sum*(0.5f/16.0f);
            };
            return (at(ix,iz)*(1-tx)+at(ix+1,iz)*tx)*(1-tz) + (at(ix,iz+1)*(1-tx)+at(ix+1,iz+1)*tx)*tz;
        };
        const float3 color = ownerColor(owner);
        for (int y = cy*kChunk; y < std::min(g.h,(cy+1)*kChunk); ++y)
            for (int x = cx*kChunk; x < std::min(g.w,(cx+1)*kChunk); ++x) {
                bool near = false, solid = true;
                for (int dz=-2; dz<=2; ++dz) for (int dx=-2; dx<=2; ++dx) {
                    const float value = g.inside(x+dx,y+dz) ? values[size_t(g.index(x+dx,y+dz))] : 0.0f;
                    near |= value > 0; solid &= value >= 1;
                }
                if (!near) continue;
                if (solid) {
                    auto p = [&](float a,float b) { return float3{a,v.heightAt(g,a,b)+terr_.lift,b}; };
                    quad(fill,p(float(x),float(y)),p(float(x+1),float(y)),p(float(x+1),float(y+1)),p(float(x),float(y+1)),up,c4(color,terr_.fill));
                    continue;
                }
                for (int j=0; j<subdivisions; ++j) for (int i=0; i<subdivisions; ++i) {
                    const float x0=x+float(i)/subdivisions, z0=y+float(j)/subdivisions;
                    const float x1=x+float(i+1)/subdivisions, z1=y+float(j+1)/subdivisions;
                    const float2 points[]={{x0,z0},{x1,z0},{x1,z1},{x0,z1}};
                    float3 p[4]; uint32_t fc[4], bc[4]; bool visible=false, edge=false;
                    for (int k=0;k<4;++k) {
                        const float f=sample(points[k].x,points[k].y);
                        const float alpha=smooth(0.35f,0.65f,f);
                        const float line=(1-smooth(0.025f,0.12f,std::abs(f-0.5f)))*0.8f;
                        p[k]={points[k].x,v.heightAt(g,points[k].x,points[k].y)+terr_.lift,points[k].y};
                        fc[k]=c4(color,alpha*terr_.fill); bc[k]=c4(color*0.5f+float3{0.5f,0.5f,0.5f},line);
                        visible |= alpha>0; edge |= line>0;
                    }
                    if (visible) quad(fill,p[0],p[1],p[2],p[3],up,fc[0],fc[1],fc[2],fc[3]);
                    if (edge && terr_.border>0) quad(border,p[0],p[1],p[2],p[3],up,bc[0],bc[1],bc[2],bc[3]);
                }
            }
    }
    setMesh(c.fill,fill); setMesh(c.border,border); c.dT=false;
}

void RtsGroundFx::buildTerritory(Chunk& c, int cx, int cy, const MapGrid& g, const RtsView& v) {
    if (terr_.smooth) { buildSmoothTerritory(c, cx, cy, g, v); return; }
    MeshBuilder fb, bb;
    const float3 up{0, 1, 0};
    const float e = 0.002f;
    auto H = [&](float x, float z, int ix, int iz) {
        return v.heightAt(g, std::clamp(x, float(ix) + e, float(ix + 1) - e), std::clamp(z, float(iz) + e, float(iz + 1) - e)) + kTerrY;
    };
    auto P = [&](float x, float z, int ix, int iz) { return float3{x, H(x, z, ix, iz), z}; };
    auto owner = [&](int x, int y) -> int { return g.inside(x, y) ? memT_[size_t(y * g.w + x)] : -1; };
    std::vector<std::string> roomIds;
    if (rules_) for (auto& r : rules_->rooms) roomIds.push_back(r.id);
    const float bw = std::clamp(terr_.border, 0.02f, 0.5f);
    for (int y = cy * kChunk; y < std::min(g.h, (cy + 1) * kChunk); ++y)
        for (int x = cx * kChunk; x < std::min(g.w, (cx + 1) * kChunk); ++x) {
            const size_t i = size_t(y * g.w + x);
            const int o = memT_[i];
            if (o == 0 || memD_[i]) continue;
            const float3 oc = ownerColor(o);
            const float X = float(x), Z = float(y);
            if (terr_.fill > 0.001f) {
                // a touch of per-cell variation reads as claimed floor tiles
                float k = 0.92f + 0.08f * rnd(x, y, 21);
                quad(fb, P(X, Z, x, y), P(X + 1, Z, x, y), P(X + 1, Z + 1, x, y), P(X, Z + 1, x, y), up, c4(oc * k, terr_.fill));
            }
            if (int16_t rm = memRoom_[i]; rm >= 0) {
                float3 rc = roomColor(rm);
                if (size_t(rm) < roomIds.size()) if (auto it = terr_.rooms.find(roomIds[size_t(rm)]); it != terr_.rooms.end()) rc = it->second;
                const float m = 0.06f;
                quad(fb, P(X + m, Z + m, x, y), P(X + 1 - m, Z + m, x, y), P(X + 1 - m, Z + 1 - m, x, y), P(X + m, Z + 1 - m, x, y), up,
                     c4(rc * (0.9f + 0.1f * rnd(x, y, 5)), terr_.roomFill));
            }
            // border strips inside the cell along edges facing another owner
            static const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
            for (int d = 0; d < 4; ++d) {
                int n = owner(x + dx[d], y + dy[d]);
                if (n < 0 || n == o) continue;
                // outer edge a-b (on the cell boundary), inner edge shifted by bw towards the centre
                float3 a, b, ia, ib;
                if (d == 0) { a = P(X + 1, Z, x, y); b = P(X + 1, Z + 1, x, y); ia = P(X + 1 - bw, Z, x, y); ib = P(X + 1 - bw, Z + 1, x, y); }
                else if (d == 1) { a = P(X, Z, x, y); b = P(X, Z + 1, x, y); ia = P(X + bw, Z, x, y); ib = P(X + bw, Z + 1, x, y); }
                else if (d == 2) { a = P(X, Z + 1, x, y); b = P(X + 1, Z + 1, x, y); ia = P(X, Z + 1 - bw, x, y); ib = P(X + 1, Z + 1 - bw, x, y); }
                else { a = P(X, Z, x, y); b = P(X + 1, Z, x, y); ia = P(X, Z + bw, x, y); ib = P(X + 1, Z + bw, x, y); }
                const float3 bright = oc * 0.55f + float3{0.45f, 0.45f, 0.45f};
                quad(bb, a, b, ib, ia, up, c4(bright, 0.95f), c4(bright, 0.95f), c4(oc, 0.0f), c4(oc, 0.0f));
                if (hi_) { // crisp core line on the edge
                    float3 ca = lerp(a, ia, 0.18f), cb = lerp(b, ib, 0.18f);
                    quad(bb, a, b, cb, ca, up, c4({1, 1, 1}, 0.55f));
                }
            }
        }
    setMesh(c.fill, fb);
    setMesh(c.border, bb);
    c.dT = false;
}

// ------------------------------------------------------------------ diggable rock

float RtsGroundFx::digTop(const MapGrid& g, const RtsView& v, int x, int y) const {
    return v.heightAt(g, float(x) + 0.5f, float(y) + 0.5f) + dig_.height;
}

void RtsGroundFx::buildDig(Chunk& c, int cx, int cy, const MapGrid& g, const RtsView& v) {
    MeshBuilder rb, vb, gb;
    auto kindAt = [&](int x, int y) -> int { return g.inside(x, y) ? memD_[size_t(y * g.w + x)] : -1; };
    const float bevel = hi_ ? 0.09f : 0.0f;
    for (int y = cy * kChunk; y < std::min(g.h, (cy + 1) * kChunk); ++y)
        for (int x = cx * kChunk; x < std::min(g.w, (cx + 1) * kChunk); ++x) {
            const int k = memD_[size_t(y * g.w + x)];
            if (!k) continue;
            const DigKind& K = dig_.kinds[std::min(size_t(k - 1), dig_.kinds.size() - 1)];
            const float X = float(x), Z = float(y), top = digTop(g, v, x, y);
            const float n0 = rnd(x, y, 1);
            float3 rock = K.rock * (0.97f + 0.06f * n0);
            if (K.style != 0 && !hi_) rock = lerp(rock, K.vein, 0.35f);   // low quality: vein shown as a tint only
            // reinforced wall (territory.fortify): the keeper's colour shows on the block, bricks on the faces
            const int wOwner = memW_[size_t(y * g.w + x)];
            if (wOwner) rock = lerp(rock, ownerColor(wOwner), 0.3f);
            // walls on edges towards dug / open cells; map edge: no wall
            static const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
            bool wall[4];
            for (int d = 0; d < 4; ++d) wall[d] = kindAt(x + dx[d], y + dy[d]) == 0;
            const float x0 = X + (wall[1] ? bevel : 0), x1 = X + 1 - (wall[0] ? bevel : 0);
            const float z0 = Z + (wall[3] ? bevel : 0), z1 = Z + 1 - (wall[2] ? bevel : 0);
            // continuous per-corner noise (per-cell values read as a checkerboard); vein cells tinted
            auto cornerC = [&](int ix, int iz) {
                float3 c = rock * (0.9f + 0.16f * rnd(ix, iz, 3));
                if (K.style != 0) c = lerp(c, K.vein * 0.8f, 0.18f + 0.22f * rnd(ix, iz, 13));
                return c4(c, 1);
            };
            quad(rb, {x0, top, z0}, {x1, top, z0}, {x1, top, z1}, {x0, top, z1}, {0, 1, 0},
                 cornerC(x, y), cornerC(x + 1, y), cornerC(x + 1, y + 1), cornerC(x, y + 1));
            const float wallTop = top - bevel * 0.9f;
            for (int d = 0; d < 4; ++d) {
                if (!wall[d]) continue;
                // outer edge endpoints (on the cell boundary) and inner (top) endpoints
                float3 oa, ob, ia, ib, nrm;
                if (d == 0) { oa = {X + 1, wallTop, Z}; ob = {X + 1, wallTop, Z + 1}; ia = {x1, top, z0}; ib = {x1, top, z1}; nrm = {1, 0, 0}; }
                else if (d == 1) { oa = {X, wallTop, Z}; ob = {X, wallTop, Z + 1}; ia = {x0, top, z0}; ib = {x0, top, z1}; nrm = {-1, 0, 0}; }
                else if (d == 2) { oa = {X, wallTop, Z + 1}; ob = {X + 1, wallTop, Z + 1}; ia = {x0, top, z1}; ib = {x1, top, z1}; nrm = {0, 0, 1}; }
                else { oa = {X, wallTop, Z}; ob = {X + 1, wallTop, Z}; ia = {x0, top, z0}; ib = {x1, top, z0}; nrm = {0, 0, -1}; }
                if (bevel > 0) quad(rb, oa, ob, ib, ia, normalize(nrm + float3{0, 1, 0}), c4(rock * 0.92f, 1));
                // the wall reaches down to the floor next to it (slightly below to hide seams)
                const float gx = (oa.x + ob.x) * 0.5f + nrm.x * 0.3f, gz = (oa.z + ob.z) * 0.5f + nrm.z * 0.3f;
                const float floorY = v.heightAt(g, gx, gz) - 0.05f;
                float3 fa{oa.x, floorY, oa.z}, fb2{ob.x, floorY, ob.z};
                const float3 wc = rock * 0.72f, wd = rock * 0.5f;
                quad(rb, oa, ob, fb2, fa, nrm, c4(wc, 1), c4(wc, 1), c4(wd, 1), c4(wd, 1));
                if (wOwner) { // masonry courses in the owner colour (two bands, offset like bricks)
                    const float3 oc = ownerColor(wOwner);
                    float3 along = ob - oa; along.y = 0;
                    const float3 off = nrm * 0.012f;
                    for (int s = 0; s < (hi_ ? 2 : 1); ++s) {
                        const float y0 = floorY + 0.18f + 0.34f * float(s) * (wallTop - floorY - 0.3f), y1 = y0 + 0.1f;
                        const float t0 = s ? 0.1f : 0.0f, t1 = s ? 0.9f : 1.0f;
                        const float3 a0 = oa + along * t0 + off, a1 = oa + along * t1 + off;
                        quad(vb, float3{a0.x, y0, a0.z}, float3{a1.x, y0, a1.z}, float3{a1.x, y1, a1.z}, float3{a0.x, y1, a0.z}, nrm, c4(oc * 0.9f, 1));
                    }
                }
                if (K.style == 1 && hi_) { // vein streaks on the wall face
                    for (int s = 0; s < 2; ++s) {
                        float t = 0.2f + 0.6f * rnd(x * 4 + d, y, 31 + uint32_t(s)), hgt = 0.12f + 0.15f * rnd(x, y * 4 + d, 41 + uint32_t(s));
                        float yy = floorY + 0.1f + (wallTop - floorY - 0.3f) * rnd(x + d, y, 51 + uint32_t(s));
                        float3 along = ob - oa; along.y = 0;
                        float3 p = oa + along * t + nrm * 0.012f;
                        float3 u = normalize(along) * 0.09f;
                        quad(vb, float3{p.x - u.x, yy, p.z - u.z}, float3{p.x + u.x, yy + hgt * 0.4f, p.z + u.z},
                             float3{p.x + u.x, yy + hgt, p.z + u.z}, float3{p.x - u.x, yy + hgt * 0.6f, p.z - u.z}, nrm, c4(K.vein, 1));
                    }
                }
            }
            if (!hi_ || K.style == 0) continue;
            if (K.style == 1) { // gold nuggets on the top
                for (int s = 0; s < 4; ++s) {
                    float px = x0 + 0.12f + (x1 - x0 - 0.24f) * rnd(x, y, 61 + uint32_t(s)), pz = z0 + 0.12f + (z1 - z0 - 0.24f) * rnd(x, y, 71 + uint32_t(s));
                    float r = 0.08f + 0.08f * rnd(x, y, 81 + uint32_t(s)), a = 6.283f * rnd(x, y, 91 + uint32_t(s));
                    float3 ctr{px, top + 0.05f, pz};
                    float3 p[4];
                    for (int q = 0; q < 4; ++q) p[q] = {px + std::cos(a + q * 1.5708f) * r, top + 0.006f, pz + std::sin(a + q * 1.5708f) * r};
                    for (int q = 0; q < 4; ++q) {
                        float3 nn = normalize(cross(p[(q + 1) % 4] - p[q], ctr - p[q]));
                        if (nn.y < 0) nn = nn * -1.0f;
                        tri(vb, p[q], p[(q + 1) % 4], ctr, nn, c4(K.vein, 1), c4(K.vein, 1), c4(K.vein * 1.15f, 1));
                    }
                }
            } else { // gem crystals: tilted pyramids
                for (int s = 0; s < 3; ++s) {
                    float px = x0 + 0.18f + (x1 - x0 - 0.36f) * rnd(x, y, 101 + uint32_t(s)), pz = z0 + 0.18f + (z1 - z0 - 0.36f) * rnd(x, y, 111 + uint32_t(s));
                    float r = 0.07f + 0.05f * rnd(x, y, 121 + uint32_t(s)), h = 0.22f + 0.25f * rnd(x, y, 131 + uint32_t(s));
                    float a = 6.283f * rnd(x, y, 141 + uint32_t(s));
                    float3 tip{px + 0.08f * std::cos(a * 1.7f), top + h, pz + 0.08f * std::sin(a * 1.7f)};
                    float3 p[4];
                    for (int q = 0; q < 4; ++q) p[q] = {px + std::cos(a + q * 1.5708f) * r, top - 0.01f, pz + std::sin(a + q * 1.5708f) * r};
                    const float3 gc = K.vein * (0.85f + 0.3f * rnd(x, y, 151 + uint32_t(s)));
                    for (int q = 0; q < 4; ++q) {
                        float3 mid = (p[q] + p[(q + 1) % 4]) * 0.5f;
                        float3 out{mid.x - px, 0, mid.z - pz};
                        float3 nn = normalize(cross(p[(q + 1) % 4] - p[q], tip - p[q]));
                        if (dot(nn, out) < 0) nn = nn * -1.0f;
                        tri(gb, p[q], p[(q + 1) % 4], tip, nn, c4(gc * 0.7f, 1), c4(gc * 0.7f, 1), c4(gc * 1.2f, 1));
                    }
                }
            }
        }
    setMesh(c.rock, rb);
    setMesh(c.veins, vb);
    setMesh(c.gems, gb);
    c.dD = false;
}

void RtsGroundFx::buildMarks(Chunk& c, int cx, int cy, const MapGrid& g, const RtsView& v) {
    MeshBuilder mb;
    const float3 up{0, 1, 0};
    for (int y = cy * kChunk; y < std::min(g.h, (cy + 1) * kChunk); ++y)
        for (int x = cx * kChunk; x < std::min(g.w, (cx + 1) * kChunk); ++x) {
            const size_t i = size_t(y * g.w + x);
            if (!memM_[i] || !memD_[i]) continue;
            const float X = float(x), Z = float(y), t = digTop(g, v, x, y) + 0.012f;
            const float m = 0.1f, w = 0.07f;
            quad(mb, {X + m, t, Z + m}, {X + 1 - m, t, Z + m}, {X + 1 - m, t, Z + 1 - m}, {X + m, t, Z + 1 - m}, up, c4(dig_.mark, 0.38f));
            // frame
            const float a0 = m, a1 = 1 - m;
            quad(mb, {X + a0, t, Z + a0}, {X + a1, t, Z + a0}, {X + a1, t, Z + a0 + w}, {X + a0, t, Z + a0 + w}, up, c4(dig_.mark, 0.9f));
            quad(mb, {X + a0, t, Z + a1 - w}, {X + a1, t, Z + a1 - w}, {X + a1, t, Z + a1}, {X + a0, t, Z + a1}, up, c4(dig_.mark, 0.9f));
            quad(mb, {X + a0, t, Z + a0 + w}, {X + a0 + w, t, Z + a0 + w}, {X + a0 + w, t, Z + a1 - w}, {X + a0, t, Z + a1 - w}, up, c4(dig_.mark, 0.9f));
            quad(mb, {X + a1 - w, t, Z + a0 + w}, {X + a1, t, Z + a0 + w}, {X + a1, t, Z + a1 - w}, {X + a1 - w, t, Z + a1 - w}, up, c4(dig_.mark, 0.9f));
        }
    setMesh(c.marks, mb);
    c.dM = false;
}

void RtsGroundFx::drawProgress(Renderer3D& r, const MapGrid& g, const RtsView& v, const std::vector<uint8_t>* vision) {
    if (!dig_.progress || !rules_) return;
    const bool fog = vision && vision->size() == g.flags.size();
    for (uint32_t i : marked_) {
        if (i >= g.dig.size() || !g.dig[i] || !memD_[i] || (fog && (*vision)[i] != 2)) continue;
        const DiggableDef& d = rules_->diggables[std::min(size_t(g.dig[i] - 1), rules_->diggables.size() - 1)];
        if (d.hardness.raw <= 0 || g.digHp[i] >= d.hardness) continue;
        const float p = std::clamp(1.0f - float((g.digHp[i] / d.hardness).toDouble()), 0.0f, 1.0f);
        const int x = int(i) % g.w, y = int(i) / g.w;
        const float X = float(x), Z = float(y), t = digTop(g, v, x, y) + 0.02f;
        // cracks darken the top as the rock gives way, a bar shows the remaining hardness
        uint32_t dark = rgba(20, 14, 8, uint8_t(40 + 110 * p));
        r.triangle({X + 0.1f, t, Z + 0.1f}, {X + 0.9f, t, Z + 0.1f}, {X + 0.9f, t, Z + 0.9f}, dark);
        r.triangle({X + 0.1f, t, Z + 0.1f}, {X + 0.9f, t, Z + 0.9f}, {X + 0.1f, t, Z + 0.9f}, dark);
        const float bx0 = X + 0.15f, bx1 = X + 0.85f, bz0 = Z + 0.45f, bz1 = Z + 0.57f, tt = t + 0.004f;
        uint32_t bg = rgba(0, 0, 0, 170), fg = rgbaf({dig_.mark.x, dig_.mark.y, dig_.mark.z, 1});
        r.triangle({bx0, tt, bz0}, {bx1, tt, bz0}, {bx1, tt, bz1}, bg);
        r.triangle({bx0, tt, bz0}, {bx1, tt, bz1}, {bx0, tt, bz1}, bg);
        const float fx = bx0 + (bx1 - bx0) * p;
        r.triangle({bx0, tt + 0.002f, bz0}, {fx, tt + 0.002f, bz0}, {fx, tt + 0.002f, bz1}, fg);
        r.triangle({bx0, tt + 0.002f, bz0}, {fx, tt + 0.002f, bz1}, {bx0, tt + 0.002f, bz1}, fg);
    }
}

// ------------------------------------------------------------------ roads (cells)

void RtsGroundFx::buildRoads(Chunk& c, int cx, int cy, const MapGrid& g, const RtsView& v) {
    MeshBuilder eb, tb; // edge (wider, darker) below the surface
    const float3 up{0, 1, 0};
    auto road = [&](int x, int y) { return g.inside(x, y) && memR_[size_t(y * g.w + x)] != 0; };
    auto emit = [&](float w, float yo, uint32_t col, MeshBuilder& b, int x, int y) {
        const float X = float(x), Z = float(y), h = w * 0.5f;
        auto P = [&](float px, float pz) {
            const float e = 0.002f;
            return float3{px, v.heightAt(g, std::clamp(px, X + e, X + 1 - e), std::clamp(pz, Z + e, Z + 1 - e)) + yo, pz};
        };
        const bool E = road(x + 1, y), W = road(x - 1, y), S = road(x, y + 1), N = road(x, y - 1);
        const int cnt = int(E) + int(W) + int(S) + int(N);
        const float mx = X + 0.5f, mz = Z + 0.5f;
        // L corner: a quarter ring around the cell corner between the two arms
        if (cnt == 2 && (E != W)) {
            const float ccx = E ? X + 1 : X, ccz = S ? Z + 1 : Z;  // pivot = shared corner
            const float r0 = 0.5f - h, r1 = 0.5f + h;
            const float a0 = std::atan2(mz - ccz, 0.0f);
            float a1 = std::atan2(0.0f, mx - ccx);
            if (a1 - a0 > 3.1415927f) a1 -= 6.2831853f; else if (a0 - a1 > 3.1415927f) a1 += 6.2831853f; // the short way round
            const int seg = hi_ ? 6 : 3;
            for (int s = 0; s < seg; ++s) {
                float t0 = a0 + (a1 - a0) * float(s) / float(seg), t1 = a0 + (a1 - a0) * float(s + 1) / float(seg);
                quad(b, P(ccx + std::cos(t0) * r0, ccz + std::sin(t0) * r0), P(ccx + std::cos(t1) * r0, ccz + std::sin(t1) * r0),
                     P(ccx + std::cos(t1) * r1, ccz + std::sin(t1) * r1), P(ccx + std::cos(t0) * r1, ccz + std::sin(t0) * r1), up, col);
            }
            return;
        }
        // centre piece (rounded when it is a dead end or alone) + arms to the connected edges
        if (cnt <= 1) {
            const int seg = hi_ ? 10 : 6;
            for (int s = 0; s < seg; ++s) {
                float t0 = 6.2831853f * float(s) / float(seg), t1 = 6.2831853f * float(s + 1) / float(seg);
                tri(b, P(mx, mz), P(mx + std::cos(t0) * h, mz + std::sin(t0) * h), P(mx + std::cos(t1) * h, mz + std::sin(t1) * h), up, col, col, col);
            }
        } else {
            quad(b, P(mx - h, mz - h), P(mx + h, mz - h), P(mx + h, mz + h), P(mx - h, mz + h), up, col);
        }
        // arms; a dead end's arm runs into the centre so it merges with the round cap
        const float k = cnt <= 1 ? 0.0f : h;
        if (E) quad(b, P(mx + k, mz - h), P(X + 1, mz - h), P(X + 1, mz + h), P(mx + k, mz + h), up, col);
        if (W) quad(b, P(X, mz - h), P(mx - k, mz - h), P(mx - k, mz + h), P(X, mz + h), up, col);
        if (S) quad(b, P(mx - h, mz + k), P(mx + h, mz + k), P(mx + h, Z + 1), P(mx - h, Z + 1), up, col);
        if (N) quad(b, P(mx - h, Z), P(mx + h, Z), P(mx + h, mz - k), P(mx - h, mz - k), up, col);
    };
    for (int y = cy * kChunk; y < std::min(g.h, (cy + 1) * kChunk); ++y)
        for (int x = cx * kChunk; x < std::min(g.w, (cx + 1) * kChunk); ++x) {
            if (!memR_[size_t(y * g.w + x)]) continue;
            if (hi_) emit(std::min(1.0f, road_.width + 0.16f), kRoadY - 0.006f, c4(road_.edge, road_.alpha * 0.9f), eb, x, y);
            const float n = 0.94f + 0.06f * rnd(x, y, 17);
            emit(road_.width, kRoadY, c4(road_.color * n, road_.alpha), tb, x, y);
        }
    // one mesh: edges first so the surface blends over them
    MeshBuilder all;
    for (const MeshBuilder* b : {&eb, &tb}) {
        const uint32_t base = uint32_t(all.vertexCount());
        for (auto& vtx : b->vertices()) all.addVertex(vtx.pos, vtx.nrm, vtx.rgba);
        const auto& id = b->indices();
        for (size_t k = 0; k + 2 < id.size(); k += 3) all.addTriangle(base + id[k], base + id[k + 1], base + id[k + 2]);
    }
    setMesh(c.road, all);
    c.dR = false;
}

// ------------------------------------------------------------------ concrete (foundation cells)

void RtsGroundFx::buildConcrete(Chunk& c, int cx, int cy, const MapGrid& g, const RtsView& v) {
    MeshBuilder sb, tb;   // seams (whole cell, darker) under the slab tops
    const float3 up{0, 1, 0};
    auto emit = [&](MeshBuilder& b, int x, int y, float inset, float yo, uint32_t col) {
        const float X = float(x), Z = float(y), e = 0.002f;
        auto P = [&](float px, float pz) {
            return float3{px, v.heightAt(g, std::clamp(px, X + e, X + 1 - e), std::clamp(pz, Z + e, Z + 1 - e)) + yo, pz};
        };
        quad(b, P(X + inset, Z + inset), P(X + 1 - inset, Z + inset), P(X + 1 - inset, Z + 1 - inset), P(X + inset, Z + 1 - inset), up, col);
    };
    for (int y = cy * kChunk; y < std::min(g.h, (cy + 1) * kChunk); ++y)
        for (int x = cx * kChunk; x < std::min(g.w, (cx + 1) * kChunk); ++x) {
            if (!memC_[size_t(y * g.w + x)]) continue;
            emit(sb, x, y, 0.0f, kRoadY - 0.008f, c4(conc_.seam, conc_.alpha));
            const float n = 0.93f + 0.07f * rnd(x, y, 23);
            emit(tb, x, y, hi_ ? 0.05f : 0.03f, kRoadY - 0.004f, c4(conc_.color * n, conc_.alpha));
        }
    MeshBuilder all;
    for (const MeshBuilder* b : {&sb, &tb}) {
        const uint32_t base = uint32_t(all.vertexCount());
        for (auto& vtx : b->vertices()) all.addVertex(vtx.pos, vtx.nrm, vtx.rgba);
        const auto& id = b->indices();
        for (size_t k = 0; k + 2 < id.size(); k += 3) all.addTriangle(base + id[k], base + id[k + 1], base + id[k + 2]);
    }
    setMesh(c.concrete, all);
    c.dC = false;
}

// ------------------------------------------------------------------ lanes (MW2) and map roads between buildings

void RtsGroundFx::buildLanes(const RtsScene& s, const MapGrid& g, const RtsView& v) {
    MeshBuilder b;
    const float3 up{0, 1, 0};
    auto ribbon = [&](const std::vector<float3>& pts, float width, float alpha, float3 colr, float3 edge, float yo, bool fadeEnds) {
        if (pts.size() < 2) return;
        // cumulative length for end fades
        std::vector<float> len(pts.size(), 0);
        for (size_t k = 1; k < pts.size(); ++k) { float3 d = pts[k] - pts[k - 1]; d.y = 0; len[k] = len[k - 1] + length(d); }
        const float total = len.back();
        uint32_t prev[4] = {};
        for (size_t k = 0; k < pts.size(); ++k) {
            float3 t = pts[std::min(k + 1, pts.size() - 1)] - pts[k ? k - 1 : 0];
            t.y = 0;
            float tl = length(t);
            float3 side = tl > 1e-5f ? float3{-t.z / tl, 0, t.x / tl} : float3{1, 0, 0};
            float fade = 1;
            if (fadeEnds) fade = std::clamp(std::min(len[k], total - len[k]) / 1.2f, 0.0f, 1.0f);
            const float off[4] = {-0.5f, -0.32f, 0.32f, 0.5f};
            const float al[4] = {0.0f, 1.0f, 1.0f, 0.0f};
            uint32_t cur[4];
            for (int q = 0; q < 4; ++q) {
                float3 p = pts[k] + side * (off[q] * width);
                p.y = v.heightAt(g, p.x, p.z) + yo;
                float3 cc = (q == 0 || q == 3) ? edge : colr;
                cur[q] = b.addVertex(p, up, c4(cc, alpha * fade * (q == 0 || q == 3 ? 0.0f : al[q])));
            }
            if (k > 0)
                for (int q = 0; q < 3; ++q) { b.addTriangle(prev[q], prev[q + 1], cur[q + 1]); b.addTriangle(prev[q], cur[q + 1], cur[q]); }
            std::memcpy(prev, cur, sizeof prev);
        }
    };
    // buildings a lane must not cross (garrisons other than its own two ends)
    struct Obst { float x, z, r; Entity e; };
    std::vector<Obst> obst;
    for (const SceneObject& o : s.objects)
        if (o.garrison >= 0 && rules_ && o.type < rules_->types.size()) {
            const UnitType& t = rules_->types[o.type];
            obst.push_back({o.pos.x, o.pos.z, std::max(0.8f, float(std::max(t.footprintW, t.footprintH)) * 0.5f), o.entity});
        }
    // Mushroom Wars 2 shows paths between neighbouring buildings only: keep a pair a-b unless some
    // building c is closer to both ends than they are to each other (relative neighbourhood graph)
    auto neighbours = [&](const Lane& l, const std::vector<float3>& pts) {
        if (lane_.all || pts.size() < 2) return true;
        const float3 a = pts.front(), b = pts.back();
        const float dab = std::hypot(a.x - b.x, a.z - b.z);
        for (const Obst& ob : obst) {
            if (ob.e == l.a || ob.e == l.b) continue;
            const float da = std::hypot(a.x - ob.x, a.z - ob.z), db = std::hypot(b.x - ob.x, b.z - ob.z);
            if (std::max(da, db) < dab * 0.97f) return false;
        }
        return true;
    };
    if (s.lanes) {
        const LaneRules& LR = rules_->lanes;
        const float full = lane_.width > 0 ? lane_.width : float(LR.left + LR.right) * float(LR.spacing.toDouble()) * 0.6f + 0.6f;
        for (const Lane& l : *s.lanes) {
            std::vector<float3> pts;
            pts.reserve(l.points.size());
            for (const Vec3& p : l.points) pts.push_back({float(p.x.toDouble()), 0, float(p.z.toDouble())});
            if (!lane_.all && l.offset == 0) {
                bool blocked = false;
                for (const Obst& ob : obst) {
                    if (ob.e == l.a || ob.e == l.b) continue;
                    for (const float3& p : pts) if (std::hypot(p.x - ob.x, p.z - ob.z) < ob.r) { blocked = true; break; }
                    if (blocked) break;
                }
                if (blocked || !neighbours(l, pts)) continue;
            }
            if (l.offset == 0) {
                // centre of the band between the outermost lanes
                std::vector<float3> c = pts;
                const float shift = (float(LR.right) - float(LR.left)) * 0.5f * float(LR.spacing.toDouble());
                for (size_t k = 0; k < c.size() && std::fabs(shift) > 1e-4f; ++k) {
                    float3 t = pts[std::min(k + 1, pts.size() - 1)] - pts[k ? k - 1 : 0];
                    float tl = std::hypot(t.x, t.z);
                    if (tl > 1e-5f) { c[k].x += -t.z / tl * shift; c[k].z += t.x / tl * shift; }
                }
                ribbon(c, full, lane_.alpha, lane_.color, lane_.edge, kLaneY, true);
                if (hi_ && lane_.sideLanes) ribbon(pts, 0.14f, lane_.alpha * 0.8f, lane_.edge, lane_.edge, kLaneY + 0.004f, true);
            }
        }
        // offset lanes as faint tracks (only for pairs that were drawn)
        if (hi_ && lane_.sideLanes) {
            for (const Lane& l : *s.lanes) {
                if (l.offset == 0) continue;
                std::vector<float3> pts;
                for (const Vec3& p : l.points) pts.push_back({float(p.x.toDouble()), 0, float(p.z.toDouble())});
                bool blocked = false;
                if (!lane_.all)
                    for (const Obst& ob : obst) {
                        if (ob.e == l.a || ob.e == l.b) continue;
                        for (const float3& p : pts) if (std::hypot(p.x - ob.x, p.z - ob.z) < ob.r) { blocked = true; break; }
                        if (blocked) break;
                    }
                if (!blocked && neighbours(l, pts)) ribbon(pts, 0.1f, lane_.alpha * 0.45f, lane_.edge, lane_.edge, kLaneY + 0.004f, true);
            }
        }
    }
    // hand-made map roads between buildings (Mushroom Wars): straight ribbons
    for (auto& [a, bb] : s.roads) {
        std::vector<float3> pts;
        float3 d = bb - a; d.y = 0;
        const int n = std::max(2, int(length(d) * 2.0f) + 1);
        for (int k = 0; k <= n; ++k) pts.push_back(lerp(a, bb, float(k) / float(n)));
        ribbon(pts, lane_.width > 0 ? lane_.width : 0.9f, lane_.alpha, lane_.color, lane_.edge, kLaneY, true);
    }
    setMesh(lanes_, b);
}

// ------------------------------------------------------------------ frame


class RtsGroundFx::PreparedFrame final {
    friend class RtsGroundFx;
public:
    PreparedFrame()=default;
    PreparedFrame(const PreparedFrame&)=delete;
    PreparedFrame& operator=(const PreparedFrame&)=delete;
    ~PreparedFrame() noexcept {
        if(committed_ || !device_)return;
        // Private GPU handles are owned solely by the prepared frame. Original
        // published handles copied into staged_ are BORROWED and untouched.
        for(Mesh* mesh:journal_) {
            if(mesh->vb.valid())try {device_->destroyBuffer(mesh->vb);}catch(...) {}
            if(mesh->ib.valid())try {device_->destroyBuffer(mesh->ib);}catch(...) {}
            *mesh=Mesh{};
        }
    }
private:
    RtsGroundFx* owner_=nullptr;
    gfx::Device* device_=nullptr;
    uint64_t revision_=0;
    bool committed_=false, fullReset_=false, changedLane_=false;
    std::unique_ptr<RtsGroundFx> staged_;
    std::vector<Mesh*> journal_;
    std::vector<std::pair<size_t,uint8_t>> changedChunks_;
};

std::shared_ptr<RtsGroundFx::PreparedFrame> RtsGroundFx::prepareMapReset(
        const MapGrid& grid,bool forceSameSize) {
    if(!dev_ || !rules_ || grid.w<=0 || grid.h<=0)
        return {};
    checkGpuDevice(*dev_);
    // Rebuilding the same map is handled by the ordinary frame's sparse
    // geometry journal. Avoid retiring all chunks for an unchanged grid.
    const bool sameMap=mapMatches(grid.w,grid.h);
    if(sameMap && !forceSameSize)return {};
    auto candidate=std::make_shared<PreparedFrame>();
    candidate->owner_=this;
    candidate->device_=dev_;
    candidate->revision_=publicationRevision_;
    candidate->fullReset_=true;
    candidate->changedLane_=true;
    candidate->staged_=std::make_unique<RtsGroundFx>();
    RtsGroundFx& next=*candidate->staged_;
    next.dev_=dev_;
    next.rules_=rules_;
    next.useT_=useT_;
    next.useD_=useD_;
    next.useR_=useR_;
    next.useC_=useC_;
    next.hi_=hi_;
    next.terr_=terr_;next.dig_=dig_;
    next.lane_=lane_;next.road_=road_;next.conc_=conc_;
    next.viewer_=viewer_;
    // This clone owns no mesh, material kit, or texture. resetGrid only
    // prepares CPU cells/chunks, so an aborted candidate cannot retire or
    // destroy old published GPU handles.
    next.invalid_=true;
    next.resetGrid(grid);
    if(sameMap) {
        // Preserve knowledge learned by the viewer across an editor
        // height/detail sculpt. Only geometry is replaced; ownership,
        // room, dig and mark visibility memory must not be erased.
        next.memT_=memT_;next.memD_=memD_;
        next.memR_=memR_;next.memM_=memM_;
        next.memC_=memC_;next.memW_=memW_;
        next.memRoom_=memRoom_;
        next.marked_=marked_;
        next.territoryBlend_=territoryBlend_;
        next.territoryTime_=territoryTime_;
        next.viewer_=viewer_;
        next.nextScan_=-1; // rescan edits before first primary submission
    }
    next.geomDirty_=true;
    next.lanes_={}; // a lane built for the old map is never replayed
    return candidate;
}

std::shared_ptr<RtsGroundFx::PreparedFrame> RtsGroundFx::prepareFrame(
        Renderer3D& r,const RtsScene& s,double time,
        const std::vector<uint8_t>* vision,const MapGrid& g,const RtsView& v) {
    if(dev_)checkGpuDevice(*dev_);
    if(!dev_ || !rules_ || g.w<=0 || g.h<=0)return {};
    auto prepared=std::make_shared<PreparedFrame>();
    prepared->owner_=this;
    prepared->device_=dev_;
    prepared->revision_=publicationRevision_;
    prepared->staged_=std::make_unique<RtsGroundFx>();
    RtsGroundFx& next=*prepared->staged_;
    // Clone ONLY CPU mutable frame state and borrowed mesh handles. Shader,
    // pipeline and TextureCache remain owned by this published view. The
    // staging instance is never submitted and never calls shutdown().
    next.dev_=dev_;
    next.rules_=rules_;
    next.terr_=terr_;
    next.dig_=dig_;
    next.lane_=lane_;
    next.road_=road_;
    next.conc_=conc_;
    next.useT_=useT_;next.useD_=useD_;next.useR_=useR_;next.useC_=useC_;
    next.hi_=hi_;next.invalid_=invalid_;next.geomDirty_=geomDirty_;
    next.gridW_=gridW_;next.gridH_=gridH_;
    next.chunksX_=chunksX_;next.chunksY_=chunksY_;
    next.nextScan_=nextScan_;next.viewer_=viewer_;
    next.memT_=memT_;next.memD_=memD_;next.memR_=memR_;
    next.memM_=memM_;next.memC_=memC_;next.memW_=memW_;
    next.memRoom_=memRoom_;
    next.marked_=marked_;
    next.chunks_=chunks_;next.visible_=visible_;
    next.laneSig_=laneSig_;next.laneCount_=laneCount_;
    next.lanes_=lanes_;next.time_=time_;
    next.territoryBlend_=territoryBlend_;
    next.territoryTime_=territoryTime_;
    return prepareSceneIntoCandidate(
        std::move(prepared),r,s,time,vision,g,v);
}

std::shared_ptr<RtsGroundFx::PreparedFrame> RtsGroundFx::prepareMapSceneFrame(
        Renderer3D& r,const RtsScene& s,double time,
        const std::vector<uint8_t>* vision,const MapGrid& g,
        const RtsView& v,bool forceSameSize) {
    // Map preparation returns a private geometry-free clone. Fill THIS
    // candidate in place, instead of committing/resetting then rebuilding.
    auto candidate=prepareMapReset(g,forceSameSize);
    if(!candidate)return {};
    return prepareSceneIntoCandidate(
        std::move(candidate),r,s,time,vision,g,v);
}

std::shared_ptr<RtsGroundFx::PreparedFrame> RtsGroundFx::prepareSceneIntoCandidate(
        std::shared_ptr<PreparedFrame> prepared,
        Renderer3D& r,const RtsScene& s,double time,
        const std::vector<uint8_t>* vision,const MapGrid& g,
        const RtsView& v) {
    if(!prepared || prepared->owner_!=this ||
       prepared->device_!=dev_ ||
       prepared->revision_!=publicationRevision_ ||
       !prepared->staged_)
        throw std::logic_error("RtsGroundFx: stale scene-map candidate");
    RtsGroundFx& next=*prepared->staged_;
    next.stagingJournal_=&prepared->journal_;
    prepared->fullReset_=prepared->fullReset_ ||
        next.invalid_ || g.w!=next.gridW_ ||
        g.h!=next.gridH_ || s.viewer!=next.viewer_;
    next.time_=float(std::fmod(time,1000.0));
    bool highQuality=r.materialQuality()!=MaterialQuality::Low;
    if(const char* q=std::getenv("AIVERSE_GROUNDFX"))
        highQuality=std::string(q)!="low";
    if(highQuality!=next.hi_){next.hi_=highQuality;next.invalid_=true;}
    prepared->fullReset_=prepared->fullReset_ || next.invalid_;
    if(next.invalid_ || g.w!=next.gridW_ || g.h!=next.gridH_ ||
       s.viewer!=next.viewer_) {
        next.viewer_=s.viewer;
        next.resetGrid(g);
    }
    if(next.geomDirty_) {
        next.geomDirty_=false;
        for(Chunk& c:next.chunks_)c.dT=c.dD=c.dM=c.dR=c.dC=true;
        next.laneCount_=~size_t(0);
    }
    if((next.useT_ || next.useD_ || next.useR_ || next.useC_) &&
       (time>=next.nextScan_ || next.nextScan_<0)) {
        next.scan(g,vision,next.viewer_);
        next.nextScan_=time+(next.terr_.smooth?0.05:0.2);
    }
    if(next.useT_ && next.terr_.on && next.terr_.smooth)
        next.animateTerritory(g,time);

    // Prepare a bounded GPU batch in a completely private owner. The old
    // Mesh handles are *not* retired by NativeMeshBatch::publish because each
    // staged slot is set to empty BEFORE it is staged.
    constexpr size_t kBatchChunkLimit=16;
    constexpr size_t kBatchBytes=128ull*1024ull*1024ull;
    NativeMeshBatch batch(*dev_,next.retiredMeshes_);
    next.activeMeshBatch_=&batch;
    try {
        if(next.lane_.on) {
            uint32_t sig=s.lanes?s.laneSignature:0u;
            size_t count=(s.lanes?s.lanes->size():0)*131+s.roads.size();
            for(auto& [a,b]:s.roads)
                sig=sig*31u+uint32_t(int(a.x*4)*7+int(a.z*4)*13+
                                      int(b.x*4)*17+int(b.z*4)*19);
            if(sig!=next.laneSig_ || count!=next.laneCount_) {
                next.buildLanes(s,g,v);
                next.laneSig_=sig;
                next.laneCount_=count;
            }
        }
        next.visible_.clear();
        const mat4& vp=r.viewProj();
        size_t builtChunks=0;
        for(int cy=0;cy<next.chunksY_;++cy)
            for(int cx=0;cx<next.chunksX_;++cx) {
                const size_t index=size_t(cy*next.chunksX_+cx);
                Chunk& c=next.chunks_[index];
                const float3 mn{float(cx*kChunk),c.yMin,float(cy*kChunk)};
                const float3 mx{float((cx+1)*kChunk),c.yMax,float((cy+1)*kChunk)};
                if(!boxVisible(vp,mn,mx))continue;
                next.visible_.push_back(int(index));
                const bool needs=(next.useT_ && next.terr_.on && c.dT) ||
                    (next.useD_ && next.dig_.on && (c.dD || c.dM)) ||
                    (next.useR_ && next.road_.on && c.dR) ||
                    (next.useC_ && next.conc_.on && c.dC);
                if(!needs)continue;
                if(builtChunks>=kBatchChunkLimit ||
                   batch.stagedBytes()>=kBatchBytes)
                    continue;
                if(next.useT_ && next.terr_.on && c.dT)
                    next.buildTerritory(c,cx,cy,g,v);
                if(next.useD_ && next.dig_.on && c.dD)
                    next.buildDig(c,cx,cy,g,v);
                if(next.useD_ && next.dig_.on && c.dM)
                    next.buildMarks(c,cx,cy,g,v);
                if(next.useR_ && next.road_.on && c.dR)
                    next.buildRoads(c,cx,cy,g,v);
                if(next.useC_ && next.conc_.on && c.dC)
                    next.buildConcrete(c,cx,cy,g,v);
                ++builtChunks;
            }
        next.activeMeshBatch_=nullptr;
        // Publish only into the PRIVATE staging clone. All public old buffers
        // are retired separately by commitFrame() after the shared cohort
        // successfully prepares.
        batch.publish();
    } catch(...) {
        next.activeMeshBatch_=nullptr;
        throw;
    }

    // Map reset must retire the previous lane mesh even when the new
    // scene contains no lanes. Never discard the map candidate's flag.
    prepared->changedLane_=prepared->changedLane_ ||
        std::find(prepared->journal_.begin(),
                  prepared->journal_.end(),&next.lanes_)!=
            prepared->journal_.end();
    // Build a compact no-allocation-at-commit slot journal; dense map chunks
    // not touched by this frame keep the same stable RHI handles.
    for(size_t i=0;i<next.chunks_.size();++i) {
        Chunk& c=next.chunks_[i];
        const std::array<Mesh*,8> slots={
            &c.fill,&c.border,&c.rock,&c.veins,&c.gems,&c.marks,&c.road,&c.concrete
        };
        for(uint8_t k=0;k<slots.size();++k)
            if(std::find(prepared->journal_.begin(),prepared->journal_.end(),
                         slots[k])!=prepared->journal_.end())
                prepared->changedChunks_.emplace_back(i,k);
    }
    return prepared;
}

void RtsGroundFx::commitFrame(std::shared_ptr<PreparedFrame> frame) noexcept {
    if(!frame || frame->committed_)return;
    if(frame->owner_!=this || frame->device_!=dev_ ||
       frame->revision_!=publicationRevision_ || !frame->staged_)
        std::terminate();
    RtsGroundFx& next=*frame->staged_;
    const auto releaseSlots=[this](Chunk& c) noexcept {
        for(Mesh* m:{&c.fill,&c.border,&c.rock,&c.veins,&c.gems,
                     &c.marks,&c.road,&c.concrete})freeMesh(*m);
    };
    if(frame->fullReset_) {
        for(auto& c:chunks_)releaseSlots(c);
    }else{
        for(const auto& [index,slot]:frame->changedChunks_) {
            if(index>=chunks_.size())std::terminate();
            Chunk& c=chunks_[index];
            Mesh* slots[]={&c.fill,&c.border,&c.rock,&c.veins,&c.gems,
                           &c.marks,&c.road,&c.concrete};
            freeMesh(*slots[slot]);
        }
    }
    if(frame->changedLane_)freeMesh(lanes_);
    using std::swap;
    swap(chunks_,next.chunks_);
    swap(lanes_,next.lanes_);
    swap(territoryBlend_,next.territoryBlend_);
    swap(territoryTime_,next.territoryTime_);
    swap(memT_,next.memT_);swap(memD_,next.memD_);
    swap(memR_,next.memR_);swap(memM_,next.memM_);
    swap(memC_,next.memC_);swap(memW_,next.memW_);
    swap(memRoom_,next.memRoom_);
    swap(marked_,next.marked_);
    swap(visible_,next.visible_);
    swap(gridW_,next.gridW_);swap(gridH_,next.gridH_);
    swap(chunksX_,next.chunksX_);swap(chunksY_,next.chunksY_);
    swap(nextScan_,next.nextScan_);
    swap(viewer_,next.viewer_);
    swap(laneSig_,next.laneSig_);swap(laneCount_,next.laneCount_);
    swap(useR_,next.useR_);
    swap(hi_,next.hi_);swap(invalid_,next.invalid_);
    swap(geomDirty_,next.geomDirty_);
    swap(time_,next.time_);
    frame->committed_=true;
    ++publicationRevision_;
}

void RtsGroundFx::draw(Renderer3D& r,const RtsScene& s,double time,
                       const std::vector<uint8_t>* vision,
                       const MapGrid& g,const RtsView& v) {
    auto pending=prepareFrame(r,s,time,vision,g,v);
    commitFrame(std::move(pending));
    drawPrepared(r,s,time,vision,g,v);
}

void RtsGroundFx::drawCandidate(const std::shared_ptr<PreparedFrame>& frame,
        Renderer3D& r,const RtsScene& s,double time,
        const std::vector<uint8_t>* vision,const MapGrid& g,const RtsView& v) {
    if(!frame){drawPrepared(r,s,time,vision,g,v);return;}
    if(frame->owner_!=this || frame->revision_!=publicationRevision_ || frame->committed_)
        throw std::logic_error("GroundFX: stale candidate draw");
    auto& next=*frame->staged_;
    const auto exchange=[&]() noexcept {
    using std::swap;
    swap(chunks_,next.chunks_);
    swap(lanes_,next.lanes_);
    swap(territoryBlend_,next.territoryBlend_);
    swap(territoryTime_,next.territoryTime_);
    swap(memT_,next.memT_);swap(memD_,next.memD_);
    swap(memR_,next.memR_);swap(memM_,next.memM_);
    swap(memC_,next.memC_);swap(memW_,next.memW_);
    swap(memRoom_,next.memRoom_);
    swap(marked_,next.marked_);
    swap(visible_,next.visible_);
    swap(gridW_,next.gridW_);swap(gridH_,next.gridH_);
    swap(chunksX_,next.chunksX_);swap(chunksY_,next.chunksY_);
    swap(nextScan_,next.nextScan_);
    swap(viewer_,next.viewer_);
    swap(laneSig_,next.laneSig_);swap(laneCount_,next.laneCount_);
    swap(useR_,next.useR_);
    swap(hi_,next.hi_);swap(invalid_,next.invalid_);
    swap(geomDirty_,next.geomDirty_);
    swap(time_,next.time_);
    };
    exchange();
    try {drawPrepared(r,s,time,vision,g,v);}catch(...) {exchange();throw;}
    exchange();
}

void RtsGroundFx::drawPrepared(Renderer3D& r,const RtsScene& s,double time,
                               const std::vector<uint8_t>* vision,
                               const MapGrid& g,const RtsView& v) {
    if(!dev_ || !rules_ || g.w<=0 || g.h<=0)return;
    // rock blocks: ordinary lit opaque meshes (cast and receive sun shadows, point lights)
    if (useD_ && dig_.on)
        for (int ci : visible_) {
            Chunk& c = chunks_[size_t(ci)];
            if (c.rock.indexCount) r.drawMeshSnapshot(c.rock, mat4{}, {1, 1, 1, 1}, 0, 0.12f);
            if (c.veins.indexCount) r.drawMeshSnapshot(c.veins, mat4{}, {1, 1, 1, 1}, kDrawNoCastShadow, 0.9f, 0.25f);
            if (c.gems.indexCount) {
                float glow = 0.0f;
                for (auto& k : dig_.kinds) glow = std::max(glow, k.glow);
                r.drawMeshSnapshot(c.gems, mat4{}, {1, 1, 1, 1}, kDrawNoCastShadow, 1.0f, glow * (hi_ ? 0.85f + 0.15f * std::sin(time_ * 2.0f) : 1.0f));
            }
        }
    if (useD_ && dig_.on) drawProgress(r, g, v, vision);

    if (!pipe_.valid()) return;
    // Frame-local immutable presentation packet: no callback can traverse
    // chunks_, visible_, look settings or the TextureCache after submission.
    // Mesh GPU handles are kept alive by NativePresentationRetirement.
    struct Packet {
        Mesh mesh;
        gfx::TextureHandle texture;
        GU0 uniforms;
    };
    const TextureRef cobble=hi_ && road_.cobbles ? cobbleTexture_ : TextureRef{};
    const TextureRef dirt=hi_ ? dirtTexture_ : TextureRef{};
    const float pulse=hi_ && terr_.pulse ? 0.35f : 0.0f;
    std::vector<Packet> packets;
    packets.reserve(visible_.size()*6+1);
    const auto append=[&](const Mesh& mesh,const Tex* tex,TextureRef builtin,
                          float emissive,float wave,float spec) {
        if(!mesh.indexCount)return;
        Packet p{};
        p.mesh=mesh;
        const TextureRef t=tex && tex->ref.valid()?tex->ref:builtin;
        const bool textured=t.valid();
        GU0& u=p.uniforms;
        u.color[0]=u.color[1]=u.color[2]=u.color[3]=1;
        u.params[0]=textured?1.0f:0.0f;
        u.params[1]=tex?tex->scale:2.0f;
        u.params[2]=time_;
        u.params[3]=wave;
        u.params2[0]=emissive;
        u.params2[1]=spec;
        u.params2[2]=tex?tex->strength:1.0f;
        u.params2[3]=1;
        if(textured) {
            u.rect[0]=t.u0;u.rect[1]=t.v0;
            u.rect[2]=t.u1-t.u0;u.rect[3]=t.v1-t.v0;
        } else {
            u.rect[2]=u.rect[3]=1;
        }
        if(textured && tex && !tex->ref.valid()) {
            u.params[1]=1.6f;
            u.params2[2]=0.55f;
        }
        p.texture=textured?t.tex:white_;
        packets.push_back(p);
    };
    // Preserve the old transparent layer order across all cameras.
    for(int ci:visible_) {
        const Chunk& c=chunks_[size_t(ci)];
        if(useT_ && terr_.on) {
            append(c.fill,&terr_.tex,{},0.05f,0,0.05f);
            append(c.border,nullptr,{},terr_.glow,pulse,0.1f);
        }
    }
    for(int ci:visible_)
        if(useC_ && conc_.on)
            append(chunks_[size_t(ci)].concrete,&conc_.tex,dirt,0.0f,0,0.12f);
    for(int ci:visible_)
        if(useR_ && road_.on)
            append(chunks_[size_t(ci)].road,&road_.tex,cobble,0.0f,0,0.08f);
    if(lane_.on)append(lanes_,&lane_.tex,dirt,0.0f,0,0.05f);
    for(int ci:visible_)
        if(useD_ && dig_.on)
            append(chunks_[size_t(ci)].marks,nullptr,{},0.6f,
                   hi_?0.4f:0.0f,0.1f);
    const gfx::PipelineHandle pipeline=pipe_;
    r.drawCustom([&r,pipeline,packets=std::move(packets)](gfx::Device& dev) {
        dev.setPipeline(pipeline);
        r.bindLitFrame();
        for(const Packet& packet:packets) {
            dev.setTexture(2,packet.texture);
            dev.setUniforms(0,&packet.uniforms,sizeof packet.uniforms);
            dev.setVertexBuffer(packet.mesh.vb);
            dev.setIndexBuffer(packet.mesh.ib);
            dev.drawIndexed(packet.mesh.indexCount);
        }
    });
}

} // namespace aiverse::game
