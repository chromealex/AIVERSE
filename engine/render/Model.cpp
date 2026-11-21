#include "engine/render/WindRigModel.h"
#include "ModelTextureCache.h"
#include "AlphaCoverageMips.h"
#include <filesystem>
#include "Model.h"
#include "../data/Json.h"
#include "ModelClips.h"
#include "ModelMerge.h"
#include "GlobalGeometryPool.h"
#include "Renderer3D.h"
#include "LightmapUv.h"
#ifdef AIVERSE_RUNTIME_AUTOLOD
#include "AutoLodRuntime.h"
#endif

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

// stb_image with static linkage: the UI layer has its own copy, both may end up in one binary.
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_TGA
#define STBI_ONLY_BMP   // textures of older formats (.ms3d, .x, .3ds, Collada exports)
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wsign-compare"
#endif
#include "../../third_party/stb/stb_image.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace aiverse::render {

namespace {
std::string lower(std::string s) {
    for (auto& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
float srgbToLinear(float c) { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); }
} // namespace

int Model::findNode(const std::string& name) const {
    for (size_t i = 0; i < nodes.size(); ++i) if (nodes[i].name == name) return int(i);
    return -1;
}
int Model::findClip(const std::string& name) const {
    for (size_t i = 0; i < clips.size(); ++i) if (clips[i].name == name) return int(i);
    const std::string l = lower(name);
    for (size_t i = 0; i < clips.size(); ++i) if (lower(clips[i].name) == l) return int(i);
    // numbered variants: "Attack" finds "Attack - 1" (Warcraft III), "Attack1", "Attack_01"
    for (size_t i = 0; i < clips.size(); ++i) {
        const std::string c = lower(clips[i].name);
        if (c.size() <= l.size() || c.compare(0, l.size(), l) != 0) continue;
        bool numbered = true;
        for (size_t k = l.size(); k < c.size(); ++k)
            if (!(std::isdigit(static_cast<unsigned char>(c[k])) || c[k] == ' ' || c[k] == '-' || c[k] == '_')) numbered = false;
        if (numbered) return int(i);
    }
    return -1;
}
int Model::findMaterial(const std::string& name) const {
    for (size_t i = 0; i < materials.size(); ++i) if (materials[i].name == name) return int(i);
    return -1;
}
int Model::findVariant(const std::string& name) const {
    for (size_t i = 0; i < variants.size(); ++i) if (variants[i] == name) return int(i);
    return -1;
}

void Model::finalize() {
    if (materials.empty()) { Material m; m.name = "default"; materials.push_back(m); }
    for (auto& n : nodes) n.children.clear();
    for (size_t i = 0; i < nodes.size(); ++i) {
        int p = nodes[i].parent;
        if (p >= 0 && p < int(nodes.size())) nodes[size_t(p)].children.push_back(int(i));
        else nodes[i].parent = -1;
    }
    order.clear();
    std::vector<uint8_t> seen(nodes.size(), 0);
    for (size_t i = 0; i < nodes.size(); ++i) {
        if (nodes[i].parent >= 0) continue;
        std::vector<int> stack{int(i)};
        while (!stack.empty()) {
            int n = stack.back(); stack.pop_back();
            if (seen[size_t(n)]) continue; // cycles in broken files
            seen[size_t(n)] = 1;
            order.push_back(n);
            for (auto it = nodes[size_t(n)].children.rbegin(); it != nodes[size_t(n)].children.rend(); ++it) stack.push_back(*it);
        }
    }
    // primitive bounds from the vertices they reference
    for (auto& mesh : meshes) {
        mesh.boundsMin = {1e30f, 1e30f, 1e30f}; mesh.boundsMax = {-1e30f, -1e30f, -1e30f};
        mesh.deformable = false;
        for (auto& p : mesh.primitives) {
            p.boundsMin = {1e30f, 1e30f, 1e30f}; p.boundsMax = {-1e30f, -1e30f, -1e30f};
            for (uint32_t i = 0; i < p.indexCount; ++i) {
                const float3 v = vertices[indices[p.firstIndex + i]].pos;
                p.boundsMin = min3(p.boundsMin, v); p.boundsMax = max3(p.boundsMax, v);
            }
            if (p.indexCount == 0) p.boundsMin = p.boundsMax = {0, 0, 0};
            // AutoLOD's index-only morph alias must retain the source
            // channel envelope even if this LOD drops extreme triangles.
            if(p.morphAliasMesh>=0) {
                if(p.morphAliasPrimitive<0 ||
                   size_t(p.morphAliasMesh)>=meshes.size() ||
                   size_t(p.morphAliasPrimitive)>=
                       meshes[size_t(p.morphAliasMesh)].primitives.size())
                    throw std::runtime_error("invalid AutoLOD morph source");
                const Primitive& source=meshes[size_t(p.morphAliasMesh)]
                    .primitives[size_t(p.morphAliasPrimitive)];
                if(source.morphAliasMesh>=0 ||
                   source.morphTargets!=p.morphTargets ||
                   source.firstVertex!=p.firstVertex ||
                   source.vertexCount!=p.vertexCount)
                    throw std::runtime_error("incompatible AutoLOD morph source");
                p.boundsMin=min3(p.boundsMin,source.boundsMin);
                p.boundsMax=max3(p.boundsMax,source.boundsMax);
            }
            // Conservatively include all morph targets for normalized
            // weights [0,1]. For any vertex, each target can independently
            // contribute its negative or positive displacement. The LOD
            // selector only enables morph LOD when current weights lie in
            // this bounded interval.
            if(p.morphTargets>0 && !p.morphDeltas.empty()) {
                const size_t first=size_t(p.firstVertex);
                const size_t count=size_t(p.vertexCount);
                const size_t channels=size_t(p.morphTargets);
                if(channels>size_t(kMaxMorphTargets) ||
                   first>vertices.size() ||
                   count>vertices.size()-first ||
                   count==0 ||
                   channels>std::numeric_limits<size_t>::max()/6u/count ||
                   p.morphDeltas.size()!=channels*count*6u)
                    throw std::runtime_error("invalid morph bounds source");
                for(size_t v=0;v<count;++v) {
                    float3 low=vertices[first+v].pos;
                    float3 high=low;
                    for(size_t channel=0;channel<channels;++channel) {
                        const size_t at=(channel*count+v)*6u;
                        const float x=p.morphDeltas[at],
                                    y=p.morphDeltas[at+1],
                                    z=p.morphDeltas[at+2];
                        if(!std::isfinite(x) || !std::isfinite(y) ||
                           !std::isfinite(z))
                            throw std::runtime_error("nonfinite morph displacement");
                        low.x+=std::min(0.f,x); high.x+=std::max(0.f,x);
                        low.y+=std::min(0.f,y); high.y+=std::max(0.f,y);
                        low.z+=std::min(0.f,z); high.z+=std::max(0.f,z);
                    }
                    if(!std::isfinite(low.x) || !std::isfinite(low.y) ||
                       !std::isfinite(low.z) || !std::isfinite(high.x) ||
                       !std::isfinite(high.y) || !std::isfinite(high.z))
                        throw std::runtime_error("morph bounds overflow");
                    p.boundsMin=min3(p.boundsMin,low);
                    p.boundsMax=max3(p.boundsMax,high);
                }
            }
            mesh.boundsMin = min3(mesh.boundsMin, p.boundsMin); mesh.boundsMax = max3(mesh.boundsMax, p.boundsMax);
            mesh.deformable |= !p.jointPalette.empty() || p.morphTargets > 0;
        }
        if (mesh.primitives.empty()) mesh.boundsMin = mesh.boundsMax = {};
        auto& sb = mesh.shadowBatch;
        sb.boundsMin = {1e30f, 1e30f, 1e30f}; sb.boundsMax = {-1e30f, -1e30f, -1e30f};
        for (uint32_t i = 0; i < sb.indexCount; ++i) { const float3 v = vertices[indices[sb.firstIndex + i]].pos; sb.boundsMin = min3(sb.boundsMin, v); sb.boundsMax = max3(sb.boundsMax, v); }
        if (sb.indexCount == 0) sb.boundsMin = sb.boundsMax = {0, 0, 0};
    }
    // model bounds in bind pose
    std::vector<mat4> world(nodes.size());
    for (int n : order) {
        const Node& nd = nodes[size_t(n)];
        mat4 l = mat4::trs(nd.t, nd.r, nd.s);
        world[size_t(n)] = nd.parent >= 0 ? world[size_t(nd.parent)] * l : l;
    }
    bindWorld = world;
    boundsMin = {1e30f, 1e30f, 1e30f}; boundsMax = {-1e30f, -1e30f, -1e30f};
    for (int n : order) {
        const Node& nd = nodes[size_t(n)];
        if (nd.mesh < 0) continue;
        for (auto& p : meshes[size_t(nd.mesh)].primitives) {
            mat4 xf = world[size_t(n)];
            if (nd.skin >= 0 && !p.jointPalette.empty()) { // bind pose: joint world * inverse bind
                const Skin& sk = skins[size_t(nd.skin)];
                int j = p.jointPalette[0];
                if (j < int(sk.joints.size())) xf = world[size_t(sk.joints[size_t(j)])] * sk.inverseBind[size_t(j)];
            }
            for (int c = 0; c < 8; ++c) {
                float3 v{c & 1 ? p.boundsMax.x : p.boundsMin.x, c & 2 ? p.boundsMax.y : p.boundsMin.y, c & 4 ? p.boundsMax.z : p.boundsMin.z};
                v = xf.transformPoint(v);
                boundsMin = min3(boundsMin, v); boundsMax = max3(boundsMax, v);
            }
        }
    }
    if (boundsMin.x > boundsMax.x) boundsMin = boundsMax = {0, 0, 0};
    markGeometryChanged(); // regenerated hierarchy, node transforms and bounds
    markMaterialChanged(); // default/imported material table may have changed
}

namespace {
using Loader = std::unique_ptr<Model> (*)(const std::string&, const std::string&, const FileReader&, std::vector<std::string>*);
struct Format { const char* ext; Loader load; };
const Format kFormats[] = {
    {".gltf", loadGltf}, {".glb", loadGltf},
#if !defined(AIVERSE_NO_OBJ)
    {".obj", loadObj},
#endif
#if !defined(AIVERSE_NO_FBX)
    {".fbx", loadFbx},
#endif
#if !defined(AIVERSE_NO_LEGACY_MODELS)
    {".vox", loadVox}, {".vxl", loadVxl}, {".ply", loadPly}, {".stl", loadStl}, {".3ds", load3ds}, {".dae", loadDae},
    {".mdx", loadMdx}, {".mdl", loadMdl}, {".3do", load3do}, {".w3d", loadW3d},
#endif
};
std::string extOf(const std::string& path) {
    const size_t dot = path.find_last_of('.'), slash = path.find_last_of("/\\");
    return dot == std::string::npos || (slash != std::string::npos && dot < slash) ? std::string() : lower(path.substr(dot));
}
Loader loaderFor(const std::string& ext) {
    for (const auto& f : kFormats) if (ext == f.ext) return f.load;
    return nullptr;
}
} // namespace

bool isNativeModelFormat(const std::string& path) { return loaderFor(extOf(path)) != nullptr; }
namespace {
void applyModelBatchingSidecar(Model& model, const std::string& path, const FileReader& read) {
    std::string bytes;
    if (!read(path + ".batching.json", bytes)) return;
    // FileReader is an arbitrary content callback. Some existing importers
    // return their source bytes for unrecognized paths instead of false.
    // An opt-in sidecar must begin with an object, otherwise ignore it.
    const auto first = bytes.find_first_not_of(" \t\r\n");
    if (first == std::string::npos || bytes[first] != '{') return;
    const Json config = Json::parse(bytes);
    if (!config.isObject()) throw std::runtime_error(path + ".batching.json: expected JSON object");
    model.batching.mergeRigid = config.getBool("mergeRigid", false);
    // Allows projects to opt out individual sensitive assets without
    // disabling import-time LOD generation for other models.
    if (config.has("autoLod") && config["autoLod"].type() != Json::Type::Bool)
        throw std::runtime_error(path + ".batching.json: autoLod must be boolean");
    model.batching.autoLod = config.getBool("autoLod", true);
    if(config.has("independentShadowLod")&&config["independentShadowLod"].type()!=Json::Type::Bool)throw std::runtime_error(path+".batching.json: independentShadowLod must be boolean");
    model.batching.independentShadowLod=config.getBool("independentShadowLod",false);
    // Optional per-asset policy for native importers and project sidecars.
    // It is applied before GPU upload; editing it requires asset reupload.
    if (config.has("geometryPool")) {
        const Json& setting = config["geometryPool"];
        if (!setting.isString())
            throw std::runtime_error(path + ".batching.json: geometryPool must be a string");
        const std::string value = setting.str();
        using PoolPolicy = Model::Batching::GeometryPoolPolicy;
        if (value == "inherit")
            model.batching.geometryPoolPolicy = PoolPolicy::Inherit;
        else if (value == "dedicated")
            model.batching.geometryPoolPolicy = PoolPolicy::Dedicated;
        else if (value == "rigidLod")
            model.batching.geometryPoolPolicy = PoolPolicy::AllowRigidLods;
        else
            throw std::runtime_error(path + ".batching.json: geometryPool must be inherit, dedicated or rigidLod");
    }
    if (config.has("maxExtent")) {
        const float extent = float(config["maxExtent"].toDouble());
        if (!std::isfinite(extent) || extent <= 0.0f || extent > 1000000.0f)
            throw std::runtime_error(path + ".batching.json: maxExtent must be finite and > 0");
        model.batching.maxExtent = extent;
    }
    if (config.has("preserveNodes")) {
        if (!config["preserveNodes"].isArray())
            throw std::runtime_error(path + ".batching.json: preserveNodes must be an array");
        for (const Json& entry : config["preserveNodes"].items()) {
            if (!entry.isString() || entry.str().empty())
                throw std::runtime_error(path + ".batching.json: preserveNodes entries must be nonempty names");
            model.batching.preserveNodes.push_back(entry.str());
        }
    }
    if (config.has("mutableNodes")) {
        if (!config["mutableNodes"].isArray())
            throw std::runtime_error(path + ".batching.json: mutableNodes must be an array");
        for (const Json& entry : config["mutableNodes"].items()) {
            if (!entry.isString() || entry.str().empty())
                throw std::runtime_error(path + ".batching.json: mutableNodes entries must be nonempty names");
            bool found = false;
            for (Node& node : model.nodes) if (node.name == entry.str()) {
                node.runtimeMutable = true;
                found = true;
            }
            if (!found) throw std::runtime_error(path + ".batching.json: unknown mutable node " + entry.str());
        }
    }
}
} // namespace


namespace { thread_local std::shared_ptr<ModelTextureCache> loadingTextures; }
std::unique_ptr<Model> loadModel(const std::string& path, const FileReader& read, std::vector<std::string>* warnings, std::shared_ptr<ModelTextureCache> textures) {
    struct Scope { std::shared_ptr<ModelTextureCache> old; ~Scope() { loadingTextures = std::move(old); } } scope{loadingTextures};
    loadingTextures = std::move(textures);
    const std::string ext = extOf(path);
    Loader load = loaderFor(ext);
    std::string bytes;
    if (!load) {
        // converted on the host (tools/models/modelconv): "<file>.glb" next to the source
        if (read(path + ".glb", bytes)) {
            auto m = loadGltf(path + ".glb", bytes, read, warnings);
            m->path = path;
            applyModelBatchingSidecar(*m, path, read);
            wind::applyWindSidecar(*m, path, read, warnings);   // "<model>.wind.json" (WindRigModel.h)
            return m;
        }
#if defined(AIVERSE_NO_FBX)
        if (ext == ".fbx") throw std::runtime_error(path + ": FBX import is disabled in this build (AIVERSE_FBX=OFF); convert to .glb");
#endif
#if defined(AIVERSE_NO_OBJ)
        if (ext == ".obj") throw std::runtime_error(path + ": OBJ import is disabled in this build (AIVERSE_OBJ=OFF); convert to .glb");
#endif
#if defined(AIVERSE_NO_LEGACY_MODELS)
        if (ext == ".vox" || ext == ".vxl" || ext == ".ply" || ext == ".stl" || ext == ".3ds" ||
            ext == ".dae" || ext == ".mdx" || ext == ".mdl" || ext == ".3do" || ext == ".w3d")
            throw std::runtime_error(path + ": legacy model import is disabled in this build (AIVERSE_LEGACY_MODELS=OFF); convert to .glb");
#endif
        if (!read(path, bytes)) throw std::runtime_error(path + ": file not found");
        throw std::runtime_error(path + ": unsupported model format '" + ext + "' (convert it with tools/models/modelconv, "
                                 "or use .gltf, .glb, .obj, .fbx, .dae, .3ds, .ply, .stl, .vox, .vxl, .mdx, .mdl, .3do, .w3d)");
    }
    if (!read(path, bytes)) throw std::runtime_error(path + ": file not found");
    auto m = load(path, bytes, read, warnings);
    m->path = path;
    // generated lightmap UVs next to the model (light baker, LightmapUv.h)
    if (std::string uv; read(path + ".lmuv", uv)) {
        std::string err;
        if (!applyLightmapUvs(*m, uv, &err) && warnings) warnings->push_back(path + ".lmuv: " + err);
    }
    applyClipSidecar(*m, path, read, warnings);   // "<model>.clips.json": splits, external clips (ModelClips.h)
    applyModelBatchingSidecar(*m, path, read);
    wind::applyWindSidecar(*m, path, read, warnings);   // "<model>.wind.json": hierarchical wind masks (WindRigModel.h)
    return m;
}

bool readModelImage(const std::string& path, const FileReader& reader, Image& out) {
    if (loadingTextures) return loadingTextures->read(path, reader, out);
    std::string bytes;
    return reader(path, bytes) && decodeImage(bytes, out);
}

bool ModelTextureCache::read(const std::string& path, const FileReader& reader, Image& out) {
    const auto key = std::filesystem::path(path).lexically_normal().generic_string();
    std::lock_guard lock(mutex_);
    auto it = decoded_.find(key);
    if (it == decoded_.end()) {
        std::string bytes; Image image;
        if (!reader(key, bytes) || !decodeImage(bytes, image)) return false;
        image.sharedRgba = std::make_shared<const std::vector<uint8_t>>(std::move(image.rgba));
        size_t size = image.sharedRgba->size();
        if (image.compressed) for (const auto& level : image.compressed->levels) size += level.size();
        while (!decoded_.empty() && bytes_ + size > kCpuBudget) {
            auto oldest = std::min_element(decoded_.begin(), decoded_.end(), [](const auto& a, const auto& b) { return a.second.used < b.second.used; });
            bytes_ -= oldest->second.bytes; decoded_.erase(oldest);
        }
        it = decoded_.emplace(key, Decoded{std::move(image), size, ++clock_}).first;
        bytes_ += size; ++decodedCount_;
    }
    it->second.used = ++clock_;
    const auto name = out.name; out = it->second.image; out.name = name;
    out.textureKey = key; out.textureCache = shared_from_this();
    return true;
}

bool decodeImage(const std::string& bytes, Image& out) {
    if (isKtx2(bytes)) {
        auto ci = std::make_shared<CompressedImage>();
        std::string err;
        std::vector<uint8_t> rgba;
        if (!parseKtx2(bytes, *ci, err)) return false;
        // no CPU decoder (ASTC): usable only where the GPU samples it natively (uploadModel)
        if (!decodeBlocks(*ci, 0, rgba) && !isAstc(ci->format)) return false;
        out.width = ci->width; out.height = ci->height;
        out.rgba = std::move(rgba);
        out.compressed = std::move(ci);
        return true;
    }
    if (bytes.size() >= 4 && (bytes.compare(0, 4, "BLP1") == 0 || bytes.compare(0, 4, "BLP2") == 0)) {
#if defined(AIVERSE_NO_BLP)
        return false;
#else
        return decodeBlp(bytes, out);
#endif
    }
    if (bytes.size() >= 4 && bytes.compare(0, 4, "DDS ") == 0) return decodeDds(bytes, out);
    int w = 0, h = 0, n = 0;
    stbi_uc* px = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(bytes.data()), int(bytes.size()), &w, &h, &n, 4);
    if (!px) return false;
    out.width = w; out.height = h;
    out.rgba.assign(px, px + size_t(w) * size_t(h) * 4);
    stbi_image_free(px);
    return true;
}

std::unique_ptr<Model> modelFromParts(const std::string& name, const std::vector<ModelPart>& parts) {
    auto m = std::make_unique<Model>();
    m->path = name;
    Node root; root.name = "root";
    m->nodes.push_back(root);
    for (const auto& part : parts) {
        const auto& sv = part.mesh->vertices();
        const auto& si = part.mesh->indices();
        Primitive p;
        p.firstIndex = uint32_t(m->indices.size());
        p.indexCount = uint32_t(si.size());
        p.material = int(m->materials.size());
        Material mat = part.material;
        if (mat.name.empty()) mat.name = part.name;
        m->materials.push_back(mat);
        const uint32_t base = uint32_t(m->vertices.size());
        // ModelVertex stream overrides and renderer LOD/geometry policies
        // require the full model-relative vertex interval for each primitive.
        // modelFromParts previously left these counts at their zero defaults.
        p.firstVertex = base;
        p.vertexCount = static_cast<uint32_t>(sv.size());
        for (const auto& v : sv) {
            ModelVertex mv;
            mv.pos = v.pos; mv.nrm = v.nrm;
            mv.uv = {v.pos.x + v.pos.z, v.pos.y}; // planar, only for tiling detail textures
            auto ch = [&](int s) { return uint32_t(std::lround(srgbToLinear(float((v.rgba >> s) & 255u) / 255.0f) * 255.0f)); };
            mv.color = ch(0) | (ch(8) << 8) | (ch(16) << 16) | (v.rgba & 0xff000000u);
            m->vertices.push_back(mv);
        }
        for (uint32_t i : si) m->indices.push_back(base + i);
        MeshDef md; md.name = part.name; md.primitives.push_back(p);
        Node n; n.name = part.name; n.parent = 0; n.mesh = int(m->meshes.size());
        // decompose a translate/scale/rotateY part transform (enough for the built-in shapes)
        const mat4& x = part.xf;
        n.t = {x.at(0, 3), x.at(1, 3), x.at(2, 3)};
        float sx = length({x.at(0, 0), x.at(1, 0), x.at(2, 0)}), sy = length({x.at(0, 1), x.at(1, 1), x.at(2, 1)}), sz = length({x.at(0, 2), x.at(1, 2), x.at(2, 2)});
        n.s = {sx, sy, sz};
        n.r = quat::axisAngle({0, 1, 0}, std::atan2(x.at(0, 2), x.at(0, 0)));
        m->meshes.push_back(md);
        m->nodes.push_back(n);
    }
    m->finalize();
    return m;
}

void generateTangents(std::vector<ModelVertex>& v, const std::vector<uint32_t>& idx, uint32_t first, uint32_t count) {
    std::vector<float3> tan(v.size()), bit(v.size());
    for (uint32_t i = first; i + 2 < first + count; i += 3) {
        const uint32_t a = idx[i], b = idx[i + 1], c = idx[i + 2];
        const float3 e1 = v[b].pos - v[a].pos, e2 = v[c].pos - v[a].pos;
        const float du1 = v[b].uv.x - v[a].uv.x, dv1 = v[b].uv.y - v[a].uv.y;
        const float du2 = v[c].uv.x - v[a].uv.x, dv2 = v[c].uv.y - v[a].uv.y;
        float d = du1 * dv2 - du2 * dv1;
        if (std::fabs(d) < 1e-12f) continue;
        d = 1.0f / d;
        const float3 t = (e1 * dv2 - e2 * dv1) * d, bt = (e2 * du1 - e1 * du2) * d;
        for (uint32_t k : {a, b, c}) { tan[k] += t; bit[k] += bt; }
    }
    for (uint32_t i = first; i < first + count; ++i) {
        const uint32_t k = idx[i];
        const float3 n = v[k].nrm;
        float3 t = tan[k] - n * dot(n, tan[k]);
        if (dot(t, t) < 1e-12f) t = std::fabs(n.x) < 0.9f ? cross(n, {1, 0, 0}) : cross(n, {0, 1, 0});
        t = normalize(t);
        const float w = dot(cross(n, t), bit[k]) < 0 ? -1.0f : 1.0f;
        v[k].tangent = {t.x, t.y, t.z, w};
    }
}

namespace {
gfx::PixelFormat gpuFormat(const CompressedImage& c) {
    switch (c.format) {
        case BlockFormat::ETC2_RGB8: return gfx::PixelFormat::ETC2_RGB8;
        case BlockFormat::ETC2_RGBA8: return gfx::PixelFormat::ETC2_RGBA8;
        case BlockFormat::BC1: return c.vkFormat == 131 || c.vkFormat == 132 ? gfx::PixelFormat::RGBA8 : gfx::PixelFormat::BC1;
        case BlockFormat::BC3: return gfx::PixelFormat::BC3;
        case BlockFormat::BC4: return gfx::PixelFormat::BC4;
        case BlockFormat::EAC_R11: return gfx::PixelFormat::EAC_R11;
        case BlockFormat::BC7: return gfx::PixelFormat::BC7;
        case BlockFormat::ASTC_4x4: return gfx::PixelFormat::ASTC_4x4;
        case BlockFormat::ASTC_6x6: return gfx::PixelFormat::ASTC_6x6;
        default: return gfx::PixelFormat::RGBA8; // BC5 requires shader normal-Z reconstruction
    }
}

void uploadImage(gfx::Device& dev, Image& img) {
    if (img.tex.valid()) return;
    // Virtualized base-colour images can retain a small fallback *instead*
    // of uploading their full-resolution source. This must be configured
    // before upload; never replace shared/cached GPU texture handles.
    // Exact alpha cutouts and image reuse are checked by the opt-in
    // ModelVirtualTextureFallback.h asset policy.
    if (img.virtualFallbackMaxDimension > 0) {
        const auto& pixels = img.sharedRgba ? *img.sharedRgba : img.rgba;
        const size_t w0 = img.width > 0 ? size_t(img.width) : 0;
        const size_t h0 = img.height > 0 ? size_t(img.height) : 0;
        if (!w0 || !h0 || w0 > (size_t(1) << 20) ||
            h0 > (size_t(1) << 20) ||
            w0 > std::numeric_limits<size_t>::max()/h0 ||
            w0*h0 > std::numeric_limits<size_t>::max()/4u ||
            pixels.size() != w0*h0*4u)
            return; // No silent full-resolution upload on an invalid VT source.
        // Preserve the exact source alpha at full resolution. A single R8
        // channel is enough for cutout coverage in colour, shadow, selection
        // and feedback passes. Filtering and address mode match the source.
        if (img.virtualFallbackPreserveAlpha) {
            if (!dev.supports(gfx::PixelFormat::R8)) return;
            std::vector<uint8_t> alpha(w0*h0);
            for (size_t i=0;i<alpha.size();++i)
                alpha[i]=pixels[i*4u+3u];
            gfx::TextureDesc ad;
            ad.width=img.width; ad.height=img.height;
            ad.format=gfx::PixelFormat::R8;
            ad.filter=img.nearest?gfx::Filter::Nearest:gfx::Filter::Linear;
            ad.repeat=img.repeat;
            ad.mipmaps=!img.nearest;
            ad.data=alpha.data();
            ad.label="VT full-resolution alpha coverage";
            img.virtualAlphaMask=dev.createTexture(ad);
            if (!img.virtualAlphaMask.valid()) return;
        }
        std::vector<uint8_t> coarse(pixels.begin(),pixels.end());
        int width = img.width, height = img.height;
        while (std::max(width,height) > img.virtualFallbackMaxDimension) {
            const int nextW = std::max(1,(width+1)/2);
            const int nextH = std::max(1,(height+1)/2);
            std::vector<uint8_t> reduced(size_t(nextW)*size_t(nextH)*4u);
            for (int y = 0; y < nextH; ++y)
                for (int x = 0; x < nextW; ++x) {
                    uint32_t channel[4] = {};
                    for (int dy = 0; dy < 2; ++dy)
                        for (int dx = 0; dx < 2; ++dx) {
                            const int sx = std::min(width-1,x*2+dx);
                            const int sy = std::min(height-1,y*2+dy);
                            const size_t pos = (size_t(sy)*size_t(width)+size_t(sx))*4u;
                            for (int c = 0; c < 4; ++c)
                                channel[c] += coarse[pos+size_t(c)];
                        }
                    const size_t dst = (size_t(y)*size_t(nextW)+size_t(x))*4u;
                    for (int c = 0; c < 4; ++c)
                        reduced[dst+size_t(c)] = uint8_t((channel[c]+2u)/4u);
                }
            coarse.swap(reduced);
            width = nextW;
            height = nextH;
        }
        gfx::TextureDesc td;
        td.width = width; td.height = height;
        td.format = gfx::PixelFormat::RGBA8;
        td.filter = img.nearest ? gfx::Filter::Nearest : gfx::Filter::Linear;
        td.repeat = img.repeat;
        td.mipmaps = !img.nearest;
        td.data = coarse.data();
        td.label = img.name.c_str();
        img.tex = dev.createTexture(td);
        if (!img.tex.valid() && img.virtualAlphaMask.valid()) {
            dev.destroyTexture(img.virtualAlphaMask);
            img.virtualAlphaMask={};
        }
        img.gpuCompressed = false;
        return;
    }
    if (img.compressed && !img.compressed->levels.empty()) {
        // keep KTX2 blocks compressed in VRAM when the GPU samples them (shaders decode sRGB themselves)
        const CompressedImage& c = *img.compressed;
        const gfx::PixelFormat pf = gpuFormat(c);
        if (pf != gfx::PixelFormat::RGBA8 && dev.supports(pf)) {
            std::vector<const void*> data;
            std::vector<size_t> sizes;
            // Model shader resources are sampler2D. Preserve the previous
            // first-face behavior for KTX2 array/cube/volume sources here;
            // generalized texture consumers can use all preserved slices.
            for (size_t l = 0; l < c.levels.size(); ++l) {
                data.push_back(c.levels[l].data());
                sizes.push_back(levelByteSize(c.format, levelWidth(c, int(l)),
                                               levelHeight(c, int(l))));
            }
            gfx::TextureDesc td;
            td.width = c.width; td.height = c.height;
            td.format = pf;
            td.filter = img.nearest ? gfx::Filter::Nearest : gfx::Filter::Linear;
            td.repeat = img.repeat;
            td.levelCount = int(c.levels.size());
            td.levelData = data.data();
            td.levelSize = sizes.data();
            td.label = img.name.c_str();
            img.tex = dev.createTexture(td);
            img.gpuCompressed = img.tex.valid();
            if (img.tex.valid()) return;
        }
    }
    const auto& pixels = img.sharedRgba ? *img.sharedRgba : img.rgba;
    if (pixels.empty()) return;
    gfx::TextureDesc td;
    td.width = img.width; td.height = img.height;
    td.format = gfx::PixelFormat::RGBA8;
    td.filter = img.nearest ? gfx::Filter::Nearest : gfx::Filter::Linear;
    td.repeat = img.repeat;
    td.mipmaps = !img.nearest;
    td.data = pixels.data();
    td.label = img.name.c_str();
    if(img.alphaCoverageThreshold && img.preserveAlphaCoverage && !img.nearest && !img.compressed) {
        std::vector<AlphaCoverageMip> levels;
        if(buildAlphaCoverageMips(img.width,img.height,pixels,img.alphaCoverageThreshold,levels)) {
            td.mipmaps=false;td.mipLevels=int(levels.size()+1);
            auto texture=dev.createTexture(td);
            bool complete=texture.valid();
            for(size_t i=0;complete&&i<levels.size();++i) {
                const auto& level=levels[i];gfx::TextureUploadDesc upload;
                upload.mipLevel=int(i+1);upload.width=level.width;upload.height=level.height;
                upload.rowPitchBytes=level.width*4;upload.data=level.rgba.data();
                complete=dev.updateTextureRegion(texture,upload);
            }
            if(complete){img.tex=texture;img.alphaCoverageMipmaps=true;return;}
            if(texture.valid())dev.destroyTexture(texture);
            td.mipmaps=true;td.mipLevels=0;
        }
    }
    img.tex = dev.createTexture(td);
}
} // namespace

namespace {
// Normal maps can be added later by material overrides / the material library (docs/models.md), to
// any model: primitives that came without tangents but have UVs get them now, while the CPU copy
// still exists. Tangents a loader wrote are kept.
void ensureTangents(Model& m) {
    for (const auto& mesh : m.meshes)
        for (const auto& p : mesh.primitives) {
            if (p.indexCount < 3) continue;
            bool defaults = true, uvs = false;
            for (uint32_t i = 0; i < p.indexCount && (defaults || !uvs); ++i) {
                const ModelVertex& v = m.vertices[m.indices[p.firstIndex + i]];
                defaults = defaults && v.tangent.x == 1 && v.tangent.y == 0 && v.tangent.z == 0 && v.tangent.w == 1;
                uvs = uvs || v.uv.x != 0 || v.uv.y != 0;
            }
            if (defaults && uvs) generateTangents(m.vertices, m.indices, p.firstIndex, p.indexCount);
        }
}
} // namespace

void ModelTextureCache::invalidateSource(const std::string& path) {
    const auto key=std::filesystem::path(path).lexically_normal().generic_string();
    if(key.empty())return;
    {
        // CPU workers may still hold shared pixel leases; removing an LRU
        // entry does not invalidate those leases.
        std::lock_guard lock(mutex_);
        const auto it=decoded_.find(key);
        if(it!=decoded_.end()) {
            bytes_-=it->second.bytes;
            decoded_.erase(it);
        }
    }
    // Called only on render thread, like upload(). Existing GPU leases are
    // never mutated or destroyed at this point.
    ++sourceRevisions_[key];
}

std::string ModelTextureCache::gpuKey(const Image& image) const {
    const auto revision=sourceRevisions_.find(image.textureKey);
    const uint64_t epoch=revision==sourceRevisions_.end()?0:revision->second;
    return image.textureKey+(image.repeat?"#repeat":"#clamp")+
           (image.nearest?"#nearest":"#linear")+
           (image.alphaCoverageThreshold?"#alphaCoverage"+std::to_string(image.alphaCoverageThreshold):"")+
           "#revision"+std::to_string(epoch);
}

bool ModelTextureCache::borrowResident(Image& image) const {
    if(image.textureKey.empty())return false;
    const auto key=gpuKey(image);
    const auto it=gpu_.find(key);
    if(it==gpu_.end() || !it->second.tex.valid())return false;
    image.tex=it->second.tex;
    image.textureLease=it->second.textureLease;
    image.borrowedTexture=true;
    image.gpuCompressed=it->second.gpuCompressed;
    image.alphaCoverageMipmaps=it->second.alphaCoverageMipmaps;
    return true;
}

void ModelTextureCache::upload(gfx::Device& dev, Image& image) {
    const auto key=gpuKey(image);
    auto it = gpu_.find(key);
    if (it == gpu_.end()) {
        // Allocate the CPU lease/control block and map node BEFORE creating
        // a GPU texture. An allocation failure after createTexture would
        // otherwise destroy a local Image while its GPU handle is live,
        // bypassing explicit Device retirement and GPU completion fences.
        Image owner = image;
        owner.textureCache.reset();
        owner.borrowedTexture = false;
        owner.textureLease = std::make_shared<int>(0);
        auto [inserted, fresh] = gpu_.try_emplace(key, std::move(owner));
        it = inserted;
        if (fresh) {
            try {
                uploadImage(dev, it->second);
            } catch (...) {
                // uploadImage assigns a new handle only after a successful
                // createTexture; no potentially live handle is destroyed on
                // a failed Device allocation.
                if (!it->second.tex.valid()) gpu_.erase(it);
                throw;
            }
            if (!it->second.tex.valid()) {
                gpu_.erase(it);
                return;
            }
            it->second.rgba.clear();
            it->second.sharedRgba.reset();
            it->second.compressed.reset();
        }
    }
    image.textureLease = it->second.textureLease; image.tex = it->second.tex; image.gpuCompressed = it->second.gpuCompressed; image.borrowedTexture = true; image.alphaCoverageMipmaps=it->second.alphaCoverageMipmaps;
}
ModelTextureCache::PreparedGpuPromotion
ModelTextureCache::prepareGpuPromotion(
        const std::vector<Model*>& models,
        const std::vector<PendingImage>& appended) {
    PreparedGpuPromotion plan(*this);
    // Both existing scratch Model images and future material-override slots
    // participate in ONE preallocated set of GPU-cache owner nodes.
    const auto include=[&](const Image& image,Model* target,size_t index) {
        if(image.textureCache.get()!=this || image.borrowedTexture ||
           !image.tex.valid() || image.textureKey.empty())
            return;
        const auto key=gpuKey(image);
        Image* owner=nullptr;
        if(auto resident=gpu_.find(key);resident!=gpu_.end()) {
            owner=&resident->second;
        } else {
            auto [it,inserted]=plan.pending_.try_emplace(key);
            if(inserted) {
                // Copying the descriptor does NOT transfer GPU ownership:
                // the original Model/staged image retains its handle
                // until the plan successfully commits.
                Image prepared=image;
                prepared.textureCache.reset();
                prepared.borrowedTexture=false;
                prepared.textureLease=std::make_shared<int>(0);
                prepared.rgba.clear();
                prepared.sharedRgba.reset();
                prepared.compressed.reset();
                it->second=std::move(prepared);
            }
            owner=&it->second;
        }
        plan.bindings_.push_back({target,index,owner});
    };
    for(Model* model:models) {
        if(!model)continue;
        for(size_t i=0;i<model->images.size();++i)
            include(model->images[i],model,i);
    }
    for(const auto& proposed:appended) {
        if(!proposed.model || !proposed.staged ||
           proposed.appendIndex<proposed.model->images.size())
            throw std::invalid_argument("invalid speculative material image reference");
        include(*proposed.staged,proposed.model,proposed.appendIndex);
    }
    plan.redundant_.reserve(plan.bindings_.size());
    return plan;
}

std::vector<Image> ModelTextureCache::PreparedGpuPromotion::commit() {
    if(committed_ || !cache_)
        throw std::logic_error("shared GPU texture plan already committed");
    // All map keys are checked before the first node is transferred.
    // A host that interleaves another cache publication must retry staging.
    for(const auto& [key,owner]:pending_) {
        (void)owner;
        if(cache_->gpu_.contains(key))
            throw std::logic_error("shared GPU texture key changed during staging: "+key);
    }
    // Material image slots are appended immediately before commit().
    // Validate all indices BEFORE mutating shared GPU residency.
    for(const auto& binding:bindings_)
        if(!binding.model ||
           binding.index>=binding.model->images.size())
            throw std::logic_error("speculative material image was not committed");
    // Merging preallocated std::map nodes does not create a new GPU object.
    // Image* pointers into moved map nodes remain stable.
    cache_->gpu_.merge(pending_);
    for(const auto& binding:bindings_) {
        Image& image=binding.model->images[binding.index];
        const Image& owner=*binding.sharedOwner;
        if(image.tex.valid() && image.tex!=owner.tex) {
            Image retired;
            retired.tex=image.tex;
            retired.borrowedTexture=false;
            // No allocation: redundant_ capacity was reserved in prepare.
            redundant_.push_back(std::move(retired));
        }
        image.tex=owner.tex;
        image.textureLease=owner.textureLease;
        image.borrowedTexture=true;
        image.gpuCompressed=owner.gpuCompressed;
    }
    committed_=true;
    return std::move(redundant_);
}

void ModelTextureCache::trim(gfx::Device& dev) {
    for(auto it=gpu_.begin();it!=gpu_.end();) {
        if(it->second.textureLease && it->second.textureLease.use_count()==1) {
            if(it->second.tex.valid())dev.destroyTexture(it->second.tex);
            it=gpu_.erase(it);
        } else ++it;
    }
    // Residency reclamation is infrequent, unlike normal image reads.
    // Clear decoded source pixels too: sharedRgba leases held by a pending
    // upload remain valid even after their map entry is removed. This can
    // return up to the configured 64 MiB CPU LRU budget to the allocator.
    std::lock_guard lock(mutex_);
    decoded_.clear();
    bytes_ = 0;
}
void ModelTextureCache::shutdown(gfx::Device& dev) {
    for (auto& [key, image] : gpu_) if (image.tex.valid()) dev.destroyTexture(image.tex);
    gpu_.clear();
    sourceRevisions_.clear();
    std::lock_guard lock(mutex_); decoded_.clear(); bytes_ = 0;
}

void prepareModelUpload(Model& m) {
    if (m.uploadPrepared || m.vertices.empty()) return;

    // One shared engine path for game clients, editor previews, tools and all model formats.
    // Geometry compaction is opt-in because arbitrary client-side pose changes cannot
    // be inferred from a static glTF/FBX hierarchy. An asset policy may declare
    // preserveNodes, and individual nodes can be tagged runtimeMutable.
    const auto countPrimitives = [&]() {
        size_t total = 0;
        for (const auto& node : m.nodes)
            if (node.mesh >= 0 && size_t(node.mesh) < m.meshes.size())
                total += m.meshes[size_t(node.mesh)].primitives.size();
        return total;
    };
    m.batching.primitivesBefore = countPrimitives();
    m.batching.compacted = false;
    // Opt-in experiment for rigid multi-part assets (cars, props). The merge
    // algorithm itself rejects skinned, animated, morph, variant, LOD and
    // runtimeMutable nodes. Leave disabled by default: untagged client-side
    // node animation cannot be inferred from imported model metadata.
    const char* automaticRigid = std::getenv("AIVERSE_AUTO_RIGID_MERGE");
    const bool rigidMerge = m.batching.mergeRigid ||
        (automaticRigid && std::strcmp(automaticRigid, "1") == 0);
    m.batching.reason = rigidMerge ? "" : "rigid compaction not enabled for asset";
    if (rigidMerge) {
        ModelMergeOptions options;
        options.maxExtent = m.batching.maxExtent;
        options.keepNodes = m.batching.preserveNodes;
        const auto merged = mergeStaticPrimitives(m, options);
        m.batching.compacted = merged.merged;
        m.batching.reason = merged.reason;
    }
    m.batching.primitivesAfter = countPrimitives();
    if (automaticRigid && std::strcmp(automaticRigid, "1") == 0 &&
        m.batching.compacted && m.batching.primitivesAfter < m.batching.primitivesBefore) {
        std::fprintf(stderr, "[rigid-merge] model=%s before=%zu after=%zu\n",
            m.path.c_str(), m.batching.primitivesBefore, m.batching.primitivesAfter);
    }

    // Generate missing imported LODs after final source tangents are
    // prepared, but before shadow/material batching and GPU upload. This
    // preserves LOD0 tangent values: the coarse index streams reuse the
    // original vertices and must not recompute their normals/tangents.
    ensureTangents(m);
    // Missing imported LODs are generated once per CPU model preparation,
    // before shadow/material batches and before GPU geometry upload.
    // Node hierarchy and LOD0 mesh data remain unchanged. An external
    // asset policy or AIVERSE_AUTOLOD=off restores authored-only behavior.
#ifdef AIVERSE_RUNTIME_AUTOLOD
    const char* lodFlag = std::getenv("AIVERSE_AUTOLOD");
    if (!lodFlag || (std::strcmp(lodFlag, "off") != 0 &&
                     std::strcmp(lodFlag, "0") != 0))
        generateMissingModelLods(m);
    else {
        m.autoLod.attempted = true;
        m.autoLod.reason = "disabled by AIVERSE_AUTOLOD";
    }
#else
    m.autoLod.attempted = true;
    m.autoLod.reason = "runtime automatic LOD disabled at build time";
#endif
    if (const char* trace = std::getenv("AIVERSE_AUTOLOD_TRACE"); trace && std::strcmp(trace, "1") == 0 &&
        isNativeModelFormat(m.path)) {
        std::fprintf(stderr, "[auto-lod] model=%s meshes=%zu generated=%zu added-indices=%llu reason=%s\n",
            m.path.c_str(), m.meshes.size(), m.autoLod.generatedLevels,
            (unsigned long long)m.autoLod.addedIndices, m.autoLod.reason.c_str());
    }

        const char* shadowBatches = std::getenv("AIVERSE_AUTO_SHADOW_BATCHES");
        if (!shadowBatches || std::string(shadowBatches) != "off") prepareShadowBatches(m);
        const char* materialBatches = std::getenv("AIVERSE_AUTO_MATERIAL_BATCHES");
        if (!materialBatches || std::string(materialBatches) != "off") prepareMaterialBatches(m);
    m.uploadPrepared = true;
    m.markGeometryChanged(); // runtime LOD / static / shadow / material batches
}

bool uploadModel(gfx::Device& dev, Model& m, bool keepCpu,
                 bool publishSharedTextures, bool keepPickingGeometry) {
    const bool hasCpuVertices = !m.vertices.empty();
    const bool needsGpuBuffers = hasCpuVertices || !m.indices.empty() ||
        m.vb.valid() || m.ib.valid() || bool(m.geometryPoolPage);
    // Recover an incomplete previous standalone upload without mistaking its
    // orphan VB/IB for a fully initialized Model. A shared-page Model always
    // acquires both handles together and must be released by its owner.
    if (m.vb.valid() != m.ib.valid()) {
        if (m.geometryPoolPage) return false;
        if (m.vb.valid()) dev.destroyBuffer(m.vb);
        if (m.ib.valid()) dev.destroyBuffer(m.ib);
        m.vb = {}; m.ib = {};
    }
    // CPU preparation can create morph aliases. Preflight the FINAL model
    // topology before allocating a single GPU buffer/texture.
    if(hasCpuVertices && !m.vb.valid())prepareModelUpload(m);
    for(const auto& mesh:m.meshes)
        for(const auto& primitive:mesh.primitives) {
            if(primitive.morphAliasMesh>=0) {
                const size_t sm=size_t(primitive.morphAliasMesh);
                if(primitive.morphAliasPrimitive<0 ||
                   sm>=m.meshes.size() ||
                   size_t(primitive.morphAliasPrimitive)>=m.meshes[sm].primitives.size() ||
                   !primitive.morphDeltas.empty() ||
                   m.meshes[sm].morphTargetNames!=mesh.morphTargetNames)
                    return false;
                const auto& source=m.meshes[sm].primitives[
                    size_t(primitive.morphAliasPrimitive)];
                if(source.morphAliasMesh>=0 || source.morphTargets<=0 ||
                   source.morphTargets!=primitive.morphTargets ||
                   source.firstVertex!=primitive.firstVertex ||
                   source.vertexCount!=primitive.vertexCount ||
                   (source.morphDeltas.empty() && !m.morphTex.valid()))
                    return false;
                continue;
            }
            if(primitive.morphAliasPrimitive>=0)return false;
            if(primitive.morphTargets<0 ||
               primitive.morphTargets>kMaxMorphTargets)return false;
            if(primitive.morphTargets==0) {
                if(!primitive.morphDeltas.empty())return false;
                continue;
            }
            if(primitive.vertexCount==0)return false;
            if(primitive.morphDeltas.empty()) {
                if(!m.morphTex.valid())return false;
                continue;
            }
            const size_t channels=size_t(primitive.morphTargets);
            const size_t vertices=size_t(primitive.vertexCount);
            if(vertices>std::numeric_limits<size_t>::max()/6u/channels ||
               primitive.morphDeltas.size()!=vertices*channels*6u)
                return false;
        }
    if (hasCpuVertices && !m.vb.valid()) {
        if (!uploadModelToGeometryPool(dev, m)) {
            gfx::BufferDesc vb; vb.type = gfx::BufferType::Vertex; vb.size = m.vertices.size() * sizeof(ModelVertex); vb.data = m.vertices.data(); vb.label = m.path.c_str();
            const gfx::BufferHandle newVb = dev.createBuffer(vb);
            if (!newVb.valid()) return false; // keep CPU arrays for retry
            gfx::BufferDesc ib; ib.type = gfx::BufferType::Index; ib.size = m.indices.size() * sizeof(uint32_t); ib.data = m.indices.data();
            const gfx::BufferHandle newIb = dev.createBuffer(ib);
            if (!newIb.valid()) {
                dev.destroyBuffer(newVb);
                return false; // do not publish a half-uploaded Model
            }
            m.vb = newVb;
            m.ib = newIb;
        }
    }
    if(hasCpuVertices && !m.geometryPoolPage &&
       (!m.vb.valid() || !m.ib.valid()))
        return false;
    // A single Image cannot have conflicting alpha-test policies or also be
    // linear data/blended alpha. Keep those cases and authored compressed/VT
    // mips unchanged instead of silently changing another material's content.
    std::vector<int> cutoutThreshold(m.images.size(),0);
    const auto disable=[&](int image){if(image>=0&&size_t(image)<cutoutThreshold.size())cutoutThreshold[size_t(image)]=-1;};
    for(const auto& material:m.materials) {
        for(int image:{material.mrTex,material.normalTex,material.occlusionTex,material.emissiveTex,material.teamTex})disable(image);
        if(material.alpha!=AlphaMode::Mask)disable(material.baseTex);
        for(int image:material.graphTextures)disable(image);
        if(material.alpha==AlphaMode::Mask&&material.baseTex>=0&&size_t(material.baseTex)<cutoutThreshold.size()&&
           std::isfinite(material.alphaCutoff)&&material.alphaCutoff>0&&
           std::isfinite(material.baseColor.w)&&material.baseColor.w>0&&material.alphaCutoff<=material.baseColor.w) {
            auto& threshold=cutoutThreshold[size_t(material.baseTex)];
            const int value=std::clamp(int(std::ceil(material.alphaCutoff/material.baseColor.w*255.f)),1,255);
            if(threshold==0)threshold=value;else if(threshold!=value)threshold=-1;
        }
    }
    const char* coverageOption=std::getenv("AIVERSE_ALPHA_COVERAGE_MIPS");
    const bool coverageEnabled=!(coverageOption&&std::strcmp(coverageOption,"0")==0);
    for(size_t i=0;i<m.images.size();++i) {
        auto& image=m.images[i];
        if(!image.tex.valid()) {
            const bool eligible=coverageEnabled&&image.preserveAlphaCoverage&&!image.nearest&&!image.compressed&&image.virtualFallbackMaxDimension==0;
            image.alphaCoverageThreshold=eligible&&cutoutThreshold[i]>0?uint8_t(cutoutThreshold[i]):0;
            image.alphaCoverageMipmaps=false;
        }
    }
    std::vector<bool> pickAlpha(m.images.size(),false);
    if(keepPickingGeometry)for(const auto& material:m.materials)
        if(material.alpha!=AlphaMode::Opaque&&material.baseTex>=0&&size_t(material.baseTex)<pickAlpha.size())pickAlpha[size_t(material.baseTex)]=true;
    size_t imageIndex=0;
    for (auto& img : m.images) {
        const auto previousTexture = img.tex;
        // Reduced VT fallbacks are per-Image resources and must not borrow
        // the full-resolution shared texture cache entry. Speculative model
        // publication still keeps transactional GPU ownership and retirement.
        if (img.virtualFallbackMaxDimension > 0) {
            uploadImage(dev, img);
        } else if (img.textureCache && publishSharedTextures) {
            img.textureCache->upload(dev, img);
        } else if (img.textureCache && img.textureCache->borrowResident(img)) {
            // Existing shared lease: a speculative Model does not own it.
        } else {
            // Speculative cache miss: use a dedicated transaction-owned image.
            // Do not publish this resource into shared cache before commit.
            uploadImage(dev, img);
        }
        if (!(previousTexture == img.tex))
            m.markMaterialChanged(); // texture binding changed during upload
        // A reduced VT fallback must materialize even when it has no resident
        // shared source. Other known-size images must also have GPU storage.
        if ((img.virtualFallbackMaxDimension > 0 ||
             (img.width > 0 && img.height > 0)) && !img.tex.valid())
            return false;
        if (!keepCpu && !pickAlpha[imageIndex]) {
            // Release CPU pixels only after successful upload. swap cannot
            // throw like shrink_to_fit, keeping rollback deterministic.
            std::vector<uint8_t>{}.swap(img.rgba);
            img.compressed.reset();
            img.sharedRgba.reset();
        }
        ++imageIndex;
    }
    // Two RGBA32F texels per (morph channel, mesh vertex). The texture
    // base offset is supplied to GLSL/MSL as a FLOAT uniform, which has
    // exact integer precision only up to 2^24. Cap this texture accordingly
    // rather than silently sampling the wrong channel on large facial rigs.
    constexpr size_t kMorphWidth=2048u;
    constexpr size_t kMaxMorphTexels=1u<<24; // 256 MiB RGBA32F, float-exact offsets
    size_t morphBudgetTexels=kMaxMorphTexels;
    // Optional project/device GPU morph budget in MiB. Smaller budgets
    // reject an oversized asset BEFORE allocation; no silent morph
    // channel truncation or LOD-pose corruption. RHI may impose even lower
    // maximum texture dimensions and can still reject createTexture().
    if(const char* budget=std::getenv("AIVERSE_MORPH_TEXTURE_MIB")) {
        char* end=nullptr;
        const unsigned long mib=std::strtoul(budget,&end,10);
        if(end==budget || *end!='\0' || mib<1 || mib>256)return false;
        morphBudgetTexels=std::min(
            morphBudgetTexels,size_t(mib)*(1u<<20)/16u);
    }
    size_t texels=0;
    for(const auto& mesh:m.meshes)
        for(const auto& primitive:mesh.primitives)
            if(primitive.morphTargets>0 && !primitive.morphDeltas.empty()) {
                const size_t added=primitive.morphDeltas.size()/3u;
                if(added>morphBudgetTexels-texels)return false;
                texels+=added;
            }
    if (texels > 0 && !m.morphTex.valid()) {
        const size_t rows=(texels+kMorphWidth-1u)/kMorphWidth;
        const int width=int(kMorphWidth),height=int(rows);
        std::vector<float> data(kMorphWidth*rows*4u, 0.0f);
        size_t at = 0;
        for (auto& mesh : m.meshes)
            for (auto& p : mesh.primitives) {
                if (p.morphTargets <= 0 || p.morphDeltas.empty()) continue;
                p.morphTexel = uint32_t(at);
                for (size_t i = 0; i + 2 < p.morphDeltas.size(); i += 3, ++at)
                    for (int k = 0; k < 3; ++k) data[at * 4 + size_t(k)] = p.morphDeltas[i + size_t(k)];
            }
        gfx::TextureDesc td;
        td.width = width; td.height = height;
        td.format = gfx::PixelFormat::RGBA32F;
        td.filter = gfx::Filter::Nearest;
        td.data = data.data();
        td.label = "morph targets";
        m.morphTex = dev.createTexture(td);
        if (!m.morphTex.valid()) return false;
        m.morphTexWidth = width;
    }
    // Resolve generated coarse primitives AFTER source offsets were assigned
    // by the only RGBA32F allocation; aliases never consume extra texels.
    for(auto& mesh:m.meshes)
        for(auto& p:mesh.primitives)
            if(p.morphAliasMesh>=0)
                p.morphTexel=m.meshes[size_t(p.morphAliasMesh)]
                    .primitives[size_t(p.morphAliasPrimitive)].morphTexel;
    if (!keepCpu && !keepPickingGeometry) {
        // Never discard morph deltas before a successful GPU morph texture
        // allocation. Failed uploads retain their CPU source for diagnostics
        // and safe retry/rollback by the host.
        if (m.morphTex.valid()) {
            for (auto& mesh : m.meshes)
                for (auto& p : mesh.primitives)
                    if (p.morphTargets > 0 && !p.morphDeltas.empty()) {
                        std::vector<float>{}.swap(p.morphDeltas);
                    }
        }
        std::vector<ModelVertex>{}.swap(m.vertices);
        std::vector<uint32_t>{}.swap(m.indices);
    }
    // Validate original upload request after possible CPU pixel cleanup.
    return !needsGpuBuffers || bool(m.geometryPoolPage) ||
           (m.vb.valid() && m.ib.valid());
}

bool uploadStandaloneImage(gfx::Device& dev, Image& image) {
    uploadImage(dev,image);
    return image.tex.valid();
}

int addImage(gfx::Device& dev, Model& m, Image img) {
    for (size_t i = 0; i < m.images.size(); ++i)
        if (!img.name.empty() && m.images[i].name == img.name)
            return int(i);
    // Preallocate Model storage before allocating a GPU texture; a vector
    // growth failure after createTexture could otherwise drop a live handle.
    m.images.reserve(m.images.size()+1);
    uploadImage(dev, img);
    if (!img.tex.valid()) return -1; // never publish an invalid material index
    // Empty-vector swap frees CPU pixels without a potentially throwing
    // shrink_to_fit after the new GPU resource has been allocated.
    std::vector<uint8_t>{}.swap(img.rgba);
    m.images.push_back(std::move(img));
    m.markMaterialChanged(); // new texture may be selected by model materials
    return int(m.images.size() - 1);
}

void appendModelOwnedGpuMemory(const Model& m,
                               GpuOwnedMemoryCounter& counter) {
    // Shared pooled geometry is owned and counted by GlobalGeometryPool.
    if (!m.geometryPoolPage) {
        counter.buffer(m.vb);
        counter.buffer(m.ib);
    }
    for (const auto& image : m.images)
        // releaseModel frees precisely those images whose GPU handle is
        // not leased from another owner; mirror its actual ownership flag.
        if (!image.borrowedTexture)
            counter.texture(image.tex);
    counter.texture(m.morphTex);
    counter.texture(m.baked.tex);
}

void releaseModel(gfx::Device& dev, Model& m) {
    if (m.geometryPoolPage) releaseModelGeometryPool(dev, m);
    else {
        if (m.vb.valid()) dev.destroyBuffer(m.vb);
        if (m.ib.valid()) dev.destroyBuffer(m.ib);
    }
    m.vb = {}; m.ib = {};
    for (auto& img : m.images) {
        if (img.tex.valid() && !img.borrowedTexture) dev.destroyTexture(img.tex);
        if (img.virtualAlphaMask.valid())
            dev.destroyTexture(img.virtualAlphaMask);
        img.virtualAlphaMask={};
        img.tex = {};
        // Drop the shared cache lease at explicit release, not only when the
        // Model object itself happens to be destroyed. The texture cache's
        // trim() can then free images no remaining model references.
        img.textureLease.reset();
        img.borrowedTexture = false;
    }
    if (m.morphTex.valid()) dev.destroyTexture(m.morphTex);
    if (m.baked.tex.valid()) dev.destroyTexture(m.baked.tex);
    m.morphTex = {}; m.baked = Model::Baked{};
    m.markGeometryChanged(); // later upload may reuse the same GPU handle values
    m.markMaterialChanged(); // texture bindings removed; handles may be reused
}

} // namespace aiverse::render
