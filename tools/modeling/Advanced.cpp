#include "Modeling.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <set>
namespace aiverse::modeling {
namespace {
using Key = std::array<int64_t, 3>;
Key key(float3 p) {
    return {int64_t(std::llround(p.x * 100000)), int64_t(std::llround(p.y * 100000)),
            int64_t(std::llround(p.z * 100000))};
}
Mesh weld(const Mesh &m) {
    Mesh out;
    std::map<Key, uint32_t> ids;
    std::vector<uint32_t> map;
    for (auto v : m.vertices) {
        auto [it, added] = ids.emplace(key(v.pos), uint32_t(out.vertices.size()));
        if (added)
            out.vertices.push_back(v);
        map.push_back(it->second);
    }
    for (auto i : m.indices)
        out.indices.push_back(map[i]);
    return out;
}
void tri(Mesh &out, float3 a, float3 b, float3 c) {
    auto crossN = cross(b - a, c - a);
    if (length(crossN) < 1e-9f)
        return;
    auto n = normalize(crossN);
    for (auto p : {a, b, c}) {
        ModelVertex v{};
        v.pos = p;
        v.nrm = n;
        out.indices.push_back(uint32_t(out.vertices.size()));
        out.vertices.push_back(v);
    }
}
} // namespace
void bevelConvex(Mesh &mesh, float amount) {
    require(amount > 0, "bevel amount must be positive");
    require(mesh.indices.size() <= 15000, "convex bevel input limited to 5000 triangles");
    auto m = weld(mesh);
    using Edge = std::pair<uint32_t, uint32_t>;
    std::map<Edge, std::vector<float3>> edges;
    std::vector<std::vector<float3>> faces;
    for (size_t i = 0; i < m.indices.size(); i += 3) {
        uint32_t a = m.indices[i], b = m.indices[i + 1], c = m.indices[i + 2];
        auto pa = m.vertices[a].pos, pb = m.vertices[b].pos, pc = m.vertices[c].pos;
        auto n = normalize(cross(pb - pa, pc - pa));
        float w = dot(n, pa);
        for (auto &v : m.vertices)
            require(dot(n, v.pos) - w <= 1e-4f, "edge bevel currently requires a convex closed mesh");
        faces.push_back({pa, pb, pc});
        for (auto e : {Edge{a, b}, Edge{b, c}, Edge{c, a}}) {
            if (e.first > e.second)
                std::swap(e.first, e.second);
            edges[e].push_back(n);
        }
    }
    std::vector<std::pair<float3, float>> planes;
    for (auto &[e, n] : edges) {
        require(n.size() == 2, "bevel requires closed manifold topology");
        if (dot(n[0], n[1]) > .9999f)
            continue;
        auto sum = n[0] + n[1];
        require(length(sum) > 1e-5f, "degenerate bevel edge");
        auto normal = normalize(sum);
        planes.push_back({normal, dot(normal, m.vertices[e.first].pos) - amount});
    }
    require(planes.size() <= 512, "bevel edge budget exceeded");
    for (auto &[normal, d] : planes) {
        std::vector<std::vector<float3>> clipped;
        std::map<Key, float3> cuts;
        for (auto &f : faces) {
            std::vector<float3> next;
            for (size_t i = 0; i < f.size(); ++i) {
                auto a = f[i], b = f[(i + 1) % f.size()];
                float da = dot(normal, a) - d, db = dot(normal, b) - d;
                bool insideA = da <= 1e-6f, insideB = db <= 1e-6f;
                if (insideA)
                    next.push_back(a);
                if (insideA != insideB) {
                    auto p = lerp(a, b, da / (da - db));
                    next.push_back(p);
                    cuts[key(p)] = p;
                }
            }
            if (next.size() >= 3)
                clipped.push_back(std::move(next));
        }
        if (cuts.size() >= 3) {
            std::vector<float3> cap;
            float3 center{};
            for (auto &[k, p] : cuts) {
                (void)k;
                cap.push_back(p);
                center += p;
            }
            center = center * (1.f / float(cap.size()));
            auto u = normalize(cross(std::abs(normal.y) < .9f ? float3{0, 1, 0} : float3{1, 0, 0}, normal)),
                 v = cross(normal, u);
            std::sort(cap.begin(), cap.end(), [&](float3 a, float3 b) {
                a = a - center;
                b = b - center;
                return std::atan2(dot(a, v), dot(a, u)) < std::atan2(dot(b, v), dot(b, u));
            });
            clipped.push_back(std::move(cap));
        }
        faces = std::move(clipped);
        require(!faces.empty(), "bevel amount removes the entire mesh");
    }
    Mesh out;
    for (auto &f : faces)
        for (size_t i = 1; i + 1 < f.size(); ++i)
            tri(out, f[0], f[i], f[i + 1]);
    require(!out.indices.empty(), "empty bevel result");
    mesh = std::move(out);
    modifier(mesh, Json::parse(R"({"kind":"uv","mode":"box"})"));
}
void smoothSubdivision(Mesh &mesh, int levels) {
    require(levels >= 1 && levels <= 4, "smooth subdivision levels must be 1..4");
    auto m = weld(mesh);
    for (int level = 0; level < levels; ++level) {
        require(m.indices.size() * 4 <= 1500000, "subdivision budget exceeded");
        using Edge = std::pair<uint32_t, uint32_t>;
        std::map<Edge, std::vector<uint32_t>> edges;
        std::vector<std::set<uint32_t>> neighbors(m.vertices.size()), boundary(m.vertices.size());
        for (size_t i = 0; i < m.indices.size(); i += 3)
            for (size_t k = 0; k < 3; ++k) {
                auto a = m.indices[i + k], b = m.indices[i + (k + 1) % 3], op = m.indices[i + (k + 2) % 3];
                neighbors[a].insert(b);
                neighbors[b].insert(a);
                if (a > b)
                    std::swap(a, b);
                edges[{a, b}].push_back(op);
            }
        for (auto &[e, op] : edges) {
            require(op.size() <= 2, "subdivision requires manifold edges");
            if (op.size() == 1) {
                boundary[e.first].insert(e.second);
                boundary[e.second].insert(e.first);
            }
        }
        Mesh out;
        out.vertices = m.vertices;
        for (size_t i = 0; i < m.vertices.size(); ++i) {
            auto p = m.vertices[i].pos;
            if (!boundary[i].empty()) {
                require(boundary[i].size() == 2, "invalid boundary vertex");
                auto sum = float3{};
                for (auto n : boundary[i])
                    sum += m.vertices[n].pos;
                out.vertices[i].pos = p * .75f + sum * .125f;
            } else {
                size_t count = neighbors[i].size();
                require(count >= 3, "invalid subdivision valence");
                float beta = count == 3 ? 3.f / 16.f : 3.f / (8.f * float(count));
                auto sum = float3{};
                for (auto n : neighbors[i])
                    sum += m.vertices[n].pos;
                out.vertices[i].pos = p * (1 - float(count) * beta) + sum * beta;
            }
        }
        std::map<Edge, uint32_t> mid;
        for (auto &[e, op] : edges) {
            auto a = m.vertices[e.first].pos, b = m.vertices[e.second].pos;
            ModelVertex v{};
            v.pos = op.size() == 1 ? (a + b) * .5f
                                   : (a + b) * .375f + (m.vertices[op[0]].pos + m.vertices[op[1]].pos) * .125f;
            mid[e] = uint32_t(out.vertices.size());
            out.vertices.push_back(v);
        }
        auto middle = [&](uint32_t a, uint32_t b) {
            if (a > b)
                std::swap(a, b);
            return mid.at({a, b});
        };
        for (size_t i = 0; i < m.indices.size(); i += 3) {
            auto a = m.indices[i], b = m.indices[i + 1], c = m.indices[i + 2], ab = middle(a, b), bc = middle(b, c),
                 ca = middle(c, a);
            out.indices.insert(out.indices.end(), {a, ab, ca, ab, b, bc, ca, bc, c, ab, bc, ca});
        }
        m = std::move(out);
    }
    mesh = std::move(m);
    modifier(mesh, Json::parse(R"({"kind":"normals"})"));
    modifier(mesh, Json::parse(R"({"kind":"uv","mode":"box"})"));
}
} // namespace aiverse::modeling
