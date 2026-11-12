// Light baker tests (agent-lightbake): small scenes with known answers, determinism, file round trip,
// lightmap UV unwrap and sidecars. Run: aiverse_bake_tests
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include "engine/bake/BakeScene.h"
#include "engine/bake/Half.h"
#include "engine/bake/LightmapUnwrap.h"
#include "engine/bake/SceneBuilder.h"
#include "engine/render/LightmapUv.h"
#include "engine/render/Renderer3D.h"

using namespace aiverse;
using namespace aiverse::bake;
using render::float3;
using render::float2;
using render::mat4;

static int g_checks = 0, g_failed = 0;
#define CHECK(c) do { ++g_checks; if (!(c)) { ++g_failed; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
static bool near(float a, float b, float tol) { return std::fabs(a - b) <= tol; }

// quad in the xz plane facing +y (or -y when down), uv2 = unit square
static BakeMesh quad(float size, float y = 0, bool down = false) {
    BakeMesh m;
    const float h = size * 0.5f;
    m.pos = {{-h, y, -h}, {h, y, -h}, {h, y, h}, {-h, y, h}};
    const float ny = down ? -1.0f : 1.0f;
    m.nrm.assign(4, {0, ny, 0});
    m.uv = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    m.uv2 = m.uv;
    m.idx = down ? std::vector<uint32_t>{0, 1, 2, 0, 2, 3} : std::vector<uint32_t>{0, 2, 1, 0, 3, 2};
    m.triMaterial = {0, 0};
    return m;
}
static BakeMesh box(float3 c, float3 e) {   // closed box, outward normals, no uv2 (caster only)
    BakeMesh m;
    const float3 n[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    for (int f = 0; f < 6; ++f) {
        const float3 N = n[f];
        const float3 U = std::fabs(N.y) > 0.5f ? float3{1, 0, 0} : float3{0, 1, 0};
        const float3 V = render::cross(N, U);
        const uint32_t b = uint32_t(m.pos.size());
        for (int k = 0; k < 4; ++k) {
            const float su = (k == 1 || k == 2) ? 1.0f : -1.0f, sv = k >= 2 ? 1.0f : -1.0f;
            const float3 p = N + U * su + V * sv;
            m.pos.push_back({c.x + p.x * e.x, c.y + p.y * e.y, c.z + p.z * e.z});
            m.nrm.push_back(N);
        }
        m.idx.insert(m.idx.end(), {b, b + 1, b + 2, b, b + 2, b + 3});
        m.triMaterial.insert(m.triMaterial.end(), {0, 0});
    }
    return m;
}
static float3 texel(const LightmapData& d, const LightmapRegion& r, float u, float v) {
    const auto& p = d.pages[r.page];
    const int x = std::min(p.width - 1, int((u * r.st[0] + r.st[2]) * float(p.width)));
    const int y = std::min(p.height - 1, int((v * r.st[1] + r.st[3]) * float(p.height)));
    return decodeRgbm(&p.rgbm[(size_t(y) * size_t(p.width) + size_t(x)) * 4], d.rgbmRange);
}
static float maskAt(const LightmapData& d, const LightmapRegion& r, float u, float v) {
    const auto& p = d.pages[r.page];
    const int x = std::min(p.width - 1, int((u * r.st[0] + r.st[2]) * float(p.width)));
    const int y = std::min(p.height - 1, int((v * r.st[1] + r.st[3]) * float(p.height)));
    return float(p.mask[(size_t(y) * size_t(p.width) + size_t(x)) * 4]) / 255.0f;
}

static BakeScene openPlane() {
    BakeScene s;
    s.materials.push_back({});
    s.meshes.push_back(quad(8));
    BakeInstance in; in.mesh = 0; in.key = 1; in.name = "plane";
    s.instances.push_back(in);
    s.sun.on = false;
    s.sky.sky = {0.4f, 0.5f, 0.6f}; s.sky.ground = {0.1f, 0.08f, 0.05f};
    return s;
}
static BakeSettings fast() {
    BakeSettings st;
    st.texelsPerUnit = 2; st.samples = 128; st.bounces = 1; st.probeSpacing = 0; st.denoise = false; st.threads = 2;
    return st;
}

static void testOpenSky() {
    BakeScene s = openPlane();
    LightmapData d; std::string err;
    CHECK(bakeLighting(s, fast(), d, err));
    CHECK(d.regions.count(1) == 1);
    const auto& r = d.regions.at(1);
    // open plane facing up under a linear sky: irradiance = the realtime ambient at n.y = 1 = sky
    const float3 c = texel(d, r, 0.5f, 0.5f);
    std::printf("open sky: %.4f %.4f %.4f (expect 0.4 0.5 0.6)\n", c.x, c.y, c.z);
    CHECK(near(c.x, 0.4f, 0.02f) && near(c.y, 0.5f, 0.02f) && near(c.z, 0.6f, 0.025f));
}

static void testSunSubtractiveAndShadow() {
    BakeScene s = openPlane();
    s.sun.on = true; s.sun.dir = {0, 1, 0}; s.sun.color = {1, 1, 1}; s.sun.angle = 0.5f;
    s.sky.sky = {0, 0, 0}; s.sky.ground = {0, 0, 0};
    s.materials[0].albedo = {0, 0, 0};   // no bounce: exact direct only
    // a floating box (caster only) over the plane's centre
    s.meshes.push_back(box({0, 2, 0}, {1, 0.2f, 1}));
    BakeInstance b; b.mesh = 1; b.receive = false; b.name = "box";
    s.instances.push_back(b);
    BakeSettings st = fast();
    st.mode = BakedMode::Subtractive;
    LightmapData d; std::string err;
    CHECK(bakeLighting(s, st, d, err));
    const auto& r = d.regions.at(1);
    const float3 lit = texel(d, r, 0.95f, 0.95f), shadow = texel(d, r, 0.5f, 0.5f);
    std::printf("subtractive: lit %.3f, shadow %.3f\n", lit.x, shadow.x);
    CHECK(near(lit.x, 1.0f, 0.03f));
    CHECK(shadow.x < 0.03f);
    CHECK(d.pages[0].mask.empty());   // no mask outside shadowmask mode
    st.mode = BakedMode::Shadowmask;
    CHECK(bakeLighting(s, st, d, err));
    const auto& r2 = d.regions.at(1);
    CHECK(near(maskAt(d, r2, 0.95f, 0.95f), 1.0f, 0.01f));
    CHECK(maskAt(d, r2, 0.5f, 0.5f) < 0.02f);
    CHECK(texel(d, r2, 0.95f, 0.95f).x < 0.02f);   // shadowmask: the sun stays realtime, not in the lightmap
}

static void testBounceAndEmissive() {
    BakeScene s = openPlane();
    s.sky.sky = {0, 0, 0}; s.sky.ground = {0, 0, 0};
    BakeMaterial glow; glow.albedo = {0, 0, 0}; glow.emissive = {4, 2, 1};
    s.materials.push_back(glow);
    BakeMesh panel = quad(2, 1.0f, true);
    panel.triMaterial = {1, 1};
    s.meshes.push_back(panel);
    BakeInstance p; p.mesh = 1; p.receive = false; p.name = "panel";
    s.instances.push_back(p);
    LightmapData d; std::string err;
    CHECK(bakeLighting(s, fast(), d, err));
    const auto& r = d.regions.at(1);
    const float3 under = texel(d, r, 0.5f, 0.5f), far = texel(d, r, 0.02f, 0.02f);
    std::printf("emissive: under %.3f %.3f %.3f, corner %.3f\n", under.x, under.y, under.z, far.x);
    // a 2x2 panel 1 unit above: view factor ~0.55 -> about 0.55 * (4, 2, 1)
    CHECK(under.x > 1.5f && under.x < 3.0f && under.y > under.z);
    CHECK(far.x < under.x * 0.25f);
}

static void testProbes() {
    BakeScene s = openPlane();
    s.instances[0].world = mat4::translate({0, -1000, 0});   // ground far below: probes see the open sky
    s.meshes[0] = quad(0.01f);
    BakeSettings st = fast();
    st.probeSpacing = 1; st.probeHeight = 1; st.probeSamples = 2048;
    LightmapData d; std::string err;
    CHECK(bakeLighting(s, st, d, err));
    CHECK(!d.probes.empty());
    const float3 up = d.probes.irradiance(d.probes.origin, {0, 1, 0}), dn = d.probes.irradiance(d.probes.origin, {0, -1, 0});
    std::printf("probes: up %.3f %.3f %.3f down %.3f %.3f %.3f\n", up.x, up.y, up.z, dn.x, dn.y, dn.z);
    CHECK(near(up.x, 0.4f, 0.03f) && near(up.z, 0.6f, 0.03f));
    CHECK(near(dn.x, 0.1f, 0.03f) && near(dn.z, 0.05f, 0.03f));
}

static void testDeterminismHashAndFiles() {
    BakeScene s = openPlane();
    s.sun.on = true;
    s.meshes.push_back(box({1, 1, 1}, {0.5f, 1, 0.5f}));
    BakeInstance b; b.mesh = 1; b.receive = false; s.instances.push_back(b);
    BakeSettings st = fast();
    st.denoise = true; st.probeSpacing = 2; st.samples = 32; st.bounces = 2;
    LightmapData a, c; std::string err;
    st.threads = 1; CHECK(bakeLighting(s, st, a, err));
    st.threads = 4; CHECK(bakeLighting(s, st, c, err));
    CHECK(a.pages.size() == c.pages.size() && a.pages[0].rgbm == c.pages[0].rgbm && a.pages[0].mask == c.pages[0].mask);
    CHECK(a.probes.coeffs == c.probes.coeffs);
    CHECK(a.hash == c.hash && a.hash == hashBakeInput(s, st));
    BakeScene s2 = s; s2.sun.dir = {0.2f, 1, 0};
    CHECK(hashBakeInput(s2, st) != a.hash);
    BakeSettings st2 = st; st2.samples = 33;
    CHECK(hashBakeInput(s, st2) != a.hash);
    // folder round trip in memory
    std::map<std::string, std::string> files;
    CHECK(saveLightmapData("maps/test.bake", a, [&](const std::string& p, const std::string& b2) { files[p] = b2; return true; }, err));
    CHECK(files.count("maps/test.bake/lightmap.json") && files.count("maps/test.bake/lm0.png") && files.count("maps/test.bake/probes.bin"));
    LightmapData back;
    const bool ok = loadLightmapData("maps/test.bake", [&](const std::string& p, std::string& o) {
        auto it = files.find(p); if (it == files.end()) return false; o = it->second; return true; }, back, err);
    if (!ok) std::printf("load: %s\n", err.c_str());
    CHECK(ok);
    CHECK(back.hash == a.hash && back.mode == a.mode && back.pages.size() == a.pages.size());
    CHECK(back.pages[0].rgbm == a.pages[0].rgbm && back.pages[0].mask == a.pages[0].mask);
    CHECK(back.regions.size() == a.regions.size() && std::fabs(back.regions.at(1).st[2] - a.regions.at(1).st[2]) < 1e-6f);
    CHECK(back.probes.nx == a.probes.nx && back.probes.coeffs.size() == a.probes.coeffs.size());
    float maxErr = 0;
    for (size_t i = 0; i < a.probes.coeffs.size(); ++i) maxErr = std::max(maxErr, std::fabs(back.probes.coeffs[i] - a.probes.coeffs[i]) / std::max(1.0f, std::fabs(a.probes.coeffs[i])));
    CHECK(maxErr < 1e-3f);
    LightmapData hdr;
    CHECK(readLightmapHeader("maps/test.bake", [&](const std::string& p, std::string& o) { auto it = files.find(p); if (it == files.end()) return false; o = it->second; return true; }, hdr, err) && hdr.hash == a.hash);
    CHECK(bakeFolderFor("maps/level1.json") == "maps/level1.bake");
}

static void testUnwrapAndSidecar() {
    render::MeshBuilder mb;
    mb.box({0, 0.5f, 0}, {1, 1, 1});
    mb.box({2, 0.25f, 0}, {0.5f, 0.5f, 2});
    render::Material mt;
    auto m = render::modelFromParts("crate", {{"body", &mb, mt, mat4{}}});
    m->path = "models/crate.glb";
    CHECK(!render::meshHasLightmapUv(*m, 0));
    const uint64_t srcHash = render::lightmapSourceHash(*m);
    const auto origTris = m->indices.size();
    std::string sidecar, err;
    const int n = unwrapLightmapUvs(*m, 8, 2, sidecar, err);
    if (n != 1) std::printf("unwrap n=%d err=%s meshes=%zu prims=%zu\n", n, err.c_str(), m->meshes.size(), m->meshes.empty() ? 0 : m->meshes[0].primitives.size());
    CHECK(n == 1 && !sidecar.empty());
    CHECK(render::meshHasLightmapUv(*m, 0));
    CHECK(m->indices.size() == origTris);
    // charts do not overlap: rasterize every triangle's interior at 256^2 and count double coverage
    std::vector<uint8_t> cov(256 * 256, 0);
    int overlaps = 0, covered = 0;
    for (size_t t = 0; t + 2 < m->indices.size(); t += 3) {
        float2 q[3];
        for (int k = 0; k < 3; ++k) { const float2 uv = render::lightmapUv(m->vertices[m->indices[t + size_t(k)]]); q[k] = {uv.x * 256, uv.y * 256}; CHECK(uv.x >= 0 && uv.x <= 1 && uv.y >= 0 && uv.y <= 1); }
        const float area = (q[1].x - q[0].x) * (q[2].y - q[0].y) - (q[2].x - q[0].x) * (q[1].y - q[0].y);
        if (std::fabs(area) < 1e-6f) continue;
        for (int y = 0; y < 256; ++y) for (int x = 0; x < 256; ++x) {
            const float px = float(x) + 0.5f, py = float(y) + 0.5f;
            const float b1 = ((px - q[0].x) * (q[2].y - q[0].y) - (q[2].x - q[0].x) * (py - q[0].y)) / area;
            const float b2 = ((q[1].x - q[0].x) * (py - q[0].y) - (px - q[0].x) * (q[1].y - q[0].y)) / area;
            if (b1 < 0.02f || b2 < 0.02f || 1 - b1 - b2 < 0.02f) continue;
            if (cov[size_t(y) * 256 + size_t(x)]++) ++overlaps; else ++covered;
        }
    }
    std::printf("unwrap: %d texels covered, %d overlaps\n", covered, overlaps);
    CHECK(covered > 256 * 256 / 5 && overlaps == 0);
    // the sidecar applies to a fresh copy of the same geometry, and refuses other geometry
    auto fresh = render::modelFromParts("crate", {{"body", &mb, mt, mat4{}}});
    CHECK(render::lightmapSourceHash(*fresh) == srcHash);
    CHECK(render::applyLightmapUvs(*fresh, sidecar, &err));
    CHECK(fresh->vertices.size() == m->vertices.size());
    bool same = true;
    for (size_t i = 0; i < fresh->vertices.size() && same; ++i) same = std::memcmp(&fresh->vertices[i], &m->vertices[i], sizeof(render::ModelVertex)) == 0;
    CHECK(same);
    render::MeshBuilder other; other.box({0, 0, 0}, {2, 2, 2});
    auto wrong = render::modelFromParts("other", {{"body", &other, mt, mat4{}}});
    CHECK(!render::applyLightmapUvs(*wrong, sidecar, &err));
    // scene builder: model from a reader, sidecar written, instance keyed like the renderer
    std::map<std::string, std::string> written;
    BakeScene scene;
    // (modelFromParts models have no file: feed the builder a model through an in-memory OBJ)
    const std::string obj = "v -1 0 -1\nv 1 0 -1\nv 1 0 1\nv -1 0 1\nv -1 2 -1\nv 1 2 -1\nv 1 2 1\nv -1 2 1\n"
                            "f 1 2 3 4\nf 8 7 6 5\nf 1 5 6 2\nf 2 6 7 3\nf 3 7 8 4\nf 4 8 5 1\n";
    SceneBuilder sb(scene, [&](const std::string& p, std::string& o) { if (p == "models/box.obj") { o = obj; return true; } auto it = written.find(p); if (it == written.end()) return false; o = it->second; return true; },
                    [&](const std::string& p, const std::string& b) { written[p] = b; return true; }, 4);
    std::vector<mat4> nodes(1, mat4::translate({3, 0, 0}));
    auto probe = render::loadModel("models/box.obj", [&](const std::string& p, std::string& o) { if (p == "models/box.obj") { o = obj; return true; } return false; });
    nodes.assign(probe->nodes.size(), mat4::translate({3, 0, 0}));
    CHECK(sb.addModel("models/box.obj", nodes, nullptr, true, 1, err));
    CHECK(sb.unwrapped == 1 && written.count("models/box.obj.lmuv") == 1);
    CHECK(!scene.instances.empty() && scene.instances[0].receive);
    int meshNode = -1;
    for (size_t i = 0; i < probe->nodes.size(); ++i) if (probe->nodes[i].mesh >= 0) { meshNode = int(i); break; }
    CHECK(meshNode >= 0 && scene.instances[0].key == lightmapRegionKey("models/box.obj", meshNode, nodes[size_t(meshNode)]));
    // loading again picks the sidecar up (same geometry the baker used)
    auto again = render::loadModel("models/box.obj", [&](const std::string& p, std::string& o) { if (p == "models/box.obj") { o = obj; return true; } auto it = written.find(p); if (it == written.end()) return false; o = it->second; return true; });
    CHECK(render::meshHasLightmapUv(*again, 0));
}

static void testSmallHelpers() {
    for (float f : {0.0f, 1.0f, -2.5f, 0.3333f, 1234.5f, 1e-5f}) CHECK(std::fabs(halfToFloat(floatToHalf(f)) - f) <= std::fabs(f) * 1e-3f + 1e-7f);
    uint8_t px[4];
    encodeRgbm({1.5f, 0.25f, 3.0f}, 8, px);
    const float3 c = decodeRgbm(px, 8);
    CHECK(near(c.x, 1.5f, 0.05f) && near(c.y, 0.25f, 0.03f) && near(c.z, 3.0f, 0.05f));
    mat4 w = mat4::translate({1.0f, 2.0f, 3.0f});
    mat4 w2 = w; w2.m[12] += 1e-5f;   // the same placement recomputed with float noise
    CHECK(lightmapRegionKey("a.glb", 1, w) == lightmapRegionKey("a.glb", 1, w2));
    CHECK(lightmapRegionKey("a.glb", 1, w) != lightmapRegionKey("a.glb", 2, w));
    CHECK(lightmapRegionKey("a.glb", 1, w) != lightmapRegionKey("a.glb", 1, mat4::translate({1.5f, 2, 3})));
    BakeSettings s; std::string err;
    CHECK(settingsFromJson("{\"quality\": \"preview\", \"mode\": \"subtractive\", \"samples\": 7}", s, err) && s.samples == 7 && s.bounces == 1 && s.mode == BakedMode::Subtractive);
    CHECK(!settingsFromJson("{\"mode\": \"nope\"}", s, err) && err.find("nope") != std::string::npos);
    BakeSettings t; CHECK(settingsFromJson(settingsToJson(s), t, err) && settingsToJson(t) == settingsToJson(s));
}

int main() {
    testSmallHelpers();
    testOpenSky();
    testSunSubtractiveAndShadow();
    testBounceAndEmissive();
    testProbes();
    testDeterminismHashAndFiles();
    testUnwrapAndSidecar();
    std::printf("bake tests: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
