// Hierarchical wind masks on render::Model (CPU side, before GPU upload).
//
//   * WindMaskAtlas         one shared atlas per mask library (CPU bytes shared
//                           by every model; optional single GPU upload that all
//                           models borrow).
//   * bindWindRig           writes the per-vertex payload, attaches the rig
//                           table + atlas images and the shared windRigGraph().
//   * rigFromModelNodes     builds the hierarchy from the model's own node tree
//                           (glTF: trunk node -> branch nodes -> leaf nodes),
//                           flattening node transforms into model space.
//   * bindNearest           binds vertices of a single mesh to the nearest
//                           segment of an explicit skeleton.
//   * applyWindSidecar      "<model>.wind.json" + project "wind/masks.json" and
//                           "wind/profiles.json"; called from loadModel.
#pragma once
#include "engine/render/Model.h"
#include "engine/render/WindRig.h"
#include "engine/render/WindRigShader.h"
#include "engine/data/Json.h"
#include <algorithm>
#include <cctype>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

namespace aiverse::render::wind {

class WindMaskAtlas {
public:
    static std::shared_ptr<WindMaskAtlas> fromLibrary(const WindMaskLibrary& lib) {
        auto a = std::make_shared<WindMaskAtlas>();
        auto bytes = std::make_shared<std::vector<uint8_t>>();
        lib.packAtlas(*bytes, a->width_, a->height_);
        a->key_ = windTextureKey(*bytes);
        a->masks_ = lib.size();
        a->rgba_ = std::move(bytes);
        return a;
    }
    const std::shared_ptr<const std::vector<uint8_t>>& rgba() const { return rgba_; }
    int width() const { return width_; }
    int height() const { return height_; }
    uint64_t key() const { return key_; }
    size_t masks() const { return masks_; }
    // Optional: upload once; models created AFTER this borrow the texture.
    bool upload(gfx::Device& dev) {
        if (tex_.valid()) return true;
        gfx::TextureDesc d;
        d.width = width_; d.height = height_;
        d.format = gfx::PixelFormat::RGBA8;
        d.filter = gfx::Filter::Nearest; d.repeat = false; d.mipmaps = false;
        d.data = rgba_->data(); d.label = "wind mask atlas";
        tex_ = dev.createTexture(d);
        return tex_.valid();
    }
    // Owner releases after every borrowing model has been retired.
    void release(gfx::Device& dev) { if (tex_.valid()) dev.destroyTexture(tex_); tex_ = {}; }
    gfx::TextureHandle texture() const { return tex_; }
private:
    std::shared_ptr<const std::vector<uint8_t>> rgba_;
    int width_ = 0, height_ = 0;
    uint64_t key_ = 0;
    size_t masks_ = 0;
    gfx::TextureHandle tex_;
};

// Per-vertex binding, parallel to Model::vertices. node = kNoNode leaves the
// vertex rigid (the vertex still gets a payload so it stays still).
struct WindBinding {
    std::vector<uint16_t> node;
    std::vector<float> flutter;
};

struct WindBindReport {
    size_t vertices = 0, nodes = 0, materials = 0;
    size_t vertexColorMaterials = 0;   // materials that had used vertex colours
    uint64_t rigKey = 0;
};

namespace model_detail {
// Uploads may release CPU pixels; tools still need the bytes. Small,
// content-keyed and shared: 16 B per part, one entry per unique rig/atlas.
inline std::shared_ptr<const std::vector<uint8_t>> windBytes(uint64_t key,
        std::shared_ptr<const std::vector<uint8_t>> bytes = {}) {
    static std::mutex lock;
    static std::map<uint64_t, std::shared_ptr<const std::vector<uint8_t>>> table;
    std::lock_guard<std::mutex> guard(lock);
    if (bytes) {
        if (table.size() >= 8192 && !table.count(key)) table.erase(table.begin());
        table[key] = bytes;
        return bytes;
    }
    auto it = table.find(key);
    return it == table.end() ? nullptr : it->second;
}
inline uint64_t keyFromImageName(const std::string& name) {
    const auto at = name.find('#');
    if (at == std::string::npos) return 0;
    try { return std::stoull(name.substr(at + 1)); } catch (...) { return 0; }
}
inline Image dataImage(const std::string& name, std::shared_ptr<const std::vector<uint8_t>> rgba,
                       int w, int h) {
    Image img;
    img.name = name;
    img.width = w; img.height = h;
    img.sharedRgba = std::move(rgba);
    img.nearest = true; img.repeat = false;
    img.preserveAlphaCoverage = false;
    return img;
}
inline float legacyWeight(const WindRig& rig, const WindMaskLibrary* lib, uint16_t node, V3 p) {
    float sum = 0; V3 q = p;
    for (int l = 0; l < kMaxLevels && node != kNoNode && node < rig.nodes.size(); ++l) {
        const auto& n = rig.nodes[node];
        const float t = std::clamp(dot(q - n.pivot, n.axis) / n.length, 0.f, 1.f);
        const float m = lib ? sampleMask(lib->mask(n.mask), t) : t;
        sum += m * n.flex * 0.5f;
        q = n.pivot; node = n.parent;
    }
    return std::clamp(sum, 0.f, 1.f);
}
} // namespace model_detail

// `materials`: indices to rig; empty = every material used by bound vertices.
inline WindBindReport bindWindRig(Model& model, const WindRig& rig, const WindMaskAtlas& atlas,
                                  const WindBinding& binding, std::vector<int> materials = {},
                                  const WindMaskLibrary* libraryForLegacyWeights = nullptr) {
    if (model.vb.valid()) throw std::logic_error("wind rig must be bound before GPU upload");
    if (model.skinned()) throw std::invalid_argument("wind rig: skinned models are not supported");
    if (binding.node.size() != model.vertices.size() ||
        (!binding.flutter.empty() && binding.flutter.size() != model.vertices.size()))
        throw std::invalid_argument("wind rig binding size != model vertices");
    for (const auto& n : rig.nodes)
        if (n.mask >= atlas.masks()) throw std::invalid_argument("wind rig uses a mask missing from the atlas");
    WindBindReport rep;
    // Rig table image (content-keyed so identical prototypes can share it).
    auto bytes = std::make_shared<std::vector<uint8_t>>();
    int w = 0, h = 0;
    packWindRig(rig, *bytes, w, h);
    rep.rigKey = windTextureKey(*bytes);
    model_detail::windBytes(rep.rigKey, bytes);
    model_detail::windBytes(atlas.key(), atlas.rgba());
    const int rigImage = int(model.images.size());
    model.images.push_back(model_detail::dataImage("wind-rig#" + std::to_string(rep.rigKey), bytes, w, h));
    const int atlasImage = int(model.images.size());
    auto atlasImg = model_detail::dataImage("wind-masks#" + std::to_string(atlas.key()), atlas.rgba(),
                                            atlas.width(), atlas.height());
    if (atlas.texture().valid()) { atlasImg.tex = atlas.texture(); atlasImg.borrowedTexture = true; }
    model.images.push_back(std::move(atlasImg));
    // Per-vertex payload.
    for (size_t i = 0; i < model.vertices.size(); ++i) {
        auto& v = model.vertices[i];
        const uint16_t node = binding.node[i] < rig.nodes.size() ? binding.node[i] : kNoNode;
        const float flutter = binding.flutter.empty() ? 0.f : binding.flutter[i];
        const float legacy = model_detail::legacyWeight(rig, libraryForLegacyWeights, node,
                                                        {v.pos.x, v.pos.y, v.pos.z});
        v.color = encodeWindVertex(node, flutter, legacy);
        if (node != kNoNode) ++rep.vertices;
    }
    if (materials.empty()) {
        std::vector<bool> used(model.materials.size(), false);
        for (const auto& mesh : model.meshes)
            for (const auto& p : mesh.primitives)
                if (p.material >= 0 && size_t(p.material) < used.size()) used[size_t(p.material)] = true;
        for (size_t i = 0; i < used.size(); ++i) if (used[i]) materials.push_back(int(i));
    }
    for (int index : materials) {
        if (index < 0 || size_t(index) >= model.materials.size())
            throw std::out_of_range("wind rig material index");
        auto& m = model.materials[size_t(index)];
        // Only a pure vertex-motion graph (unchanged fragment colour, e.g. the
        // legacy crown wind) may be superseded; real material graphs are kept.
        const bool pureVertexMotion = m.shaderGraph && [&] {
            const auto it = m.shaderGraph->outputs.find("color");
            return it != m.shaderGraph->outputs.end() && it->second == "sg_input" &&
                   m.shaderGraph->outputs.size() <= 3 && m.shaderGraph->textures.size() <= 2;
        }();
        if (m.shaderGraph && m.shaderGraph != windRigGraph() && !pureVertexMotion)
            throw std::invalid_argument("wind rig: material '" + m.name +
                                        "' already has a custom shader graph");
        if (m.vertexColors) ++rep.vertexColorMaterials;
        m.shaderGraph = windRigGraph();
        m.graphTextures[0] = rigImage;
        m.graphTextures[1] = atlasImage;
        m.vertexColors = false;
        m.vertexOcclusion = false;
        m.backOcclusion = false;
        ++rep.materials;
    }
    model.batching.mergeRigid = false;   // one rig per model; never merge foreign rigs
    model.markMaterialChanged();
    rep.nodes = rig.nodes.size();
    return rep;
}

// ------------------------------------------------------- authoring rules

struct WindRule {
    std::string node;                 // exact or prefix* (case-insensitive) node name
    std::string mask;
    std::optional<float> stiffness, flex, flutter;
};

struct WindModelSpec {
    WindProfile profile;
    std::vector<WindRule> rules;
    std::vector<std::string> flutterMaterials;   // e.g. leaves: flutter 1
    std::vector<std::string> materials;          // rig only these (empty = all used)
    std::vector<WindNodeDesc> skeleton;          // explicit: vertices bound to nearest segment
    std::vector<std::string> skeletonNames;
    uint64_t seed = 1;
};

inline bool windNameMatches(const std::string& pattern, const std::string& name) {
    const auto lower = [](std::string s) {
        for (auto& c : s) c = char(std::tolower(unsigned(c)));
        return s;
    };
    const std::string p = lower(pattern), n = lower(name);
    if (!p.empty() && p.back() == '*') return n.compare(0, p.size() - 1, p, 0, p.size() - 1) == 0;
    return p == n;
}

inline const WindRule* findWindRule(const WindModelSpec& spec, const std::string& name) {
    const WindRule* hit = nullptr;
    for (const auto& r : spec.rules) if (windNameMatches(r.node, name)) hit = &r;   // last wins
    return hit;
}

// Nearest-segment binding (squared distance to pivot..tip). Ties prefer the
// deeper node, so leaves on a twig bind to the twig, not the trunk.
inline WindBinding bindNearest(const Model& model, const WindRig& rig,
                               const std::vector<uint8_t>* include = nullptr) {
    WindBinding b;
    b.node.assign(model.vertices.size(), kNoNode);
    for (size_t i = 0; i < model.vertices.size(); ++i) {
        if (include && !(*include)[i]) continue;
        const V3 p{model.vertices[i].pos.x, model.vertices[i].pos.y, model.vertices[i].pos.z};
        float best = 1e30f; uint16_t node = kNoNode;
        for (size_t k = 0; k < rig.nodes.size(); ++k) {
            const auto& n = rig.nodes[k];
            const float t = std::clamp(dot(p - n.pivot, n.axis), 0.f, n.length);
            const V3 d = p - (n.pivot + n.axis * t);
            const float dist = dot(d, d) * (1.f - 0.02f * float(std::min<int>(n.depth, 8)));
            if (dist < best) { best = dist; node = uint16_t(k); }
        }
        b.node[i] = node;
    }
    return b;
}

namespace model_detail {
// Vertex ranges of a mesh and its authored LOD meshes.
inline void meshVertices(const Model& m, int mesh, std::vector<std::pair<uint32_t, uint32_t>>& out,
                         std::vector<bool>& seen) {
    if (mesh < 0 || size_t(mesh) >= m.meshes.size() || seen[size_t(mesh)]) return;
    seen[size_t(mesh)] = true;
    for (const auto& p : m.meshes[size_t(mesh)].primitives)
        if (p.vertexCount) out.push_back({p.firstVertex, p.firstVertex + p.vertexCount});
    for (const auto& lod : m.meshes[size_t(mesh)].lods) meshVertices(m, lod.mesh, out, seen);
}
} // namespace model_detail

// Builds the rig from the model's node tree: every node with a mesh is one
// part; its parent part is the nearest ancestor with a mesh. Pivot = node
// origin, tip = the mesh vertex farthest from it. Node transforms are baked
// into model space (all nodes become identity), so one rig space serves all
// parts. Refuses skinned / animated models (their nodes must stay live).
inline WindRig rigFromModelNodes(Model& model, const WindModelSpec& spec, const WindMaskLibrary& lib,
                                 WindBinding& binding) {
    if (model.vb.valid()) throw std::logic_error("wind rig must be built before GPU upload");
    if (model.skinned() || !model.clips.empty())
        throw std::invalid_argument("wind rig from nodes: skinned/animated models keep live nodes");
    if (model.bindWorld.size() != model.nodes.size()) model.finalize();
    // Meshes must belong to one node (a shared mesh cannot carry two node ids).
    std::vector<int> meshOwner(model.meshes.size(), -1);
    for (size_t n = 0; n < model.nodes.size(); ++n) {
        const int mesh = model.nodes[n].mesh;
        if (mesh < 0) continue;
        if (meshOwner[size_t(mesh)] >= 0 && meshOwner[size_t(mesh)] != int(n))
            throw std::invalid_argument("wind rig: mesh '" + model.meshes[size_t(mesh)].name +
                                        "' is used by several nodes; split it before rigging");
        meshOwner[size_t(mesh)] = int(n);
    }
    binding.node.assign(model.vertices.size(), kNoNode);
    binding.flutter.assign(model.vertices.size(), 0.f);
    std::vector<int> partOf(model.nodes.size(), -1);
    std::vector<WindNodeDesc> desc;
    std::vector<bool> flutterMaterial(model.materials.size(), false);
    for (const auto& name : spec.flutterMaterials) {
        const int m = model.findMaterial(name);
        if (m >= 0) flutterMaterial[size_t(m)] = true;
    }
    std::vector<bool> transformed(model.vertices.size(), false);
    std::vector<int> order = model.order;   // parents before children
    if (order.empty()) for (size_t i = 0; i < model.nodes.size(); ++i) order.push_back(int(i));
    for (int n : order) {
        const auto& node = model.nodes[size_t(n)];
        int parentPart = -1;
        for (int a = node.parent; a >= 0; a = model.nodes[size_t(a)].parent)
            if (partOf[size_t(a)] >= 0) { parentPart = partOf[size_t(a)]; break; }
        if (node.mesh < 0) continue;
        const mat4& W = model.bindWorld[size_t(n)];
        const mat4 N = W.normalMatrix();
        std::vector<std::pair<uint32_t, uint32_t>> ranges;
        std::vector<bool> seen(model.meshes.size(), false);
        model_detail::meshVertices(model, node.mesh, ranges, seen);
        const float4 origin = W * float4{0, 0, 0, 1};
        WindNodeDesc d;
        d.parent = parentPart;
        d.pivot = {origin.x, origin.y, origin.z};
        float far = -1;
        const int part = int(desc.size());
        const WindRule* rule = findWindRule(spec, node.name);
        for (const auto& [a, b] : ranges)
            for (uint32_t i = a; i < b && i < model.vertices.size(); ++i) {
                auto& v = model.vertices[i];
                if (!transformed[i]) {
                    const float4 p = W * float4{v.pos.x, v.pos.y, v.pos.z, 1};
                    v.pos = {p.x, p.y, p.z};
                    v.nrm = normalize(N.transformDir(v.nrm));
                    const float3 t = normalize(W.transformDir({v.tangent.x, v.tangent.y, v.tangent.z}));
                    v.tangent = {t.x, t.y, t.z, v.tangent.w};
                    transformed[i] = true;
                }
                binding.node[i] = uint16_t(part);
                const V3 p{v.pos.x, v.pos.y, v.pos.z};
                const float dist = length(p - d.pivot);
                if (dist > far) { far = dist; d.tip = p; }
                if (rule && rule->flutter) binding.flutter[i] = *rule->flutter;
            }
        if (far <= 1e-4f) d.tip = d.pivot + V3{0, 1, 0};
        if (rule) { d.mask = rule->mask; d.stiffness = rule->stiffness; d.flex = rule->flex; }
        partOf[size_t(n)] = part;
        desc.push_back(d);
    }
    if (desc.empty()) throw std::invalid_argument("wind rig: model has no mesh nodes");
    for (const auto& mesh : model.meshes)
        for (const auto& p : mesh.primitives)
            if (p.material >= 0 && size_t(p.material) < flutterMaterial.size() && flutterMaterial[size_t(p.material)])
                for (uint32_t i = p.firstVertex; i < p.firstVertex + p.vertexCount && i < model.vertices.size(); ++i)
                    binding.flutter[i] = std::max(binding.flutter[i], 1.f);
    // Geometry is now in model space: every node becomes identity.
    for (auto& node : model.nodes) { node.t = {0, 0, 0}; node.r = {}; node.s = {1, 1, 1}; }
    model.finalize();
    return buildWindRig(desc, spec.profile, lib, spec.seed);
}

// --------------------------------------------------------- inspection

struct ModelWindRigView {
    std::shared_ptr<const std::vector<uint8_t>> rig, atlas;
    int rigWidth = 0, atlasWidth = 0;
    WindRig decoded;
};

// The rig bound to a model (first windRigGraph material), from the shared
// CPU bytes the images keep after upload. nullopt if the model is not rigged.
inline std::optional<ModelWindRigView> findWindRig(const Model& model) {
    for (const auto& m : model.materials) {
        if (m.shaderGraph != windRigGraph()) continue;
        const int r = m.graphTextures[0], a = m.graphTextures[1];
        if (r < 0 || a < 0 || size_t(r) >= model.images.size() || size_t(a) >= model.images.size()) continue;
        const auto& ri = model.images[size_t(r)];
        const auto& ai = model.images[size_t(a)];
        ModelWindRigView v;
        v.rig = ri.sharedRgba ? ri.sharedRgba : model_detail::windBytes(model_detail::keyFromImageName(ri.name));
        v.atlas = ai.sharedRgba ? ai.sharedRgba : model_detail::windBytes(model_detail::keyFromImageName(ai.name));
        if (!v.rig || !v.atlas) continue;
        v.rigWidth = ri.width; v.atlasWidth = ai.width;
        v.decoded = decodeWindRig(*v.rig, v.rigWidth);
        return v;
    }
    return std::nullopt;
}

struct WindDebugLine { V3 a, b; uint32_t rgba = 0xffffffffu; };

// Skeleton debug view: every part as a polyline pivot -> tip, displaced by the
// same reference math as the GPU, coloured by depth, brightness = mask value.
// `world` maps object space to the scene; `segments` per part (2..16).
inline void windRigDebugLines(const ModelWindRigView& view, const render::mat4& world,
                              const WindDrawParams& params, std::vector<WindDebugLine>& out,
                              int segments = 6, size_t maxParts = 4096) {
    segments = std::clamp(segments, 2, 16);
    const PackedWindRig eval(*view.rig, view.rigWidth, *view.atlas, view.atlasWidth);
    static const uint32_t palette[5][3] = {{160, 96, 46}, {242, 199, 38}, {51, 217, 89}, {51, 191, 255}, {230, 80, 230}};
    const auto toWorld = [&](V3 p) {
        const render::float4 w = world * render::float4{p.x, p.y, p.z, 1};
        return V3{w.x, w.y, w.z};
    };
    const auto& nodes = view.decoded.nodes;
    // Same mask bytes as the shader, for brightness.
    const auto maskAt = [&](uint8_t mask, float t) {
        MaskSamples m{};
        for (int s = 0; s < kMaskSamples; ++s) {
            const size_t at = (size_t(mask) * kMaskTexels) * 4 + size_t(s);
            m[size_t(s)] = at < view.atlas->size() ? (*view.atlas)[at] : 0;
        }
        return sampleMask(m, t);
    };
    for (size_t i = 0; i < nodes.size() && i < maxParts; ++i) {
        const auto& n = nodes[i];
        const auto* c = palette[std::min<int>(n.depth, 4)];
        V3 prev{};
        for (int k = 0; k <= segments; ++k) {
            const float t = float(k) / float(segments);
            const V3 p = n.pivot + n.axis * (n.length * t);
            const V3 moved = p + eval.offset(p, {0, 1, 0}, encodeWindVertex(uint16_t(i), 0, 0), params);
            const V3 w = toWorld(moved);
            if (k) {
                const float m = maskAt(n.mask, t - 0.5f / float(segments));
                const float b = 0.35f + 0.65f * m;
                out.push_back({prev, w, uint32_t(c[0] * b) | (uint32_t(c[1] * b) << 8) |
                                        (uint32_t(c[2] * b) << 16) | 0xff000000u});
            }
            prev = w;
        }
    }
}

// ------------------------------------------------------------- sidecars

// Process-wide reuse: one parsed library/atlas per masks.json content.
struct WindProjectData {
    WindMaskLibrary masks;
    std::shared_ptr<WindMaskAtlas> atlas;
    std::map<std::string, WindProfile> profiles;
};

inline std::shared_ptr<const WindProjectData> windProjectData(const FileReader& read,
                                                              std::vector<std::string>* warnings = nullptr) {
    static std::mutex lock;
    static std::map<std::string, std::shared_ptr<const WindProjectData>> cache;
    std::string masksText, profilesText;
    // Project root first, then a content/ mount (HTML Studio reader).
    if (read) {
        if (!read("wind/masks.json", masksText)) read("content/wind/masks.json", masksText);
        if (!read("wind/profiles.json", profilesText)) read("content/wind/profiles.json", profilesText);
    }
    const std::string key = masksText + '\x1f' + profilesText;
    std::lock_guard<std::mutex> guard(lock);
    if (auto it = cache.find(key); it != cache.end()) return it->second;
    auto data = std::make_shared<WindProjectData>();
    try { if (!masksText.empty()) data->masks.loadJson(Json::parse(masksText)); }
    catch (const std::exception& e) { if (warnings) warnings->push_back(std::string("wind/masks.json: ") + e.what()); }
    try {
        if (!profilesText.empty()) {
            const Json j = Json::parse(profilesText);
            const Json& p = j.has("profiles") ? j["profiles"] : j;
            for (const auto& [name, value] : p.members()) data->profiles[name] = WindProfile::fromJson(value);
        }
    } catch (const std::exception& e) { if (warnings) warnings->push_back(std::string("wind/profiles.json: ") + e.what()); }
    data->atlas = WindMaskAtlas::fromLibrary(data->masks);
    if (cache.size() > 16) cache.clear();
    cache[key] = data;
    return data;
}

inline WindModelSpec parseWindSpec(const Json& j, const WindProjectData& project) {
    if (!j.isObject()) throw std::invalid_argument("wind spec must be an object");
    WindModelSpec s;
    const Json& prof = j["profile"];
    if (prof.isString()) {
        auto it = project.profiles.find(prof.str());
        if (it == project.profiles.end()) throw std::invalid_argument("unknown wind profile '" + prof.str() + "'");
        s.profile = it->second;
    } else if (prof.isObject()) s.profile = WindProfile::fromJson(prof);
    const auto opt = [](const Json& o, const char* k) -> std::optional<float> {
        if (!o.has(k)) return std::nullopt;
        const double v = o[k].toDouble(-1);
        if (!std::isfinite(v) || v < 0 || v > 4) throw std::invalid_argument(std::string("wind ") + k + " out of range");
        return float(v);
    };
    for (const auto& r : j["rules"].items()) {
        WindRule rule;
        rule.node = r.getStr("node");
        if (rule.node.empty()) throw std::invalid_argument("wind rule needs 'node'");
        rule.mask = r.getStr("mask");
        if (!rule.mask.empty()) (void)project.masks.require(rule.mask);
        rule.stiffness = opt(r, "stiffness"); rule.flex = opt(r, "flex"); rule.flutter = opt(r, "flutter");
        s.rules.push_back(rule);
    }
    for (const auto& m : j["flutterMaterials"].items()) s.flutterMaterials.push_back(m.str());
    for (const auto& m : j["materials"].items()) s.materials.push_back(m.str());
    std::map<std::string, int> names;
    for (const auto& n : j["skeleton"].items()) {
        WindNodeDesc d;
        const auto v3 = [](const Json& a) {
            if (!a.isArray() || a.items().size() != 3) throw std::invalid_argument("wind skeleton point must be [x,y,z]");
            return V3{float(a[size_t(0)].toDouble()), float(a[size_t(1)].toDouble()), float(a[size_t(2)].toDouble())};
        };
        d.pivot = v3(n["pivot"]); d.tip = v3(n["tip"]);
        const std::string parent = n.getStr("parent");
        if (!parent.empty()) {
            auto it = names.find(parent);
            if (it == names.end()) throw std::invalid_argument("wind skeleton parent '" + parent + "' must come first");
            d.parent = it->second;
        }
        d.mask = n.getStr("mask");
        if (!d.mask.empty()) (void)project.masks.require(d.mask);
        d.stiffness = opt(n, "stiffness"); d.flex = opt(n, "flex");
        const std::string name = n.getStr("name", "node" + std::to_string(s.skeleton.size()));
        names[name] = int(s.skeleton.size());
        s.skeletonNames.push_back(name);
        s.skeleton.push_back(d);
    }
    s.seed = uint64_t(j.getInt("seed", 1));
    return s;
}

// Applies a parsed spec. Returns the bind report.
inline WindBindReport applyWindSpec(Model& model, const WindModelSpec& spec, const WindProjectData& project) {
    WindBinding binding;
    WindRig rig;
    if (!spec.skeleton.empty()) {
        rig = buildWindRig(spec.skeleton, spec.profile, project.masks, spec.seed);
        binding = bindNearest(model, rig);
        binding.flutter.assign(model.vertices.size(), 0.f);
        for (const auto& name : spec.flutterMaterials) {
            const int m = model.findMaterial(name);
            if (m < 0) continue;
            for (const auto& mesh : model.meshes)
                for (const auto& p : mesh.primitives)
                    if (p.material == m)
                        for (uint32_t i = p.firstVertex; i < p.firstVertex + p.vertexCount && i < binding.flutter.size(); ++i)
                            binding.flutter[i] = 1;
        }
    } else {
        rig = rigFromModelNodes(model, spec, project.masks, binding);
    }
    std::vector<int> materials;
    for (const auto& name : spec.materials) {
        const int m = model.findMaterial(name);
        if (m < 0) throw std::invalid_argument("wind spec: unknown material '" + name + "'");
        materials.push_back(m);
    }
    return bindWindRig(model, rig, *project.atlas, binding, materials, &project.masks);
}

// "<model path>.wind.json" next to a model. Missing file = no change.
inline bool applyWindSidecar(Model& model, const std::string& path, const FileReader& read,
                             std::vector<std::string>* warnings) {
    std::string text;
    if (!read || !read(path + ".wind.json", text)) return false;
    try {
        const auto project = windProjectData(read, warnings);
        applyWindSpec(model, parseWindSpec(Json::parse(text), *project), *project);
        return true;
    } catch (const std::exception& e) {
        if (warnings) warnings->push_back(path + ".wind.json: " + e.what());
        return false;
    }
}

} // namespace aiverse::render::wind
