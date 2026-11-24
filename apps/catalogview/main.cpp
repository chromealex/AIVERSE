// aiverse_catalogview: the website's 3D preview of catalog packages (docs/packages.md § Web viewer).
// Shows the materials of a material library on a chosen shape (sphere, cube, cylinder) or on a tiled
// ground slab (plane), or glTF / GLB models with a LOD / STRATA switch, through the engine's own
// Renderer3D (the same PBR path games use), with an orbit camera. Textures are shown as published.
//
// Input is a view folder (built by AIVERSE-PACKAGES tools/catalog.py for every package that has a
// "viewer" block): <src>/view.json + the files it lists, laid out with the project-relative paths
// the package uses (materials.json texture paths resolve as they would inside a project).
//
//   native:  aiverse_catalogview --src DIR [--item N] [--shape sphere|cube|cylinder|plane]
//                                [--lod auto|strata|N] [--clusters] [--wire] [--yaw deg] [--pitch deg]
//   web:     catalogview/index.html?src=<url of the view folder>&item=N&shape=cube&lod=strata
//            the page fetches view.json and the files of the shown item into the in-memory file
//            system (/view), then calls the exported cv_* functions — no other network access.
//
// Exports for the page: cv_select(item), cv_shape(0..3), cv_lod(-1 auto | -2 STRATA | N),
// cv_clusters(0/1), cv_wire(0/1). After every change the viewer reports what it shows as one JSON line,
// to window.cvInfo(json) on the web and on stdout natively ("catalogview-info: {...}"):
//   material: {"kind":"material","id","shape","maps":[{"map","file","width","height","bytes"}]}
//   model:    {"kind":"model","id","mode":"auto|lod|strata","lod","levels":[{"level","triangles"}],
//              "generatedLevels","textures":[...],"strata":{"state":"off|ready|declined|failed",...}}
//
// view.json:
//   {"schema":1,"kind":"materials","library":"materials.json",
//    "items":[{"id":"terrain_grass","title":"Grass","title-ru":"Трава","files":["materials/terrain/textures/a.png",...],
//              "repeat":2.0}], "shared":["materials.json"]}
//   {"schema":1,"kind":"models","items":[{"id":"oak","title":"Oak","model":"models/oak.glb",
//              "files":["models/oak.glb","models/tex/x.png"]}]}
//   (item titles: "title" is English, translations "title-<lang>"; the page picks one, this app ignores them)
#include "../../engine/platform/Platform.h"
#include "../../engine/render/GeometryScene.h"
#include "../../engine/render/Model.h"
#include "../../engine/render/NativePresentationRetirement.h"
#include "../../engine/render/Renderer3D.h"
#include "../../engine/data/Json.h"
#include "../../engine/view/AppearanceAuthoringService.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

using namespace aiverse;
using namespace aiverse::render;

namespace {

constexpr float kPi = 3.14159265f;
enum Shape { kSphere = 0, kCube, kCylinder, kPlane, kShapeCount };
const char* const kShapeNames[kShapeCount] = {"sphere", "cube", "cylinder", "plane"};
const char* const kMaterialMaps[] = {"texture", "normalMap", "metallicRoughnessMap", "occlusionMap", "emissiveMap"};
constexpr int kLodAuto = -1, kLodStrata = -2;

bool readFile(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::stringstream s;
    s << f.rdbuf();
    out = s.str();
    return true;
}

// Mesh + the UV of every vertex it emitted, in emission order (modelFromParts keeps that order).
struct UvMesh {
    MeshBuilder mesh;
    std::vector<float2> uv;
    uint32_t vertex(float3 p, float3 n, float2 t) {
        uv.push_back(t);
        return mesh.addVertex(p, n, 0xffffffffu);
    }
    // counter-clockwise as seen from the side n points to
    void triangle(uint32_t a, uint32_t b, uint32_t c, float3 n) {
        const auto& v = mesh.vertices();
        if (dot(cross(v[b].pos - v[a].pos, v[c].pos - v[a].pos), n) >= 0) mesh.addTriangle(a, b, c);
        else mesh.addTriangle(a, c, b);
    }
};

// Texel density is the same on every shape: one texture repeat per `repeat` metres, rounded to whole
// repeats around closed surfaces so their seams meet.
int around(float length, float repeat) { return std::max(1, int(std::lround(length / repeat))); }

void addSphere(UvMesh& m, float3 c, float r, float repeat) {
    const int rings = 48, seg = 96;
    const float nu = float(around(2 * kPi * r, repeat)), nv = float(around(kPi * r, repeat));
    const uint32_t start = uint32_t(m.mesh.vertexCount());
    for (int y = 0; y <= rings; ++y) {
        const float phi = float(y) / rings * kPi;
        for (int x = 0; x <= seg; ++x) {  // the seam column is duplicated: u runs 0..nu without wrapping
            const float th = float(x) / seg * 2 * kPi;
            const float3 n{std::sin(phi) * std::sin(th), std::cos(phi), std::sin(phi) * std::cos(th)};
            m.vertex(c + n * r, n, {float(x) / seg * nu, float(y) / rings * nv});
        }
    }
    for (int y = 0; y < rings; ++y)
        for (int x = 0; x < seg; ++x) {
            const uint32_t a = start + uint32_t(y * (seg + 1) + x), b = a + uint32_t(seg + 1);
            const auto& v = m.mesh.vertices();
            if (y != 0) m.triangle(a, b, a + 1, normalize(v[a].nrm + v[b].nrm + v[a + 1].nrm));
            if (y != rings - 1) m.triangle(a + 1, b, b + 1, normalize(v[a + 1].nrm + v[b].nrm + v[b + 1].nrm));
        }
}

void addCube(UvMesh& m, float3 c, float s, float repeat) {
    const float h = s * 0.5f, k = s / repeat;
    const float3 normals[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    for (auto n : normals) {
        // u to the right and v downwards as seen from outside, so every face shows the image upright
        const float3 up = std::fabs(n.y) > 0.5f ? float3{0, 0, -n.y} : float3{0, 1, 0};
        const float3 right = cross(up, n);
        const float3 fc = c + n * h, du = right * h, dv = up * h;
        const uint32_t a = m.vertex(fc - du + dv, n, {0, 0}), b = m.vertex(fc + du + dv, n, {k, 0});
        const uint32_t cc = m.vertex(fc + du - dv, n, {k, k}), d = m.vertex(fc - du - dv, n, {0, k});
        m.triangle(a, b, cc, n);
        m.triangle(a, cc, d, n);
    }
}

void addCylinder(UvMesh& m, float3 base, float r, float height, float repeat) {
    const int seg = 96;
    const float nu = float(around(2 * kPi * r, repeat));
    const uint32_t side = uint32_t(m.mesh.vertexCount());
    for (int x = 0; x <= seg; ++x) {
        const float th = float(x) / seg * 2 * kPi;
        const float3 n{std::sin(th), 0, std::cos(th)};
        const float u = float(x) / seg * nu;
        m.vertex(base + n * r, n, {u, height / repeat});
        m.vertex(base + n * r + float3{0, height, 0}, n, {u, 0});
    }
    for (int x = 0; x < seg; ++x) {
        const uint32_t a = side + uint32_t(x * 2);
        const float3 n = normalize(m.mesh.vertices()[a].nrm + m.mesh.vertices()[a + 2].nrm);
        m.triangle(a, a + 2, a + 3, n);
        m.triangle(a, a + 3, a + 1, n);
    }
    for (int cap = 0; cap < 2; ++cap) {
        const float y = cap ? height : 0;
        const float3 n{0, cap ? 1.0f : -1.0f, 0};
        const uint32_t centre = m.vertex(base + float3{0, y, 0}, n, {0.5f * r / repeat, 0.5f * r / repeat});
        const uint32_t rim = uint32_t(m.mesh.vertexCount());
        for (int x = 0; x <= seg; ++x) {
            const float th = float(x) / seg * 2 * kPi;
            const float px = std::sin(th) * r, pz = std::cos(th) * r;
            m.vertex(base + float3{px, y, pz}, n, {(px + r * 0.5f) / repeat, (pz + r * 0.5f) / repeat});
        }
        for (int x = 0; x < seg; ++x) m.triangle(centre, rim + uint32_t(x), rim + uint32_t(x + 1), n);
    }
}

void addGround(UvMesh& m, float half, float repeat) {
    const int n = 12;
    for (int z = 0; z < n; ++z)
        for (int x = 0; x < n; ++x) {
            const float x0 = -half + 2 * half * x / n, x1 = -half + 2 * half * (x + 1) / n;
            const float z0 = -half + 2 * half * z / n, z1 = -half + 2 * half * (z + 1) / n;
            const float3 up{0, 1, 0};
            const uint32_t a = m.vertex({x0, 0, z1}, up, {x0 / repeat, z1 / repeat});
            const uint32_t b = m.vertex({x1, 0, z1}, up, {x1 / repeat, z1 / repeat});
            const uint32_t c = m.vertex({x1, 0, z0}, up, {x1 / repeat, z0 / repeat});
            const uint32_t d = m.vertex({x0, 0, z0}, up, {x0 / repeat, z0 / repeat});
            m.triangle(a, b, c, up);
            m.triangle(a, c, d, up);
        }
}

// The material preview: the shape alone, or the ground slab for kPlane, as a Model so materials
// with texture maps render exactly as on imported models (tangents generated for normal maps).
std::unique_ptr<Model> makeMaterialModel(float repeat, int shape) {
    repeat = std::max(repeat, 0.05f);
    UvMesh body, ground;
    switch (shape) {
    case kCube: addCube(body, {0, 0.8f, 0}, 1.6f, repeat); break;
    case kCylinder: addCylinder(body, {0, 0, 0}, 0.8f, 1.7f, repeat); break;
    case kPlane: break;
    default: addSphere(body, {0, 1.05f, 0}, 1.0f, repeat); break;
    }
    if (shape == kPlane) addGround(ground, 4.0f, repeat);   // a shape is shown on its own, without a floor
    Material base;
    base.vertexColors = false;
    std::vector<ModelPart> parts;
    std::vector<const UvMesh*> sources;
    if (!body.mesh.empty()) { parts.push_back({"shape", &body.mesh, base, mat4{}}); sources.push_back(&body); }
    if (!ground.mesh.empty()) { parts.push_back({"ground", &ground.mesh, base, mat4{}}); sources.push_back(&ground); }
    auto m = modelFromParts("catalogview/material", parts);
    if (!m) return m;
    for (size_t p = 0; p < sources.size(); ++p) {
        m->meshes[p].primitives[0].material = 0;  // one material on every part
        const auto& prim = m->meshes[p].primitives[0];
        for (uint32_t i = 0; i < prim.vertexCount; ++i) m->vertices[prim.firstVertex + i].uv = sources[p]->uv[i];
        generateTangents(m->vertices, m->indices, prim.firstIndex, prim.indexCount);
    }
    m->materials.resize(1);
    m->batching.autoLod = false;
    return m;
}

std::string jsonString(const std::string& s) { return Json::string(s).dump(); }

uint64_t trianglesAt(const Model& m, int level) {
    uint64_t t = 0;
    for (const auto& node : m.nodes) {
        if (node.mesh < 0 || size_t(node.mesh) >= m.meshes.size()) continue;
        const MeshDef* mesh = &m.meshes[size_t(node.mesh)];
        if (level > 0 && !mesh->lods.empty()) {
            const int k = std::min(level, int(mesh->lods.size())) - 1;
            const int lm = mesh->lods[size_t(k)].mesh;
            if (lm >= 0 && size_t(lm) < m.meshes.size()) mesh = &m.meshes[size_t(lm)];
        }
        for (const auto& p : mesh->primitives) t += p.indexCount / 3;
    }
    return t;
}

int lodLevels(const Model& m) {
    size_t n = 0;
    for (const auto& node : m.nodes)
        if (node.mesh >= 0 && size_t(node.mesh) < m.meshes.size()) n = std::max(n, m.meshes[size_t(node.mesh)].lods.size());
    return int(n) + 1;
}

// Triangle edges of every LOD level in model space, captured from the CPU copy before GPU upload.
struct Wire {
    std::vector<std::vector<float3>> levels;  // pairs of points per level
    static constexpr size_t kMaxEdges = 300000;
    void build(const Model& m) {
        levels.clear();
        const int n = lodLevels(m);
        for (int level = 0; level < n; ++level) {
            std::vector<float3> lines;
            std::vector<std::pair<uint32_t, uint32_t>> seen;
            for (size_t ni = 0; ni < m.nodes.size(); ++ni) {
                const Node& node = m.nodes[ni];
                if (node.mesh < 0 || size_t(node.mesh) >= m.meshes.size()) continue;
                const MeshDef* mesh = &m.meshes[size_t(node.mesh)];
                if (level > 0 && !mesh->lods.empty()) {
                    const int lm = mesh->lods[size_t(std::min(level, int(mesh->lods.size())) - 1)].mesh;
                    if (lm >= 0 && size_t(lm) < m.meshes.size()) mesh = &m.meshes[size_t(lm)];
                }
                const mat4 w = ni < m.bindWorld.size() ? m.bindWorld[ni] : mat4{};
                seen.clear();
                for (const auto& p : mesh->primitives)
                    for (uint32_t i = 0; i + 2 < p.indexCount; i += 3)
                        for (int e = 0; e < 3; ++e) {
                            const size_t ia = p.firstIndex + i + uint32_t(e), ib = p.firstIndex + i + uint32_t((e + 1) % 3);
                            if (ib >= m.indices.size()) continue;
                            uint32_t a = m.indices[ia], b = m.indices[ib];
                            if (a > b) std::swap(a, b);
                            seen.push_back({a, b});
                        }
                std::sort(seen.begin(), seen.end());
                seen.erase(std::unique(seen.begin(), seen.end()), seen.end());
                for (const auto& [a, b] : seen) {
                    if (lines.size() / 2 >= kMaxEdges || b >= m.vertices.size()) break;
                    lines.push_back(w.transformPoint(m.vertices[a].pos));
                    lines.push_back(w.transformPoint(m.vertices[b].pos));
                }
            }
            levels.push_back(std::move(lines));
        }
    }
};

struct App {
    std::string root;            // view folder
    Json view, library;
    gfx::Device* dev = nullptr;
    Renderer3D r3d;
    Camera cam;
    std::unique_ptr<Model> model;
    NativePresentationRetirement retired;  // replaced models, freed once the GPU is done with them
    GeometryScene strata;                  // STRATA route of the shown model (built on first request)
    int item = -1, requested = 0, shape = kSphere, lod = kLodAuto;
    bool clusters = false, wire = false;
    Wire wireData;
    int shownLevel = -1;
    uint64_t strataTrisTotal = 0, strataDrawnTris = 0;   // triangles of the cluster cut drawn last frame         // LOD level the renderer drew last frame (from its stats)
    enum class StrataState { Off, Ready, Declined, Failed } strataState = StrataState::Off;
    std::string strataError;
    std::string maps;            // JSON array of the textures of the shown item
    bool dragging = false, autoRotate = true;
    float lastX = 0, lastY = 0, idle = 0;

    std::string path(const std::string& rel) const { return root + "/" + rel; }
    bool materials() const { return view.getStr("kind") == "materials"; }

    void log(const std::string& s) {
        std::printf("catalogview: %s\n", s.c_str());
        std::fflush(stdout);
    }

    bool loadView() {
        std::string text;
        if (!readFile(path("view.json"), text)) { log("no view.json in " + root); return false; }
        try { view = Json::parse(text); } catch (const std::exception& e) { log(std::string("view.json: ") + e.what()); return false; }
        if (materials()) {
            if (!readFile(path(view.getStr("library", "materials.json")), text)) { log("material library missing"); return false; }
            try { library = Json::parse(text); } catch (const std::exception& e) { log(std::string("library: ") + e.what()); return false; }
        }
        return true;
    }

    void report() {
        if (item < 0) return;
        const Json& it = view["items"][size_t(item)];
        std::string j = "{\"id\":" + jsonString(it.getStr("id"));
        if (materials()) {
            j = "{\"kind\":\"material\"," + j.substr(1) + ",\"shape\":\"" + kShapeNames[shape] + "\",\"maps\":" + maps + "}";
        } else if (model) {
            j = "{\"kind\":\"model\"," + j.substr(1) + ",\"mode\":\"" +
                (lod == kLodStrata ? "strata" : lod == kLodAuto ? "auto" : "lod") + "\",\"lod\":" + std::to_string(lod) +
                ",\"clusters\":" + (clusters ? "true" : "false") +
                ",\"wire\":" + (wire ? "true" : "false") + ",\"shownLevel\":" + std::to_string(shownLevel) + ",\"levels\":[";
            const int n = lodLevels(*model);
            for (int l = 0; l < n; ++l)
                j += (l ? "," : "") + std::string("{\"level\":") + std::to_string(l) + ",\"triangles\":" + std::to_string(trianglesAt(*model, l)) + "}";
            j += "],\"generatedLevels\":" + std::to_string(model->autoLod.generatedLevels) + ",\"autoLodReason\":" + jsonString(model->autoLod.reason) + ",\"textures\":" + maps + ",\"strata\":{\"state\":\"";
            const auto& s = strata.stats();
            switch (strataState) {
            case StrataState::Off: j += "off\""; break;
            case StrataState::Ready:
                j += "ready\",\"clusters\":" + std::to_string(s.strataMeshlets) + ",\"nodes\":" + std::to_string(s.strataNodes) +
                     ",\"indexBytes\":" + std::to_string(s.strataIndexBytes) + ",\"stageMs\":" + std::to_string(int(s.strataStageMs)) +
                     ",\"drawnTriangles\":" + std::to_string(strataDrawnTris);
                break;
            case StrataState::Declined: j += "declined\",\"reason\":" + jsonString(strataError); break;
            case StrataState::Failed: j += "failed\",\"reason\":" + jsonString(strataError); break;
            }
            j += "}}";
        } else {
            return;
        }
        std::printf("catalogview-info: %s\n", j.c_str());
        std::fflush(stdout);
#ifdef __EMSCRIPTEN__
        EM_ASM({ if (window.cvInfo) window.cvInfo(UTF8ToString($0)); }, j.c_str());
#endif
    }

    std::string imageEntry(const std::string& map, const std::string& file, int w, int h, size_t bytes) {
        return "{\"map\":" + jsonString(map) + ",\"file\":" + jsonString(file) + ",\"width\":" + std::to_string(w) +
               ",\"height\":" + std::to_string(h) + ",\"bytes\":" + std::to_string(bytes) + "}";
    }

    void replaceModel(std::unique_ptr<Model> next) {
        if (model) {
            r3d.invalidateModelCachesFor(model.get());
            retired.retireModel(*dev, model);
        }
        model = std::move(next);
    }

    void dropStrata() {
        if (strataState != StrataState::Off) strata.clear(*dev);
        strataState = StrataState::Off;
        strataError.clear();
        strataTrisTotal = strataDrawnTris = 0;
    }

    void showMaterial(const Json& it) {
        auto next = makeMaterialModel(float(it["repeat"].toDouble(2.0)), shape);
        if (!next) { log("geometry failed"); return; }
        prepareModelUpload(*next);
        if (!uploadModel(*dev, *next)) { log("GPU upload failed"); return; }
        Model* m = next.get();
        Material mt = m->materials[0];
        const std::string id = it.getStr("id");
        const Json& entry = library.has("materials") ? library["materials"][id] : library[id];
        std::vector<std::string> list;
        auto loadTexture = [&](const std::string& rel) -> int {
            Image img;
            std::string bytes;
            if (!readFile(path(rel), bytes) || !decodeImage(bytes, img)) { log("texture missing: " + rel); return -1; }
            std::string map = "texture";
            for (const char* k : kMaterialMaps)
                if (entry.getStr(k) == rel) { map = k; break; }
            list.push_back(imageEntry(map, rel, img.width, img.height, bytes.size()));
            img.name = rel;
            img.repeat = true;
            return addImage(*dev, *m, std::move(img));
        };
        view::AppearanceAuthoringService::applyMaterial(mt, library, Json::string(id), loadTexture,
                                                        [&](const std::string& w) { log("material: " + w); });
        mt.vertexColors = false;
        mt.uvScale = {1, 1};
        m->materials[0] = mt;
        m->markMaterialChanged();
        maps = "[";
        for (size_t i = 0; i < list.size(); ++i) maps += (i ? "," : "") + list[i];
        maps += "]";
        replaceModel(std::move(next));
        if (shape == kPlane) r3d.setShadowBounds({-4.2f, -0.1f, -4.2f}, {4.2f, 0.3f, 4.2f});
        else r3d.setShadowBounds({-1.2f, -0.1f, -1.2f}, {1.2f, 2.2f, 1.2f});
        if (shape == kPlane) {
            cam.target = {0, 0, 0};
            cam.distance = 4.5f;
            cam.pitch = std::max(cam.pitch, 0.75f);
        } else {
            cam.target = {0, 0.8f, 0};
            cam.distance = 5.2f;
        }
        cam.minDistance = 1.2f;
        cam.maxDistance = 14.0f;
    }

    void showModel(const Json& it) {
        std::vector<std::string> warn;
        auto next = loadModel(path(it.getStr("model")), readFile, &warn);
        for (auto& w : warn) log(w);
        if (!next) { log("cannot load " + it.getStr("model")); return; }
        // the camera alone picks the LOD on screen; the shadow map (fixed to the model bounds) picks its own
        next->batching.independentShadowLod = true;
        prepareModelUpload(*next);
        wireData.build(*next);   // the CPU geometry is released by the upload
        shownLevel = -1;
        if (!uploadModel(*dev, *next)) { log("GPU upload failed"); return; }
        maps = "[";
        int k = 0;
        for (const auto& img : next->images)
            if (img.width > 0)
                maps += (k++ ? "," : "") + imageEntry("image", img.name, img.width, img.height, 0);
        maps += "]";
        const float3 c = (next->boundsMin + next->boundsMax) * 0.5f;
        const float r = std::max(0.25f, length(next->boundsMax - next->boundsMin) * 0.5f);
        cam.target = c;
        // the sun's shadow map covers the model itself, not a frustum fitted to the camera: the
        // shadow must not come and go as the camera turns
        r3d.setShadowBounds(next->boundsMin - float3{r, r, r} * 0.05f, next->boundsMax + float3{r, r, r} * 0.05f);
        cam.distance = r * 2.6f;
        cam.minDistance = r * 0.3f;
        cam.maxDistance = r * 80.0f;   // far enough for the coarsest LOD (switches near 30 px on screen)
        replaceModel(std::move(next));
    }

    // STRATA is built from the model file on first request (synchronous staging, see docs/strata.md).
    void ensureStrata() {
        if (materials() || item < 0 || strataState != StrataState::Off) return;
        const Json& it = view["items"][size_t(item)];
        const Json scene = Json::parse(R"({"version":1,"strata":{"enabled":true,"select":"all"},"instances":[{"id":"model","model":)" +
                                       jsonString(it.getStr("model")) + R"(,"strata":true}]})");
        try {
            strata.setStrataDebugGeometryCaptureEnabled(true);
            strata.load(*dev, scene, [this](const std::string& rel, std::string& out) { return readFile(path(rel), out); });
            const auto& s = strata.stats();
            if (s.strataPortableSources > 0 || s.strataSources > 0) {
                strataState = StrataState::Ready;
            } else {
                strataState = StrataState::Declined;
                strataError = s.strataSkippedSources ? "the model is not eligible for STRATA (animated, morphs or too small)"
                                                     : "STRATA is not built into this viewer";
            }
        } catch (const std::exception& e) {
            strataState = StrataState::Failed;
            strataError = e.what();
            log(std::string("strata: ") + e.what());
        }
        applyClusters();
    }

    void applyClusters() {
        if (strataState != StrataState::Ready) return;
        strata::DebugViewOptions o;
        o.enabled = clusters;      // one switch: coloured clusters with their edges
        o.clusterBounds = false;
        o.clusterWireframe = clusters;
        o.clusterFill = clusters;
        o.instanceBounds = false;
        o.fillAlpha = 0.55f;
        strata.setStrataDebugView(o);
    }

    void show(int index) {
        const Json& items = view["items"];
        if (!items.isArray() || items.size() == 0) { log("view has no items"); return; }
        index = std::clamp(index, 0, int(items.size()) - 1);
        const Json& it = items[size_t(index)];
        if (index != item) dropStrata();
        item = index;
        if (materials()) showMaterial(it);
        else showModel(it);
        if (lod == kLodStrata) ensureStrata();
        log("showing " + it.getStr("id"));
        report();
    }

    void setShape(int s) {
        shape = std::clamp(s, 0, kShapeCount - 1);
        if (materials() && item >= 0) show(item);
    }

    void setLod(int l) {
        lod = l < kLodStrata ? kLodAuto : l;
        if (lod == kLodStrata) ensureStrata();
        report();
    }

    void setClusters(bool on) {
        clusters = on;
        applyClusters();
        report();
    }

    void setWire(bool on) {
        wire = on;
        applyClusters();
        report();
    }

    void init(gfx::Device& d) {
        dev = &d;
        r3d.init(d);
        r3d.setShadowQuality(ShadowQuality::Medium);
        cam.yaw = 0.6f;
        cam.pitch = 0.42f;
        cam.fovY = 0.7f;
        cam.zNear = 0.05f;
        cam.zFar = 400.0f;
        if (loadView()) show(requested);
    }

    void event(const platform::Event& e) {
        using T = platform::EventType;
        if (e.type == T::MouseDown || e.type == T::TouchBegin) { dragging = true; lastX = e.x; lastY = e.y; autoRotate = false; }
        else if (e.type == T::MouseUp || e.type == T::TouchEnd || e.type == T::TouchCancel) { dragging = false; idle = 0; }
        else if ((e.type == T::MouseMove || e.type == T::TouchMove) && dragging) {
            cam.yaw -= (e.x - lastX) * 0.01f;
            // models and shapes are seen from below too; the ground slab only from above
            const float low = materials() && shape == kPlane ? 0.05f : -1.45f;
            cam.pitch = std::clamp(cam.pitch + (e.y - lastY) * 0.01f, low, 1.45f);
            lastX = e.x; lastY = e.y;
        } else if (e.type == T::MouseWheel) {
            cam.distance = std::clamp(cam.distance * std::pow(1.1f, -e.wheelY), cam.minDistance, cam.maxDistance);
        }
    }

    void frame(gfx::Device& d, double dt) {
        if (!dragging) {
            idle += float(dt);
            if (idle > 4) autoRotate = true;
        }
        if (autoRotate) cam.yaw += float(dt) * 0.25f;
        r3d.beginScene(cam, d.framebufferWidth(), d.framebufferHeight());
        if (lod == kLodStrata && strataState == StrataState::Ready) {
            strata.draw(r3d);
            // the cut STRATA chose for this view (it changes with the distance): report it when it changes
            const uint64_t total = strata.stats().strataPortableTriangles;
            const uint64_t drawn = total - std::min(total, strataTrisTotal);
            strataTrisTotal = total;
            if (drawn && drawn != strataDrawnTris) {
                strataDrawnTris = drawn;
                report();
            }
        } else if (model) {
            ModelDraw md;
            md.model = model.get();
            md.world = mat4{};
            md.lodLevel = lod >= 0 ? lod : -1;
            r3d.drawModel(md);
            if (wire && !wireData.levels.empty()) {
                const int level = std::clamp(lod >= 0 ? lod : std::max(shownLevel, 0), 0, int(wireData.levels.size()) - 1);
                const auto& lines = wireData.levels[size_t(level)];
                for (size_t i = 0; i + 1 < lines.size(); i += 2) r3d.line(lines[i], lines[i + 1], rgba(20, 18, 16, 200));
            }
        }
        gfx::PassDesc pass;
        pass.clearColor[0] = 0.953f; pass.clearColor[1] = 0.937f; pass.clearColor[2] = 0.910f;  // site paper #F3EFE8
        r3d.render(pass);
        if (model && !materials() && lod != kLodStrata) {
            // the level the renderer actually chose (the busiest one this frame)
            const auto& st = r3d.modelStats();
            int best = 0;
            for (int l = 1; l < 4; ++l)
                if (st.lodNodes[l] > st.lodNodes[best]) best = l;
            if (st.lodNodes[best] > 0 && best != shownLevel) {
                shownLevel = best;
                report();
            }
        }
    }

    void afterFrame(gfx::Device& d) {
        retired.collect(d);
        strata.collectRetired(d);
    }

    void shutdown() {
        if (!dev) return;
        if (model) {
            r3d.invalidateModelCachesFor(model.get());
            retired.retireModel(*dev, model);
        }
        strata.shutdown(*dev);
        retired.completeForShutdown(*dev);
        r3d.shutdown();
    }
};

App* g_app = nullptr;

}  // namespace

extern "C" {
#ifdef __EMSCRIPTEN__
#define CV_EXPORT EMSCRIPTEN_KEEPALIVE
#else
#define CV_EXPORT
#endif
// Web page: after fetching the files of item N into /view, show it.
CV_EXPORT void cv_select(int index) {
    if (!g_app) return;
    if (!g_app->dev) { g_app->requested = index; return; }
    g_app->show(index);
}
CV_EXPORT void cv_shape(int shape) {
    if (!g_app) return;
    if (!g_app->dev) { g_app->shape = std::clamp(shape, 0, kShapeCount - 1); return; }
    g_app->setShape(shape);
}
CV_EXPORT void cv_lod(int level) {
    if (!g_app) return;
    if (!g_app->dev) { g_app->lod = level; return; }
    g_app->setLod(level);
}
// Pinch zoom from the page (two-finger gestures never reach the orbit drag): factor > 1 moves closer.
CV_EXPORT void cv_zoom(float factor) {
    if (!g_app || !(factor > 0.f)) return;
    g_app->cam.distance = std::clamp(g_app->cam.distance / factor, g_app->cam.minDistance, g_app->cam.maxDistance);
    g_app->autoRotate = false;
    g_app->idle = 0;
}
CV_EXPORT void cv_wire(int on) {
    if (!g_app) return;
    if (!g_app->dev) { g_app->wire = on != 0; return; }
    g_app->setWire(on != 0);
}
CV_EXPORT void cv_clusters(int on) {
    if (!g_app) return;
    if (!g_app->dev) { g_app->clusters = on != 0; return; }
    g_app->setClusters(on != 0);
}
}

int aiverseMain(int argc, char** argv) {
    static App app;
    g_app = &app;
    app.root = "/view";
    float yawDeg = -1, pitchDeg = -1;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : std::string(); };
        if (a == "--src") app.root = next();
        else if (a == "--item") app.requested = std::atoi(next().c_str());
        else if (a == "--shape") {
            const std::string s = next();
            for (int k = 0; k < kShapeCount; ++k)
                if (s == kShapeNames[k]) app.shape = k;
        } else if (a == "--lod") {
            const std::string s = next();
            app.lod = s == "strata" ? kLodStrata : s == "auto" ? kLodAuto : std::max(0, std::atoi(s.c_str()));
        } else if (a == "--clusters") app.clusters = true;
        else if (a == "--wire") app.wire = true;
        else if (a == "--yaw") yawDeg = float(std::atof(next().c_str()));
        else if (a == "--pitch") pitchDeg = float(std::atof(next().c_str()));
    }
    platform::AppConfig cfg;
    cfg.title = "AIVERSE catalog preview";
    cfg.width = 960;
    cfg.height = 540;
    cfg.orientation = "any";
    platform::AppCallbacks cb;
    cb.init = [&, yawDeg, pitchDeg](gfx::Device& d) {
        app.init(d);
        if (yawDeg >= 0) { app.cam.yaw = yawDeg * kPi / 180; app.autoRotate = false; }
        if (pitchDeg >= 0) app.cam.pitch = pitchDeg * kPi / 180;
    };
    cb.frame = [&](gfx::Device& d, double dt) { app.frame(d, dt); };
    cb.afterFrame = [&](gfx::Device& d) { app.afterFrame(d); };
    cb.event = [&](const platform::Event& e) { app.event(e); };
    cb.shutdown = [&] { app.shutdown(); };
    return platform::run(cfg, cb);
}
