#include "SceneBuilder.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include "../data/Json.h"
#include "LightmapUnwrap.h"

namespace aiverse::bake {

namespace {
struct Hasher {
    uint64_t h = 1469598103934665603ull;
    void bytes(const void* p, size_t n) { h = fnv1a(p, n, h); }
    template <class T> void pod(const T& v) { bytes(&v, sizeof v); }
    void f3(float3 v) { pod(v.x); pod(v.y); pod(v.z); }
    void str(const std::string& s) { const uint64_t n = s.size(); pod(n); bytes(s.data(), s.size()); }
};
} // namespace

uint64_t hashBakeInput(const BakeScene& sc, const BakeSettings& s) {
    Hasher h;
    h.str("aiverse-lightbake-1");
    h.str(settingsToJson(s));
    h.pod(sc.sourceHash);
    for (const auto& m : sc.materials) {
        h.f3(m.albedo); h.f3(m.emissive); h.pod(m.alphaCutoff); h.pod(m.doubleSided);
        if (m.texture) { h.pod(m.texture->width); h.pod(m.texture->height); h.bytes(m.texture->rgba.data(), m.texture->rgba.size()); }
    }
    for (const auto& m : sc.meshes) {
        h.bytes(m.pos.data(), m.pos.size() * sizeof(float3)); h.bytes(m.nrm.data(), m.nrm.size() * sizeof(float3));
        h.bytes(m.uv.data(), m.uv.size() * sizeof(float2)); h.bytes(m.uv2.data(), m.uv2.size() * sizeof(float2));
        h.bytes(m.idx.data(), m.idx.size() * 4); h.bytes(m.triMaterial.data(), m.triMaterial.size() * 2);
    }
    for (const auto& i : sc.instances) { h.pod(i.mesh); h.bytes(i.world.m, sizeof i.world.m); h.pod(i.key); h.pod(i.receive); h.pod(i.cast); h.pod(i.scale); h.bytes(i.materials.data(), i.materials.size() * 2); }
    h.pod(sc.sun.on); h.f3(sc.sun.dir); h.f3(sc.sun.color); h.pod(sc.sun.angle);
    h.f3(sc.sky.sky); h.f3(sc.sky.ground); h.pod(sc.sky.intensity);
    for (const auto& l : sc.lights) { h.f3(l.pos); h.f3(l.color); h.pod(l.radius); h.f3(l.dir); h.pod(l.cosOuter); h.pod(l.cosInner); h.pod(l.bakedOnly); }
    h.pod(sc.terrain.instance); h.bytes(sc.terrain.origin, sizeof sc.terrain.origin); h.bytes(sc.terrain.extent, sizeof sc.terrain.extent);
    return h.h;
}

std::string settingsToJson(const BakeSettings& s) {
    Json j = Json::object();
    j.set("mode", Json::string(bakedModeName(s.mode)));
    j.set("texelsPerUnit", Json::number(double(s.texelsPerUnit), 4));
    j.set("pageSize", Json::number(int64_t(s.pageSize)));
    j.set("minRegion", Json::number(int64_t(s.minRegion)));
    j.set("maxRegion", Json::number(int64_t(s.maxRegion)));
    j.set("padding", Json::number(int64_t(s.padding)));
    j.set("samples", Json::number(int64_t(s.samples)));
    j.set("bounces", Json::number(int64_t(s.bounces)));
    j.set("aoDistance", Json::number(double(s.aoDistance), 4));
    j.set("aoStrength", Json::number(double(s.aoStrength), 4));
    j.set("denoise", Json::fromBool(s.denoise));
    j.set("indirectIntensity", Json::number(double(s.indirectIntensity), 4));
    j.set("maskSamples", Json::number(int64_t(s.maskSamples)));
    j.set("probeSpacing", Json::number(double(s.probeSpacing), 4));
    j.set("probeHeight", Json::number(double(s.probeHeight), 4));
    j.set("probeSamples", Json::number(int64_t(s.probeSamples)));
    j.set("maxProbes", Json::number(int64_t(s.maxProbes)));
    j.set("seed", Json::number(int64_t(s.seed)));
    j.set("rgbmRange", Json::number(double(s.rgbmRange), 4));
    return j.dump(-1);   // threads are not part of the result (bit-identical for any count)
}

bool settingsFromJson(const std::string& text, BakeSettings& s, std::string& error) {
    Json j;
    try { j = Json::parse(text.empty() ? "{}" : text); } catch (const std::exception& e) { error = std::string("bake settings: ") + e.what(); return false; }
    if (!j.isObject()) { error = "bake settings: expected an object"; return false; }
    auto f = [&](const char* k, float& v, float lo, float hi) { if (j.has(k)) v = std::clamp(float(j[k].toDouble(v)), lo, hi); };
    auto i = [&](const char* k, int& v, int lo, int hi) { if (j.has(k)) v = int(std::clamp<int64_t>(j[k].integer(v), lo, hi)); };
    if (j.has("mode") && !parseBakedMode(j.getStr("mode"), s.mode)) { error = "bake settings: unknown mode '" + j.getStr("mode") + "' (indirect, shadowmask, subtractive)"; return false; }
    if (j.has("quality")) {   // presets first, explicit keys below win
        const std::string q = j.getStr("quality");
        if (q == "preview") { s.samples = 16; s.bounces = 1; s.texelsPerUnit = 2; s.probeSamples = 64; s.maskSamples = 4; }
        else if (q == "medium") { s.samples = 64; s.bounces = 2; s.texelsPerUnit = 4; s.probeSamples = 256; s.maskSamples = 16; }
        else if (q == "high") { s.samples = 256; s.bounces = 3; s.texelsPerUnit = 8; s.probeSamples = 512; s.maskSamples = 32; }
        else { error = "bake settings: unknown quality '" + q + "' (preview, medium, high)"; return false; }
    }
    f("texelsPerUnit", s.texelsPerUnit, 0.05f, 128);
    i("pageSize", s.pageSize, 64, 8192);
    i("minRegion", s.minRegion, 1, 4096);
    i("maxRegion", s.maxRegion, 4, 8192);
    i("padding", s.padding, 1, 16);
    i("samples", s.samples, 1, 16384);
    i("bounces", s.bounces, 0, 8);
    f("aoDistance", s.aoDistance, 0, 1000);
    f("aoStrength", s.aoStrength, 0, 8);
    if (j.has("denoise")) s.denoise = j["denoise"].boolean(s.denoise);
    f("indirectIntensity", s.indirectIntensity, 0, 16);
    i("maskSamples", s.maskSamples, 1, 1024);
    f("probeSpacing", s.probeSpacing, 0, 1000);
    f("probeHeight", s.probeHeight, 0.1f, 1000);
    i("probeSamples", s.probeSamples, 16, 16384);
    i("maxProbes", s.maxProbes, 8, 1 << 20);
    i("threads", s.threads, 0, 256);
    if (j.has("seed")) s.seed = uint32_t(j["seed"].integer(s.seed));
    f("rgbmRange", s.rgbmRange, 1, 64);
    return true;
}

// ---------------------------------------------------------------- scene builder

SceneBuilder::Loaded* SceneBuilder::load(const std::string& path, std::string& error) {
    if (auto it = models_.find(path); it != models_.end()) return it->second.model ? &it->second : nullptr;
    Loaded& l = models_[path];
    try {
        std::vector<std::string> warn;
        l.model = render::loadModel(path, read_, &warn);
        for (auto& w : warn) if (w.find(".lmuv") != std::string::npos) warnings.push_back(w);
    } catch (const std::exception& e) {
        error = e.what();
        return nullptr;
    }
    render::Model& m = *l.model;
    std::string sidecar, err;
    const int n = unwrapLightmapUvs(m, tpu_, 2, sidecar, err);
    if (n < 0) warnings.push_back(err);
    if (n > 0) {
        ++unwrapped;
        if (!write_ || !write_(path + ".lmuv", sidecar)) warnings.push_back(path + ".lmuv: could not be written (the game will not find these lightmap UVs)");
    }
    scene_.sources.push_back(path);
    // one bake mesh per model mesh (object space)
    l.meshIndex.assign(m.meshes.size(), -1);
    for (size_t mi = 0; mi < m.meshes.size(); ++mi) {
        const auto& md = m.meshes[mi];
        bool skinned = false;
        for (const auto& p : md.primitives) skinned |= !p.jointPalette.empty();
        if (skinned || md.primitives.empty()) continue;
        BakeMesh bm;
        const bool lmuv = render::meshHasLightmapUv(m, int(mi));
        for (const auto& p : md.primitives) {
            const uint32_t base = uint32_t(bm.pos.size());
            uint32_t first = 0, count = 0;
            render::primitiveVertexRange(m, p, first, count);
            for (uint32_t v = 0; v < count; ++v) {
                const auto& mv = m.vertices[first + v];
                bm.pos.push_back(mv.pos); bm.nrm.push_back(mv.nrm); bm.uv.push_back(mv.uv);
                if (lmuv) bm.uv2.push_back(render::lightmapUv(mv));
            }
            for (uint32_t i = 0; i < p.indexCount; ++i) bm.idx.push_back(m.indices[p.firstIndex + i] - first + base);
            bm.triMaterial.insert(bm.triMaterial.end(), p.indexCount / 3, uint16_t(std::max(p.material, 0)));   // model material index (BakeInstance::materials maps it)
        }
        l.meshIndex[mi] = int(scene_.meshes.size());
        scene_.meshes.push_back(std::move(bm));
    }
    return &l;
}

int SceneBuilder::material(Loaded& l, const render::Material& mt, int) {
    BakeMaterial bm;
    bm.albedo = {mt.baseColor.x, mt.baseColor.y, mt.baseColor.z};
    bm.emissive = mt.emissive * mt.emissiveStrength;
    bm.doubleSided = mt.doubleSided;
    if (mt.shading == render::ShadingModel::Unlit) { bm.emissive = bm.albedo; bm.albedo = {0, 0, 0}; }   // unlit surfaces glow with their colour
    const auto& imgs = l.model->images;
    std::string texName;
    if (mt.baseTex >= 0 && size_t(mt.baseTex) < imgs.size() && !imgs[size_t(mt.baseTex)].rgba.empty()) {
        texName = l.model->path + "#" + std::to_string(mt.baseTex);
        auto& img = images_[texName];
        if (!img) img = std::make_shared<render::Image>(imgs[size_t(mt.baseTex)]);
        bm.texture = img;
        if (mt.alpha == render::AlphaMode::Mask) bm.alphaCutoff = mt.alphaCutoff;
    }
    // identical materials (by content) share one entry: deterministic, whatever object holds them
    uint64_t key = fnv1a(texName.data(), texName.size());
    key = fnv1a(&bm.albedo, sizeof bm.albedo, key); key = fnv1a(&bm.emissive, sizeof bm.emissive, key);
    key = fnv1a(&bm.alphaCutoff, sizeof bm.alphaCutoff, key); key = fnv1a(&bm.doubleSided, sizeof bm.doubleSided, key);
    if (auto it = materialIds_.find(key); it != materialIds_.end()) return it->second;
    const int id = int(scene_.materials.size());
    scene_.materials.push_back(std::move(bm));
    materialIds_[key] = id;
    return id;
}

bool SceneBuilder::addModel(const std::string& path, const std::vector<mat4>& nodeWorld, const std::vector<render::Material>* overrides,
                            bool receive, float scale, std::string& error, const std::vector<uint8_t>* nodeMask) {
    Loaded* l = load(path, error);
    if (!l) return false;
    const render::Model& m = *l->model;
    const auto& mats = overrides && overrides->size() >= m.materials.size() ? *overrides : m.materials;
    std::vector<uint16_t> map(m.materials.size());
    for (size_t k = 0; k < map.size(); ++k) map[k] = uint16_t(material(*l, mats[k], int(k)));
    for (size_t n = 0; n < m.nodes.size() && n < nodeWorld.size(); ++n) {
        const int mesh = m.nodes[n].mesh;
        if (mesh < 0 || size_t(mesh) >= l->meshIndex.size() || l->meshIndex[size_t(mesh)] < 0) continue;
        if (nodeMask && (n >= nodeMask->size() || !(*nodeMask)[n])) continue;
        BakeInstance in;
        in.mesh = l->meshIndex[size_t(mesh)];
        in.materials = map;
        in.world = nodeWorld[n];
        in.key = lightmapRegionKey(path, int(n), nodeWorld[n]);
        in.receive = receive && !scene_.meshes[size_t(in.mesh)].uv2.empty();
        in.scale = scale;
        in.name = path + ":" + m.nodes[n].name;
        scene_.instances.push_back(std::move(in));
    }
    return true;
}

void SceneBuilder::addTerrain(const std::function<float(float, float)>& heightAt, float x0, float z0, float ex, float ez, float step, float3 albedo) {
    step = std::max(step, 0.05f);
    const int nx = std::max(2, int(std::ceil(ex / step)) + 1), nz = std::max(2, int(std::ceil(ez / step)) + 1);
    BakeMesh bm;
    for (int z = 0; z < nz; ++z)
        for (int x = 0; x < nx; ++x) {
            const float u = float(x) / float(nx - 1), v = float(z) / float(nz - 1);
            const float wx = x0 + u * ex, wz = z0 + v * ez;
            bm.pos.push_back({wx, heightAt(wx, wz), wz});
            bm.uv.push_back({u, v}); bm.uv2.push_back({u, v});
        }
    bm.nrm.resize(bm.pos.size());
    for (int z = 0; z < nz; ++z)
        for (int x = 0; x < nx; ++x) {
            const auto& l = bm.pos[size_t(z * nx + std::max(x - 1, 0))], &r = bm.pos[size_t(z * nx + std::min(x + 1, nx - 1))];
            const auto& d = bm.pos[size_t(std::max(z - 1, 0) * nx + x)], &u = bm.pos[size_t(std::min(z + 1, nz - 1) * nx + x)];
            bm.nrm[size_t(z * nx + x)] = render::normalize(render::cross(u - d, r - l));
        }
    for (int z = 0; z + 1 < nz; ++z)
        for (int x = 0; x + 1 < nx; ++x) {
            const uint32_t a = uint32_t(z * nx + x), b = a + 1, c = a + uint32_t(nx), d = c + 1;
            bm.idx.insert(bm.idx.end(), {a, c, b, b, c, d});
        }
    BakeMaterial mt; mt.albedo = albedo;
    bm.triMaterial.assign(bm.idx.size() / 3, uint16_t(scene_.materials.size()));
    scene_.materials.push_back(mt);
    BakeInstance in;
    in.mesh = int(scene_.meshes.size());
    in.name = "terrain";
    scene_.meshes.push_back(std::move(bm));
    scene_.terrain.instance = int(scene_.instances.size());
    scene_.terrain.origin[0] = x0; scene_.terrain.origin[1] = z0;
    scene_.terrain.extent[0] = ex; scene_.terrain.extent[1] = ez;
    scene_.instances.push_back(std::move(in));
}

} // namespace aiverse::bake
