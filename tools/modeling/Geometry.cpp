#include "Modeling.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <array>
#include <numeric>
namespace aiverse::modeling {
namespace {
constexpr float pi = 3.14159265358979323846f;
void budget(const Mesh &m) {
    require(m.vertices.size() <= 500000 && m.indices.size() <= 1500000, "geometry budget exceeded");
}
void finiteMesh(const Mesh &m) {
    for (const auto &v : m.vertices)
        for (float f : {v.pos.x, v.pos.y, v.pos.z, v.nrm.x, v.nrm.y, v.nrm.z, v.uv.x, v.uv.y})
            require(std::isfinite(f) && std::abs(f) <= 1e9f, "geometry contains nonfinite or out-of-range coordinates");
}
void triangle(Mesh &m, float3 a, float3 b, float3 c, float2 ua = {0, 0}, float2 ub = {1, 0}, float2 uc = {0, 1}) {
    auto n = cross(b - a, c - a);
    if (length(n) < 1e-8f)
        return;
    n = normalize(n);
    for (auto pair : {std::pair{a, ua}, std::pair{b, ub}, std::pair{c, uc}}) {
        ModelVertex v{};
        v.pos = pair.first;
        v.nrm = n;
        v.uv = pair.second;
        m.indices.push_back(uint32_t(m.vertices.size()));
        m.vertices.push_back(v);
    }
    budget(m);
}
void quad(Mesh &m, float3 a, float3 b, float3 c, float3 d) {
    triangle(m, a, b, c, {0, 0}, {1, 0}, {1, 1});
    triangle(m, a, c, d, {0, 0}, {1, 1}, {0, 1});
}
std::vector<float2> profile(const Json &j) {
    require(j.isArray() && j.size() >= 3 && j.size() <= 256, "profile needs 3..256 [x,y] points");
    std::vector<float2> p;
    for (auto &v : j.items()) {
        require(v.isArray() && v.size() == 2, "profile point needs two coordinates");
        p.push_back({number(v[0], 0, "profile"), number(v[1], 0, "profile")});
    }
    return p;
}
float cross2(float2 a, float2 b, float2 c) { return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x); }
std::vector<std::array<int, 3>> triangulate(std::vector<float2> &p) {
    float area = 0;
    for (size_t i = 0; i < p.size(); ++i) {
        auto a = p[i], b = p[(i + 1) % p.size()];
        area += a.x * b.y - a.y * b.x;
    }
    require(std::abs(area) > 1e-7f, "profile has zero area");
    if (area < 0)
        std::reverse(p.begin(), p.end());
    for (size_t i = 0; i < p.size(); ++i)
        for (size_t j = i + 1; j < p.size(); ++j) {
            size_t ni = (i + 1) % p.size(), nj = (j + 1) % p.size();
            if (ni == j || nj == i)
                continue;
            require(!(cross2(p[i], p[ni], p[j]) * cross2(p[i], p[ni], p[nj]) < 0 &&
                      cross2(p[j], p[nj], p[i]) * cross2(p[j], p[nj], p[ni]) < 0),
                    "self-intersecting profile");
        }
    std::vector<int> ids(p.size());
    std::iota(ids.begin(), ids.end(), 0);
    std::vector<std::array<int, 3>> out;
    while (ids.size() > 3) {
        bool found = false;
        for (size_t k = 0; k < ids.size(); ++k) {
            int a = ids[(k + ids.size() - 1) % ids.size()], b = ids[k], c = ids[(k + 1) % ids.size()];
            if (cross2(p[a], p[b], p[c]) <= 1e-7f)
                continue;
            bool inside = false;
            for (int v : ids)
                if (v != a && v != b && v != c && cross2(p[a], p[b], p[v]) >= 0 && cross2(p[b], p[c], p[v]) >= 0 &&
                    cross2(p[c], p[a], p[v]) >= 0)
                    inside = true;
            if (inside)
                continue;
            out.push_back({a, b, c});
            ids.erase(ids.begin() + ptrdiff_t(k));
            found = true;
            break;
        }
        require(found, "cannot triangulate profile (duplicate or collinear edges)");
    }
    out.push_back({ids[0], ids[1], ids[2]});
    return out;
}
} // namespace
Mesh geometry(const Json &s, const std::atomic<bool> *cancel) {
    require(s.isObject(), "geometry must be an object");
    if (cancel && cancel->load())
        throw std::runtime_error("build cancelled");
    Mesh m;
    auto kind = s.getStr("kind", "box");
    auto count = [&](const char *k, int d, int lo, int hi) {
        auto n = s.getInt(k, d);
        require(n >= lo && n <= hi, std::string(k) + " outside limits");
        return int(n);
    };
    int seg = kind == "cable" ? 24 : count("segments", 24, 3, 128);
    float r = number(s["radius"], .5f, "radius"), h = number(s["height"], 1, "height");
    require(r > 0 && h > 0, "radius and height must be positive");
    if (kind == "cable") {
        m = cableGeometry(s, cancel);
    } else if (kind == "box" || kind == "bevelBox") {
        auto size = vector3(s["size"], {1, 1, 1}, "size");
        require(size.x > 0 && size.y > 0 && size.z > 0, "size must be positive");
        float bevel = kind == "bevelBox" ? number(s["bevel"], .1f, "bevel") : 0;
        require(bevel >= 0 && bevel < std::min({size.x, size.y, size.z}) * .5f, "bevel exceeds half size");
        int steps = bevel > 0 ? count("steps", 4, 2, 16) : 1;
        for (auto n : {float3{1, 0, 0}, float3{-1, 0, 0}, float3{0, 1, 0}, float3{0, -1, 0}, float3{0, 0, 1},
                       float3{0, 0, -1}}) {
            auto u = normalize(cross(std::abs(n.y) > .5f ? float3{0, 0, 1} : float3{0, 1, 0}, n));
            auto v = cross(n, u);
            auto point = [&](int x, int y) {
                float3 p =
                    mul(n + u * (float(x) / float(steps) * 2 - 1) + v * (float(y) / float(steps) * 2 - 1), size * .5f);
                if (bevel > 0) {
                    float3 inner = size * .5f - float3{bevel, bevel, bevel};
                    float3 q{std::clamp(p.x, -inner.x, inner.x), std::clamp(p.y, -inner.y, inner.y),
                             std::clamp(p.z, -inner.z, inner.z)};
                    p = q + normalize(p - q) * bevel;
                }
                return p;
            };
            for (int y = 0; y < steps; ++y)
                for (int x = 0; x < steps; ++x)
                    quad(m, point(x, y), point(x + 1, y), point(x + 1, y + 1), point(x, y + 1));
        }
    } else if (kind == "sphere" || kind == "cylinder" || kind == "cone" || kind == "lathe") {
        std::vector<float2> p;
        if (kind == "sphere") {
            int rings = count("rings", 12, 3, 128);
            for (int j = 0; j <= rings; ++j) {
                float a = pi * float(j) / float(rings);
                p.push_back({r * std::sin(a), -r * std::cos(a)});
            }
        } else if (kind == "lathe") {
            p = profile(s["profile"]);
            for (auto q : p)
                require(q.x >= 0, "lathe radius must be nonnegative");
        } else {
            float top = kind == "cone" ? 0 : number(s["topRadius"], r, "topRadius");
            require(top >= 0, "negative top radius");
            p = {{0, -h * .5f}, {r, -h * .5f}, {top, h * .5f}, {0, h * .5f}};
        }
        auto point = [&](float2 q, int k) {
            float a = 2 * pi * float(k) / float(seg);
            return float3{q.x * std::cos(a), q.y, -q.x * std::sin(a)};
        };
        for (size_t j = 0; j + 1 < p.size(); ++j)
            for (int k = 0; k < seg; ++k) {
                auto a = point(p[j], k), b = point(p[j], k + 1), c = point(p[j + 1], k + 1), d = point(p[j + 1], k);
                quad(m, a, b, c, d);
            }
    } else if (kind == "extrude" || kind == "sweep") {
        auto p = profile(s["profile"]);
        auto tris = triangulate(p);
        std::vector<float3> path;
        if (kind == "extrude")
            path = {{0, 0, -h * .5f}, {0, 0, h * .5f}};
        else {
            require(s["path"].isArray() && s["path"].size() >= 2 && s["path"].size() <= 256,
                    "sweep path needs 2..256 points");
            for (auto &v : s["path"].items())
                path.push_back(vector3(v, {}, "path"));
        }
        std::vector<std::vector<float3>> rings;
        float3 previous{1, 0, 0};
        for (size_t j = 0; j < path.size(); ++j) {
            auto tangent = j + 1 < path.size() ? path[j + 1] - path[j] : path[j] - path[j - 1];
            require(length(tangent) > 1e-6f, "duplicate path points");
            tangent = normalize(tangent);
            auto u = previous - tangent * dot(previous, tangent);
            if (length(u) < 1e-4f)
                u = cross(std::abs(tangent.y) < .9f ? float3{0, 1, 0} : float3{1, 0, 0}, tangent);
            u = normalize(u);
            auto v = cross(tangent, u);
            previous = u;
            std::vector<float3> ring;
            for (auto q : p)
                ring.push_back(path[j] + u * q.x + v * q.y);
            rings.push_back(std::move(ring));
        }
        for (auto t : tris) {
            triangle(m, rings.front()[t[2]], rings.front()[t[1]], rings.front()[t[0]]);
            triangle(m, rings.back()[t[0]], rings.back()[t[1]], rings.back()[t[2]]);
        }
        for (size_t j = 0; j + 1 < rings.size(); ++j)
            for (size_t k = 0; k < p.size(); ++k) {
                size_t next = (k + 1) % p.size();
                quad(m, rings[j][k], rings[j][next], rings[j + 1][next], rings[j + 1][k]);
            }
    } else if (kind == "mesh") {
        require(s["vertices"].isArray() && s["vertices"].size() <= 500000, "mesh vertices missing or oversized");
        for (auto &p : s["vertices"].items()) {
            ModelVertex v{};
            v.pos = vector3(p, {}, "vertices");
            m.vertices.push_back(v);
        }
        require(s["indices"].isArray() && s["indices"].size() % 3 == 0 && s["indices"].size() <= 1500000,
                "invalid triangle indices");
        for (auto &v : s["indices"].items()) {
            auto i = v.integer(-1);
            require(i >= 0 && size_t(i) < m.vertices.size(), "vertex index out of range");
            m.indices.push_back(uint32_t(i));
        }
        modifier(m, Json::parse(R"({"kind":"normals"})"));
    } else
        throw std::runtime_error("unknown geometry kind '" + kind + "'");
    require(!m.indices.empty(), "geometry is empty");
    for (auto &op : s["modifiers"].items()) {
        if (cancel && cancel->load())
            throw std::runtime_error("build cancelled");
        modifier(m, op);
    }
    budget(m);
    finiteMesh(m);
    return m;
}
void modifier(Mesh &m, const Json &op) {
    auto kind = op.getStr("kind");
    if (kind == "bevel") {
        bevelConvex(m, number(op["amount"], .05f, "bevel amount"));
    } else if (kind == "smoothSubdivision") {
        smoothSubdivision(m, int(op.getInt("levels", 1)));
    } else if (kind == "transform") {
        auto t = vector3(op["translation"], {}, "geometry translation"),
             s = vector3(op["scale"], {1, 1, 1}, "geometry scale"),
             r = vector3(op["rotation"], {}, "geometry Euler rotation");
        require(s.x > 0 && s.y > 0 && s.z > 0, "geometry scale must be positive");
        auto q = quat::axisAngle({0, 0, 1}, r.z) * quat::axisAngle({0, 1, 0}, r.y) * quat::axisAngle({1, 0, 0}, r.x);
        for (auto &v : m.vertices) {
            v.pos = q.rotate(mul(v.pos, s)) + t;
            v.nrm = normalize(q.rotate({v.nrm.x / s.x, v.nrm.y / s.y, v.nrm.z / s.z}));
        }
    } else if (kind == "mirror" || kind == "array") {
        Mesh src = m;
        int copies = kind == "mirror" ? 1 : int(op.getInt("count", 2)) - 1;
        require(copies >= 0 && copies <= 63, "array count must be 1..64");
        require(src.vertices.size() * size_t(copies + 1) <= 500000, "array exceeds vertex budget");
        auto offset = vector3(op["offset"], {1, 0, 0}, "offset");
        auto axis = op.getStr("axis", "x");
        require(axis == "x" || axis == "y" || axis == "z", "mirror axis must be x/y/z");
        for (int c = 1; c <= copies; ++c) {
            uint32_t base = uint32_t(m.vertices.size());
            for (auto v : src.vertices) {
                if (kind == "array")
                    v.pos = v.pos + offset * float(c);
                else {
                    if (axis == "x") {
                        v.pos.x = -v.pos.x;
                        v.nrm.x = -v.nrm.x;
                    }
                    if (axis == "y") {
                        v.pos.y = -v.pos.y;
                        v.nrm.y = -v.nrm.y;
                    }
                    if (axis == "z") {
                        v.pos.z = -v.pos.z;
                        v.nrm.z = -v.nrm.z;
                    }
                    v.tangent.w = -v.tangent.w;
                }
                m.vertices.push_back(v);
            }
            for (size_t i = 0; i < src.indices.size(); i += 3) {
                m.indices.push_back(base + src.indices[i]);
                m.indices.push_back(base + src.indices[i + (kind == "mirror" ? 2 : 1)]);
                m.indices.push_back(base + src.indices[i + (kind == "mirror" ? 1 : 2)]);
            }
        }
    } else if (kind == "subdivide") {
        int levels = int(op.getInt("levels", 1));
        require(levels >= 1 && levels <= 4, "subdivide levels must be 1..4");
        for (int l = 0; l < levels; ++l) {
            require(m.indices.size() * 4 <= 1500000, "subdivision budget exceeded");
            Mesh out;
            for (size_t i = 0; i < m.indices.size(); i += 3) {
                auto a = m.vertices[m.indices[i]], b = m.vertices[m.indices[i + 1]], c = m.vertices[m.indices[i + 2]];
                auto ab = (a.pos + b.pos) * .5f, bc = (b.pos + c.pos) * .5f, ca = (c.pos + a.pos) * .5f;
                triangle(out, a.pos, ab, ca);
                triangle(out, ab, b.pos, bc);
                triangle(out, ca, bc, c.pos);
                triangle(out, ab, bc, ca);
            }
            m = std::move(out);
        }
    } else if (kind == "simplify") {
        float cell = number(op["cell"], .05f, "cell");
        require(cell >= 1e-5f, "simplification cell must be at least 0.00001");
        std::map<std::array<int64_t, 3>, uint32_t> cells;
        Mesh out;
        std::vector<uint32_t> remap;
        for (auto v : m.vertices) {
            std::array<int64_t, 3> key{int64_t(std::floor(v.pos.x / cell)), int64_t(std::floor(v.pos.y / cell)),
                                       int64_t(std::floor(v.pos.z / cell))};
            auto [it, inserted] = cells.emplace(key, uint32_t(out.vertices.size()));
            if (inserted)
                out.vertices.push_back(v);
            remap.push_back(it->second);
        }
        for (size_t i = 0; i < m.indices.size(); i += 3) {
            auto a = remap[m.indices[i]], b = remap[m.indices[i + 1]], c = remap[m.indices[i + 2]];
            if (a != b && a != c && b != c) {
                out.indices.insert(out.indices.end(), {a, b, c});
            }
        }
        require(!out.indices.empty(), "simplification removed all faces");
        m = std::move(out);
        modifier(m, Json::parse(R"({"kind":"normals"})"));
    } else if (kind == "normals") {
        for (auto &v : m.vertices)
            v.nrm = {};
        for (size_t i = 0; i < m.indices.size(); i += 3) {
            auto &a = m.vertices[m.indices[i]];
            auto &b = m.vertices[m.indices[i + 1]];
            auto &c = m.vertices[m.indices[i + 2]];
            auto n = cross(b.pos - a.pos, c.pos - a.pos);
            a.nrm = a.nrm + n;
            b.nrm = b.nrm + n;
            c.nrm = c.nrm + n;
        }
        for (auto &v : m.vertices)
            v.nrm = length(v.nrm) > 1e-8f ? normalize(v.nrm) : float3{0, 1, 0};
    } else if (kind == "uvAtlas") {
        // One chart per triangle. This deliberately favors predictable non-overlap over seam minimization.
        Mesh out;
        size_t triangles = m.indices.size() / 3;
        int grid = int(std::ceil(std::sqrt(double(triangles))));
        float gap = number(op["padding"], .04f, "atlas padding");
        require(gap >= 0 && gap < .25f, "padding must be 0..0.25");
        for (size_t i = 0; i < triangles; ++i) {
            float x = float(i % size_t(grid)), y = float(i / size_t(grid));
            float2 uv[3] = {{gap, gap}, {1 - gap, gap}, {gap, 1 - gap}};
            for (int k = 0; k < 3; ++k) {
                auto v = m.vertices[m.indices[i * 3 + size_t(k)]];
                v.uv = {(x + uv[k].x) / float(grid), (y + uv[k].y) / float(grid)};
                out.indices.push_back(uint32_t(out.vertices.size()));
                out.vertices.push_back(v);
            }
        }
        m = std::move(out);
    } else if (kind == "uv") {
        auto mode = op.getStr("mode", "box");
        require(mode == "box" || mode == "planar" || mode == "cylindrical", "unknown UV projection");
        float scale = number(op["scale"], 1, "uv scale");
        for (auto &v : m.vertices) {
            auto p = v.pos;
            if (mode == "cylindrical")
                v.uv = {std::atan2(p.z, p.x) / (2 * pi) + .5f, p.y};
            else if (mode == "planar")
                v.uv = {p.x, p.z};
            else if (std::abs(v.nrm.x) > std::abs(v.nrm.y) && std::abs(v.nrm.x) > std::abs(v.nrm.z))
                v.uv = {p.z, p.y};
            else if (std::abs(v.nrm.y) > std::abs(v.nrm.z))
                v.uv = {p.x, p.z};
            else
                v.uv = {p.x, p.y};
            v.uv = {v.uv.x * scale, v.uv.y * scale};
        }
    } else
        throw std::runtime_error("unknown modifier '" + kind + "'");
    budget(m);
    finiteMesh(m);
}
} // namespace aiverse::modeling
