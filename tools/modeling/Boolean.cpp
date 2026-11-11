// Bounded BSP CSG for closed, consistently wound triangle meshes. Host only.
#include "Modeling.h"
#include <algorithm>
#include <cmath>
namespace aiverse::modeling {
namespace {
constexpr float eps = 1e-5f;
struct Polygon {
    std::vector<ModelVertex> v;
    float3 n{};
    float w = 0;
    void plane() {
        n = normalize(cross(v[1].pos - v[0].pos, v[2].pos - v[0].pos));
        w = dot(n, v[0].pos);
    }
    void flip() {
        std::reverse(v.begin(), v.end());
        for (auto &x : v)
            x.nrm = x.nrm * -1;
        n = n * -1;
        w = -w;
    }
};
ModelVertex mix(const ModelVertex &a, const ModelVertex &b, float t) {
    auto v = a;
    v.pos = lerp(a.pos, b.pos, t);
    v.nrm = normalize(lerp(a.nrm, b.nrm, t));
    v.uv = {a.uv.x + (b.uv.x - a.uv.x) * t, a.uv.y + (b.uv.y - a.uv.y) * t};
    return v;
}
struct Budget {
    size_t splits = 0;
    void step() { require(++splits < 2000000, "CSG work budget exceeded"); }
};
struct Bsp {
    float3 n{};
    float w = 0;
    bool valid = false;
    std::vector<Polygon> polygons;
    std::unique_ptr<Bsp> front, back;
    Budget *budget;
    explicit Bsp(Budget &b) : budget(&b) {}
    void split(const Polygon &p, std::vector<Polygon> &cf, std::vector<Polygon> &cb, std::vector<Polygon> &f,
               std::vector<Polygon> &b) {
        budget->step();
        int type = 0;
        std::vector<int> types;
        for (auto &v : p.v) {
            float d = dot(n, v.pos) - w;
            int t = d < -eps ? 2 : d > eps ? 1 : 0;
            type |= t;
            types.push_back(t);
        }
        if (type == 0) {
            (dot(n, p.n) > 0 ? cf : cb).push_back(p);
            return;
        }
        if (type == 1) {
            f.push_back(p);
            return;
        }
        if (type == 2) {
            b.push_back(p);
            return;
        }
        Polygon fp, bp;
        for (size_t i = 0; i < p.v.size(); ++i) {
            size_t j = (i + 1) % p.v.size();
            auto vi = p.v[i], vj = p.v[j];
            int ti = types[i], tj = types[j];
            if (ti != 2)
                fp.v.push_back(vi);
            if (ti != 1)
                bp.v.push_back(vi);
            if ((ti | tj) == 3) {
                float t = (w - dot(n, vi.pos)) / dot(n, vj.pos - vi.pos);
                auto v = mix(vi, vj, t);
                fp.v.push_back(v);
                bp.v.push_back(v);
            }
        }
        if (fp.v.size() >= 3) {
            fp.n = p.n;
            fp.w = p.w;
            f.push_back(std::move(fp));
        }
        if (bp.v.size() >= 3) {
            bp.n = p.n;
            bp.w = p.w;
            b.push_back(std::move(bp));
        }
    }
    void build(const std::vector<Polygon> &list, int depth = 0) {
        require(depth < 256, "CSG depth budget exceeded");
        if (list.empty())
            return;
        if (!valid) {
            valid = true;
            n = list[0].n;
            w = list[0].w;
        }
        std::vector<Polygon> f, b;
        for (auto &p : list)
            split(p, polygons, polygons, f, b);
        if (!f.empty()) {
            if (!front)
                front = std::make_unique<Bsp>(*budget);
            front->build(f, depth + 1);
        }
        if (!b.empty()) {
            if (!back)
                back = std::make_unique<Bsp>(*budget);
            back->build(b, depth + 1);
        }
    }
    std::vector<Polygon> clip(const std::vector<Polygon> &list) {
        if (!valid)
            return list;
        std::vector<Polygon> f, b;
        for (auto &p : list)
            split(p, f, b, f, b);
        if (front)
            f = front->clip(f);
        if (back)
            b = back->clip(b);
        else
            b.clear();
        f.insert(f.end(), b.begin(), b.end());
        return f;
    }
    void clipTo(Bsp &other) {
        polygons = other.clip(polygons);
        if (front)
            front->clipTo(other);
        if (back)
            back->clipTo(other);
    }
    void invert() {
        for (auto &p : polygons)
            p.flip();
        n = n * -1;
        w = -w;
        if (front)
            front->invert();
        if (back)
            back->invert();
        std::swap(front, back);
    }
    std::vector<Polygon> all() {
        auto out = polygons;
        for (auto *c : {front.get(), back.get()})
            if (c) {
                auto a = c->all();
                out.insert(out.end(), a.begin(), a.end());
            }
        return out;
    }
};
std::vector<Polygon> polygons(const Mesh &m) {
    require(m.indices.size() <= 30000, "CSG input limited to 10000 triangles per operand");
    std::vector<Polygon> out;
    for (size_t i = 0; i < m.indices.size(); i += 3) {
        Polygon p;
        for (size_t j = 0; j < 3; ++j)
            p.v.push_back(m.vertices[m.indices[i + j]]);
        if (length(cross(p.v[1].pos - p.v[0].pos, p.v[2].pos - p.v[0].pos)) < eps * eps)
            continue;
        p.plane();
        out.push_back(std::move(p));
    }
    return out;
}
} // namespace
Mesh booleanMesh(const Mesh &ma, const Mesh &mb, const std::string &op) {
    require(op == "union" || op == "subtract" || op == "intersect", "unknown boolean operation");
    Budget budget;
    Bsp a(budget), b(budget);
    a.build(polygons(ma));
    b.build(polygons(mb));
    if (op == "union") {
        a.clipTo(b);
        b.clipTo(a);
        b.invert();
        b.clipTo(a);
        b.invert();
        a.build(b.all());
    }
    if (op == "subtract") {
        a.invert();
        a.clipTo(b);
        b.clipTo(a);
        b.invert();
        b.clipTo(a);
        b.invert();
        a.build(b.all());
        a.invert();
    }
    if (op == "intersect") {
        a.invert();
        b.clipTo(a);
        b.invert();
        a.clipTo(b);
        b.clipTo(a);
        a.build(b.all());
        a.invert();
    }
    Mesh out;
    for (auto &p : a.all())
        for (size_t i = 1; i + 1 < p.v.size(); ++i) {
            if (length(cross(p.v[i].pos - p.v[0].pos, p.v[i + 1].pos - p.v[0].pos)) < eps * eps)
                continue;
            for (auto v : {p.v[0], p.v[i], p.v[i + 1]}) {
                out.indices.push_back(uint32_t(out.vertices.size()));
                out.vertices.push_back(v);
            }
            require(out.vertices.size() <= 500000, "CSG output budget exceeded");
        }
    require(!out.indices.empty(), "boolean result is empty");
    return out;
}
} // namespace aiverse::modeling
