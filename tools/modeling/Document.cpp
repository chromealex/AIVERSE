#include "Modeling.h"
#include "engine/render/AnimationGraph.h"
#include <algorithm>
#include <cmath>
#include <set>
#include <map>
namespace aiverse::modeling {
void require(bool c, const std::string &s) {
    if (!c)
        throw std::runtime_error("modeling: " + s);
}
float number(const Json &v, float d, const std::string &p) {
    if (v.isNull())
        return d;
    require(v.isNumber() && std::isfinite(v.toDouble()) && std::abs(v.toDouble()) <= 1000000,
            p + ": expected finite number within +/-1000000");
    return float(v.toDouble());
}
float3 vector3(const Json &v, float3 d, const std::string &p) {
    if (v.isNull())
        return d;
    require(v.isArray() && v.size() == 3, p + ": expected three numbers");
    return {number(v[0], 0, p), number(v[1], 0, p), number(v[2], 0, p)};
}
namespace {
void id(const std::string &s) {
    require(!s.empty() && s.size() <= 96 &&
                s.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-") ==
                    std::string::npos,
            "invalid identifier '" + s + "'");
}
Json resolved(const Json &j, const Json &params, int depth = 0) {
    require(depth < 32, "document nesting limit");
    if (j.isObject() && j.has("param")) {
        auto name = j.getStr("param");
        require(params.has(name), "unknown parameter '" + name + "'");
        return Json::number(double(number(params[name], 0, name) * number(j["scale"], 1, "parameter scale") +
                                   number(j["offset"], 0, "parameter offset")),
                            8);
    }
    if (j.isArray()) {
        auto a = Json::array();
        for (auto &v : j.items())
            a.push(resolved(v, params, depth + 1));
        return a;
    }
    if (j.isObject()) {
        auto a = Json::object();
        for (auto &[k, v] : j.members())
            a.set(k, resolved(v, params, depth + 1));
        return a;
    }
    return j;
}
quat rotation(const Json &j) {
    if (j.isNull())
        return {};
    require(j.isArray() && j.size() == 4, "rotation must be quaternion [x,y,z,w]");
    quat q{number(j[0], 0, "rotation"), number(j[1], 0, "rotation"), number(j[2], 0, "rotation"),
           number(j[3], 1, "rotation")};
    require(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w > 1e-8f, "zero quaternion");
    return qnormalize(q);
}
} // namespace
Json evaluateDocument(const Json &doc) { return resolved(doc, doc["parameters"]); }
Json emptyDocument() {
    return Json::parse(
        R"({"version":1,"parameters":{},"nodes":[],"materials":[{"id":"default","color":[0.65,0.7,0.8,1]}],"clips":[],"skins":[],"graph":null})");
}
Json catalog() {
    return Json::parse(
        R"({"version":1,"geometry":["box","bevelBox","sphere","cylinder","cone","lathe","extrude","sweep","mesh","boolean"],"modifiers":["bevel","smoothSubdivision","transform","uvAtlas","mirror","array","subdivide","simplify","normals","uv"],"boolean":["union","subtract","intersect"],"uv":["box","planar","cylindrical"],"edit":["put","remove","parameters","graph","replace"],"collections":["nodes","materials","clips","skins"],"limits":{"nodes":512,"vertices":500000,"indices":1500000,"keys":100000,"history":32},"conventions":{"up":"Y","forward":"+Z","rotation":"quaternion xyzw","units":"metres","rig":"existing AnimationGraph format"},"notes":["subdivide is linear triangle subdivision","simplify is vertex clustering; inspect topology after use","bevelBox rounds a box; bevel chamfers all noncoplanar edges of convex closed meshes","UV projections can overlap; uvAtlas uses a separate nonoverlapping chart per triangle","CSG requires closed, consistently wound input; coplanar tolerance 0.00001","Motion Matching selects existing frames; it does not generate clips"]})");
}
Json edit(const Json &doc, const Json &commands) {
    require(commands.isArray() && commands.size() <= 256, "commands must contain at most 256 edits");
    Json next = doc;
    for (auto &c : commands.items()) {
        auto op = c.getStr("op");
        if (op == "replace") {
            require(c["document"].isObject(), "replacement must be document");
            next = c["document"];
            continue;
        }
        if (op == "parameters") {
            require(c["values"].isObject(), "parameter values must be object");
            for (auto &[k, v] : c["values"].members()) {
                id(k);
                number(v, 0, k);
                next.member("parameters").set(k, v);
            }
            continue;
        }
        if (op == "graph") {
            next.set("graph", c["value"]);
            continue;
        }
        auto collection = c.getStr("collection");
        require(collection == "nodes" || collection == "materials" || collection == "clips" || collection == "skins",
                "unknown edit collection");
        auto key = c.getStr("id");
        id(key);
        auto &arr = next.member(collection);
        require(arr.isArray(), "collection must be array");
        size_t i = 0;
        while (i < arr.size() && arr[i].getStr("id") != key)
            ++i;
        if (op == "remove") {
            require(i < arr.size(), "missing item '" + key + "'");
            arr.eraseAt(i);
        } else if (op == "put") {
            auto v = c["value"];
            require(v.isObject(), "put value must be object");
            v.set("id", Json::string(key));
            if (i == arr.size())
                arr.push(v);
            else
                arr.at(i) = v;
        } else
            require(false, "unknown edit operation");
    }
    return next;
}
Build build(const Json &input, const std::atomic<bool> *cancel, GeometryCache *cache) {
    require(input.isObject() && input.getInt("version") == 1, "unsupported document version");
    require(input.dump(-1).size() <= 16 * 1024 * 1024, "document exceeds 16 MiB");
    auto doc = evaluateDocument(input);
    require(doc["nodes"].isArray() && doc["nodes"].size() <= 512, "nodes must be array of at most 512 entries");
    auto m = std::make_unique<Model>();
    m->path = "authoring";
    std::map<std::string, int> nodes, materials;
    Json warnings = Json::array();
    for (auto &j : doc["materials"].items()) {
        auto key = j.getStr("id");
        id(key);
        require(!materials.count(key), "duplicate material '" + key + "'");
        Material mt;
        mt.name = key;
        auto color = j["color"];
        if (!color.isNull()) {
            require(color.isArray() && (color.size() == 3 || color.size() == 4),
                    "material color must have 3 or 4 values");
            mt.baseColor = {number(color[0], 1, key), number(color[1], 1, key), number(color[2], 1, key),
                            color.size() == 4 ? number(color[3], 1, key) : 1};
            require(mt.baseColor.x >= 0 && mt.baseColor.y >= 0 && mt.baseColor.z >= 0 && mt.baseColor.w >= 0 &&
                        mt.baseColor.w <= 1,
                    "invalid material color");
        }
        mt.metallic = number(j["metallic"], 0, key);
        mt.roughness = number(j["roughness"], .7f, key);
        require(mt.metallic >= 0 && mt.metallic <= 1 && mt.roughness >= 0 && mt.roughness <= 1,
                "material factors must be 0..1");
        mt.doubleSided = j.getBool("doubleSided");
        mt.emissive = vector3(j["emissive"], {}, key);
        mt.alpha = mt.baseColor.w < 1 ? AlphaMode::Blend : AlphaMode::Opaque;
        materials[key] = int(m->materials.size());
        m->materials.push_back(mt);
    }
    if (m->materials.empty()) {
        Material mt;
        mt.name = "default";
        materials[mt.name] = 0;
        m->materials.push_back(mt);
    }
    for (auto &j : doc["nodes"].items()) {
        auto key = j.getStr("id");
        id(key);
        require(!nodes.count(key), "duplicate node '" + key + "'");
        nodes[key] = int(m->nodes.size());
        Node n;
        n.name = key;
        n.t = vector3(j["translation"], {}, key);
        n.r = rotation(j["rotation"]);
        n.s = vector3(j["scale"], {1, 1, 1}, key);
        require(n.s.x > 1e-6f && n.s.y > 1e-6f && n.s.z > 1e-6f,
                key + ": scales must be positive; use mirror modifier");
        m->nodes.push_back(n);
    }
    for (size_t i = 0; i < m->nodes.size(); ++i) {
        auto &j = doc["nodes"][i];
        auto parent = j.getStr("parent");
        if (!parent.empty()) {
            require(nodes.count(parent), "missing parent '" + parent + "'");
            m->nodes[i].parent = nodes.at(parent);
        }
        std::set<int> seen;
        int cur = int(i);
        while (cur >= 0) {
            require(seen.insert(cur).second, "node hierarchy cycle");
            auto p = doc["nodes"][size_t(cur)].getStr("parent");
            if (p.empty())
                break;
            require(nodes.count(p), "missing parent '" + p + "'");
            cur = nodes.at(p);
        }
    }
    std::map<std::string, Mesh> built;
    size_t generatedVertices = 0;
    for (size_t i = 0; i < m->nodes.size(); ++i) {
        if (cancel && cancel->load())
            throw std::runtime_error("build cancelled");
        auto &j = doc["nodes"][i];
        if (!j.has("geometry"))
            continue;
        auto g = j["geometry"];
        Mesh mesh;
        if (g.getStr("kind") == "boolean") {
            auto a = g.getStr("a"), b = g.getStr("b");
            require(built.count(a) && built.count(b), "boolean operands must precede result");
            mesh = booleanMesh(built.at(a), built.at(b), g.getStr("operation", "union"));
            for (auto &mod : g["modifiers"].items())
                modifier(mesh, mod);
        } else {
            auto key = g.dump(-1);
            bool found = false;
            if (cache && key.size() < 65536) {
                std::lock_guard lock(cache->mutex);
                auto it = cache->meshes.find(key);
                if (it != cache->meshes.end()) {
                    mesh = it->second;
                    found = true;
                }
            }
            if (!found) {
                mesh = geometry(g, cancel);
                if (cache && key.size() < 65536) {
                    std::lock_guard lock(cache->mutex);
                    while (!cache->meshes.empty() && cache->vertices + mesh.vertices.size() > 500000) {
                        auto it = cache->meshes.begin();
                        cache->vertices -= it->second.vertices.size();
                        cache->meshes.erase(it);
                    }
                    auto [it, added] = cache->meshes.emplace(key, mesh);
                    if (added)
                        cache->vertices += mesh.vertices.size();
                }
            }
        }
        // Boolean operands are evaluated in their node-local geometry coordinates. Explicit offsets are geometry
        // modifiers.
        generatedVertices += mesh.vertices.size();
        require(generatedVertices <= 1000000, "construction geometry budget exceeded");
        built[m->nodes[i].name] = mesh;
        if (j.getBool("construction"))
            continue;
        require(m->vertices.size() + mesh.vertices.size() <= 500000 &&
                    m->indices.size() + mesh.indices.size() <= 1500000,
                "model geometry budget exceeded");
        auto mat = j.getStr("material", m->materials.front().name);
        require(materials.count(mat), "unknown material '" + mat + "'");
        Primitive p;
        p.material = materials.at(mat);
        p.firstVertex = uint32_t(m->vertices.size());
        p.vertexCount = uint32_t(mesh.vertices.size());
        p.firstIndex = uint32_t(m->indices.size());
        p.indexCount = uint32_t(mesh.indices.size());
        m->vertices.insert(m->vertices.end(), mesh.vertices.begin(), mesh.vertices.end());
        for (auto ix : mesh.indices)
            m->indices.push_back(p.firstVertex + ix);
        generateTangents(m->vertices, m->indices, p.firstIndex, p.indexCount);
        MeshDef md;
        md.name = m->nodes[i].name;
        md.primitives.push_back(p);
        m->nodes[i].mesh = int(m->meshes.size());
        m->meshes.push_back(md);
    }
    m->finalize();
    for (const auto &matrix : m->bindWorld)
        for (float f : matrix.m)
            require(std::isfinite(f) && std::abs(f) <= 1e9f, "hierarchy transform exceeds numeric limits");
    std::map<std::string, int> skins;
    for (auto &j : doc["skins"].items()) {
        auto key = j.getStr("id");
        id(key);
        require(!skins.count(key), "duplicate skin");
        Skin skin;
        skin.name = key;
        require(j["joints"].isArray() && j["joints"].size() > 0 && j["joints"].size() <= 80,
                "skin requires 1..80 joints");
        std::set<int> unique;
        for (auto &v : j["joints"].items()) {
            require(nodes.count(v.str()), "unknown skin joint");
            int n = nodes.at(v.str());
            require(unique.insert(n).second, "duplicate skin joint");
            skin.joints.push_back(n);
            skin.inverseBind.push_back(m->bindWorld[size_t(n)].inverse());
        }
        skins[key] = int(m->skins.size());
        m->skins.push_back(skin);
    }
    for (size_t i = 0; i < m->nodes.size(); ++i) {
        auto &j = doc["nodes"][i];
        auto key = j.getStr("skin");
        if (key.empty())
            continue;
        require(skins.count(key) && m->nodes[i].mesh >= 0, "skin requires known skin and visible mesh");
        m->nodes[i].skin = skins.at(key);
        auto &skin = m->skins[size_t(m->nodes[i].skin)];
        auto &p = m->meshes[size_t(m->nodes[i].mesh)].primitives[0];
        for (size_t k = 0; k < skin.joints.size(); ++k)
            p.jointPalette.push_back(uint16_t(k));
        auto weights = j["weights"];
        require(weights.isNull() || (weights.isArray() && weights.size() == p.vertexCount),
                "weights must match generated vertex count");
        auto rigid = j.getStr("bindJoint");
        int rigidIndex = 0;
        if (!rigid.empty()) {
            require(nodes.count(rigid), "unknown bind joint");
            auto it = std::find(skin.joints.begin(), skin.joints.end(), nodes.at(rigid));
            require(it != skin.joints.end(), "bind joint absent from skin");
            rigidIndex = int(it - skin.joints.begin());
        }
        for (uint32_t k = 0; k < p.vertexCount; ++k) {
            auto &v = m->vertices[p.firstVertex + k];
            if (weights.isNull()) {
                v.joints[0] = uint8_t(rigidIndex);
                v.weights[0] = 255;
            } else {
                auto row = weights[k];
                require(row.isArray() && row.size() > 0 && row.size() <= 4, "vertex needs 1..4 influences");
                float sum = 0;
                std::vector<float> w;
                std::set<int> used;
                for (auto &pair : row.items()) {
                    require(pair.isArray() && pair.size() == 2, "weight must be [jointIndex,weight]");
                    int ix = int(pair[0].integer(-1));
                    require(ix >= 0 && size_t(ix) < skin.joints.size() && used.insert(ix).second,
                            "invalid weight joint");
                    float f = number(pair[1], 0, "weight");
                    require(f >= 0, "negative weight");
                    w.push_back(f);
                    sum += f;
                }
                require(sum > 0, "zero weight sum");
                int remaining = 255;
                for (size_t z = 0; z < row.size(); ++z) {
                    int q = z + 1 == row.size() ? remaining : std::min(remaining, int(std::lround(w[z] / sum * 255)));
                    v.joints[z] = uint8_t(row[z][0].integer());
                    v.weights[z] = uint8_t(q);
                    remaining -= q;
                }
            }
        }
        // Skin vertices live in model space, matching inverse bind matrices.
        auto xf = m->bindWorld[i];
        for (uint32_t k = 0; k < p.vertexCount; ++k) {
            auto &v = m->vertices[p.firstVertex + k];
            v.pos = xf.transformPoint(v.pos);
            auto inverse = xf.inverse();
            auto n = v.nrm;
            v.nrm = normalize(float3{inverse.at(0, 0) * n.x + inverse.at(1, 0) * n.y + inverse.at(2, 0) * n.z,
                                     inverse.at(0, 1) * n.x + inverse.at(1, 1) * n.y + inverse.at(2, 1) * n.z,
                                     inverse.at(0, 2) * n.x + inverse.at(1, 2) * n.y + inverse.at(2, 2) * n.z});
        }
    }
    size_t totalKeys = 0;
    std::set<std::string> clipIds;
    for (auto &j : doc["clips"].items()) {
        AnimClip clip;
        clip.name = j.getStr("id");
        id(clip.name);
        require(clipIds.insert(clip.name).second, "duplicate clip");
        require(j["channels"].isArray() && j["channels"].size() <= 2048, "invalid channels");
        std::set<std::pair<int, int>> channels;
        for (auto &track : j["channels"].items()) {
            AnimChannel c;
            require(nodes.count(track.getStr("node")), "unknown animation node");
            c.node = nodes.at(track.getStr("node"));
            auto path = track.getStr("path", "rotation");
            require(path == "rotation" || path == "translation" || path == "scale", "unsupported channel path");
            c.path = path == "rotation"      ? AnimChannel::Rotation
                     : path == "translation" ? AnimChannel::Translation
                                             : AnimChannel::Scale;
            c.comps = path == "rotation" ? 4 : 3;
            require(channels.insert({c.node, int(c.path)}).second, "duplicate animation channel");
            auto interp = track.getStr("interpolation", "linear");
            require(interp == "linear" || interp == "step" || interp == "cubic", "invalid interpolation");
            c.interp = interp == "step"    ? AnimChannel::Step
                       : interp == "cubic" ? AnimChannel::CubicSpline
                                           : AnimChannel::Linear;
            auto &times = track["times"];
            auto &values = track["values"];
            totalKeys += times.size();
            require(times.isArray() && times.size() > 0 && totalKeys <= 100000, "key budget exceeded or missing keys");
            require(values.isArray() && values.size() == times.size() * (interp == "cubic" ? 3 : 1),
                    "key values count mismatch");
            float prev = -1;
            for (auto &t : times.items()) {
                float f = number(t, 0, "key time");
                require(f >= 0 && f > prev, "key times must strictly increase");
                c.times.push_back(f);
                prev = f;
            }
            for (size_t k = 0; k < values.size(); ++k) {
                auto &v = values[k];
                require(v.isArray() && v.size() == c.comps, "key value component mismatch");
                if (c.path == AnimChannel::Rotation && (interp != "cubic" || k % 3 == 1)) {
                    auto q = rotation(v);
                    for (float f : {q.x, q.y, q.z, q.w})
                        c.values.push_back(f);
                } else
                    for (auto &v2 : v.items())
                        c.values.push_back(number(v2, 0, "key value"));
            }
            clip.duration = std::max(clip.duration, prev);
            clip.channels.push_back(c);
        }
        m->clips.push_back(clip);
    }
    for (auto &mesh : m->meshes)
        for (auto &p : mesh.primitives)
            generateTangents(m->vertices, m->indices, p.firstIndex, p.indexCount);
    m->finalize();
    if (!doc["graph"].isNull())
        AnimationGraph::compile(*m, doc["graph"]);
    Json bounds = Json::object();
    auto vectorJson = [](float3 p) {
        auto j = Json::array();
        for (float v : {p.x, p.y, p.z})
            j.push(Json::number(double(v), 8));
        return j;
    };
    bounds.set("min", vectorJson(m->boundsMin));
    bounds.set("max", vectorJson(m->boundsMax));
    size_t degenerates = 0;
    double surface = 0;
    for (size_t i = 0; i < m->indices.size(); i += 3) {
        auto a = m->vertices[m->indices[i]].pos, b = m->vertices[m->indices[i + 1]].pos,
             c = m->vertices[m->indices[i + 2]].pos;
        float area = length(cross(b - a, c - a)) * .5f;
        if (area < 1e-9f)
            ++degenerates;
        surface += area;
    }
    if (degenerates)
        warnings.push(Json::string(std::to_string(degenerates) + " degenerate triangles"));
    Json report = Json::object();
    report.set("bounds", bounds);
    report.set("degenerateTriangles", Json::number(int64_t(degenerates)));
    report.set("localSurfaceArea", Json::number(surface, 6));
    report.set("estimatedDraws", Json::number(int64_t(m->meshes.size())));
    report.set("vertices", Json::number(int64_t(m->vertices.size())));
    report.set("triangles", Json::number(int64_t(m->indices.size() / 3)));
    report.set("nodes", Json::number(int64_t(m->nodes.size())));
    report.set("clips", Json::number(int64_t(m->clips.size())));
    report.set("warnings", warnings);
    report.set("ok", Json::fromBool(true));
    return {std::move(m), report};
}
} // namespace aiverse::modeling
